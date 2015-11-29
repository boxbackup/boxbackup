// --------------------------------------------------------------------------
//
// File
//		Name:    HTTPResponse.h
//		Purpose: Response object for HTTP connections
//		Created: 26/3/04
//
// --------------------------------------------------------------------------

#ifndef HTTPRESPONSE__H
#define HTTPRESPONSE__H

#include <string>
#include <vector>

#include "CollectInBufferStream.h"
#include "HTTPHeaders.h"

class IOStreamGetLine;

// --------------------------------------------------------------------------
//
// Class
//		Name:    HTTPResponse
//		Purpose: Response object for HTTP connections
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
class HTTPResponse : public CollectInBufferStream
{
public:
	HTTPResponse(IOStream* pStreamToSendTo);
	HTTPResponse();
	~HTTPResponse();

	// allow copying, but be very careful with the response stream,
	// you can only read it once! (this class doesn't police it).
	HTTPResponse(const HTTPResponse& rOther);
	HTTPResponse &operator=(const HTTPResponse &rOther);

	void SetResponseCode(int Code);
	int GetResponseCode() const { return mResponseCode; }
	void SetContentType(const char *ContentType)
	{
		mHeaders.SetContentType(ContentType);
	}
	const std::string& GetContentType() { return mHeaders.GetContentType(); }
	int64_t GetContentLength() { return mHeaders.GetContentLength(); }

	void SetAsRedirect(const char *RedirectTo, bool IsLocalURI = true);
	void SetAsNotFound(const char *URI);

	void Send(int Timeout = IOStream::TimeOutInfinite);
	void SendContinue(int Timeout = IOStream::TimeOutInfinite);
	void Receive(IOStream& rStream, int Timeout = IOStream::TimeOutInfinite);

	bool GetHeader(const std::string& name, std::string* pValueOut) const
	{
		return mHeaders.GetHeader(name, pValueOut);
	}
	std::string GetHeaderValue(const std::string& name)
	{
		return mHeaders.GetHeaderValue(name);
	}
	void AddHeader(const std::string& name, const std::string& value)
	{
		mHeaders.AddHeader(name, value);
	}
	HTTPHeaders& GetHeaders() { return mHeaders; }

	// Set dynamic content flag, default is content is dynamic
	void SetResponseIsDynamicContent(bool IsDynamic) {mResponseIsDynamicContent = IsDynamic;}
	// Set keep alive control, default is to mark as to be closed
	void SetKeepAlive(bool KeepAlive)
	{
		mHeaders.SetKeepAlive(KeepAlive);
	}
	bool IsKeepAlive() {return mHeaders.IsKeepAlive();}

	void SetCookie(const char *Name, const char *Value, const char *Path = "/", int ExpiresAt = 0);

	enum
	{
		Code_OK = 200,
		Code_NoContent = 204,
		Code_MovedPermanently = 301,
		Code_Found = 302,	// redirection
		Code_NotModified = 304,
		Code_TemporaryRedirect = 307,
		Code_MethodNotAllowed = 400,
		Code_Unauthorized = 401,
		Code_Forbidden = 403,
		Code_NotFound = 404,
		Code_Conflict = 409,
		Code_InternalServerError = 500,
		Code_NotImplemented = 501
	};

	static const char *ResponseCodeToString(int ResponseCode);

	void WriteStringDefang(const char *String, unsigned int StringLen);
	void WriteStringDefang(const std::string &rString) {WriteStringDefang(rString.c_str(), rString.size());}

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    HTTPResponse::WriteString(const std::string &)
	//		Purpose: Write a string to the response (simple sugar function)
	//		Created: 9/4/04
	//
	// --------------------------------------------------------------------------
	void WriteString(const std::string &rString)
	{
		Write(rString.c_str(), rString.size());
	}

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    HTTPResponse::SetDefaultURIPrefix(const std::string &)
	//		Purpose: Set default prefix used to local redirections
	//		Created: 26/3/04
	//
	// --------------------------------------------------------------------------
	static void SetDefaultURIPrefix(const std::string &rPrefix)
	{
		msDefaultURIPrefix = rPrefix;
	}

	// Update Content-Length from current buffer size.
	void SetForReading()
	{
		CollectInBufferStream::SetForReading();
		mHeaders.SetContentLength(GetSize());
	}

	// Clear all state for reading again
	void Reset()
	{
		CollectInBufferStream::Reset();
		mHeaders = HTTPHeaders();
		mResponseCode = HTTPResponse::Code_NoContent;
		mResponseIsDynamicContent = true;
		mpStreamToSendTo = NULL;
	}

private:
	int mResponseCode;
	bool mResponseIsDynamicContent;
	IOStream* mpStreamToSendTo; // nonzero only when constructed with a stream

	static std::string msDefaultURIPrefix;
	HTTPHeaders mHeaders;
};

#endif // HTTPRESPONSE__H

