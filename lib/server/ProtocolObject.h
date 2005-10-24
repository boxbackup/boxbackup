// --------------------------------------------------------------------------
//
// File
//		Name:    ProtocolObject.h
//		Purpose: Protocol object base class
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------

#ifndef PROTOCOLOBJECT__H
#define PROTOCOLOBJECT__H

class Protocol;

// --------------------------------------------------------------------------
//
// Class
//		Name:    ProtocolObject
//		Purpose: Basic object representation of objects to pass through a Protocol session
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
class ProtocolObject
{
public:
	ProtocolObject();
	virtual ~ProtocolObject();
	ProtocolObject(const ProtocolObject &rToCopy);

	// Info about this object
	virtual int GetType() const;
	virtual bool IsError(int &rTypeOut, int &rSubTypeOut) const;
	virtual bool IsConversationEnd() const;

	// reading and writing with Protocol objects
	virtual void SetPropertiesFromStreamData(Protocol &rProtocol);
	virtual void WritePropertiesToStreamData(Protocol &rProtocol) const;	
};

#endif // PROTOCOLOBJECT__H

