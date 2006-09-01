// --------------------------------------------------------------------------
//
// File
//		Name:    WAFFormItemDate.h
//		Purpose: Form data field type for a date
//		Created: 23/11/04
//
// --------------------------------------------------------------------------

#ifndef WAFFORMITEMDATE__H
#define WAFFORMITEMDATE__H

// --------------------------------------------------------------------------
//
// Class
//		Name:    WAFFormItemDate
//		Purpose: Class representing a date used by a form
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
class WAFFormItemDate
{
public:
	WAFFormItemDate();
	
	enum
	{
		Year = 0, Month = 1, Day = 2
	};
	
	// Get at data
	int GetYear() const {return mDate[Year];}
	int GetMonth() const {return mDate[Month];}
	int GetDay() const {return mDate[Day];}
	
	// Accept data from the form, returning validation state
	uint8_t DataFromForm(int Element, const char *Data, bool FieldIsOptional);
	
	bool CheckValidDate();
	
private:
	int16_t mDate[3];
	int8_t mValidationState;
};

#endif // WAFFORMITEMDATE__H

