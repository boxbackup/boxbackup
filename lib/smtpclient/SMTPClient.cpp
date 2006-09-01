// --------------------------------------------------------------------------
//
// File
//		Name:    SMTPClient.cpp
//		Purpose: Send emails using SMTP
//		Created: 28/10/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdlib.h>
#include <limits.h>

#include "SMTPClient.h"
#include "SocketStream.h"
#include "Socket.h"
#include "IOStreamGetLine.h"
#include "autogen_SMTPClientException.h"
#include "MemBlockStream.h"

#include "MemLeakFindOn.h"

// Well known port number.
#define SMTP_PORT	25

// Reply code
#define SMTPREPLY_POSITIVE_PRELIMIARY(x)	((x) >= 100 && (x) < 200)
#define SMTPREPLY_POSITIVE_COMPLETION(x)	((x) >= 200 && (x) < 300)
#define SMTPREPLY_POSITIVE_INTERMEDIATE(x)	((x) >= 300 && (x) < 400)
#define SMTPREPLY_NEGATIVE_TRANSIENT(x)		((x) >= 400 && (x) < 500)
#define SMTPREPLY_NEGATIVE_PERMANENT(x)		((x) >= 500 && (x) < 600)

// Internal data
class SMTPClient_Internals
{
public:
	SMTPClient_Internals() : mGetLine(mSocket) {}
	~SMTPClient_Internals() {}
	
	SocketStream mSocket;
	IOStreamGetLine mGetLine;
};



