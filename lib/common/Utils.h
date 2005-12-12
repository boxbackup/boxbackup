// --------------------------------------------------------------------------
//
// File
//		Name:    Utils.h
//		Purpose: Utility function
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#ifndef UTILS__H
#define UTILS__H

#include <string>
#include <vector>

#include "MemLeakFindOn.h"

void SplitString(const std::string &String, char SplitOn, std::vector<std::string> &rOutput);

#ifdef SHOW_BACKTRACE_ON_EXCEPTION
	void DumpStackBacktrace();
#endif

bool FileExists(const char *Filename, int64_t *pFileSize = 0, bool TreatLinksAsNotExisting = false);

enum
{
	ObjectExists_NoObject = 0,
	ObjectExists_File = 1,
	ObjectExists_Dir = 2
};
int ObjectExists(const char *Filename);

#include "MemLeakFindOff.h"

#endif // UTILS__H
