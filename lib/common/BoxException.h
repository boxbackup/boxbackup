// --------------------------------------------------------------------------
//
// File
//		Name:    BoxException.h
//		Purpose: Exception
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------

#ifndef BOXEXCEPTION__H
#define BOXEXCEPTION__H

#include <exception>
#include <string>

// --------------------------------------------------------------------------
//
// Class
//		Name:    BoxException
//		Purpose: Exception
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------
class BoxException : public std::exception
{
public:
	BoxException();
	~BoxException() throw ();
	
	virtual unsigned int GetType() const throw() = 0;
	virtual unsigned int GetSubType() const throw() = 0;
	virtual const std::string& GetMessage() const = 0;

private:
};

#define EXCEPTION_IS_TYPE(exception_obj, type, subtype) \
	(exception_obj.GetType() == type::ExceptionType && \
	exception_obj.GetSubType() == type::subtype)

#endif // BOXEXCEPTION__H

