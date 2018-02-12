// --------------------------------------------------------------------------
//
// File
//		Name:    BoxException.h
//		Purpose: BoxException class definition
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------

#ifndef BOXEXCEPTION__H
#define BOXEXCEPTION__H

#include <exception>
#include <string>

#define EXCEPTION_IS_TYPE(exception_obj, type, subtype) \
	exception_obj.IsType(type::ExceptionType, type::subtype)

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
	bool IsType(unsigned int Type, unsigned int SubType)
	{
		return GetType() == Type && GetSubType() == SubType;
	}
	virtual const std::string& GetMessage() const = 0;

private:
};

#endif // BOXEXCEPTION__H

