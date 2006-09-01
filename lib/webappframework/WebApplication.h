// --------------------------------------------------------------------------
//
// File
//		Name:    WebApplication.h
//		Purpose: Web application base class
//		Created: 30/3/04
//
// --------------------------------------------------------------------------

#ifndef WEBAPPLICATION__H
#define WEBAPPLICATION__H

#include <string>
#include <vector>

// Include all the utility functions, so they're handily accessible
#include "WAFUtilityFns.h"

#include "HTTPServer.h"
class WebApplicationObject;

// --------------------------------------------------------------------------
//
// Class
//		Name:    WebApplication
//		Purpose: Web application base class
//		Created: 30/3/04
//
// --------------------------------------------------------------------------
class WebApplication : public HTTPServer
{
public:
	WebApplication();
	~WebApplication();
private:
	// no copying
	WebApplication(const WebApplication &);
	WebApplication &operator=(const WebApplication &);
public:

	virtual void SetupInInitialProcess();

	virtual void HTTPConnectionOpening();
	virtual void HTTPConnectionClosing();

	void Handle(const HTTPRequest &rRequest, HTTPResponse &rResponse);
	virtual bool HandlePage(const HTTPRequest &rRequest, HTTPResponse &rResponse,
		uint32_t Language, uint32_t Page, std::vector<std::string> &rURLElements);
	virtual bool GetStaticFile(const char *URI, const void **ppFileOut,
		int *pFileSizeOut, const char **ppFileMIMETypeOut);
	virtual WebApplicationObject &GetApplicationObject();
	virtual const char *GetURLBase() const;
	virtual bool GetHomePageURI(std::string &rHomePageLocation, bool &rAsRedirect) const;

	// Utility functions
	static uint32_t FourCharStringToInt(const char *Element);
	void UnhandledResponse(const HTTPRequest &rRequest, HTTPResponse &rResponse);
	static const std::string &GetFormDataString(const HTTPRequest &rRequest, const std::string &rKey);
	const std::string &GetConfigurationVariable(const char *ConfigVar);
};

#endif // WEBAPPLICATION__H

