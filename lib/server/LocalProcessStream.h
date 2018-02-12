// --------------------------------------------------------------------------
//
// File
//		Name:    LocalProcessStream.h
//		Purpose: Opens a process, and presents stdin/stdout as a stream.
//		Created: 12/3/04
//
// --------------------------------------------------------------------------

#ifndef LOCALPROCESSSTREAM__H
#define LOCALPROCESSSTREAM__H

#include <memory>
#include <string>

#include "IOStream.h"

std::auto_ptr<IOStream> LocalProcessStream(const std::string& rCommandLine,
	pid_t &rPidOut);

#endif // LOCALPROCESSSTREAM__H

