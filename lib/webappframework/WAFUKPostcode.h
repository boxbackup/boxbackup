// --------------------------------------------------------------------------
//
// File
//		Name:    WAFUKPostcode.h
//		Purpose: UK postcode validation and handling
//		Created: 31/8/05
//
// --------------------------------------------------------------------------

#ifndef WAFUKPOSTCODE__H
#define WAFUKPOSTCODE__H

#include <string>

// --------------------------------------------------------------------------
//
// Class
//		Name:    WAFUKPostcode
//		Purpose: UK postcode validation and handling
//		Created: 31/8/05
//
// --------------------------------------------------------------------------
class WAFUKPostcode
{
public:
	WAFUKPostcode();
	~WAFUKPostcode();

	bool ParseAndValidate(const std::string &rInput);
	
	bool IsValid() const {return mOutcode.size() > 0 && mIncode.size() > 0;}
	const std::string &Outcode() const {return mOutcode;}
	const std::string &Incode() const {return mIncode;}
	std::string NormalisedPostcode() const;

private:
	std::string mOutcode;
	std::string mIncode;
};

#endif // WAFUKPOSTCODE__H

