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
#include "HTTPResponse.h"
#include "HTTPQueryDecoder.h"
#include "autogen_HTTPException.h"
#include "IOStream.h"
#include "IOStreamGetLine.h"
#include "Logging.h"
#include "PartialReadStream.h"
#include "ReadGatherStream.h"

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
	  mHTTPVersion(0),
	  mpCookies(0),
	  mExpectContinue(false),
	  mpStreamToReadFrom(NULL)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPRequest::HTTPRequest(enum Method,
//			 const std::string&)
//		Purpose: Alternate constructor for hand-crafted requests
//		Created: 03/01/09
//
// --------------------------------------------------------------------------
HTTPRequest::HTTPRequest(enum Method method, const std::string& rURI)
	: mMethod(method),
	  mRequestURI(rURI),
	  mHTTPVersion(HTTPVersion_1_1),
	  mpCookies(0),
	  mExpectContinue(false),
	  mpStreamToReadFrom(NULL)
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
//		Name:    HTTPRequest::GetMethodName()
//		Purpose: Returns the name of the request's HTTP method verb
//			 as a string.
//		Created: 28/7/15
//
// --------------------------------------------------------------------------

std::string HTTPRequest::GetMethodName() const
{
	switch(mMethod)
	{
		case Method_UNINITIALISED: return "uninitialised";
		case Method_UNKNOWN: return "unknown";
		case Method_GET: return "GET";
		case Method_HEAD: return "HEAD";
		case Method_POST: return "POST";
		case Method_PUT: return "PUT";
		case Method_DELETE: return "DELETE";
		default:
			std::ostringstream oss;
			oss << "unknown-" << mMethod;
			return oss.str();
	};
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPRequest::Receive(IOStreamGetLine &, int)
//		Purpose: Read the request from an IOStreamGetLine (and
//			 attached stream).
//			 Returns false if there was no valid request,
//			 probably due to a kept-alive connection closing.
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
bool HTTPRequest::Receive(IOStreamGetLine &rGetLine, int Timeout)
{
	// Check caller's logic
	if(mMethod != Method_UNINITIALISED)
	{
		THROW_EXCEPTION(HTTPException, RequestAlreadyBeenRead);
	}

	// Read the first line, which is of a different format to the rest of the lines
	std::string requestLine;
	if(!rGetLine.GetLine(requestLine, false /* no preprocessing */, Timeout))
	{
		// Didn't get the request line, probably end of connection which had been kept alive
		return false;
	}
	BOX_TRACE("Request line: " << requestLine);

	// Check the method
	size_t p = 0;	// current position in string
	p = requestLine.find(' '); // end of first word

	if(p == std::string::npos)
	{
		// No terminating space, looks bad
		p = requestLine.size();
	}
	else
	{
		mHttpVerb = requestLine.substr(0, p);
		if (mHttpVerb == "GET")
		{
			mMethod = Method_GET;
		}
		else if (mHttpVerb == "HEAD")
		{
			mMethod = Method_HEAD;
		}
		else if (mHttpVerb == "POST")
		{
			mMethod = Method_POST;
		}
		else if (mHttpVerb == "PUT")
		{
			mMethod = Method_PUT;
		}
		else if (mHttpVerb == "DELETE")
		{
			mMethod = Method_DELETE;
		}
		else
		{
			BOX_WARNING("Received HTTP request with unrecognised method: " <<
				mHttpVerb);
			mMethod = Method_UNKNOWN;
		}
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
				THROW_EXCEPTION_MESSAGE(HTTPException, BadRequest,
					"Unable to parse HTTP version number: " <<
					requestLinePtr);
			}

			// Store version
			mHTTPVersion = (major * HTTPVersion__MajorMultiplier) + minor;
		}
		else
		{
			// Not good -- wrong string found
			THROW_EXCEPTION_MESSAGE(HTTPException, BadRequest,
				"Unable to parse HTTP request line: " <<
				requestLinePtr);
		}
	}

	BOX_TRACE("HTTPRequest: method=" << mMethod << ", uri=" <<
		mRequestURI << ", version=" << mHTTPVersion);

	// If HTTP 1.1 or greater, assume keep-alive
	if(mHTTPVersion >= HTTPVersion_1_1)
	{
		mHeaders.SetKeepAlive(true);
	}

	// Decode query string?
	if((mMethod == Method_GET || mMethod == Method_HEAD) && !mQueryString.empty())
	{
		HTTPQueryDecoder decoder(mQuery);
		decoder.DecodeChunk(mQueryString.c_str(), mQueryString.size());
		decoder.Finish();
	}

	// Now parse the headers
	mHeaders.ReadFromStream(rGetLine, Timeout);

	std::string expected;
	if(GetHeader("Expect", &expected))
	{
		if(expected == "100-continue")
		{
			mExpectContinue = true;
		}
	}

	const std::string& cookies = mHeaders.GetHeaderValue("cookie", false);
	// false => not required, returns "" if header is not present.
	if(!cookies.empty())
	{
		ParseCookies(cookies);
	}

	// Parse form data?
	int64_t contentLength = mHeaders.GetContentLength();
	if(mMethod == Method_POST && contentLength >= 0)
	{
		// Too long? Don't allow people to be nasty by sending lots of data
		if(contentLength > MAX_CONTENT_SIZE)
		{
			THROW_EXCEPTION_MESSAGE(HTTPException, POSTContentTooLong,
				"Client tried to upload " << contentLength << " bytes of "
				"content, but our maximum supported size is " <<
				MAX_CONTENT_SIZE);
		}

		// Some data in the request to follow, parsing it bit by bit
		HTTPQueryDecoder decoder(mQuery);

		// Don't forget any data left in the GetLine object
		int fromBuffer = rGetLine.GetSizeOfBufferedData();
		if(fromBuffer > contentLength)
		{
			fromBuffer = contentLength;
		}

		if(fromBuffer > 0)
		{
			BOX_TRACE("Decoding " << fromBuffer << " bytes of "
				"data from getline buffer");
			decoder.DecodeChunk((const char *)rGetLine.GetBufferedData(), fromBuffer);
			// And tell the getline object to ignore the data we just used
			rGetLine.IgnoreBufferedData(fromBuffer);
		}

		// Then read any more data, as required
		int bytesToGo = contentLength - fromBuffer;
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
				THROW_EXCEPTION_MESSAGE(HTTPException, RequestReadFailed,
					"Failed to read complete request with the timeout");
			}
			decoder.DecodeChunk(buf, r);
			bytesToGo -= r;
		}
		// Finish off
		decoder.Finish();
	}
	else
	{
		IOStream::pos_type bytesToCopy = rGetLine.GetSizeOfBufferedData();
		if (contentLength != -1 && bytesToCopy > contentLength)
		{
			bytesToCopy = contentLength;
		}
		Write(rGetLine.GetBufferedData(), bytesToCopy);
		SetForReading();
		mpStreamToReadFrom = &(rGetLine.GetUnderlyingStream());
	}

	return true;
}

