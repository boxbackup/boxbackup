// --------------------------------------------------------------------------
//
// File
//		Name:    ProtocolObject.h
//		Purpose: Protocol object base class
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "ProtocolObject.h"
#include "CommonException.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    ProtocolObject::ProtocolObject()
//		Purpose: Default constructor
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
ProtocolObject::ProtocolObject()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ProtocolObject::ProtocolObject()
//		Purpose: Destructor
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
ProtocolObject::~ProtocolObject()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ProtocolObject::ProtocolObject()
//		Purpose: Copy constructor
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
ProtocolObject::ProtocolObject(const ProtocolObject &rToCopy)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ProtocolObject::IsError(int &, int &)
//		Purpose: Does this represent an error, and if so, what is the type and subtype?
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
bool ProtocolObject::IsError(int &rTypeOut, int &rSubTypeOut) const
{
	return false;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ProtocolObject::IsConversationEnd()
//		Purpose: Does this command end the conversation?
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
bool ProtocolObject::IsConversationEnd() const
{
	return false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ProtocolObject::GetType()
//		Purpose: Return type of the object
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
int ProtocolObject::GetType() const
{
	// This isn't implemented in the base class!
	THROW_EXCEPTION(CommonException, Internal)
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ProtocolObject::SetPropertiesFromStreamData(Protocol &)
//		Purpose: Set the properties of the object from the stream data ready in the Protocol object
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void ProtocolObject::SetPropertiesFromStreamData(Protocol &rProtocol)
{
	// This isn't implemented in the base class!
	THROW_EXCEPTION(CommonException, Internal)
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    ProtocolObject::WritePropertiesToStreamData(Protocol &)
//		Purpose: Write the properties of the object into the stream data in the Protocol object
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void ProtocolObject::WritePropertiesToStreamData(Protocol &rProtocol) const
{
	// This isn't implemented in the base class!
	THROW_EXCEPTION(CommonException, Internal)
}



