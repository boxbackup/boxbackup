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

	S3Client(const std::string& HostName, int Port, const std::string& rAccessKey,
		const std::string& rSecretKey, const std::string& VirtualHostName = "")
	: mpSimulator(NULL),
	  mHostName(HostName),
	  mPort(Port),
	  mVirtualHostName(VirtualHostName),
	  mAccessKey(rAccessKey),
	  mSecretKey(rSecretKey),
	  mNetworkTimeout(30000)
	{ }

	S3Client(const Configuration& s3config)
	: mpSimulator(NULL),
	  mHostName(s3config.GetKeyValue("HostName")),
	  mPort(s3config.GetKeyValueInt("Port")),
	  mVirtualHostName(s3config.GetKeyValue("S3VirtualHostName")),
	  mAccessKey(s3config.GetKeyValue("AccessKey")),
	  mSecretKey(s3config.GetKeyValue("SecretKey")),
	  mNetworkTimeout(30000)
	{ }

	class BucketEntry {
	public:
		BucketEntry(const std::string& name, const std::string& etag,
			int64_t size)
		: mName(name),
		  mEtag(etag),
		  mSize(size)
		{ }
		const std::string& name() const { return mName; }
		const std::string& etag() const { return mEtag; }
		const int64_t size() const { return mSize; }
	private:
		std::string mName, mEtag;
		int64_t mSize;
	};

	int ListBucket(std::vector<S3Client::BucketEntry>* p_contents_out,
		std::vector<std::string>* p_common_prefixes_out,
		const std::string& prefix = "", const std::string& delimiter = "/",
		bool* p_truncated_out = NULL, int max_keys = -1,
		const std::string& marker = "");
	HTTPResponse GetObject(const std::string& rObjectURI,
		const std::string& MD5Checksum = "");
	HTTPResponse HeadObject(const std::string& rObjectURI);
	HTTPResponse PutObject(const std::string& rObjectURI,
		IOStream& rStreamToSend, const char* pContentType = NULL);
	HTTPResponse DeleteObject(const std::string& rObjectURI);
	void CheckResponse(const HTTPResponse& response, const std::string& message,
		bool ExpectNoContent = false) const;
	int GetNetworkTimeout() const { return mNetworkTimeout; }
	const std::string& GetAccessKey() const { return mAccessKey; }
	const std::string& GetSecretKey() const { return mSecretKey; }

	private:
	HTTPServer* mpSimulator;
	// mHostName is the network address that we will connect to (e.g. localhost):
	std::string mHostName;
	int mPort;
	// mVirtualHostName is the Host header that we will send, e.g.
	// "quotes.s3.amazonaws.com". If empty, mHostName will be used as a default.
	std::string mVirtualHostName;
	std::auto_ptr<SocketStream> mapClientSocket;
	std::string mAccessKey, mSecretKey;
	int mNetworkTimeout; // milliseconds

	HTTPResponse FinishAndSendRequest(HTTPRequest::Method Method,
		const std::string& rRequestURI,
		IOStream* pStreamToSend = NULL,
		const char* pStreamContentType = NULL,
		const std::string& MD5Checksum = "");
	HTTPResponse FinishAndSendRequest(HTTPRequest request,
		IOStream* pStreamToSend = NULL, const char* pStreamContentType = NULL,
		const std::string& MD5Checksum = "");
	HTTPResponse SendRequest(HTTPRequest& rRequest,
		IOStream* pStreamToSend = NULL,
		const char* pStreamContentType = NULL);
};

#endif // S3CLIENT__H

