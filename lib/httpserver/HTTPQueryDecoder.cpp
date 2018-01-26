// --------------------------------------------------------------------------
//
// File
//		Name:    HTTPQueryDecoder.cpp
//		Purpose: Utility class to decode HTTP query strings
//		Created: 26/3/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <cctype> // for std::isalnum
#include <cstdlib>
#include <ostream>

#include "HTTPQueryDecoder.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPQueryDecoder::HTTPQueryDecoder(
//			 HTTPRequest::Query_t &)
//		Purpose: Constructor. Pass in the query contents you want
//			 to decode the query string into.
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
HTTPQueryDecoder::HTTPQueryDecoder(HTTPRequest::Query_t &rDecodeInto)
	: mrDecodeInto(rDecodeInto),
	  mInKey(true),
	  mEscapedState(0)
{
	// Insert the terminator for escaped characters
	mEscaped[2] = '\0';
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPQueryDecoder::~HTTPQueryDecoder()
//		Purpose: Destructor.
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
HTTPQueryDecoder::~HTTPQueryDecoder()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPQueryDecoder::Decode(const char *, int)
//		Purpose: Decode a chunk of query string -- call several times with
//				 the bits as they are received, and then call Finish()
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
void HTTPQueryDecoder::DecodeChunk(const char *pQueryString, int QueryStringSize)
{
	for(int l = 0; l < QueryStringSize; ++l)
	{
		char c = pQueryString[l];
		
		// BEFORE unescaping, check to see if we need to flip key / value
		if(mEscapedState == 0)
		{
			if(mInKey && c == '=')
			{
				// Set to store characters in the value
				mInKey = false;
				continue;
			}
			else if(!mInKey && c == '&')
			{
				// Need to store the current key/value pair
				mrDecodeInto.insert(HTTPRequest::QueryEn_t(mCurrentKey, mCurrentValue));
				// Blank the strings
				mCurrentKey.erase();
				mCurrentValue.erase();
			
				// Set to store characters in the key
				mInKey = true;
				continue;
			}
		}
		
		// Decode an escaped value?
		if(mEscapedState == 1)
		{
			// Waiting for char one of the escaped hex value
			mEscaped[0] = c;
			mEscapedState = 2;
			continue;
		}
		else if(mEscapedState == 2)
		{
			// Escaped value, decode it
			mEscaped[1] = c;	// str terminated in constructor
			mEscapedState = 0;	// stop being in escaped mode
			long ch = ::strtol(mEscaped, NULL, 16);
			if(ch <= 0 || ch > 255)
			{
				// Bad character, just ignore
				continue;
			}
			
			// Use this instead
			c = (char)ch;
		}		
		else if(c == '+')
		{
			c = ' ';
		}
		else if(c == '%')
		{
			mEscapedState = 1;
			continue;
		}

		// Store decoded value into the appropriate string
		if(mInKey)
		{
			mCurrentKey += c;
		}
		else
		{
			mCurrentValue += c;
		}
	}
	
	// Don't do anything here with left over values, DecodeChunk might be called
	// again. Let Finish() clean up.
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPQueryDecoder::Finish()
//		Purpose: Finish the decoding. Necessary to get the last item!
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
void HTTPQueryDecoder::Finish()
{
	// Insert any remaining value.
	if(!mCurrentKey.empty())
	{
		mrDecodeInto.insert(HTTPRequest::QueryEn_t(mCurrentKey, mCurrentValue));
		// Blank values, just in case
		mCurrentKey.erase();
		mCurrentValue.erase();
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPQueryDecoder::URLEncode()
//		Purpose: URL-encode a value according to Amazon's rules,
//			 which are similar to RFC 3986.
//		Created: 2015-11-15
//
// --------------------------------------------------------------------------

std::string HTTPQueryDecoder::URLEncode(const std::string& value)
{
	std::ostringstream out;
	for(std::string::const_iterator i = value.begin(); i != value.end(); i++)
	{
		// Do not URL encode any of the unreserved characters that RFC 3986
		// defines: A-Z, a-z, 0-9, hyphen ( - ), underscore ( _ ), period ( . ),
		// and tilde ( ~ ).
		if(std::isalnum(*i) || *i == '-' || *i == '_' || *i == '.' || *i == '~')
		{
			out << *i;
		}
		else
		{
			// Percent encode all other characters with %XY, where X and Y are
			// hex characters 0-9 and uppercase A-F.
			out << "%" <<
				std::hex <<
				std::uppercase <<
				std::internal <<
				std::setw(2) <<
				std::setfill('0') <<
				(int)(unsigned char)(*i) <<
				std::dec;
		}
	}
	return out.str();
}
