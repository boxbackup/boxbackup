// --------------------------------------------------------------------------
//
// File
//		Name:    TestWebAppCode.cpp
//		Purpose: Test code for web app
//		Created: 19/4/04
//
// --------------------------------------------------------------------------

#include "Box.h"

// #include "autogen_webapp/TestWebAppPageLogin.h"
#include "autogen_webapp/TestWebAppPageMain.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    TestWebAppFormLogin::Validate()
//		Purpose: Extra validation for form
//		Created: 19/4/04
//
// --------------------------------------------------------------------------

// MOVED TO Login.pl file with Code unit

/* void TestWebAppFormLogin::Validate()
{
	// Valid password is the username backwards, generate it
	std::string validpass;
	for(std::string::reverse_iterator i(mUsername.rbegin()); i != mUsername.rend(); ++i)
	{
		validpass += *i;
	}
	
	if(validpass == mPassword && validpass.size() > 0)
	{
		mPasswordValidityError = WebAppForm::Valid;
	}
}*/


// --------------------------------------------------------------------------
//
// Function
//		Name:    TestWebAppFormWidgets::Validate()
//		Purpose: Extra validation for form on Main page
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
void TestWebAppFormWidgets::Validate()
{
	// Add the error message from the form.
	for(std::vector<int32_t>::const_iterator i(mEmitErrors.begin()); i != mEmitErrors.end(); ++i)
	{
		AddError(GetTranslatedStrings()[*i]);
	}
}



