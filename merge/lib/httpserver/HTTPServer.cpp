// --------------------------------------------------------------------------
//
// File
//		Name:    HTTPServer.cpp
//		Purpose: HTTP server class
//		Created: 26/3/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>

#include "HTTPServer.h"
#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "IOStreamGetLine.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPServer::HTTPServer()
//		Purpose: Constructor
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
HTTPServer::HTTPServer()
	: mTimeout(20000)	// default timeout leaves a little while for clients to get the second request in.
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPServer::~HTTPServer()
//		Purpose: Destructor
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
HTTPServer::~HTTPServer()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPServer::DaemonName()
//		Purpose: As interface, generic name for daemon
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
const char *HTTPServer::DaemonName() const
{
	return "generic-httpserver";
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPServer::GetConfigVerify()
//		Purpose: As interface -- return most basic config so it's only necessary to
//				 provide this if you want to add extra directives.
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
const ConfigurationVerify *HTTPServer::GetConfigVerify() const
{
	static ConfigurationVerifyKey verifyserverkeys[] = 
	{
		HTTPSERVER_VERIFY_SERVER_KEYS(ConfigurationVerifyKey::NoDefaultValue) // no default addresses
	};

	static ConfigurationVerify verifyserver[] = 
	{
		{
			"Server",
			0,
			verifyserverkeys,
			ConfigTest_Exists | ConfigTest_LastEntry,
			0
		}
	};
	
	static ConfigurationVerifyKey verifyrootkeys[] = 
	{
		HTTPSERVER_VERIFY_ROOT_KEYS
	};

	static ConfigurationVerify verify =
	{
		"root",
		verifyserver,
		verifyrootkeys,
		ConfigTest_Exists | ConfigTest_LastEntry,
		0
	};

	return &verify;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPServer::Run()
//		Purpose: As interface.
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
void HTTPServer::Run()
{
	// Do some configuration stuff
	const Configuration &conf(GetConfiguration());
	HTTPResponse::SetDefaultURIPrefix(conf.GetKeyValue("AddressPrefix"));

	// Let the base class do the work
	ServerStream<SocketStream, 80>::Run();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPServer::Connection(SocketStream &) 
//		Purpose: As interface, handle connection
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
void HTTPServer::Connection(SocketStream &rStream)
{
	// Create a get line object to use
	IOStreamGetLine getLine(rStream);

	// Notify dervived claases
	HTTPConnectionOpening();

	bool handleRequests = true;
	while(handleRequests)
	{
		// Parse the request
		HTTPRequest request;
		if(!request.Receive(getLine, mTimeout))
		{
			// Didn't get request, connection probably closed.
			break;
		}
	
		// Generate a response
		HTTPResponse response(&rStream);
		
		try
		{
			Handle(request, response);
		}
		catch(BoxException &e)
		{
			char exceptionCode[256];
			::sprintf(exceptionCode, "%s (%d/%d)", e.what(),
				e.GetType(), e.GetSubType());
			SendInternalErrorResponse(exceptionCode, response);
		}
		catch(...)
		{
			SendInternalErrorResponse("unknown", response);
		}
		
		// Keep alive?
		if(request.GetClientKeepAliveRequested())
		{
			// Mark the response to the client as supporting keepalive
			response.SetKeepAlive(true);
		}
		else
		{
			// Stop now
			handleRequests = false;
		}
	
		// Send the response (omit any content if this is a HEAD method request)
		response.Send(request.GetMethod() == HTTPRequest::Method_HEAD);
	}

	// Notify derived classes
	HTTPConnectionClosing();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPServer::SendInternalErrorResponse(const char*,
//			 HTTPResponse&)
//		Purpose: Generates an error message in the provided response
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
void HTTPServer::SendInternalErrorResponse(const std::string& rErrorMsg,
	HTTPResponse& rResponse)
{
	#define ERROR_HTML_1 "<html><head><title>Internal Server Error</title></head>\n" \
			"<h1>Internal Server Error</h1>\n" \
			"<p>An error, type "
	#define ERROR_HTML_2 " occured when processing the request.</p>" \
			"<p>Please try again later.</p>" \
			"</body>\n</html>\n"

	// Generate the error page
	// rResponse.SetResponseCode(HTTPResponse::Code_InternalServerError);
	rResponse.SetContentType("text/html");
	rResponse.Write(ERROR_HTML_1, sizeof(ERROR_HTML_1) - 1);
	rResponse.IOStream::Write(rErrorMsg.c_str());
	rResponse.Write(ERROR_HTML_2, sizeof(ERROR_HTML_2) - 1);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPServer::HTTPConnectionOpening()
//		Purpose: Override to get notifications of connections opening
//		Created: 22/12/04
//
// --------------------------------------------------------------------------
void HTTPServer::HTTPConnectionOpening()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPServer::HTTPConnectionClosing()
//		Purpose: Override to get notifications of connections closing
//		Created: 22/12/04
//
// --------------------------------------------------------------------------
void HTTPServer::HTTPConnectionClosing()
{
}


