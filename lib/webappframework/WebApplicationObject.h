// --------------------------------------------------------------------------
//
// File
//		Name:    WebApplicationObject.h
//		Purpose: Base class for web application ob
//		Created: 17/5/04
//
// --------------------------------------------------------------------------

#ifndef WEBAPPLICATIONCOBJECT__H
#define WEBAPPLICATIONCOBJECT__H

class Configuration;

// --------------------------------------------------------------------------
//
// Class
//		Name:    WebApplicationObject
//		Purpose: Base class for web application objects (hold application state)
//		Created: 17/5/04
//
// --------------------------------------------------------------------------
class WebApplicationObject
{
public:
	WebApplicationObject();
	virtual ~WebApplicationObject();
private:
	// no copying
	WebApplicationObject(const WebApplicationObject &);
	WebApplicationObject &operator=(const WebApplicationObject &);
public:

	// Interface
	virtual void ApplicationStart(const Configuration &rConfiguration);
	virtual void ChildStart(const Configuration &rConfiguration);
	virtual void RequestStart(const Configuration &rConfiguration);
	virtual void RequestFinish();
	virtual void ChildFinish();
};

#endif // WEBAPPLICATIONCOBJECT__H

