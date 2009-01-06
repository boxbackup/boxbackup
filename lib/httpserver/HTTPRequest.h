// --------------------------------------------------------------------------
//
// File
//		Name:    HTTPRequest.h
//		Purpose: Request object for HTTP connections
//		Created: 26/3/04
//
// --------------------------------------------------------------------------

#ifndef HTTPREQUEST__H
#define HTTPREQUEST__H

#include <string>
#include <map>

#include "CollectInBufferStream.h"

class HTTPResponse;
class IOStream;
class IOStreamGetLine;

// --------------------------------------------------------------------------
//
// Class
//		Name:    HTTPRequest
//		Purpose: Request object for HTTP connections
//		Created: 26/3/04
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
		Method_PUT = 4
	};
	
	HTTPRequest();
	HTTPRequest(enum Method method, const std::string& rURI);
	~HTTPRequest();
private:
	// no copying
	HTTPRequest(const HTTPRequest &);
	HTTPRequest &operator=(const HTTPRequest &);
public:
	typedef std::multimap<std::string, std::string> Query_t;
	typedef std::pair<std::string, std::string> QueryEn_t, Header;

	enum
	{
		HTTPVersion__MajorMultiplier = 1000,
		HTTPVersion_0_9 = 9,
		HTTPVersion_1_0 = 1000,
		HTTPVersion_1_1 = 1001
	};

	bool Receive(IOStreamGetLine &rGetLine, int Timeout);
	bool Send(IOStream &rStream, int Timeout, bool ExpectContinue = false);
	void SendWithStream(IOStream &rStreamToSendTo, int Timeout,
		IOStream* pStreamToSend, HTTPResponse& rResponse);
	void ReadContent(IOStream& rStreamToWriteTo);

	typedef std::map<std::string, std::string> CookieJar_t;
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    HTTPResponse::Get*()
	//		Purpose: Various Get accessors
	//		Created: 26/3/04
	//
	// --------------------------------------------------------------------------
	enum Method GetMethod() const {return mMethod;}
	const std::string &GetRequestURI() const {return mRequestURI;}

	// Note: the HTTPRequest generates and parses the Host: header
	// Do not attempt to set one yourself with AddHeader().
	const std::string &GetHostName() const {return mHostName;}
	void SetHostName(const std::string& rHostName)
	{
		mHostName = rHostName;
	}

	const int GetHostPort() const {return mHostPort;}
	const std::string &GetQueryString() const {return mQueryString;}
	int GetHTTPVersion() const {return mHTTPVersion;}
	const Query_t &GetQuery() const {return mQuery;}
	int GetContentLength() const {return mContentLength;}
	const std::string &GetContentType() const {return mContentType;}
	const CookieJar_t *GetCookies() const {return mpCookies;} // WARNING: May return NULL
	bool GetCookie(const char *CookieName, std::string &rValueOut) const;
	const std::string &GetCookie(const char *CookieName) const;
	bool GetHeader(const std::string& rName, std::string* pValueOut) const
	{
		for (std::vector<Header>::const_iterator
			i  = mExtraHeaders.begin();
			i != mExtraHeaders.end(); i++)
		{
			if (i->first == rName)
			{
				*pValueOut = i->second;
				return true;
			}
		}
		return false;
	}
	std::vector<Header> GetHeaders() { return mExtraHeaders; }

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    HTTPRequest::GetClientKeepAliveRequested()
	//		Purpose: Returns true if the client requested that the connection
	//				 should be kept open for further requests.
	//		Created: 22/12/04
	//
	// --------------------------------------------------------------------------
	bool GetClientKeepAliveRequested() const {return mClientKeepAliveRequested;}
	void SetClientKeepAliveRequested(bool keepAlive)
	{
		mClientKeepAliveRequested = keepAlive;
	}

	void AddHeader(const std::string& rName, const std::string& rValue)
	{
		mExtraHeaders.push_back(Header(rName, rValue));
	}
	bool IsExpectingContinue() const { return mExpectContinue; }
	const char* GetVerb() const
	{
		if (!mHttpVerb.empty())
		{
			return mHttpVerb.c_str();
		}
		switch (mMethod)
		{
			case Method_UNINITIALISED: return "Uninitialized";
			case Method_UNKNOWN: return "Unknown";
			case Method_GET: return "GET";
			case Method_HEAD: return "HEAD";
			case Method_POST: return "POST";
			case Method_PUT: return "PUT";
		}
		return "Bad";
	}
	
private:
	void ParseHeaders(IOStreamGetLine &rGetLine, int Timeout);
	void ParseCookies(const std::string &rHeader, int DataStarts);

	enum Method mMethod;
	std::string mRequestURI;
	std::string mHostName;
	int mHostPort;
	std::string mQueryString;
	int mHTTPVersion;
	Query_t mQuery;
	int mContentLength;
	std::string mContentType;
	CookieJar_t *mpCookies;
	bool mClientKeepAliveRequested;
	std::vector<Header> mExtraHeaders;
	bool mExpectContinue;
	IOStream* mpStreamToReadFrom;
	std::string mHttpVerb;
};

#endif // HTTPREQUEST__H

