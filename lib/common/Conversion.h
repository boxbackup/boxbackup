// --------------------------------------------------------------------------
//
// File
//		Name:    Conversion.h
//		Purpose: Convert between various types
//		Created: 9/4/04
//
// --------------------------------------------------------------------------

#ifndef CONVERSION__H
#define CONVERSION__H

#include <string>

namespace BoxConvert
{
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BoxConvert::Convert<to_type, from_type>(to_type &, from_type)
	//		Purpose: Convert from types to types
	//		Created: 9/4/04
	//
	// --------------------------------------------------------------------------
	template<typename to_type, typename from_type>
	inline to_type Convert(from_type From)
	{
		// Default conversion, simply use C++ conversion
		return From;
	}

	// Specialise for string -> integer
	int32_t _ConvertStringToInt(const char *pString, int Size);
	template<>
	inline int32_t Convert<int32_t, const std::string &>(const std::string &rFrom)
	{
		return BoxConvert::_ConvertStringToInt(rFrom.c_str(), 32);
	}
	template<>
	inline int16_t Convert<int16_t, const std::string &>(const std::string &rFrom)
	{
		return BoxConvert::_ConvertStringToInt(rFrom.c_str(), 16);
	}
	template<>
	inline int8_t Convert<int8_t, const std::string &>(const std::string &rFrom)
	{
		return BoxConvert::_ConvertStringToInt(rFrom.c_str(), 8);
	}
	template<>
	inline int32_t Convert<int32_t, const char *>(const char *pFrom)
	{
		return BoxConvert::_ConvertStringToInt(pFrom, 32);
	}
	template<>
	inline int16_t Convert<int16_t, const char *>(const char *pFrom)
	{
		return BoxConvert::_ConvertStringToInt(pFrom, 16);
	}
	template<>
	inline int8_t Convert<int8_t, const char *>(const char *pFrom)
	{
		return BoxConvert::_ConvertStringToInt(pFrom, 8);
	}
	
	// Specialise for integer -> string
	void _ConvertIntToString(std::string &rTo, int32_t From);
	template<>
	inline std::string Convert<std::string, int32_t>(int32_t From)
	{
		std::string r;
		BoxConvert::_ConvertIntToString(r, From);
		return r;
	}
	template<>
	inline std::string Convert<std::string, int16_t>(int16_t From)
	{
		std::string r;
		BoxConvert::_ConvertIntToString(r, From);
		return r;
	}
	template<>
	inline std::string Convert<std::string, int8_t>(int8_t From)
	{
		std::string r;
		BoxConvert::_ConvertIntToString(r, From);
		return r;
	}
	
};

#endif // CONVERSION__H

