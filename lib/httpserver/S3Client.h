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

#include "Configuration.h"
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
	  mSecretKey(rSecretKey),
	  mNetworkTimeout(30000)
	{ }

	S3Client(std::string HostName, int Port, const std::string& rAccessKey,
		const std::string& rSecretKey)
	: mpSimulator(NULL),
	  mHostName(HostName),
	  mPort(Port),
	  mAccessKey(rAccessKey),
	  mSecretKey(rSecretKey),
	  mNetworkTimeout(30000)
	{ }

	S3Client(const Configuration& s3config)
	: mpSimulator(NULL),
	  mHostName(s3config.GetKeyValue("HostName")),
	  mPort(s3config.GetKeyValueInt("Port")),
	  mAccessKey(s3config.GetKeyValue("AccessKey")),
	  mSecretKey(s3config.GetKeyValue("SecretKey")),
	  mNetworkTimeout(30000)
	{ }

	HTTPResponse GetObject(const std::string& rObjectURI,
		const std::string& MD5Checksum = "");
	HTTPResponse HeadObject(const std::string& rObjectURI);
	HTTPResponse PutObject(const std::string& rObjectURI,
		IOStream& rStreamToSend, const char* pContentType = NULL);
	void CheckResponse(const HTTPResponse& response, const std::string& message) const;
	int GetNetworkTimeout() const { return mNetworkTimeout; }

	private:
	HTTPServer* mpSimulator;
	std::string mHostName;
	int mPort;
	std::auto_ptr<SocketStream> mapClientSocket;
	std::string mAccessKey, mSecretKey;
	int mNetworkTimeout; // milliseconds

	HTTPResponse FinishAndSendRequest(HTTPRequest::Method Method,
		const std::string& rRequestURI,
		IOStream* pStreamToSend = NULL,
		const char* pStreamContentType = NULL,
		const std::string& MD5Checksum = "");
	HTTPResponse SendRequest(HTTPRequest& rRequest,
		IOStream* pStreamToSend = NULL,
		const char* pStreamContentType = NULL);
};

#endif // S3CLIENT__H

