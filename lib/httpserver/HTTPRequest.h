// --------------------------------------------------------------------------
//
// File
//		Name:    HTTPRequest.h
//		Purpose: Request object for HTTP connections
//		Created: 26/3/2004
//
// --------------------------------------------------------------------------

#ifndef HTTPREQUEST__H
#define HTTPREQUEST__H

#include <string>
#include <map>

#include "CollectInBufferStream.h"
#include "HTTPHeaders.h"
#include "SocketStream.h"

class HTTPResponse;
class IOStream;
class IOStreamGetLine;

// --------------------------------------------------------------------------
//
// Class
//		Name:    HTTPRequest
//		Purpose: Request object for HTTP connections. Although it
//			 inherits from CollectInBufferStream, not all of the
//			 request data is held in memory, only the beginning.
//			 Use ReadContent() to write it all (including the
//			 buffered beginning) to another stream, e.g. a file.
//		Created: 26/3/2004
//
// --------------------------------------------------------------------------
class HTTPRequest : public CollectInBufferStream
{
public:
	enum Method
	{
		Method_UNINITIALISED = -1,
		Method_UNKNOWN = 0,
		Method_GET = 1,
		Method_HEAD = 2,
		Method_POST = 3,
		Method_PUT = 4,
		Method_DELETE = 5
	};
	
	HTTPRequest();
	HTTPRequest(enum Method method, const std::string& rURI);
	~HTTPRequest();

	HTTPRequest(const HTTPRequest &to_copy)
	: mMethod(to_copy.mMethod),
	  mRequestURI(to_copy.mRequestURI),
	  mQueryString(to_copy.mQueryString),
	  mHTTPVersion(to_copy.mHTTPVersion),
	  mQuery(to_copy.mQuery),
	  // it's not safe to copy this, as it may be consumed or destroyed:
	  mpCookies(NULL),
	  mHeaders(to_copy.mHeaders),
	  mExpectContinue(to_copy.mExpectContinue),
	  // it's not safe to copy this, as it may be consumed or destroyed:
	  mpStreamToReadFrom(NULL),
	  mHttpVerb(to_copy.mHttpVerb)
	// If you ever add members, be sure to update this list too!
	{ }

	HTTPRequest &operator=(const HTTPRequest &to_copy)
	{
		mMethod = to_copy.mMethod;
		mRequestURI = to_copy.mRequestURI;
		mQueryString = to_copy.mQueryString;
		mHTTPVersion = to_copy.mHTTPVersion;
		mQuery = to_copy.mQuery;
		// it's not safe to copy this; as it may be modified or destroyed:
		mpCookies = NULL;
		mHeaders = to_copy.mHeaders;
		mExpectContinue = to_copy.mExpectContinue;
		// it's not safe to copy this; as it may be consumed or destroyed:
		mpStreamToReadFrom = NULL;
		mHttpVerb = to_copy.mHttpVerb;
		// If you ever add members, be sure to update this list too!

		return *this;
	}

public:
	typedef std::multimap<std::string, std::string> Query_t;
	typedef Query_t::value_type QueryEn_t;
	typedef std::pair<std::string, std::string> Header;

	enum
	{
		HTTPVersion__MajorMultiplier = 1000,
		HTTPVersion_0_9 = 9,
		HTTPVersion_1_0 = 1000,
		HTTPVersion_1_1 = 1001
	};

	bool Receive(IOStreamGetLine &rGetLine, int Timeout);
	void SendHeaders(IOStream &rStream, int Timeout, bool ExpectContinue = false);
	void Send(IOStream &rStream, int Timeout, bool ExpectContinue = false);
	IOStream::pos_type SendWithStream(SocketStream &rStreamToSendTo, int Timeout,
		IOStream* pStreamToSend, HTTPResponse& rResponse);
	void ReadContent(IOStream& rStreamToWriteTo, int Timeout);

	typedef std::map<std::string, std::string> CookieJar_t;
	
