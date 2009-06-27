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

bool FileExists(const std::string& rFilename, int64_t *pFileSize = 0,
	bool TreatLinksAsNotExisting = false);

enum
{
	ObjectExists_NoObject = 0,
	ObjectExists_File = 1,
	ObjectExists_Dir = 2
};
int ObjectExists(const std::string& rFilename);
std::string HumanReadableSize(int64_t Bytes);
std::string FormatUsageBar(int64_t Blocks, int64_t Bytes, int64_t Max,
	bool MachineReadable);
std::string FormatUsageLineStart(const std::string& rName,
	bool MachineReadable);

#include "MemLeakFindOff.h"

#endif // UTILS__H
