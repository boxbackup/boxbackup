#include "Box.h"


using namespace Win32;


// --------------------------------------------------------------------------
//
// Function
//		Name:    ConvertToWideString
//		Purpose: Converts a string from specified codepage to
//			 a wide string (wchar_t*).
//			 In case of fire, logs the error and returns NULL.
//		Created: 4th February 2006
//
// --------------------------------------------------------------------------
static const wchar_t* ConvertToWideString(const char* pString, unsigned int codepage, bool logErrors)
{
	try
	{
		return multi2wide(pString,codepage).c_str();
	}
	catch(Win32Exception &e)
	{
		if (logErrors)
			BOX_LOG_WIN_ERROR_NUMBER(e.GetMessage(),e.GetLastError());
	}
	return NULL;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ConvertUtf8ToWideString
//		Purpose: Converts a string from UTF-8 to a wide string.
//			 In case of fire, logs the error and returns NULL.
//		Created: 4th February 2006
//
// --------------------------------------------------------------------------
const wchar_t* ConvertUtf8ToWideString(const char* pString) throw()
{
	return ConvertToWideString(pString, CP_UTF8, true);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ConvertFromWideString
//		Purpose: Converts a wide string to a narrow string in the
//			 specified code page.
//			 In case of fire, logs the error and returns NULL.
//		Created: 4th February 2006
//
// --------------------------------------------------------------------------
const char* ConvertFromWideString(const wchar_t* pString, unsigned int codepage, bool logErrors) throw()
{
	try
	{
		return wide2multi(pString,codepage).c_str();
	}
	catch(Win32Exception &e)
	{
		if (logErrors)
			BOX_LOG_WIN_ERROR_NUMBER(e.GetMessage(),e.GetLastError());
	}
	return NULL;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ConvertEncoding(const std::string&, int,
//			 std::string&, int)
//		Purpose: Converts a string from one code page to another.
//			 On success, replaces contents of rDest and returns
//			 true. In case of fire, logs the error and returns
//			 false.
//		Created: 15th October 2006
//
// --------------------------------------------------------------------------
bool ConvertEncoding(const std::string& rSource, int sourceCodePage, std::string& rDest, int destCodePage) throw()
{
	try
	{
		wide2multi( multi2wide(rSource,sourceCodePage), rDest, destCodePage );
		return true;
	}
	EMU_EXCEPTION_HANDLING_RETURN(false)
}

bool ConvertToUtf8(const std::string& rSource, std::string& rDest, int sourceCodePage) throw()
{
	return ConvertEncoding(rSource, sourceCodePage, rDest, CP_UTF8);
}

bool ConvertFromUtf8(const std::string& rSource, std::string& rDest, int destCodePage) throw()
{
	return ConvertEncoding(rSource, CP_UTF8, rDest, destCodePage);
}

bool ConvertConsoleToUtf8(const std::string& rSource, std::string& rDest) throw()
{
	return ConvertToUtf8(rSource, rDest, GetConsoleCP());
}

bool ConvertUtf8ToConsole(const std::string& rSource, std::string& rDest) throw()
{
	return ConvertFromUtf8(rSource, rDest, GetConsoleOutputCP());
}
