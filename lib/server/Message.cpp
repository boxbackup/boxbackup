// --------------------------------------------------------------------------
//
// File
//		Name:    Message.h
//		Purpose: Protocol object base class
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "Exception.h"
#include "Message.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    Message::Message()
//		Purpose: Default constructor
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
Message::Message()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Message::Message()
//		Purpose: Destructor
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
Message::~Message()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Message::Message()
//		Purpose: Copy constructor
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
Message::Message(const Message &rToCopy)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Message::IsError(int &, int &)
//		Purpose: Does this represent an error, and if so, what is the type and subtype?
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
bool Message::IsError(int &rTypeOut, int &rSubTypeOut) const
{
	return false;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Message::IsConversationEnd()
//		Purpose: Does this command end the conversation?
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
bool Message::IsConversationEnd() const
{
	return false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Message::GetType()
//		Purpose: Return type of the object
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
int Message::GetType() const
{
	// This isn't implemented in the base class!
	THROW_EXCEPTION(CommonException, Internal)
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Message::SetPropertiesFromStreamData(Protocol &)
//		Purpose: Set the properties of the object from the stream data ready in the Protocol object
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Message::SetPropertiesFromStreamData(Protocol &rProtocol)
{
	// This isn't implemented in the base class!
	THROW_EXCEPTION(CommonException, Internal)
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    Message::WritePropertiesToStreamData(Protocol &)
//		Purpose: Write the properties of the object into the stream data in the Protocol object
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Message::WritePropertiesToStreamData(Protocol &rProtocol) const
{
	// This isn't implemented in the base class!
	THROW_EXCEPTION(CommonException, Internal)
}



