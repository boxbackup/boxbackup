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

#include <syslog.h>

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
#ifndef PLATFORM_RANDOM_DEVICE_NONE
	if(::RAND_load_file(PLATFORM_RANDOM_DEVICE, 1024) != 1024)
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

