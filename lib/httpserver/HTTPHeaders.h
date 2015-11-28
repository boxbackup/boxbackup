// --------------------------------------------------------------------------
//
// File
//		Name:    HTTPHeaders.h
//		Purpose: Utility class to decode HTTP headers
//		Created: 16/8/2015
//
// --------------------------------------------------------------------------

#ifndef HTTPHEADERS__H
#define HTTPHEADERS__H

#include <string>
#include <vector>

#include "autogen_HTTPException.h"
#include "IOStream.h"

class IOStreamGetLine;

// --------------------------------------------------------------------------
//
// Class
//		Name:    HTTPHeaders
//		Purpose: Utility class to decode HTTP headers
//		Created: 16/8/2015
//
// --------------------------------------------------------------------------
class HTTPHeaders
{
public:
	HTTPHeaders()
	: mKeepAlive(false),
	  mHostPort(80), // default if not specified
	  mContentLength(-1)
	{ }
	virtual ~HTTPHeaders() { }
	// copying is fine

	void ReadFromStream(IOStreamGetLine &rGetLine, int Timeout);
	void ParseHeaderLine(const std::string& line);
	void AddHeader(const std::string& name, const std::string& value);
	void WriteTo(IOStream& rOutput, int Timeout) const;
	typedef std::pair<std::string, std::string> Header;
	bool GetHeader(const std::string& name, std::string* pValueOut) const
	{
		const std::string lc_name = ToLowerCase(name);

		for (std::vector<Header>::const_iterator
			i  = mExtraHeaders.begin();
			i != mExtraHeaders.end(); i++)
		{
			if (i->first == lc_name)
			{
				*pValueOut = i->second;
				return true;
			}
		}

		return false;
	}
	std::string GetHeaderValue(const std::string& name, bool required = true) const
	{
		std::string value;
		if (GetHeader(name, &value))
		{
			return value;
		}

		if(required)
		{
			THROW_EXCEPTION_MESSAGE(CommonException, ConfigNoKey,
				"Expected header was not present: " << name);
		}
		else
		{
			return "";
		}
	}
	const std::vector<Header> GetExtraHeaders() const { return mExtraHeaders; }
	void SetKeepAlive(bool KeepAlive) {mKeepAlive = KeepAlive;}
	bool IsKeepAlive() const {return mKeepAlive;}
	void SetContentType(const std::string& rContentType)
	{
		mContentType = rContentType;
	}
	const std::string& GetContentType() const { return mContentType; }
	void SetContentLength(int64_t ContentLength) { mContentLength = ContentLength; }
	int64_t GetContentLength() const { return mContentLength; }
	const std::string &GetHostName() const {return mHostName;}
	const int GetHostPort() const {return mHostPort;}
	std::string GetHostNameWithPort() const;
	void SetHostName(const std::string& rHostName)
	{
		AddHeader("host", rHostName);
	}

private:
	bool mKeepAlive;
	std::string mContentType;
	std::string mHostName;
	int mHostPort;
	int64_t mContentLength; // only used when reading response from stream
	std::vector<Header> mExtraHeaders;

	std::string ToLowerCase(const std::string& input) const
	{
		std::string output = input;
		for (std::string::iterator c = output.begin();
			c != output.end(); c++)
		{
			*c = tolower(*c);
		}
		return output;
	}
};

#endif // HTTPHEADERS__H

