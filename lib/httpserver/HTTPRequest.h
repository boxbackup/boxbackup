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
class HTTPRequest
{
public:
	HTTPRequest();
	~HTTPRequest();
private:
	// no copying
	HTTPRequest(const HTTPRequest &);
	HTTPRequest &operator=(const HTTPRequest &);
public:

	typedef std::multimap<std::string, std::string> Query_t;
	typedef std::pair<std::string, std::string> QueryEn_t;

	enum
	{
		Method_UNINITIALISED = -1,
		Method_UNKNOWN = 0,
		Method_GET = 1,
		Method_HEAD = 2,
		Method_POST = 3
	};
	
	enum
	{
		HTTPVersion__MajorMultiplier = 1000,
		HTTPVersion_0_9 = 9,
		HTTPVersion_1_0 = 1000,
		HTTPVersion_1_1 = 1001
	};

	bool Read(IOStreamGetLine &rGetLine, int Timeout);

	typedef std::map<std::string, std::string> CookieJar_t;
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    HTTPResponse::Get*()
	//		Purpose: Various Get accessors
	//		Created: 26/3/04
	//
	// --------------------------------------------------------------------------
	int GetMethod() const {return mMethod;}
	const std::string &GetRequestURI() const {return mRequestURI;}
	const std::string &GetHostName() const {return mHostName;}	// note: request does splitting of Host: header
	const int GetHostPort() const {return mHostPort;}  // into host name and port number
	const std::string &GetQueryString() const {return mQueryString;}
	int GetHTTPVersion() const {return mHTTPVersion;}
	const Query_t &GetQuery() const {return mQuery;}
	int GetContentLength() const {return mContentLength;}
	const std::string &GetContentType() const {return mContentType;}
	const CookieJar_t *GetCookies() const {return mpCookies;} // WARNING: May return NULL
	bool GetCookie(const char *CookieName, std::string &rValueOut) const;
	const std::string &GetCookie(const char *CookieName) const;


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

private:
	void ParseHeaders(IOStreamGetLine &rGetLine, int Timeout);
	void ParseCookies(const std::string &rHeader, int DataStarts);

private:
	int mMethod;
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
};

#endif // HTTPREQUEST__H

