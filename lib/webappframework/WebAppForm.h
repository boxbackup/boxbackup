// --------------------------------------------------------------------------
//
// File
//		Name:    WebAppForm.h
//		Purpose: Base class for web application forms
//		Created: 17/4/04
//
// --------------------------------------------------------------------------

#ifndef WEBAPPFORM__H
#define WEBAPPFORM__H

#include <string>

// --------------------------------------------------------------------------
//
// Class
//		Name:    WebAppForm
//		Purpose: Base class for Form objects
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
class WebAppForm
{
public:
	WebAppForm();
	WebAppForm(const WebAppForm &rToCopy);
	~WebAppForm();
	WebAppForm &operator=(const WebAppForm &rToCopy);

	// Error codes
	enum
	{
		Valid = 0,
		NotValid = 255,
		Error1 = 1,
		Error2 = 2,
		Error3 = 3,
		Error4 = 4,
		NumberFieldErr_Range = 1,
		NumberFieldErr_Format = 2,
		NumberFieldErr_Blank = 3,
		NumberFieldErr_FormatBlank = 4
	};
	
	// Misc constants
	enum
	{
		NoChoiceMade = -1
	};
};

// --------------------------------------------------------------------------
//
// Class
//		Name:    WebAppFormCustomErrors
//		Purpose: Base class for Form objects which emit custom errors
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
class WebAppFormCustomErrors : public WebAppForm
{
public:
	WebAppFormCustomErrors();
	WebAppFormCustomErrors(const WebAppFormCustomErrors &rToCopy);
	~WebAppFormCustomErrors();
	WebAppFormCustomErrors &operator=(const WebAppFormCustomErrors &rToCopy);

	// Functions for autogen code:

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    WebAppFormCustomErrors::SetStrings(const char *, const char **)
	//		Purpose: Set the strings to use -- separator string, plus the translated strings table 
	//		Created: 19/4/04
	//
	// --------------------------------------------------------------------------
	void SetStrings(const char *pErrorSeparator, const char **ppTranslatedStrings)
	{
		mpErrorSeparator = pErrorSeparator;
		mppTranslatedStrings = ppTranslatedStrings;
	}

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    WebAppFormCustomErrors::HaveErrorText()
	//		Purpose: Does this form have extra error text?
	//		Created: 19/4/04
	//
	// --------------------------------------------------------------------------
	bool HaveErrorText() const {return mErrorText.size() != 0;}
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    WebAppFormCustomErrors::GetErrorText()
	//		Purpose: Get the extra error text
	//		Created: 19/4/04
	//
	// --------------------------------------------------------------------------
	const std::string &GetErrorText() const {return mErrorText;}
	
protected:
	// Functions for derived classes
	
	void AddError(const char *Error);
	const char **GetTranslatedStrings() const;

private:
	std::string mErrorText;
	const char *mpErrorSeparator;
	const char **mppTranslatedStrings;
};

#endif // WEBAPPFORM__H

