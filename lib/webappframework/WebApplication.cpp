// --------------------------------------------------------------------------
//
// File
//		Name:    WebApplication.cpp
//		Purpose: Web application base class
//		Created: 30/3/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "WebApplication.h"
#include "WebApplicationObject.h"
#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "Utils.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplication::WebApplication()
//		Purpose: Constructor
//		Created: 30/3/04
//
// --------------------------------------------------------------------------
WebApplication::WebApplication()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplication::~WebApplication()
//		Purpose: Desctructor
//		Created: 30/3/04
//
// --------------------------------------------------------------------------
WebApplication::~WebApplication()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplication::SetupInInitialProcess()
//		Purpose: Allow the derived object to do more setup
//		Created: 20/12/04
//
// --------------------------------------------------------------------------
void WebApplication::SetupInInitialProcess()
{
	// Base class.
	HTTPServer::SetupInInitialProcess();

	// Get the web app object to do more work
	WebApplicationObject &webAppObj(GetApplicationObject());
	webAppObj.ApplicationStart(GetConfiguration());
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplication::HTTPConnectionOpening()
//		Purpose: Inform web app code of child process starting
//		Created: 22/12/04
//
// --------------------------------------------------------------------------
void WebApplication::HTTPConnectionOpening()
{
	WebApplicationObject &rappObject(GetApplicationObject());
	rappObject.ChildStart(GetConfiguration());
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplication::Handle(const HTTPRequest &, HTTPResponse &)
//		Purpose: Handle request
//		Created: 30/3/04
//
// --------------------------------------------------------------------------
void WebApplication::Handle(const HTTPRequest &rRequest, HTTPResponse &rResponse)
{
	// First check -- if the URL ends with a '/', then the request must be rejected,
	// as it would stop the generated relative links working
	std::string requestURI(rRequest.GetRequestURI());
	if(requestURI.size() == 0 || (requestURI.size() == 1 && requestURI == "/"))
	{
		// Response is for home page
		std::string newURI;
		bool redirect = true;
		if(GetHomePageURI(newURI, redirect))
		{
			if(redirect)
			{
				// Redirect the browser
				rResponse.SetAsRedirect(newURI.c_str());
				return;
			}
			else
			{
				// Replace the URI behind the scenes
				requestURI = newURI;
			}
		}
	}
	if(requestURI[requestURI.size() - 1] == '/')
	{
		UnhandledResponse(rRequest, rResponse);
		return;
	}

	// Split up URL
	std::vector<std::string> urlElements;
	SplitString(requestURI, '/', urlElements);

	// Test that the URL is within the web app namespace, and that the
	// elements are of the right size
	if(urlElements.size() < 3 || urlElements[0] != GetURLBase()
		|| urlElements[1].size() > 4 || urlElements[2].size() != 4)
	{
		// Not within the application base...
		
		// What if it's a static file?
		const void *fileData = 0;
		int fileLength = 0;
		const char *fileType = 0;
		if(GetStaticFile(requestURI.c_str(), &fileData, &fileLength, &fileType))
		{
			ASSERT(fileData != 0 && fileLength >= 0 && fileType != 0);
			
			// Set response for this static file
			rResponse.SetResponseCode(HTTPResponse::Code_OK);
			rResponse.SetContentType(fileType);
			rResponse.Write(fileData, fileLength);
			// Mark the response as not dynamic
			rResponse.SetResponseIsDynamicContent(false);
		}
		else
		{
			// Return page not found response
			rResponse.SetAsNotFound(rRequest.GetRequestURI().c_str());
		}

		return;
	}
	
	// Get the language and page names
	uint32_t language = FourCharStringToInt(urlElements[1].c_str());
	uint32_t page = FourCharStringToInt(urlElements[2].c_str());
	
	// Tell the application object about this
	WebApplicationObject &rappObject(GetApplicationObject());
	rappObject.RequestStart(GetConfiguration());

	// Get the drived class to handle the request (autogen code)
	bool handled = HandlePage(rRequest, rResponse, language, page, urlElements);

	// Default unhandled response
	if(!handled)
	{
		UnhandledResponse(rRequest, rResponse);
	}

	rappObject.RequestFinish();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplication::HTTPConnectionClosing()
//		Purpose: Inform web app code of child process ending
//		Created: 22/12/04
//
// --------------------------------------------------------------------------
void WebApplication::HTTPConnectionClosing()
{
	WebApplicationObject &rappObject(GetApplicationObject());
	rappObject.ChildFinish();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplication::HandlePage(...)
//		Purpose: Autogen classes override this. Returns true if the request
//				 was handled.
//		Created: 17/5/04
//
// --------------------------------------------------------------------------
bool WebApplication::HandlePage(const HTTPRequest &rRequest, HTTPResponse &rResponse,
		uint32_t Language, uint32_t Page, std::vector<std::string> &rURLElements)
{
	return false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplication::ElementToInt(const char *)
//		Purpose: Returns a 32 bit int representing the page name from the
//				 given element of the URL. Used for faster URL and form dispatch.
//		Created: 7/4/04
//
// --------------------------------------------------------------------------
uint32_t WebApplication::FourCharStringToInt(const char *Element)
{
	const uint8_t *e = (const uint8_t *)Element;
	uint32_t i = 0;
	if(e[0] != 0)
	{
		i = e[0];
		if(e[1] != 0)
		{
			i |= e[1] << 8;
			if(e[2] != 0)
			{
				i |= e[2] << 16;
				if(e[3] != 0)
				{
					i |= e[3] << 24;
				}
			}
		}
	}

	return i;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplication::UnhandledResponse(const HTTPRequest &, HTTPResponse &)
//		Purpose: Reports an unhandled page to the user
//		Created: 7/4/04
//
// --------------------------------------------------------------------------
void WebApplication::UnhandledResponse(const HTTPRequest &rRequest, HTTPResponse &rResponse)
{
	rResponse.SetResponseCode(HTTPResponse::Code_OK);
	rResponse.SetContentType("text/html");
	
	#define UNHANDLED_HTML_1 "<html><head><title>Invalid application request</title></head>\n<body><h1>Invalid application request</h1>\n<p>The URI <i>"
	#define UNHANDLED_HTML_2 "</i> could not be handled as it does not correspond to a valid application request.</p></body></html>\n"
	rResponse.Write(UNHANDLED_HTML_1, sizeof(UNHANDLED_HTML_1) - 1);
	rResponse.WriteStringDefang(rRequest.GetRequestURI());
	rResponse.Write(UNHANDLED_HTML_2, sizeof(UNHANDLED_HTML_2) - 1);
	// Mark the response as not dynamic
	rResponse.SetResponseIsDynamicContent(false);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplication::GetFormDataString(const HTTPRequest &, const std::string &)
//		Purpose: Utility function to retrieve the first form data string from a
//				 HTTPRequest. (Limitations: Only retrieves the first, ignoring all others,
//				 and will return a blank string if it doesn't exist.)
//		Created: 9/4/04
//
// --------------------------------------------------------------------------
const std::string &WebApplication::GetFormDataString(const HTTPRequest &rRequest, const std::string &rKey)
{
	const HTTPRequest::Query_t &query(rRequest.GetQuery());
	HTTPRequest::Query_t::const_iterator i(query.find(rKey));
	if(i == query.end())
	{
		// Not found, return an empty string
		static std::string emptyString;
		return emptyString;
	}
	else
	{
		// At least one form entry exists, return it
		return i->second;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplication::GetApplicationObject()
//		Purpose: Return the web application object
//		Created: 17/5/04
//
// --------------------------------------------------------------------------
WebApplicationObject &WebApplication::GetApplicationObject()
{
	static WebApplicationObject defaultObj;
	return defaultObj;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplication::GetURLBase()
//		Purpose: Return the URL base of the application, that is, the first elements of the URI expected.
//		Created: 20/5/04
//
// --------------------------------------------------------------------------
const char *WebApplication::GetURLBase() const
{
	return "webapp";
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplication::GetStaticFile(const char *, void **, int *, const char **)
//		Purpose: Return details of a static file (if it exists). This default
//				 implementation finds no files.
//		Created: 7/12/04
//
// --------------------------------------------------------------------------
bool WebApplication::GetStaticFile(const char *URI, const void **ppFileOut, int *pFileSizeOut, const char **ppFileMIMETypeOut)
{
	TRACE0("Default GetStaticFile() called\n");
	return false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplication::GetConfigurationVariable(const char *)
//		Purpose: Utility function to get a named config variable from 
//				 the root of the Configuration object.
//		Created: 29/10/04
//
// --------------------------------------------------------------------------
const std::string &WebApplication::GetConfigurationVariable(const char *ConfigVar)
{
	const Configuration &rconfig(GetConfiguration());
	return rconfig.GetKeyValue(ConfigVar);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WebApplication::GetHomePageURI(std::string &, bool &) const
//		Purpose: Get home page. No home page set by default.
//		Created: 9/1/05
//
// --------------------------------------------------------------------------
bool WebApplication::GetHomePageURI(std::string &rHomePageLocation, bool &rAsRedirect) const
{
	return false;
}




