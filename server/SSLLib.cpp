// --------------------------------------------------------------------------
//
// File
//		Name:    SSLLib.cpp
//		Purpose: Utility functions for dealing with the OpenSSL library
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------

#include "Box.h"

#define TLS_CLASS_IMPLEMENTATION_CPP
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#ifndef WIN32
#include <syslog.h>
#endif

#include "SSLLib.h"
#include "ServerException.h"

#include "MemLeakFindOn.h"

#ifndef NDEBUG
	bool SSLLib__TraceErrors = false;
#endif

// --------------------------------------------------------------------------
//
// Function
//		Name:    SSLLib::Initialise()
//		Purpose: Initialise SSL library
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
void SSLLib::Initialise()
{
	if(!::SSL_library_init())
	{
		LogError("Initialisation");
		THROW_EXCEPTION(ServerException, SSLLibraryInitialisationError)
	}
	
	// More helpful error messages
	::SSL_load_error_strings();

	// Extra seeding over and above what's already done by the library
#ifdef HAVE_RANDOM_DEVICE
	if(::RAND_load_file(RANDOM_DEVICE, 1024) != 1024)
	{
		THROW_EXCEPTION(ServerException, SSLRandomInitFailed)
	}
#else
	::fprintf(stderr, "No random device -- additional seeding of random number generator not performed.\n");
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SSLLib::LogError(const char *)
//		Purpose: Logs an error
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
void SSLLib::LogError(const char *ErrorDuringAction)
{
	unsigned long errcode;
	char errname[256];		// SSL docs say at least 120 bytes
	while((errcode = ERR_get_error()) != 0)
	{
		::ERR_error_string_n(errcode, errname, sizeof(errname));
		#ifndef NDEBUG
			if(SSLLib__TraceErrors)
			{
				TRACE2("SSL err during %s: %s\n", ErrorDuringAction, errname);
			}
		#endif
		::syslog(LOG_ERR, "SSL err during %s: %s", ErrorDuringAction, errname);
	}
}

