// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
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
	
	// Specialise for bool -> string
	template<>
	inline std::string Convert<std::string, bool>(bool From)
	{
		return std::string(From?"true":"false");
	}
};

#endif // CONVERSION__H

