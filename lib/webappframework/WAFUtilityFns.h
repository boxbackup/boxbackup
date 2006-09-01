// --------------------------------------------------------------------------
//
// File
//		Name:    WAFUtilityFns.h
//		Purpose: Define utility functions for the web app framework
//		Created: 7/2/05
//
// --------------------------------------------------------------------------

#ifndef WAFUTILITYFNS__H
#define WAFUTILITYFNS__H

#include <string>

#define WAFUTILITY_FIXEDPOINT_MAX_SCALE_DIGITS 8

namespace WAFUtility
{
	// ScaleDigits -- number of digits after decimal point.
	//					ie 2 implies a precision of 0.01, and multiplication factor of 100
	// PlacesRequired -- number of decimal places required in output.
	//					eg with ScaleDigits=3 and PlacesRequired=2, 10004 -> "1.0004", 10030 -> "1.003", 10200 -> "1.02", 11000 -> "1.10"
	bool ParseFixedPointDecimal(const std::string &rString, int32_t &rIntOut, int ScaleDigits);
	void FixedPointDecimalToString(int32_t Int, std::string &rStringOut, int ScaleDigits, int PlacesRequired = -1);
	
	// Number fields		
	int8_t IntepretNumberField(const std::string &rInput, int32_t &rNumberOut, int32_t BlankValue, bool BlankOK,
		int32_t RangeBegin, int32_t RangeEnd, bool HaveRangeBegin, bool HaveRangeEnd);
	// In the fixed point file, for linkage reasons
	int8_t IntepretNumberFieldFixedPoint(const std::string &rInput, int32_t &rNumberOut, int32_t BlankValue, bool BlankOK,
		int32_t RangeBegin, int32_t RangeEnd, bool HaveRangeBegin, bool HaveRangeEnd, int ScaleDigits);

	// Validation
	bool ValidatePhoneNumber(const std::string &rInput, std::string &rCanonicalPhoneNumberOut);
};

#endif // WAFUTILITYFNS__H

