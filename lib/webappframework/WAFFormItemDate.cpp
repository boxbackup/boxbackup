// --------------------------------------------------------------------------
//
// File
//		Name:    WAFFormItemDate.cpp
//		Purpose: Form data field type for a date
//		Created: 23/11/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdlib.h>
#include <errno.h>
#include <limits.h>

#include "WAFFormItemDate.h"
#include "WebAppForm.h"

#include "MemLeakFindOn.h"

#define ALL_FIELDS_OBTAINED	0x7	// bits 0, 1, 2 set

// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFFormItemDate::WAFFormItemDate()
//		Purpose: Constructor
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
WAFFormItemDate::WAFFormItemDate()
	: mValidationState(0)
{
	mDate[Year] = 0;
	mDate[Month] = 0;
	mDate[Day] = 0;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFFormItemDate::DataFromForm(int, const char *)
//		Purpose: Accept data from the form, returning validation state
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
uint8_t WAFFormItemDate::DataFromForm(int Element, const char *Data, bool FieldIsOptional)
{
	// Make sure the element number is valid.
	ASSERT(Element >= 0 && Element < 3);

	// Only convert the data if it's not the empty string (ie nothing selected)
	if(Data[0] != '\0')
	{
		// Already had the item?
		if((mValidationState & (1<<Element)) != 0)
		{
			// Not good.
			mValidationState = 0;
			return WebAppForm::NotValid;
		}
	
		// Attempt to decode the number
		errno = 0;	// necessary on some platforms
		int32_t v = ::strtol(Data, NULL, 10);
		if((v == 0 && errno == EINVAL) || v == LONG_MIN || v == LONG_MAX)
		{
			// Bad number
			return WebAppForm::NotValid;
		}
	
		// Store the number, and mark it as obtained
		mDate[Element] = v;
		mValidationState |= 1 << Element;
	}
	else if(FieldIsOptional)
	{
		// Blank string entered, so might be valid (if nothing is filled in)
		if(mValidationState == 0)
		{
			return WebAppForm::Valid;
		}
	}

	return (mValidationState == ALL_FIELDS_OBTAINED)
			?(CheckValidDate()?(WebAppForm::Valid):(WebAppForm::NotValid))
			:(WebAppForm::NotValid);
}



// --------------------------------------------------------------------------
//
// Class
//		Name:    WAFFormItemDate::CheckValidDate()
//		Purpose: Is the date a valid date?
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
bool WAFFormItemDate::CheckValidDate()
{
	// Year look OK?
	if(mDate[Year] <= 0)
	{
		return false;
	}

	// Right then... is the month valid?
	if(mDate[Month] < 1 || mDate[Month] > 12)
	{
		return false;
	}
	
	// How many days are there in the month?
	static const int monthDays[] = {-1, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int maxDayInMonth = monthDays[mDate[Month]];
	// Leap year?
	if(mDate[Month] == 2)
	{
		// Leap year or not?
		int y = mDate[Year];
		if(
			((y % 4) == 0)
				&& ( ((y % 100) != 0) || ((y % 400) == 0) )
			)
		{
			// It's a leap year
			maxDayInMonth = 29;
		}
	}
	
	// Check day is valid too
	if(mDate[Day] < 1 || mDate[Day] > maxDayInMonth)
	{
		return false;
	}
	
	return true;
}

