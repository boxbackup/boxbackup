// --------------------------------------------------------------------------
//
// File
//		Name:    WebAppForm.cpp
//		Purpose: Base class for web application forms
//		Created: 17/4/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdlib.h>
#include <limits.h>
#include <errno.h>

#include "WebAppForm.h"
#include "autogen_WebAppFrameworkException.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebAppForm::WebAppForm()
//		Purpose: Constructor
//		Created: 17/4/04
//
// --------------------------------------------------------------------------
WebAppForm::WebAppForm()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebAppForm::WebAppForm(const WebAppForm &)
//		Purpose: Copy constructor
//		Created: 17/4/04
//
// --------------------------------------------------------------------------
WebAppForm::WebAppForm(const WebAppForm &rToCopy)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebAppForm::~WebAppForm()
//		Purpose: Destructor
//		Created: 17/4/04
//
// --------------------------------------------------------------------------
WebAppForm::~WebAppForm()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebAppForm::operator=(const WebAppForm &)
//		Purpose: Assignment operator
//		Created: 17/4/04
//
// --------------------------------------------------------------------------
WebAppForm &WebAppForm::operator=(const WebAppForm &rToCopy)
{
	return *this;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebAppFormCustomErrors::WebAppFormCustomErrors()
//		Purpose: Constructor
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
WebAppFormCustomErrors::WebAppFormCustomErrors()
	: mpErrorSeparator(0),
	  mppTranslatedStrings(0)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebAppFormCustomErrors::WebAppFormCustomErrors(const WebAppFormCustomErrors &)
//		Purpose: Copy constructor
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
WebAppFormCustomErrors::WebAppFormCustomErrors(const WebAppFormCustomErrors &rToCopy)
	: WebAppForm(rToCopy),
	  mpErrorSeparator(rToCopy.mpErrorSeparator),
	  mppTranslatedStrings(rToCopy.mppTranslatedStrings)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebAppFormCustomErrors::~WebAppFormCustomErrors()
//		Purpose: Destructor
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
WebAppFormCustomErrors::~WebAppFormCustomErrors()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebAppFormCustomErrors::operator=(const WebAppFormCustomErrors &)
//		Purpose: Assignment operator
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
WebAppFormCustomErrors &WebAppFormCustomErrors::operator=(const WebAppFormCustomErrors &rToCopy)
{
	WebAppForm::operator=(rToCopy);
	mpErrorSeparator = rToCopy.mpErrorSeparator;
	mppTranslatedStrings = rToCopy.mppTranslatedStrings;
	return *this;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebAppFormCustomErrors::AddError(const char *)
//		Purpose: Adds an error string to the list of errors.
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
void WebAppFormCustomErrors::AddError(const char *Error)
{
	// Separator?
	if(mErrorText.size() != 0 && mpErrorSeparator != 0)
	{
		mErrorText += mpErrorSeparator;
	}
	
	// Add text
	if(Error != 0)
	{
		mErrorText += Error;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebAppFormCustomErrors::GetTranslatedStrings()
//		Purpose: Returns the list of translated strings, exceptions if none have been set
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
const char **WebAppFormCustomErrors::GetTranslatedStrings() const
{
	if(mppTranslatedStrings == 0)
	{
		THROW_EXCEPTION(WebAppFrameworkException, TranslatedStringsNotSet);
	}

	return mppTranslatedStrings;
}

