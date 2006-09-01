// --------------------------------------------------------------------------
//
// File
//		Name:    SMTPClient.h
//		Purpose: Send emails using SMTP
//		Created: 28/10/04
//
// --------------------------------------------------------------------------

#ifndef SMTPCLIENT__H
#define SMTPCLIENT__H

#include <string>
class IOStream;
class SocketStream;

class SMTPClient_Internals;

class SMTPClient
{
public:
	SMTPClient(const char *Server, const char *ThisHostName, int Timeout = 5000);
	SMTPClient(const std::string &rServer, const std::string &rThisHostName, int Timeout = 5000);
	~SMTPClient();
private:
	// no copying
	SMTPClient(const SMTPClient &);
	SMTPClient &operator=(const SMTPClient &);
public:

	void Connect();
	void Disconnect();
	bool IsConnected() const {return mpImpl != 0;}
	bool InUse() const {return mpClaimedBySender != 0;}

	// Quick interface
	void SendMessage(const std::string &rFrom, const std::string &rTo, const std::string &rMessage);
	
	// More complete interface
	class SendEmail
	{
	public:
		SendEmail(SMTPClient &rClient, const char *SenderAddress);
		SendEmail(SMTPClient &rClient, const std::string &rSenderAddress);
		~SendEmail();
	private:
		SendEmail(const SendEmail &);
		SendEmail &operator=(const SendEmail &);
	public:
	
		void To(const char *ToAddress);
		void To(const std::string &rToAddress);
		
		void Message(IOStream &rMessage, bool ProtectEmailContents = true);	// set to false if the email is guarenetted not to have special lines in it
	
	private:
		void EnsureStarted();
	
	private:
		SMTPClient &mrClient;
		std::string mSenderAddress;
		bool mClaimedClient;
	};
	friend class SendEmail;
	
private:
	int SendCommand(const char *Command, int CommandLength);
	int ReceiveStatusCode();
	void ClaimClient(SendEmail *pSender);
	void ReleaseClient(SendEmail *pSender);

private:
	std::string mServer;
	std::string mThisHostName;
	int mTimeout;
	SMTPClient_Internals *mpImpl;
	SendEmail *mpClaimedBySender;
};

#endif // SMTPCLIENT__H

