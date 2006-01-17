// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
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



