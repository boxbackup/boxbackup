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

#include <map>
#include <string>
#include <vector>

#include "MemLeakFindOn.h"

std::string GetBoxBackupVersion();

void SplitString(std::string String, char SplitOn, std::vector<std::string> &rOutput);
bool StartsWith(const std::string& prefix, const std::string& haystack);
bool EndsWith(const std::string& prefix, const std::string& haystack);
std::string RemovePrefix(const std::string& prefix, const std::string& haystack,
	bool force = true);
std::string RemoveSuffix(const std::string& suffix, const std::string& haystack,
	bool force = true);

void DumpStackBacktrace();

bool FileExists(const std::string& rFilename, int64_t *pFileSize = 0,
	bool TreatLinksAsNotExisting = false);

typedef enum
{
	ObjectExists_Unknown = -1,
	ObjectExists_NoObject = 0,
	ObjectExists_File = 1,
	ObjectExists_Dir = 2
} object_exists_t;

object_exists_t ObjectExists(const std::string& rFilename);
std::string HumanReadableSize(int64_t Bytes);
std::string FormatUsageBar(int64_t Blocks, int64_t Bytes, int64_t Max,
	bool MachineReadable);
std::string FormatUsageLineStart(const std::string& rName,
	bool MachineReadable);

typedef std::pair<std::string, std::string> str_pair_t;
typedef std::map<std::string, std::string> str_map_t;
typedef std::map<std::string, str_pair_t> str_map_diff_t;
str_map_diff_t compare_str_maps(const str_map_t& expected, const str_map_t& actual);

bool process_is_running(int pid);

#include "MemLeakFindOff.h"

#endif // UTILS__H
