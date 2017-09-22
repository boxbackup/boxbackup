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
	  mpStreamToSendTo(NULL)
{
}


// allow copying, but be very careful with the response stream,
// you can only read it once! (this class doesn't police it).
HTTPResponse::HTTPResponse(const HTTPResponse& rOther)
: mResponseCode(rOther.mResponseCode),
  mResponseIsDynamicContent(rOther.mResponseIsDynamicContent),
  mpStreamToSendTo(rOther.mpStreamToSendTo),
  mHeaders(rOther.mHeaders)
{
	Write(rOther.GetBuffer(), rOther.GetSize());
	if(rOther.IsSetForReading())
	{
		SetForReading();
	}
}


HTTPResponse &HTTPResponse::operator=(const HTTPResponse &rOther)
{
	Reset();
	Write(rOther.GetBuffer(), rOther.GetSize());
	mResponseCode = rOther.mResponseCode;
	mResponseIsDynamicContent = rOther.mResponseIsDynamicContent;
	mHeaders = rOther.mHeaders;
	mpStreamToSendTo = rOther.mpStreamToSendTo;
	if(rOther.IsSetForReading())
	{
		SetForReading();
	}
	return *this;
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
	case Code_BadRequest: return "400 Bad Request"; break;
	case Code_Unauthorized: return "401 Unauthorized"; break;
	case Code_Forbidden: return "403 Forbidden"; break;
	case Code_NotFound: return "404 Not Found"; break;
	case Code_Conflict: return "409 Conflict"; break;
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
//		Name:    HTTPResponse::Send(IOStream &, bool)
//		Purpose: Build the response, and send via the stream.
//		Created: 26/3/2004
//
// --------------------------------------------------------------------------
void HTTPResponse::Send(int Timeout)
{
	if (!mpStreamToSendTo)
	{
		THROW_EXCEPTION(HTTPException, NoStreamConfigured);
	}

	if (GetSize() != 0 && mHeaders.GetContentType().empty())
	{
		THROW_EXCEPTION(HTTPException, NoContentTypeSet);
	}

	// Build and send header
	{
		std::ostringstream header;
		header << "HTTP/1.1 ";
		header << ResponseCodeToString(mResponseCode);
		header << "\r\n";
		mpStreamToSendTo->Write(header.str(), Timeout);

		// Control whether the response is cached
		if(mResponseIsDynamicContent)
		{
			// dynamic is private and can't be cached
			mHeaders.AddHeader("Cache-Control", "no-cache, private");
		}
		else
		{
			// static is allowed to be cached for a day
			mHeaders.AddHeader("Cache-Control", "max-age=86400");
		}

		// Write to stream
		mHeaders.WriteTo(*mpStreamToSendTo, Timeout);

		// NOTE: header ends with blank line in all cases
		mpStreamToSendTo->Write(std::string("\r\n"), Timeout);
	}

	// Send content
	SetForReading();
	CopyStreamTo(*mpStreamToSendTo, Timeout);
}

void HTTPResponse::SendContinue(int Timeout)
{
	mpStreamToSendTo->Write(std::string("HTTP/1.1 100 Continue\r\n"), Timeout);
}

void HTTPResponse::Receive(IOStream& rStream, int Timeout)
{
	IOStreamGetLine rGetLine(rStream);

	if(rGetLine.IsEOF())
	{
		// Connection terminated unexpectedly
		THROW_EXCEPTION_MESSAGE(HTTPException, BadResponse,
			"HTTP server closed the connection without sending a response");
	}

	std::string statusLine;
	while(true)
	{
		try
		{
			statusLine = rGetLine.GetLine(false /* no preprocess */, Timeout);
			break;
		}
		catch(BoxException &e)
		{
			if(EXCEPTION_IS_TYPE(e, CommonException, SignalReceived))
			{
				// try again
				continue;
			}
			else if(EXCEPTION_IS_TYPE(e, CommonException, GetLineEOF))
			{
				THROW_EXCEPTION_MESSAGE(HTTPException, BadResponse,
					"Server disconnected before sending status line");
			}
			else if(EXCEPTION_IS_TYPE(e, CommonException, IOStreamTimedOut))
			{
				THROW_EXCEPTION_MESSAGE(HTTPException, ResponseTimedOut,
					"Server took too long to send the status line");
			}
			else
			{
				throw;
			}
		}
	}

	if(statusLine.substr(0, 7) != "HTTP/1." || statusLine[8] != ' ')
	{
		THROW_EXCEPTION_MESSAGE(HTTPException, BadResponse,
			"HTTP server sent an invalid HTTP status line: " << statusLine);
	}

	if(statusLine[5] == '1' && statusLine[7] == '1')
	{
		// HTTP/1.1 default is to keep alive
		mHeaders.SetKeepAlive(true);
	}

	// Decode the status code
	long status = ::strtol(statusLine.substr(9, 3).c_str(), NULL, 10);
	// returns zero in error case, this is OK
	if(status < 0) status = 0;
	// Store
	mResponseCode = status;

	// 100 Continue responses have no headers, terminating newline, or body
	if(status == 100)
	{
		return;
	}

	mHeaders.ReadFromStream(rGetLine, Timeout);
	int remaining_bytes = mHeaders.GetContentLength();

	// push back whatever bytes we have left
	// rGetLine.DetachFile();
	if(remaining_bytes == -1 || remaining_bytes > 0)
	{
		if(remaining_bytes != -1 &&
			remaining_bytes < rGetLine.GetSizeOfBufferedData())
		{
			// very small response, not good!
			THROW_EXCEPTION_MESSAGE(HTTPException, BadResponse,
				"HTTP server sent a very small response: " <<
				mHeaders.GetContentLength() << " bytes");
		}

		if(remaining_bytes > 0)
		{
			remaining_bytes -= rGetLine.GetSizeOfBufferedData();
		}

		Write(rGetLine.GetBufferedData(),
			rGetLine.GetSizeOfBufferedData());
	}

	while(remaining_bytes != 0) // could be -1 as well
	{
		char buffer[4096];
		int readSize = sizeof(buffer);

		if(remaining_bytes > 0 && remaining_bytes < readSize)
		{
			readSize = remaining_bytes;
		}

		readSize = rStream.Read(buffer, readSize, Timeout);
		if(readSize == 0)
		{
			break;
		}

		Write(buffer, readSize);
		if(remaining_bytes > 0)
		{
			remaining_bytes -= readSize;
		}
	}

	SetForReading();
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

	AddHeader("Set-Cookie", h);
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
		|| !mHeaders.GetContentType().empty()
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
	mHeaders.AddHeader("location", header);

	// Set up some default content
	mHeaders.SetContentType("text/html");
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
		|| !mHeaders.GetExtraHeaders().empty()
		|| !mHeaders.GetContentType().empty()
		|| GetSize() != 0)
	{
		THROW_EXCEPTION(HTTPException, CannotSetNotFoundIfReponseHasData)
	}

	// Set response code
	mResponseCode = Code_NotFound;

	// Set data
	mHeaders.SetContentType("text/html");
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


