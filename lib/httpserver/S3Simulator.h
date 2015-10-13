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
	// Increase timeout to 5 minutes, from HTTPServer default of 1 minute,
	// to help with debugging.
	S3Simulator() : HTTPServer(300000) { }
	~S3Simulator() { }

	const ConfigurationVerify* GetConfigVerify() const;
	virtual void Handle(HTTPRequest &rRequest, HTTPResponse &rResponse);
	virtual void HandleGet(HTTPRequest &rRequest, HTTPResponse &rResponse,
		bool IncludeContent = true);
	virtual void HandlePut(HTTPRequest &rRequest, HTTPResponse &rResponse);
	virtual void HandleHead(HTTPRequest &rRequest, HTTPResponse &rResponse);

	virtual const char *DaemonName() const
	{
		return "s3simulator";
	}
};

#endif // S3SIMULATOR__H

