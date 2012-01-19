// --------------------------------------------------------------------------
//
// File
//		Name:    S3Client.h
//		Purpose: Amazon S3 client helper implementation class
//		Created: 09/01/2009
//
// --------------------------------------------------------------------------

#ifndef S3CLIENT__H
#define S3CLIENT__H

#include <string>
#include <map>

#include "HTTPRequest.h"
#include "SocketStream.h"

class HTTPResponse;
class HTTPServer;
class IOStream;

// --------------------------------------------------------------------------
//
// Class
//		Name:    S3Client
//		Purpose: Amazon S3 client helper implementation class
//		Created: 09/01/2009
//
// --------------------------------------------------------------------------
class S3Client
{
	public:
	S3Client(HTTPServer* pSimulator, const std::string& rHostName,
		const std::string& rAccessKey, const std::string& rSecretKey)
	: mpSimulator(pSimulator),
	  mHostName(rHostName),
	  mAccessKey(rAccessKey),
	  mSecretKey(rSecretKey)
	{ }
	
	S3Client(std::string HostName, int Port, const std::string& rAccessKey,
		const std::string& rSecretKey)
	: mpSimulator(NULL),
	  mHostName(HostName),
	  mPort(Port),
	  mAccessKey(rAccessKey),
	  mSecretKey(rSecretKey)
	{ }
		
	HTTPResponse GetObject(const std::string& rObjectURI);
	HTTPResponse PutObject(const std::string& rObjectURI,
		IOStream& rStreamToSend, const char* pContentType = NULL);

	private:
	HTTPServer* mpSimulator;
	std::string mHostName;
	int mPort;
	std::auto_ptr<SocketStream> mapClientSocket;
	std::string mAccessKey, mSecretKey;

	HTTPResponse FinishAndSendRequest(HTTPRequest::Method Method,
		const std::string& rRequestURI,
		IOStream* pStreamToSend = NULL,
		const char* pStreamContentType = NULL);
	HTTPResponse SendRequest(HTTPRequest& rRequest,
		IOStream* pStreamToSend = NULL,
		const char* pStreamContentType = NULL);
};

#endif // S3CLIENT__H

