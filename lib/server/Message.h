// --------------------------------------------------------------------------
//
// File
//		Name:    Message.h
//		Purpose: Protocol object base class
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------

#ifndef PROTOCOLOBJECT__H
#define PROTOCOLOBJECT__H

#include <memory>
#include <string>

class Protocol;
class ProtocolContext;

// --------------------------------------------------------------------------
//
// Class
//		Name:    Message
//		Purpose: Basic object representation of objects to pass through a Protocol session
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
class Message
{
public:
	Message();
	virtual ~Message();
	Message(const Message &rToCopy);

	// Info about this object
	virtual int GetType() const;
	virtual bool IsError(int &rTypeOut, int &rSubTypeOut) const;
	virtual bool IsConversationEnd() const;

	// reading and writing with Protocol objects
	virtual void SetPropertiesFromStreamData(Protocol &rProtocol);
	virtual void WritePropertiesToStreamData(Protocol &rProtocol) const;

	virtual void LogSysLog(const char *Action) const { }
	virtual void LogFile(const char *Action, FILE *file) const { }
	virtual std::string ToString() const = 0;
};

/*
class Reply;

class Request : public Message
{
public:
	Request() { }
	virtual ~Request() { }
	Request(const Request &rToCopy) { }
	virtual std::auto_ptr<Reply> DoCommand(Protocol &rProtocol, 
		ProtocolContext &rContext) = 0;
};

class Reply : public Message
{
public:
	Reply() { }
	virtual ~Reply() { }
	Reply(const Reply &rToCopy) { }
};
*/

#endif // PROTOCOLOBJECT__H

