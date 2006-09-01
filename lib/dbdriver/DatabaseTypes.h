// --------------------------------------------------------------------------
//
// File
//		Name:    DatabaseTypes.h
//		Purpose: Types for database drivers
//		Created: 10/5/04
//
// --------------------------------------------------------------------------

#ifndef DATABASETYPES__H
#define DATABASETYPES__H


// Declarations of types
namespace Database
{
	enum
	{
		// Note: These values are chosen to match PostgreSQL OID values
		Type_String = 25, // TEXTOID
		Type_Int32 = 23, // INT4OID
		Type_Int16 = 21 // INT2OID
//		Type_Int8 = 3  // PostgreSQL doesn't have such a type, work round this later if this type is required.
	};
	
	typedef int32_t FieldType_t;
	
	// Maximum number of parameters allowed for a database query
	enum
	{
		MaxParameters = 64
	};
};


#endif // DATABASETYPES__H

