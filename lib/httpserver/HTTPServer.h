// --------------------------------------------------------------------------
//
// File
//		Name:    HTTPServer.h
//		Purpose: HTTP server class
//		Created: 26/3/04
//
// --------------------------------------------------------------------------

#ifndef HTTPSERVER__H
#define HTTPSERVER__H

#include "ServerStream.h"
#include "SocketStream.h"

class HTTPRequest;
class HTTPResponse;

// --------------------------------------------------------------------------
//
// Class
//		Name:    HTTPServer
//		Purpose: HTTP server
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
class HTTPServer : public ServerStream<SocketStream, 80>
{
public:
	HTTPServer(int Timeout = 600000);
	// default timeout leaves a little while for clients to get a second request in.
	~HTTPServer();
private:
	// no copying
	HTTPServer(const HTTPServer &);
	HTTPServer &operator=(const HTTPServer &);

public:
	int GetTimeout() const {return mTimeout;}

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    HTTPServer::Handle(const HTTPRequest &, HTTPResponse &)
	//		Purpose: Response to a request, filling in the response object for sending
	//				 at some point in the future.
	//		Created: 26/3/04
	//
	// --------------------------------------------------------------------------
	virtual void Handle(HTTPRequest &rRequest, HTTPResponse &rResponse) = 0;
	
	// For notifications to derived classes
	virtual void HTTPConnectionOpening();
	virtual void HTTPConnectionClosing();

protected:
	void SendInternalErrorResponse(const std::string& rErrorMsg,
		HTTPResponse& rResponse);

private:
	int mTimeout;	// Timeout for read operations
	const char *DaemonName() const;
	const ConfigurationVerify *GetConfigVerify() const;
	void Run();
	void Connection(std::auto_ptr<SocketStream> apStream);
};

// Root level
#define HTTPSERVER_VERIFY_ROOT_KEYS \
	ConfigurationVerifyKey("AddressPrefix", \
		ConfigTest_Exists | ConfigTest_LastEntry)

// AddressPrefix is, for example, http://localhost:1080 -- ie the beginning of the URI
// This is used for handling redirections.

// Server level
#define HTTPSERVER_VERIFY_SERVER_KEYS(DEFAULT_ADDRESSES) \
	SERVERSTREAM_VERIFY_SERVER_KEYS(DEFAULT_ADDRESSES)

#endif // HTTPSERVER__H