void HTTPRequest::ReadContent(IOStream& rStreamToWriteTo, int Timeout)
{
	// TODO FIXME: POST requests (above) do not set mpStreamToReadFrom, would we
	// ever want to call ReadContent() on them? I hope not!
	ASSERT(mpStreamToReadFrom != NULL);

	Seek(0, SeekType_Absolute);

	// Copy any data that we've already buffered.
	CopyStreamTo(rStreamToWriteTo, Timeout);
	IOStream::pos_type bytesCopied = GetSize();

	// Copy the data stream, but only upto the content-length.
	int64_t contentLength = mHeaders.GetContentLength();
	if(contentLength == -1)
	{
		// There is no content-length, so copy all of it. Include the buffered
		// data (already copied above) in the final content-length, which we
		// update in the HTTPRequest headers.
		contentLength = bytesCopied;

		// If there is a stream to read from, then copy its contents too.
		if(mpStreamToReadFrom != NULL)
		{
			contentLength +=
				mpStreamToReadFrom->CopyStreamTo(rStreamToWriteTo,
					Timeout);
		}
		mHeaders.SetContentLength(contentLength);
	}
	else
	{
		// Subtract the amount of data already buffered (and already copied above)
		// from the total content-length, to get the amount that we are allowed
		// and expected to read from the stream. This will leave the stream
		// positioned ready for the next request, or EOF, as the client decides.
		PartialReadStream partial(*mpStreamToReadFrom, contentLength -
			bytesCopied);
		partial.CopyStreamTo(rStreamToWriteTo, Timeout);

		// In case of a timeout or error, PartialReadStream::CopyStreamTo
		// should have thrown an exception, so this is just defensive, to
		// ensure that the source stream is properly positioned to read
		// from again, and the destination received the correct number of
		// bytes.
		ASSERT(!partial.StreamDataLeft());
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPRequest::Send(IOStream &, int, bool)
//		Purpose: Write a request with NO CONTENT to an IOStream using
//			 HTTP. If you want to send a request WITH content,
//			 such as a PUT or POST request, use SendWithStream()
//			 instead.
//		Created: 03/01/09
//
// --------------------------------------------------------------------------

void HTTPRequest::Send(IOStream &rStream, int Timeout, bool ExpectContinue)
{
	if(mHeaders.GetContentLength() > 0)
	{
		THROW_EXCEPTION(HTTPException, ContentLengthAlreadySet);
	}

	if(GetSize() != 0)
	{
		THROW_EXCEPTION_MESSAGE(HTTPException, WrongContentLength,
			"Tried to send a request without content, but there is data "
			"in the request buffer waiting to be sent.")
	}

	mHeaders.SetContentLength(0);
	SendHeaders(rStream, Timeout, ExpectContinue);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPRequest::SendHeaders(IOStream &, int, bool)
//		Purpose: Write the start of a request to an IOStream using
//			 HTTP. If you want to send a request WITH content,
//			 but you can't wait for a response, use this followed
//			 by sending your stream directly to the socket.
//		Created: 2015-10-08
//
// --------------------------------------------------------------------------

void HTTPRequest::SendHeaders(IOStream &rStream, int Timeout, bool ExpectContinue)
{
	switch (mMethod)
	{
	case Method_UNINITIALISED:
		THROW_EXCEPTION(HTTPException, RequestNotInitialised); break;
	case Method_UNKNOWN:
		THROW_EXCEPTION(HTTPException, BadRequest); break;
	default:
		rStream.Write(GetMethodName());
	}

	rStream.Write(" ");
	rStream.Write(mRequestURI.c_str());
	for(Query_t::iterator i = mQuery.begin(); i != mQuery.end(); i++)
	{
		rStream.Write(
			((i == mQuery.begin()) ? "?" : "&") +
			HTTPQueryDecoder::URLEncode(i->first) + "=" +
			HTTPQueryDecoder::URLEncode(i->second));
	}
	rStream.Write(" ");

	switch (mHTTPVersion)
	{
	case HTTPVersion_0_9: rStream.Write("HTTP/0.9"); break;
	case HTTPVersion_1_0: rStream.Write("HTTP/1.0"); break;
	case HTTPVersion_1_1: rStream.Write("HTTP/1.1"); break;
	default:
		THROW_EXCEPTION_MESSAGE(HTTPException, NotImplemented,
			"Unsupported HTTP version: " << mHTTPVersion);
	}

	rStream.Write("\r\n");
	mHeaders.WriteTo(rStream, Timeout);

	if (mpCookies)
	{
		THROW_EXCEPTION_MESSAGE(HTTPException, NotImplemented,
			"Cookie support not implemented yet");
	}

	if (ExpectContinue)
	{
		rStream.Write("Expect: 100-continue\r\n");
	}

	rStream.Write("\r\n");
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPRequest::SendWithStream(IOStream &rStreamToSendTo,
//			 int Timeout, IOStream* pStreamToSend,
//			 HTTPResponse& rResponse)
//		Purpose: Write a request WITH CONTENT to an IOStream using
//			 HTTP. If you want to send a request WITHOUT content,
//			 such as a GET or DELETE request, use Send() instead.
//			 Because this is interactive (it uses 100 Continue
//			 responses) it can only be sent to a SocketStream.
//		Created: 03/01/09
//
// --------------------------------------------------------------------------

IOStream::pos_type HTTPRequest::SendWithStream(SocketStream &rStreamToSendTo,
	int Timeout, IOStream* pStreamToSend, HTTPResponse& rResponse)
{
	SendHeaders(rStreamToSendTo, Timeout, true); // ExpectContinue

	rResponse.Receive(rStreamToSendTo, Timeout);
	if (rResponse.GetResponseCode() != 100)
	{
		// bad response, abort now
		return 0;
	}

	IOStream::pos_type bytes_sent = 0;

	if(mHeaders.GetContentLength() == -1)
	{
		// We don't know how long the stream is, so just send it all.
		// Including any data buffered in the HTTPRequest.
		CopyStreamTo(rStreamToSendTo, Timeout);
		pStreamToSend->CopyStreamTo(rStreamToSendTo, Timeout);
	}
	else
	{
		// Check that the length of the stream is correct, and ensure
		// that we don't send too much without realising.
		ReadGatherStream gather(false); // don't delete anything

		// Send any data buffered in the HTTPRequest first.
		gather.AddBlock(gather.AddComponent(this), GetSize());
		
		// And the remaining bytes should be read from the supplied stream.
		gather.AddBlock(gather.AddComponent(pStreamToSend),
			mHeaders.GetContentLength() - GetSize());
		
		bytes_sent = gather.CopyStreamTo(rStreamToSendTo, Timeout);
		
		if(pStreamToSend->StreamDataLeft())
		{
			THROW_EXCEPTION_MESSAGE(HTTPException, WrongContentLength,
				"Expected to send " << mHeaders.GetContentLength() <<
				" bytes, but there is still unsent data left in the "
				"stream");
		}

		if(gather.StreamDataLeft())
		{
			THROW_EXCEPTION_MESSAGE(HTTPException, WrongContentLength,
				"Expected to send " << mHeaders.GetContentLength() <<
				" bytes, but there was not enough data in the stream");
		}
	}

	// We don't support keep-alive, so we must shutdown the write side of the stream
	// to signal to the other end that we have no more data to send.
	ASSERT(!GetClientKeepAliveRequested());
	rStreamToSendTo.Shutdown(false, true); // !read, write

	// receive the final response
	rResponse.Receive(rStreamToSendTo, Timeout);
	return bytes_sent;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPRequest::ParseCookies(const std::string &)
//		Purpose: Parse the cookie header
//		Created: 20/8/04
//
// --------------------------------------------------------------------------
void HTTPRequest::ParseCookies(const std::string &rCookieString)
{
	const char *data = rCookieString.c_str();
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