	enum Method GetMethod() const {return mMethod;}
	std::string GetMethodName() const;
	std::string GetRequestURI() const;

	const std::string &GetHostName() const {return mHeaders.GetHostName();}
	void SetHostName(const std::string& rHostName)
	{
		mHeaders.SetHostName(rHostName);
	}
	const int GetHostPort() const {return mHeaders.GetHostPort();}
	const std::string &GetQueryString() const {return mQueryString;}
	int GetHTTPVersion() const {return mHTTPVersion;}
	const Query_t &GetQuery() const {return mQuery;}
	void AddParameter(const std::string& name, const std::string& value)
	{
		mQuery.insert(QueryEn_t(name, value));
	}
	void SetParameter(const std::string& name, const std::string& value)
	{
		mQuery.erase(name);
		mQuery.insert(QueryEn_t(name, value));
	}
	void RemoveParameter(const std::string& name)
	{
		mQuery.erase(name);
	}
	std::string GetParameterString(const std::string& name,
		const std::string& default_value)
	{
		return GetParameterString(name, default_value, false); // !required
	}
	std::string GetParameterString(const std::string& name)
	{
		return GetParameterString(name, "", true); // required
	}
	const Query_t GetParameters() const
	{
		return mQuery;
	}

	int GetContentLength() const {return mHeaders.GetContentLength();}
	const std::string &GetContentType() const {return mHeaders.GetContentType();}
	const CookieJar_t *GetCookies() const {return mpCookies;} // WARNING: May return NULL
	bool GetCookie(const char *CookieName, std::string &rValueOut) const;
	const std::string &GetCookie(const char *CookieName) const;
	bool GetHeader(const std::string& rName, std::string* pValueOut) const
	{
		return mHeaders.GetHeader(rName, pValueOut);
	}
	void AddHeader(const std::string& rName, const std::string& rValue)
	{
		mHeaders.AddHeader(rName, rValue);
	}
	const HTTPHeaders& GetHeaders() const { return mHeaders; }

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    HTTPRequest::GetClientKeepAliveRequested()
	//		Purpose: Returns true if the client requested that the connection
	//				 should be kept open for further requests.
	//		Created: 22/12/04
	//
	// --------------------------------------------------------------------------
	bool GetClientKeepAliveRequested() const {return mHeaders.IsKeepAlive();}
	void SetClientKeepAliveRequested(bool keepAlive)
	{
		mHeaders.SetKeepAlive(keepAlive);
	}

	bool IsExpectingContinue() const { return mExpectContinue; }

	// This is not supposed to be an API, but the S3Simulator needs to be able to
	// associate a data stream with an HTTPRequest when handling it in-process.
	void SetDataStream(IOStream* pStreamToReadFrom)
	{
		ASSERT(!mpStreamToReadFrom);
		mpStreamToReadFrom = pStreamToReadFrom;
	}

private:
	std::string GetParameterString(const std::string& name,
		const std::string& default_value, bool required)
	{
		Query_t::iterator i = mQuery.find(name);
		if(i == mQuery.end())
		{
			if(required)
			{
				THROW_EXCEPTION_MESSAGE(HTTPException, ParameterNotFound,
					name);
			}
			else
			{
				return default_value;
			}
		}
		const std::string& value(i->second);
		i++;
		if(i != mQuery.end() && i->first == name)
		{
			THROW_EXCEPTION_MESSAGE(HTTPException, DuplicateParameter, name);
		}
		return value;
	}

	void ParseCookies(const std::string &rCookieString);

	enum Method mMethod;
	std::string mRequestURI;
	std::string mQueryString;
	int mHTTPVersion;
	Query_t mQuery;
	CookieJar_t *mpCookies;
	HTTPHeaders mHeaders;
	bool mExpectContinue;
	IOStream* mpStreamToReadFrom;
	std::string mHttpVerb;
	// If you ever add members, be sure to update the copy constructor too!
};

#endif // HTTPREQUEST__H

