// --------------------------------------------------------------------------
//
// File
//		Name:    S3Simulator.h
//		Purpose: Amazon S3 simulation HTTP server for S3 testing
//		Created: 09/01/2009
//
// --------------------------------------------------------------------------

#ifndef S3SIMULATOR__H
#define S3SIMULATOR__H

#include "HTTPServer.h"

class ConfigurationVerify;
class HTTPRequest;
class HTTPResponse;

// --------------------------------------------------------------------------
//
// Class
//		Name:    S3Simulator
//		Purpose: Amazon S3 simulation HTTP server for S3 testing
//		Created: 09/01/2009
//
// --------------------------------------------------------------------------
class S3Simulator : public HTTPServer
{
public:
	S3Simulator() { }
	~S3Simulator() { }

	const ConfigurationVerify* GetConfigVerify() const;
	virtual void Handle(HTTPRequest &rRequest, HTTPResponse &rResponse);
	virtual void HandleGet(HTTPRequest &rRequest, HTTPResponse &rResponse);
	virtual void HandlePut(HTTPRequest &rRequest, HTTPResponse &rResponse);
};

#endif // S3SIMULATOR__H