// --------------------------------------------------------------------------
//
// Function
//		Name:    SMTPClient::SMTPClient(const char *, const char *, int)
//		Purpose: Constructor, taking server name, this host name, and timeout
//		Created: 28/10/04
//
// --------------------------------------------------------------------------
SMTPClient::SMTPClient(const char *Server, const char *ThisHostName, int Timeout)
	: mServer(Server),
	  mThisHostName(ThisHostName),
	  mTimeout(Timeout),
	  mpImpl(0),
	  mpClaimedBySender(0)
{
	ASSERT(!mServer.empty());
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SMTPClient::SMTPClient(const std::string &, const std::string &, int)
//		Purpose: Constructor, taking server name, this host name, and timeout
//		Created: 28/10/04
//
// --------------------------------------------------------------------------
SMTPClient::SMTPClient(const std::string &rServer, const std::string &rThisHostName, int Timeout)
	: mServer(rServer),
	  mThisHostName(rThisHostName),
	  mTimeout(Timeout),
	  mpImpl(0),
	  mpClaimedBySender(0)
{
	ASSERT(!mServer.empty());
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SMTPClient::~SMTPClient()
//		Purpose: Destructor
//		Created: 28/10/04
//
// --------------------------------------------------------------------------
SMTPClient::~SMTPClient()
{
	if(mpClaimedBySender != 0)
	{
		TRACE1("Destroying SMTPClient when currently in use by SendEmail object 0x%x\n", mpClaimedBySender);
	}
	if(mpImpl != 0)
	{
		Disconnect();
	}
	ASSERT(mpImpl == 0);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SMTPClient::Connect()
//		Purpose: Connect to server. Does nothing if already connected.
//		Created: 28/10/04
//
// --------------------------------------------------------------------------
void SMTPClient::Connect()
{
	if(mpImpl != 0)
	{
		return;
	}

	// Open socket
	try
	{
		// Create new object
		mpImpl = new SMTPClient_Internals;
		
		// Open connection
		mpImpl->mSocket.Open(Socket::TypeINET, mServer.c_str(), SMTP_PORT);
		
		// Make sure we get a header
		int openingStatus = ReceiveStatusCode();
		if(!SMTPREPLY_POSITIVE_COMPLETION(openingStatus))
		{
			THROW_EXCEPTION(SMTPClientException, ServerReportedError)
		}
		
		// Say hello!
		std::string hello("HELO ");
		hello += mThisHostName;
		hello += "\r\n";
		int result = SendCommand(hello.c_str(), hello.size());
		if(!SMTPREPLY_POSITIVE_COMPLETION(result))
		{
			THROW_EXCEPTION(SMTPClientException, ServerReportedError)
		}
	}
	catch(...)
	{
		if(mpImpl != 0)
		{
			delete mpImpl;
		}
		throw;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SMTPClient::Disconnect()
//		Purpose: Disconnect from server. Does nothing if not connected.
//		Created: 28/10/04
//
// --------------------------------------------------------------------------
void SMTPClient::Disconnect()
{
	// Don't disconnect again
	if(mpImpl == 0)
	{
		return;
	}
	
	// Absorb errors, to avoid compounding any problems
	try
	{
		// Send quit command
		SendCommand("QUIT\r\n", sizeof("QUIT\r\n")-1);

		// Close socket
		mpImpl->mSocket.Close();
	}
	catch(...)
	{
		// Ignore
	}
	
	delete mpImpl;
	mpImpl = 0;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SMTPClient::SendEmail::SendEmail(SMTPClient &, const char *)
//		Purpose: Constructor, taking SMTPClient object and sender email address
//		Created: 28/10/04
//
// --------------------------------------------------------------------------
SMTPClient::SendEmail::SendEmail(SMTPClient &rClient, const char *SenderAddress)
	: mrClient(rClient),
	  mSenderAddress(SenderAddress),
	  mClaimedClient(false)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SMTPClient::SendEmail::SendEmail(SMTPClient &, const std::string &)
//		Purpose: Constructor, taking SMTPClient object and sender email address
//		Created: 28/10/04
//
// --------------------------------------------------------------------------
SMTPClient::SendEmail::SendEmail(SMTPClient &rClient, const std::string &rSenderAddress)
	: mrClient(rClient),
	  mSenderAddress(rSenderAddress),
	  mClaimedClient(false)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SMTPClient::SendEmail::SendEmail(SMTPClient &, const char *)
//		Purpose: Destructor
//		Created: 28/10/04
//
// --------------------------------------------------------------------------
SMTPClient::SendEmail::~SendEmail()
{
	if(mClaimedClient)
	{
		mrClient.ReleaseClient(this);
	}
}



// --------------------------------------------------------------------------
//
// Class
//		Name:    SMTPClient::SendEmail::To(const char *)
//		Purpose: Set a recipient address (can call multiple times)
//		Created: 28/10/04
//
// --------------------------------------------------------------------------
void SMTPClient::SendEmail::To(const char *ToAddress)
{
	EnsureStarted();
	
	// Send the command
	std::string mailTo("RCPT TO:<");
	mailTo += ToAddress;
	mailTo += ">\r\n";
	int result = mrClient.SendCommand(mailTo.c_str(), mailTo.size());
	if(!SMTPREPLY_POSITIVE_COMPLETION(result))
	{
		THROW_EXCEPTION(SMTPClientException, ServerReportedError)
	}
}


// --------------------------------------------------------------------------
//
// Class
//		Name:    SMTPClient::SendEmail::To(const std::string &)
//		Purpose: Set a recipient address (can call multiple times)
//		Created: 28/10/04
//
// --------------------------------------------------------------------------
void SMTPClient::SendEmail::To(const std::string &rToAddress)
{
	EnsureStarted();
	
	// Send the command
	std::string mailTo("RCPT TO:<");
	mailTo += rToAddress;
	mailTo += ">\r\n";
	int result = mrClient.SendCommand(mailTo.c_str(), mailTo.size());
	if(!SMTPREPLY_POSITIVE_COMPLETION(result))
	{
		THROW_EXCEPTION(SMTPClientException, ServerReportedError)
	}
}


// --------------------------------------------------------------------------
//
// Class
//		Name:    SMTPClient::SendEmail::Message(IOStream &, bool)
//		Purpose: Send the actual message
//		Created: 28/10/04
//
// --------------------------------------------------------------------------
void SMTPClient::SendEmail::Message(IOStream &rMessage, bool ProtectEmailContents)
{
	// Claimed?
	if(!mClaimedClient)
	{
		// Means no recpients have been specified
		THROW_EXCEPTION(SMTPClientException, NoRecipientsSet)		
	}

	// Send the message data command
	int result = mrClient.SendCommand("DATA\r\n", sizeof("DATA\r\n")-1);
	if(!SMTPREPLY_POSITIVE_INTERMEDIATE(result))
	{
		THROW_EXCEPTION(SMTPClientException, ServerReportedError)
	}
	
	// Send actual message data
	if(ProtectEmailContents)
	{
		// Need to check for .'s at the beginning of the line.
		IOStreamGetLine g(rMessage);
		std::string line;
		while(!g.IsEOF() && g.GetLine(line))
		{
			if(line[0] == '.')
			{
				// Send an additional . (see RFC on transparency)
				mrClient.mpImpl->mSocket.Write(".", 1);
			}
			
			// Send line
			mrClient.mpImpl->mSocket.Write(line.c_str(), line.size());

			// Terminate line properly
			mrClient.mpImpl->mSocket.Write("\r\n", 2);
		}
	}
	else
	{
		// Caller claims data is OK to send as it is, so just copy it
		rMessage.CopyStreamTo(mrClient.mpImpl->mSocket);
	}
	
	// Terminate message and check it went OK
	result = mrClient.SendCommand("\r\n.\r\n", sizeof("\r\n.\r\n")-1);
	if(!SMTPREPLY_POSITIVE_COMPLETION(result))
	{
		THROW_EXCEPTION(SMTPClientException, ServerReportedError)
	}
	
	// Unclaim
	mrClient.ReleaseClient(this);
	mClaimedClient = false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SMTPClient::SendEmail::EnsureStarted()
//		Purpose: Private. Ensure that the client is connected and the process
//				 has started properly.
//		Created: 29/10/04
//
// --------------------------------------------------------------------------
void SMTPClient::SendEmail::EnsureStarted()
{
	// Claim it
	if(!mClaimedClient)
	{
		mrClient.ClaimClient(this);
		mClaimedClient = true;

		// Ensure it's connected
		mrClient.Connect();
		
		// Send the from command
		std::string mailFrom("MAIL FROM:<");
		mailFrom += mSenderAddress;
		mailFrom += ">\r\n";
		int result = mrClient.SendCommand(mailFrom.c_str(), mailFrom.size());
		if(!SMTPREPLY_POSITIVE_COMPLETION(result))
		{
			THROW_EXCEPTION(SMTPClientException, ServerReportedError)
		}
	}	
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SMTPClient::SendCommand(const char *, int)
//		Purpose: Private. Send a command to the SMTP server, returning the
//				 SMTP status code.
//		Created: 28/10/04
//
// --------------------------------------------------------------------------
int SMTPClient::SendCommand(const char *Command, int CommandLength)
{
	ASSERT(mpImpl != 0);
	
	//TRACE1("C: %s", Command);
	
	mpImpl->mSocket.Write(Command, CommandLength);

	return ReceiveStatusCode();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SMTPClient::ReceiveStatusCode()
//		Purpose: Receives a status code, or exceptions if an timeout occured
//				 or badly formed line receieved.
//		Created: 29/10/04
//
// --------------------------------------------------------------------------
int SMTPClient::ReceiveStatusCode()
{
	ASSERT(mpImpl != 0);
	
	// Get a line from readline
	std::string line;
	bool finished = false;
	int statusCode = 0;
	while(!finished)
	{
		// Get a line
		if(!mpImpl->mGetLine.GetLine(line, false, mTimeout))
		{
			// Timeout!
			THROW_EXCEPTION(SMTPClientException, Timeout)
		}
		
		// The first bit should be a number
		const char *lineptr = line.c_str();
		//TRACE1("S: %s\n", lineptr);
		char *endptr = 0;
		long code = ::strtol(lineptr, &endptr, 10);
		if(code == LONG_MIN || code == LONG_MAX || endptr == lineptr || endptr == 0)
		{
			// Bad format of line
			THROW_EXCEPTION(SMTPClientException, UnexpectedServerResponse)
		}
		// Code looks OK then
		statusCode = code;
		
		// Continuation?
		if(*endptr != '-')
		{
			finished = true;
		}
	}
	
	//TRACE1("status=%d\n", statusCode);
	return statusCode;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SMTPClient::ClaimClient(SendEmail *)
//		Purpose: Private. Claim the SMTPClient as in use
//		Created: 29/10/04
//
// --------------------------------------------------------------------------
void SMTPClient::ClaimClient(SMTPClient::SendEmail *pSender)
{
	ASSERT(mpClaimedBySender != pSender);

	// Already in use?
	if(mpClaimedBySender != 0)
	{
		THROW_EXCEPTION(SMTPClientException, ClientAlreadyInUse)
	}
	
	// Record
	mpClaimedBySender = pSender;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SMTPClient::ReleaseClient(SMTPClient::SendEmail *)
//		Purpose: Private. Release the SMTPClient from use
//		Created: 29/10/04
//
// --------------------------------------------------------------------------
void SMTPClient::ReleaseClient(SMTPClient::SendEmail *pSender)
{
	if(mpClaimedBySender != pSender)
	{
		THROW_EXCEPTION(SMTPClientException, InternalClaimError)
	}
	
	// Unset
	mpClaimedBySender = 0;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SMTPClient::SendMessage(const std::string &, const std::string &, const std::string &)
//		Purpose: Quick interface for sending an email to one recipient, and
//				 always protecting the email sent again the SMTP transparency
//				 feature.
//		Created: 29/10/04
//
// --------------------------------------------------------------------------
void SMTPClient::SendMessage(const std::string &rFrom, const std::string &rTo, const std::string &rMessage)
{
	SMTPClient::SendEmail send(*this, rFrom);
	send.To(rTo);
	MemBlockStream messageStream(rMessage.c_str(), rMessage.size());
	send.Message(messageStream);
}

