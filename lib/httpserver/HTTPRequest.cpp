// --------------------------------------------------------------------------
//
// File
//		Name:    HTTPRequest.cpp
//		Purpose: Request object for HTTP connections
//		Created: 26/3/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

#include "HTTPRequest.h"
#include "HTTPQueryDecoder.h"
#include "autogen_HTTPException.h"
#include "IOStream.h"
#include "IOStreamGetLine.h"

#include "MemLeakFindOn.h"

#define MAX_CONTENT_SIZE	(128*1024)

#define ENSURE_COOKIE_JAR_ALLOCATED \
	if(mpCookies == 0) {mpCookies = new CookieJar_t;}



// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPRequest::HTTPRequest()
//		Purpose: Constructor
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
HTTPRequest::HTTPRequest()
	: mMethod(Method_UNINITIALISED),
	  mHostPort(80),	// default if not specified
	  mHTTPVersion(0),
	  mContentLength(-1),
	  mpCookies(0),
	  mClientKeepAliveRequested(false)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPRequest::~HTTPRequest()
//		Purpose: Destructor
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
HTTPRequest::~HTTPRequest()
{
	// Clean up any cookies
	if(mpCookies != 0)
	{
		delete mpCookies;
		mpCookies = 0;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPRequest::Read(IOStreamGetLine &, int)
//		Purpose: Read the request from an IOStreamGetLine (and attached stream)
//				 Returns false if there was no valid request, probably due to 
//				 a kept-alive connection closing.
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
bool HTTPRequest::Read(IOStreamGetLine &rGetLine, int Timeout)
{
	// Check caller's logic
	if(mMethod != Method_UNINITIALISED)
	{
		THROW_EXCEPTION(HTTPException, RequestAlreadyBeenRead)
	}

	// Read the first line, which is of a different format to the rest of the lines
	std::string requestLine;
	if(!rGetLine.GetLine(requestLine, false /* no preprocessing */, Timeout))
	{
		// Didn't get the request line, probably end of connection which had been kept alive
		return false;
	}
//	TRACE1("Request line: %s\n", requestLine.c_str());

	// Check the method
	unsigned int p = 0;	// current position in string
	if(::strncmp(requestLine.c_str(), "GET ", 4) == 0)
	{
		p = 3;
		mMethod = Method_GET;
	}
	else if(::strncmp(requestLine.c_str(), "HEAD ", 5) == 0)
	{
		p = 4;
		mMethod = Method_HEAD;
	}
	else if(::strncmp(requestLine.c_str(), "POST ", 5) == 0)
	{
		p = 4;
		mMethod = Method_POST;
	}
	else
	{
		p = requestLine.find(' ');
		if(p == std::string::npos)
		{
			// No terminating space, looks bad
			p = requestLine.size();
		}
		mMethod = Method_UNKNOWN;
	}

	// Skip spaces to find URI
	const char *requestLinePtr = requestLine.c_str();
	while(requestLinePtr[p] != '\0' && requestLinePtr[p] == ' ')
	{
		++p;
	}
	
	// Check there's a URI following...
	if(requestLinePtr[p] == '\0')
	{
		// Didn't get the request line, probably end of connection which had been kept alive
		return false;
	}
	
	// Read the URI, unescaping any %XX hex codes
	while(requestLinePtr[p] != ' ' && requestLinePtr[p] != '\0')
	{
		// End of URI, on to query string?
		if(requestLinePtr[p] == '?')
		{
			// Put the rest into the query string, without escaping anything
			++p;
			while(requestLinePtr[p] != ' ' && requestLinePtr[p] != '\0')
			{
				mQueryString += requestLinePtr[p];
				++p;
			}
			break;
		}
		// Needs unescaping?
		else if(requestLinePtr[p] == '+')
		{
			mRequestURI += ' ';
		}
		else if(requestLinePtr[p] == '%')
		{
			// Be tolerant about this... bad things are silently accepted,
			// rather than throwing an error.
			char code[4] = {0,0,0,0};
			code[0] = requestLinePtr[++p];
			if(code[0] != '\0')
			{
				code[1] = requestLinePtr[++p];
			}
			
			// Convert into a char code
			long c = ::strtol(code, NULL, 16);
			
			// Accept it?
			if(c > 0 && c <= 255)
			{
				mRequestURI += (char)c;
			}
		}
		else
		{
			// Simple copy of character
			mRequestURI += requestLinePtr[p];
		}

		++p;
	}

	// End of URL?
	if(requestLinePtr[p] == '\0')
	{
		// Assume HTTP 0.9
		mHTTPVersion = HTTPVersion_0_9;
	}
	else
	{
		// Skip any more spaces
		while(requestLinePtr[p] != '\0' && requestLinePtr[p] == ' ')
		{
			++p;
		}

		// Check to see if there's the right string next...
		if(::strncmp(requestLinePtr + p, "HTTP/", 5) == 0)
		{
			// Find the version numbers
			int major, minor;
			if(::sscanf(requestLinePtr + p + 5, "%d.%d", &major, &minor) != 2)
			{
				THROW_EXCEPTION(HTTPException, BadRequest)		
			}
			
			// Store version
			mHTTPVersion = (major * HTTPVersion__MajorMultiplier) + minor;
		}
		else
		{
			// Not good -- wrong string found
			THROW_EXCEPTION(HTTPException, BadRequest)		
		}
	}
	
	TRACE3("HTTPRequest: method=%d, uri=%s, version=%d\n", mMethod, mRequestURI.c_str(), mHTTPVersion);
	
	// If HTTP 1.1 or greater, assume keep-alive
	if(mHTTPVersion >= HTTPVersion_1_1)
	{
		mClientKeepAliveRequested = true;
	}
	
	// Decode query string?
	if((mMethod == Method_GET || mMethod == Method_HEAD) && !mQueryString.empty())
	{
		HTTPQueryDecoder decoder(mQuery);
		decoder.DecodeChunk(mQueryString.c_str(), mQueryString.size());
		decoder.Finish();
	}
	
	// Now parse the headers
	ParseHeaders(rGetLine, Timeout);
	
	// Parse form data?
	if(mMethod == Method_POST && mContentLength >= 0)
	{
		// Too long? Don't allow people to be nasty by sending lots of data
		if(mContentLength > MAX_CONTENT_SIZE)
		{
			THROW_EXCEPTION(HTTPException, POSTContentTooLong)
		}
	
		// Some data in the request to follow, parsing it bit by bit
		HTTPQueryDecoder decoder(mQuery);
		// Don't forget any data left in the GetLine object
		int fromBuffer = rGetLine.GetSizeOfBufferedData();
		if(fromBuffer > mContentLength) fromBuffer = mContentLength;
		if(fromBuffer > 0)
		{
			TRACE1("Decoding %d bytes of data from getline buffer\n", fromBuffer);
			decoder.DecodeChunk((const char *)rGetLine.GetBufferedData(), fromBuffer);
			// And tell the getline object to ignore the data we just used
			rGetLine.IgnoreBufferedData(fromBuffer);
		}
		// Then read any more data, as required
		int bytesToGo = mContentLength - fromBuffer;
		while(bytesToGo > 0)
		{
			char buf[4096];
			int toRead = sizeof(buf);
			if(toRead > bytesToGo) toRead = bytesToGo;
			IOStream &rstream(rGetLine.GetUnderlyingStream());
			int r = rstream.Read(buf, toRead, Timeout);
			if(r == 0)
			{
				// Timeout, just error
				THROW_EXCEPTION(HTTPException, RequestReadFailed)
			}
			decoder.DecodeChunk(buf, r);
			bytesToGo -= r;
		}
		// Finish off
		decoder.Finish();
	}
	
	return true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPRequest::ParseHeaders(IOStreamGetLine &, int)
//		Purpose: Private. Parse the headers of the request
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
void HTTPRequest::ParseHeaders(IOStreamGetLine &rGetLine, int Timeout)
{
	std::string header;
	bool haveHeader = false;
	while(true)
	{
		if(rGetLine.IsEOF())
		{
			// Header terminates unexpectedly
			THROW_EXCEPTION(HTTPException, BadRequest)		
		}

		std::string currentLine;	
		if(!rGetLine.GetLine(currentLine, false /* no preprocess */, Timeout))
		{
			// Timeout
			THROW_EXCEPTION(HTTPException, RequestReadFailed)
		}
		
		// Is this a continuation of the previous line?
		bool processHeader = haveHeader;
		if(!currentLine.empty() && (currentLine[0] == ' ' || currentLine[0] == '\t'))
		{
			// A continuation, don't process anything yet
			processHeader = false;
		}
		//TRACE3("%d:%d:%s\n", processHeader, haveHeader, currentLine.c_str());
		
		// Parse the header -- this will actually process the header
		// from the previous run around the loop.
		if(processHeader)
		{
			// Find where the : is in the line
			const char *h = header.c_str();
			int p = 0;
			while(h[p] != '\0' && h[p] != ':')
			{
				++p;
			}
			// Skip white space
			int dataStart = p + 1;
			while(h[dataStart] == ' ' || h[dataStart] == '\t')
			{
				++dataStart;
			}
		
			if(p == sizeof("Content-Length")-1
				&& ::strncasecmp(h, "Content-Length", sizeof("Content-Length")-1) == 0)
			{
				// Decode number
				long len = ::strtol(h + dataStart, NULL, 10);	// returns zero in error case, this is OK
				if(len < 0) len = 0;
				// Store
				mContentLength = len;
			}
			else if(p == sizeof("Content-Type")-1
				&& ::strncasecmp(h, "Content-Type", sizeof("Content-Type")-1) == 0)
			{
				// Store rest of string as content type
				mContentType = h + dataStart;
			}
			else if(p == sizeof("Host")-1
				&& ::strncasecmp(h, "Host", sizeof("Host")-1) == 0)
			{
				// Store host header
				mHostName = h + dataStart;
				
				// Is there a port number to split off?
				std::string::size_type colon = mHostName.find_first_of(':');
				if(colon != std::string::npos)
				{
					// There's a port in the string... attempt to turn it into an int
					mHostPort = ::strtol(mHostName.c_str() + colon + 1, 0, 10);
					
					// Truncate the string to just the hostname
					mHostName = mHostName.substr(0, colon);
					
					TRACE2("Host: header, hostname = '%s', host port = %d\n", mHostName.c_str(), mHostPort);
				}
			}
			else if(p == sizeof("Cookie")-1
				&& ::strncasecmp(h, "Cookie", sizeof("Cookie")-1) == 0)
			{
				// Parse cookies
				ParseCookies(header, dataStart);
			}
			else if(p == sizeof("Connection")-1
				&& ::strncasecmp(h, "Connection", sizeof("Connection")-1) == 0)
			{
				// Connection header, what is required?
				const char *v = h + dataStart;
				if(::strcasecmp(v, "close") == 0)
				{
					mClientKeepAliveRequested = false;
				}
				else if(::strcasecmp(v, "keep-alive") == 0)
				{
					mClientKeepAliveRequested = true;
				}
				// else don't understand, just assume default for protocol version
			}
			// else ignore it
			
			// Unset have header flag, as it's now been processed
			haveHeader = false;
		}

		// Store the chunk of header the for next time round
		if(haveHeader)
		{
			header += currentLine;
		}
		else
		{
			header = currentLine;
			haveHeader = true;
		}

		// End of headers?
		if(currentLine.empty())
		{
			// All done!
			break;
		}		
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPRequest::ParseCookies(const std::string &, int)
//		Purpose: Parse the cookie header
//		Created: 20/8/04
//
// --------------------------------------------------------------------------
void HTTPRequest::ParseCookies(const std::string &rHeader, int DataStarts)
{
	const char *data = rHeader.c_str() + DataStarts;
	const char *pos = data;
	const char *itemStart = pos;
	std::string name;
	
	enum
	{
		s_NAME, s_VALUE, s_VALUE_QUOTED, s_FIND_NEXT_NAME
	} state = s_NAME;

	do	
	{
		switch(state)
		{
		case s_NAME:
			{
				if(*pos == '=')
				{
					// Found the name. Store
					name.assign(itemStart, pos - itemStart);
					// Looking at values now
					state = s_VALUE;
					if((*(pos + 1)) == '"')
					{
						// Actually it's a quoted value, skip over that
						++pos;
						state = s_VALUE_QUOTED;
					}
					// Record starting point for this item
					itemStart = pos + 1;
				}
			}
			break;
		
		case s_VALUE:
			{
				if(*pos == ';' || *pos == ',' || *pos == '\0')
				{
					// Name ends
					ENSURE_COOKIE_JAR_ALLOCATED
					std::string value(itemStart, pos - itemStart);
					(*mpCookies)[name] = value;
					// And move to the waiting stage
					state = s_FIND_NEXT_NAME;
				}
			}
			break;
		
		case s_VALUE_QUOTED:
			{
				if(*pos == '"')
				{
					// That'll do nicely, save it
					ENSURE_COOKIE_JAR_ALLOCATED
					std::string value(itemStart, pos - itemStart);
					(*mpCookies)[name] = value;
					// And move to the waiting stage
					state = s_FIND_NEXT_NAME;
				}
			}
			break;
		
		case s_FIND_NEXT_NAME:
			{
				// Skip over terminators and white space to get to the next name
				if(*pos != ';' && *pos != ',' && *pos != ' ' && *pos != '\t')
				{
					// Name starts here
					itemStart = pos;
					state = s_NAME;
				}
			}
			break;
		
		default:
			// Ooops
			THROW_EXCEPTION(HTTPException, Internal)
			break;
		}
	}
	while(*(pos++) != 0);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPRequest::GetCookie(const char *, std::string &) const
//		Purpose: Fetch a cookie's value. If cookie not present, returns false
//				 and string is unaltered.
//		Created: 20/8/04
//
// --------------------------------------------------------------------------
bool HTTPRequest::GetCookie(const char *CookieName, std::string &rValueOut) const
{
	// Got any cookies?
	if(mpCookies == 0)
	{
		return false;
	}
	
	// See if it's there
	CookieJar_t::const_iterator v(mpCookies->find(std::string(CookieName)));
	if(v != mpCookies->end())
	{
		// Return the value
		rValueOut = v->second;
		return true;
	}
	
	return false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPRequest::GetCookie(const char *)
//		Purpose: Return a string for the given cookie, or the null string if the
//				 cookie has not been recieved.
//		Created: 22/8/04
//
// --------------------------------------------------------------------------
const std::string &HTTPRequest::GetCookie(const char *CookieName) const
{
	static const std::string noCookie;

	// Got any cookies?
	if(mpCookies == 0)
	{
		return noCookie;
	}
	
	// See if it's there
	CookieJar_t::const_iterator v(mpCookies->find(std::string(CookieName)));
	if(v != mpCookies->end())
	{
		// Return the value
		return v->second;
	}
	
	return noCookie;
}



