// --------------------------------------------------------------------------
//
// File
//		Name:    HTTPHeaders
//		Purpose: Utility class to decode HTTP headers
//		Created: 16/8/2015
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdlib.h>

#include "HTTPHeaders.h"
#include "IOStreamGetLine.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPHeaders::ReadFromStream(IOStreamGetLine &rGetLine,
//			 int Timeout);
//		Purpose: Read headers from a stream into internal storage.
//		Created: 2015-08-22
//
// --------------------------------------------------------------------------
void HTTPHeaders::ReadFromStream(IOStreamGetLine &rGetLine, int Timeout)
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
			ParseHeaderLine(header);

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
//		Name:    HTTPHeaders::ParseHeaderLine
//		Purpose: Splits a line into name and value, and adds it to
//			 this header set.
//		Created: 2015-08-22
//
// --------------------------------------------------------------------------
void HTTPHeaders::ParseHeaderLine(const std::string& rLine)
{
	// Find where the : is in the line
	std::string::size_type colon = rLine.find(':');
	if(colon == std::string::npos || colon == 0 ||
		colon > rLine.size() - 2)
	{
		THROW_EXCEPTION_MESSAGE(HTTPException, BadResponse,
			"Invalid header line: " << rLine);
	}

	std::string name = rLine.substr(0, colon);
	std::string value = rLine.substr(colon + 2);
	AddHeader(name, value);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPHeaders::AddHeader(const std::string& name,
//			 const std::string& value)
//		Purpose: Add a single header, with the specified name and
//			 value, to the internal list of headers.
//		Created: 2015-08-22
//
// --------------------------------------------------------------------------
void HTTPHeaders::AddHeader(const std::string& rName, const std::string& value)
{
	std::string name = ToLowerCase(rName);

	if(name == "content-length")
	{
		// Decode number
		long len = ::strtol(value.c_str(), NULL, 10);
		// returns zero in error case, this is OK
		if(len < 0) len = 0;
		// Store
		mContentLength = len;
	}
	else if(name == "content-type")
	{
		// Store rest of string as content type
		mContentType = value;
	}
	else if(name == "connection")
	{
		// Connection header, what is required?
		if(::strcasecmp(value.c_str(), "close") == 0)
		{
			mKeepAlive = false;
		}
		else if(::strcasecmp(value.c_str(), "keep-alive") == 0)
		{
			mKeepAlive = true;
		}
		// else don't understand, just assume default for protocol version
	}
	else if (name == "host")
	{
		// Store host header
		mHostName = value;

		// Is there a port number to split off?
		std::string::size_type colon = mHostName.find_first_of(':');
		if(colon != std::string::npos)
		{
			// There's a port in the string... attempt to turn it into an int
			mHostPort = ::strtol(mHostName.c_str() + colon + 1, 0, 10);

			// Truncate the string to just the hostname
			mHostName = mHostName.substr(0, colon);
		}
	}
	else
	{
		mExtraHeaders.push_back(Header(name, value));
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPHeaders::WriteTo(IOStream& rOutput, int Timeout)
//		Purpose: Write all headers to the supplied stream.
//		Created: 2015-08-22
//
// --------------------------------------------------------------------------
void HTTPHeaders::WriteTo(IOStream& rOutput, int Timeout) const
{
	std::ostringstream oss;

	if (mContentLength != -1)
	{
		oss << "Content-Length: " << mContentLength << "\r\n";
	}

	if (mContentType != "")
	{
		oss << "Content-Type: " << mContentType << "\r\n";
	}

	if (mHostName != "")
	{
		oss << "Host: " << GetHostNameWithPort() << "\r\n";
	}

	if (mKeepAlive)
	{
		oss << "Connection: keep-alive\r\n";
	}
	else
	{
		oss << "Connection: close\r\n";
	}

	for (std::vector<Header>::const_iterator i = mExtraHeaders.begin();
		i != mExtraHeaders.end(); i++)
	{
		oss << i->first << ": " << i->second << "\r\n";
	}

	rOutput.Write(oss.str(), Timeout);
}

std::string HTTPHeaders::GetHostNameWithPort() const
{

	if (mHostPort != 80)
	{
		std::ostringstream oss;
		oss << mHostName << ":" << mHostPort;
		return oss.str();
	}
	else
	{
		return mHostName;
	}
}

