// --------------------------------------------------------------------------
//
// File
//		Name:    WebApplicationObject.cpp
//		Purpose: Base class for web application ob
//		Created: 17/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "WebApplicationObject.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplicationObject::WebApplicationObject()
//		Purpose: Constructor
//		Created: 17/5/04
//
// --------------------------------------------------------------------------
WebApplicationObject::WebApplicationObject()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplicationObject::~WebApplicationObject()
//		Purpose: Destructor
//		Created: 17/5/04
//
// --------------------------------------------------------------------------
WebApplicationObject::~WebApplicationObject()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplicationObject::ApplicationStart(const Configuration &)
//		Purpose: Called when an application starts, just after the configuration
//				 has been read by before the initial fork.
//		Created: 20/12/04
//
// --------------------------------------------------------------------------
void WebApplicationObject::ApplicationStart(const Configuration &rConfiguration)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplicationObject::ChildStart(const Configuration &)
//		Purpose: Called when an application child process is started
//		Created: 17/5/04
//
// --------------------------------------------------------------------------
void WebApplicationObject::ChildStart(const Configuration &rConfiguration)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplicationObject::RequestStart(const Configuration &)
//		Purpose: Called when a request is started
//		Created: 17/5/04
//
// --------------------------------------------------------------------------
void WebApplicationObject::RequestStart(const Configuration &rConfiguration)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplicationObject::RequestFinish()
//		Purpose: Called when a request is finished
//		Created: 17/5/04
//
// --------------------------------------------------------------------------
void WebApplicationObject::RequestFinish()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplicationObject::ChildFinish()
//		Purpose: Called when a child is exiting
//		Created: 17/5/04
//
// --------------------------------------------------------------------------
void WebApplicationObject::ChildFinish()
{
}


