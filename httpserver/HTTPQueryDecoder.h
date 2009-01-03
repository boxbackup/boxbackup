// --------------------------------------------------------------------------
//
// File
//		Name:    HTTPQueryDecoder.h
//		Purpose: Utility class to decode HTTP query strings
//		Created: 26/3/04
//
// --------------------------------------------------------------------------

#ifndef HTTPQUERYDECODER__H
#define HTTPQUERYDECODER__H

#include "HTTPRequest.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    HTTPQueryDecoder
//		Purpose: Utility class to decode HTTP query strings
//		Created: 26/3/04
//
// --------------------------------------------------------------------------
class HTTPQueryDecoder
{
public:
	HTTPQueryDecoder(HTTPRequest::Query_t &rDecodeInto);
	~HTTPQueryDecoder();
private:
	// no copying
	HTTPQueryDecoder(const HTTPQueryDecoder &);
	HTTPQueryDecoder &operator=(const HTTPQueryDecoder &);
public:

	void DecodeChunk(const char *pQueryString, int QueryStringSize);
	void Finish();

private:
	HTTPRequest::Query_t &mrDecodeInto;
	std::string mCurrentKey;
	std::string mCurrentValue;
	bool mInKey;
	char mEscaped[4];
	int mEscapedState;
};

#endif // HTTPQUERYDECODER__H

