// --------------------------------------------------------------------------
//
// File
//		Name:    HTTPResponse.cpp
//		Purpose: Response object for HTTP connections
//		Created: 26/3/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>
#include <string.h>

#include "HTTPResponse.h"
#include "IOStreamGetLine.h"
#include "autogen_HTTPException.h"

#include "MemLeakFindOn.h"

// Static variables
std::string HTTPResponse::msDefaultURIPrefix;


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::HTTPResponse(IOStream*)
//		Purpose: Constructor for response to be sent to a stream
//		Created: 04/01/09
//
// --------------------------------------------------------------------------
HTTPResponse::HTTPResponse(IOStream* pStreamToSendTo)
	: mResponseCode(HTTPResponse::Code_NoContent),
	  mResponseIsDynamicContent(true),
	  mKeepAlive(false),
	  mContentLength(-1),
	  mpStreamToSendTo(pStreamToSendTo)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::HTTPResponse()
//		Purpose: Constructor
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
HTTPResponse::HTTPResponse()
	: mResponseCode(HTTPResponse::Code_NoContent),
	  mResponseIsDynamicContent(true),
	  mKeepAlive(false),
	  mContentLength(-1),
	  mpStreamToSendTo(NULL)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::~HTTPResponse()
//		Purpose: Destructor
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
HTTPResponse::~HTTPResponse()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::ResponseCodeToString(int)
//		Purpose: Return string equivalent of the response code,
//			 suitable for Status: headers
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
const char *HTTPResponse::ResponseCodeToString(int ResponseCode)
{
	switch(ResponseCode)
	{
	case Code_OK: return "200 OK"; break;
	case Code_NoContent: return "204 No Content"; break;
	case Code_MovedPermanently: return "301 Moved Permanently"; break;
	case Code_Found: return "302 Found"; break;
	case Code_NotModified: return "304 Not Modified"; break;
	case Code_TemporaryRedirect: return "307 Temporary Redirect"; break;
	case Code_MethodNotAllowed: return "400 Method Not Allowed"; break;
	case Code_Unauthorized: return "401 Unauthorized"; break;
	case Code_Forbidden: return "403 Forbidden"; break;
	case Code_NotFound: return "404 Not Found"; break;
	case Code_InternalServerError: return "500 Internal Server Error"; break;
	case Code_NotImplemented: return "501 Not Implemented"; break;
	default:
		{
			THROW_EXCEPTION(HTTPException, UnknownResponseCodeUsed)
		}
	}
	return "500 Internal Server Error";
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::SetResponseCode(int)
//		Purpose: Set the response code to be returned
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
void HTTPResponse::SetResponseCode(int Code)
{
	mResponseCode = Code;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::SetContentType(const char *)
//		Purpose: Set content type
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
void HTTPResponse::SetContentType(const char *ContentType)
{
	mContentType = ContentType;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::Send(IOStream &, bool)
//		Purpose: Build the response, and send via the stream.
//			 Optionally omitting the content.
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
void HTTPResponse::Send(bool OmitContent)
{
	if (!mpStreamToSendTo)
	{
		THROW_EXCEPTION(HTTPException, NoStreamConfigured);
	}
	
	if (GetSize() != 0 && mContentType.empty())
	{
		THROW_EXCEPTION(HTTPException, NoContentTypeSet);
	}

	// Build and send header
	{
		std::string header("HTTP/1.1 ");
		header += ResponseCodeToString(mResponseCode);
		header += "\r\nContent-Type: ";
		header += mContentType;
		header += "\r\nContent-Length: ";
		{
			char len[32];
			::sprintf(len, "%d", OmitContent?(0):(GetSize()));
			header += len;
		}
		// Extra headers...
		for(std::vector<std::pair<std::string, std::string> >::const_iterator i(mExtraHeaders.begin()); i != mExtraHeaders.end(); ++i)
		{
			header += "\r\n";
			header += i->first + ": " + i->second;
		}
		// NOTE: a line ending must be included here in all cases
		// Control whether the response is cached
		if(mResponseIsDynamicContent)
		{
			// dynamic is private and can't be cached
			header += "\r\nCache-Control: no-cache, private";
		}
		else
		{
			// static is allowed to be cached for a day
			header += "\r\nCache-Control: max-age=86400";
		}
		if(mKeepAlive)
		{
			header += "\r\nConnection: keep-alive\r\n\r\n";
		}
		else
		{
			header += "\r\nConnection: close\r\n\r\n";
		}
		// NOTE: header ends with blank line in all cases
		
		// Write to stream
		mpStreamToSendTo->Write(header.c_str(), header.size());
	}
	
	// Send content
	if(!OmitContent)
	{
		mpStreamToSendTo->Write(GetBuffer(), GetSize());
	}
}

void HTTPResponse::SendContinue()
{
	mpStreamToSendTo->Write("HTTP/1.1 100 Continue\r\n");
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::ParseHeaders(IOStreamGetLine &, int)
//		Purpose: Private. Parse the headers of the response
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
void HTTPResponse::ParseHeaders(IOStreamGetLine &rGetLine, int Timeout)
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
			else if(p == sizeof("Cookie")-1
				&& ::strncasecmp(h, "Cookie", sizeof("Cookie")-1) == 0)
			{
				THROW_EXCEPTION(HTTPException, NotImplemented);
				/*
				// Parse cookies
				ParseCookies(header, dataStart);
				*/
			}
			else if(p == sizeof("Connection")-1
				&& ::strncasecmp(h, "Connection", sizeof("Connection")-1) == 0)
			{
				// Connection header, what is required?
				const char *v = h + dataStart;
				if(::strcasecmp(v, "close") == 0)
				{
					mKeepAlive = false;
				}
				else if(::strcasecmp(v, "keep-alive") == 0)
				{
					mKeepAlive = true;
				}
				// else don't understand, just assume default for protocol version
			}
			else
			{
				std::string headerName = header.substr(0, p);
				AddHeader(headerName, h + dataStart);
			}
			
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

void HTTPResponse::Receive(IOStream& rStream, int Timeout)
{
	IOStreamGetLine rGetLine(rStream);

	if(rGetLine.IsEOF())
	{
		// Connection terminated unexpectedly
		THROW_EXCEPTION(HTTPException, BadResponse)		
	}

	std::string statusLine;	
	if(!rGetLine.GetLine(statusLine, false /* no preprocess */, Timeout))
	{
		// Timeout
		THROW_EXCEPTION(HTTPException, ResponseReadFailed)
	}

	if (statusLine.substr(0, 7) != "HTTP/1." ||
		statusLine[8] != ' ')
	{
		// Status line terminated unexpectedly
		BOX_ERROR("Bad response status line: " << statusLine);
		THROW_EXCEPTION(HTTPException, BadResponse)		
	}

	if (statusLine[5] == '1' && statusLine[7] == '1')
	{
		// HTTP/1.1 default is to keep alive
		mKeepAlive = true;
	}
		
	// Decode the status code
	long status = ::strtol(statusLine.substr(9, 3).c_str(), NULL, 10);
	// returns zero in error case, this is OK
	if (status < 0) status = 0;
	// Store
	mResponseCode = status;

	// 100 Continue responses have no headers, terminating newline, or body
	if (status == 100)
	{
		return;
	}
	
	ParseHeaders(rGetLine, Timeout);

	// push back whatever bytes we have left
	// rGetLine.DetachFile();
	if (mContentLength > 0)
	{
		if (mContentLength < rGetLine.GetSizeOfBufferedData())
		{
			// very small response, not good!
			THROW_EXCEPTION(HTTPException, NotImplemented);
		}

		mContentLength -= rGetLine.GetSizeOfBufferedData();

		Write(rGetLine.GetBufferedData(),
			rGetLine.GetSizeOfBufferedData());
	}

	while (mContentLength != 0) // could be -1 as well
	{
		char buffer[4096];
		int readSize = sizeof(buffer);
		if (mContentLength > 0 && mContentLength < readSize)
		{
			readSize = mContentLength;
		}
		readSize = rStream.Read(buffer, readSize, Timeout);
		if (readSize == 0)
		{
			break;
		}
		mContentLength -= readSize;
		Write(buffer, readSize);
	}

	SetForReading();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::AddHeader(const char *)
//		Purpose: Add header, given entire line
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
/*
void HTTPResponse::AddHeader(const char *EntireHeaderLine)
{
	mExtraHeaders.push_back(std::string(EntireHeaderLine));
}
*/

// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::AddHeader(const std::string &)
//		Purpose: Add header, given entire line
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
/*
void HTTPResponse::AddHeader(const std::string &rEntireHeaderLine)
{
	mExtraHeaders.push_back(rEntireHeaderLine);
}
*/

// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::AddHeader(const char *, const char *)
//		Purpose: Add header, given header name and it's value
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
void HTTPResponse::AddHeader(const char *pHeader, const char *pValue)
{
	mExtraHeaders.push_back(Header(pHeader, pValue));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::AddHeader(const char *, const std::string &)
//		Purpose: Add header, given header name and it's value
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
void HTTPResponse::AddHeader(const char *pHeader, const std::string &rValue)
{
	mExtraHeaders.push_back(Header(pHeader, rValue));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::AddHeader(const std::string &, const std::string &)
//		Purpose: Add header, given header name and it's value
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
void HTTPResponse::AddHeader(const std::string &rHeader, const std::string &rValue)
{
	mExtraHeaders.push_back(Header(rHeader, rValue));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::SetCookie(const char *, const char *, const char *, int)
//		Purpose: Sets a cookie, using name, value, path and expiry time.
//		Created: 20/8/04
//
// --------------------------------------------------------------------------
void HTTPResponse::SetCookie(const char *Name, const char *Value, const char *Path, int ExpiresAt)
{
	if(ExpiresAt != 0)
	{
		THROW_EXCEPTION(HTTPException, NotImplemented)
	}

	// Appears you shouldn't use quotes when you generate set-cookie headers.
	// Oh well. It was fun finding that out.
/*	std::string h("Set-Cookie: ");
	h += Name;
	h += "=\"";
	h += Value;
	h += "\"; Version=\"1\"; Path=\"";
	h += Path;
	h += "\"";
*/
	std::string h;
	h += Name;
	h += "=";
	h += Value;
	h += "; Version=1; Path=";
	h += Path;

	mExtraHeaders.push_back(Header("Set-Cookie", h));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::SetAsRedirect(const char *, bool)
//		Purpose: Sets the response objects to be a redirect to another page.
//				 If IsLocalURL == true, the default prefix will be added.
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
void HTTPResponse::SetAsRedirect(const char *RedirectTo, bool IsLocalURI)
{
	if(mResponseCode != HTTPResponse::Code_NoContent
		|| !mContentType.empty()
		|| GetSize() != 0)
	{
		THROW_EXCEPTION(HTTPException, CannotSetRedirectIfReponseHasData)
	}

	// Set response code
	mResponseCode = Code_Found;

	// Set location to redirect to
	std::string header;
	if(IsLocalURI) header += msDefaultURIPrefix;
	header += RedirectTo;
	mExtraHeaders.push_back(Header("Location", header));
	
	// Set up some default content
	mContentType = "text/html";
	#define REDIRECT_HTML_1 "<html><head><title>Redirection</title></head>\n<body><p><a href=\""
	#define REDIRECT_HTML_2 "\">Redirect to content</a></p></body></html>\n"
	Write(REDIRECT_HTML_1, sizeof(REDIRECT_HTML_1) - 1);
	if(IsLocalURI) Write(msDefaultURIPrefix.c_str(), msDefaultURIPrefix.size());
	Write(RedirectTo, ::strlen(RedirectTo));
	Write(REDIRECT_HTML_2, sizeof(REDIRECT_HTML_2) - 1);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::SetAsNotFound(const char *)
//		Purpose: Set the response object to be a standard page not found 404 response.
//		Created: 7/4/04
//
// --------------------------------------------------------------------------
void HTTPResponse::SetAsNotFound(const char *URI)
{
	if(mResponseCode != HTTPResponse::Code_NoContent
		|| mExtraHeaders.size() != 0
		|| !mContentType.empty()
		|| GetSize() != 0)
	{
		THROW_EXCEPTION(HTTPException, CannotSetNotFoundIfReponseHasData)
	}

	// Set response code
	mResponseCode = Code_NotFound;

	// Set data
	mContentType = "text/html";
	#define NOT_FOUND_HTML_1 "<html><head><title>404 Not Found</title></head>\n<body><h1>404 Not Found</h1>\n<p>The URI <i>"
	#define NOT_FOUND_HTML_2 "</i> was not found on this server.</p></body></html>\n"
	Write(NOT_FOUND_HTML_1, sizeof(NOT_FOUND_HTML_1) - 1);
	WriteStringDefang(std::string(URI));
	Write(NOT_FOUND_HTML_2, sizeof(NOT_FOUND_HTML_2) - 1);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPResponse::WriteStringDefang(const char *, unsigned int)
//		Purpose: Writes a string 'defanged', ie has HTML special characters escaped
//				 so that people can't output arbitary HTML by playing with
//				 URLs and form parameters, and it's safe to write strings into
//				 HTML element attribute values.
//		Created: 9/4/04
//
// --------------------------------------------------------------------------
void HTTPResponse::WriteStringDefang(const char *String, unsigned int StringLen)
{
	while(StringLen > 0)
	{
		unsigned int toWrite = 0;
		while(toWrite < StringLen 
			&& String[toWrite] != '<' 
			&& String[toWrite] != '>'
			&& String[toWrite] != '&'
			&& String[toWrite] != '"')
		{
			++toWrite;
		}
		if(toWrite > 0)
		{
			Write(String, toWrite);
			StringLen -= toWrite;
			String += toWrite;
		}
		
		// Is it a bad character next?
		while(StringLen > 0)
		{
			bool notSpecial = false;
			switch(*String)
			{
				case '<': Write("&lt;", 4); break;
				case '>': Write("&gt;", 4); break;
				case '&': Write("&amp;", 5); break;
				case '"': Write("&quot;", 6); break;
				default:
					// Stop this loop
					notSpecial = true;
					break;
			}
			if(notSpecial) break;
			++String;
			--StringLen;
		}
	}
}


