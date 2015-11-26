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

std::string GetBoxBackupVersion();

void SplitString(std::string String, char SplitOn, std::vector<std::string> &rOutput);
bool StartsWith(const std::string& prefix, const std::string& haystack);
bool EndsWith(const std::string& prefix, const std::string& haystack);
std::string RemovePrefix(const std::string& prefix, const std::string& haystack);
std::string RemoveSuffix(const std::string& suffix, const std::string& haystack);

void DumpStackBacktrace();

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

std::string BoxGetTemporaryDirectoryName();

#include "MemLeakFindOff.h"

#endif // UTILS__H
