// --------------------------------------------------------------------------
//
// File
//		Name:    Utils.cpp
//		Purpose: Utility function
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include <cstdlib>

#ifdef HAVE_CXXABI_H
	#include <cxxabi.h>
#endif

#ifdef HAVE_DLFCN_H
	#include <dlfcn.h>
#endif

#ifdef HAVE_EXECINFO_H
	#include <execinfo.h>
#endif

#ifdef HAVE_SIGNAL_H
	#include <signal.h>
#endif

#ifdef WIN32
#	include <dbghelp.h>
#endif

#ifdef NEED_BOX_VERSION_H
#	include "BoxVersion.h"
#endif

#include "Exception.h"
#include "Logging.h"
#include "Utils.h"

#include "MemLeakFindOn.h"

std::string GetBoxBackupVersion()
{
	return BOX_VERSION;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SplitString(const std::string &, char, std::vector<std::string> &)
//		Purpose: Splits a string at a given character
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void SplitString(std::string String, char SplitOn, std::vector<std::string> &rOutput)
{
	// Split it up.
	std::string::size_type begin = 0, end = 0, pos = 0;

	while(end = String.find_first_of(SplitOn, pos), end != String.npos)
	{
		// Is it preceded by the escape character?
		if(end > 0 && String[end - 1] == '\\')
		{
			// Ignore this one, don't change begin, let the next
			// match/fallback consume it instead. But remove the
			// backslash from the string, and set pos to the
			// current position, which no longer contains a
			// separator character.
			String.erase(end - 1, 1);
			pos = end;
		}
		else
		{
			// Extract the substring and move past it.
			unsigned int len = end - begin;
			if(len >= 1)
			{
				rOutput.push_back(String.substr(begin, len));
			}
			begin = end + 1;
			pos = begin;
		}
	}
	// Last string
	if(begin < String.size())
	{
		rOutput.push_back(String.substr(begin));
	}
/*#ifndef BOX_RELEASE_BUILD
	BOX_TRACE("Splitting string '" << String << " on " << (char)SplitOn);
	for(unsigned int l = 0; l < rOutput.size(); ++l)
	{
		BOX_TRACE(l << " = '" << rOutput[l] << "'");
	}
#endif*/
}

bool StartsWith(const std::string& prefix, const std::string& haystack)
{
	return haystack.size() >= prefix.size() &&
		haystack.substr(0, prefix.size()) == prefix;
}

bool EndsWith(const std::string& suffix, const std::string& haystack)
{
	return haystack.size() >= suffix.size() &&
		haystack.substr(haystack.size() - suffix.size()) == suffix;
}

std::string RemovePrefix(const std::string& prefix, const std::string& haystack,
	const char* default_value)
{
	if(StartsWith(prefix, haystack))
	{
		return haystack.substr(prefix.size());
	}
	else if(default_value == NULL)
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			"String '" << haystack << "' was expected to start with prefix "
			"'" << prefix << "', but did not.");
	}
	else
	{
		return default_value;
	}
}

std::string RemoveSuffix(const std::string& suffix, const std::string& haystack,
	const char* default_value)
{
	if(EndsWith(suffix, haystack))
	{
		return haystack.substr(0, haystack.size() - suffix.size());
	}
	else if(default_value == NULL)
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			"String '" << haystack << "' was expected to end with suffix "
			"'" << suffix << "', but did not.");
	}
	else
	{
		return default_value;
	}
}

// The backtrace routines are used by DebugMemLeakFinder, so we need to disable memory leak
// tracking during them, otherwise we could end up with infinite recursion.
#include "MemLeakFindOff.h"

const Log::Category BACKTRACE("Backtrace");

static std::string demangle(const std::string& mangled_name)
{
	std::string demangled_name = mangled_name;

#if defined WIN32
	char buffer[1024];
	if(UnDecorateSymbolName(mangled_name.c_str(), buffer, sizeof(buffer),
		UNDNAME_COMPLETE))
	{
		demangled_name = buffer;
	}
	else
	{
		BOX_LOG_WIN_ERROR("UnDecorateSymbolName failed");
	}
#elif defined HAVE_CXXABI_H
	int status;

	char* result = abi::__cxa_demangle(mangled_name.c_str(), NULL, NULL, &status);
	if(result != NULL)
	{
		demangled_name = result;
		free(result);
	}

	if (status == 0)
	{
		return demangled_name;
	}
	else if (status == -1)
	{
		BOX_WARNING("Demangle failed with memory allocation error: " << mangled_name);
	}
	else if (status == -2)
	{
		// Probably non-C++ name, don't demangle
	}
	else if (status == -3)
	{
		BOX_WARNING("Demangle failed with with invalid argument: " << mangled_name);
	}
	else
	{
		BOX_WARNING("Demangle failed with with unknown error " << status << ": " <<
			mangled_name);
	}
#endif // HAVE_CXXABI_H

	return demangled_name;
}

void DumpStackBacktrace(const std::string& filename)
{
	const int max_length = 20;
	void  *array[max_length];

#if defined WIN32
	size_t size = CaptureStackBackTrace(0, max_length, array, NULL);
#elif defined HAVE_EXECINFO_H
	size_t size = backtrace(array, 20);
#else
	BOX_TRACE("Backtrace support was not compiled in");
	return;
#endif

	// Instead of calling BOX_TRACE, we call Logging::Log directly in order to pass filename
	// as the source file. This allows exception backtraces to be turned on and off by file,
	// instead of all of them originating in Utils.cpp.
	std::ostringstream output;
	output << "Obtained " << size << " stack frames.";
	Logging::Log(Log::TRACE, filename, 0, // line
		__FUNCTION__, BACKTRACE, output.str());

	DumpStackBacktrace(filename, size, array);
}

void DumpStackBacktrace(const std::string& filename, size_t size, void * const * array)
{
#if defined WIN32
	HANDLE hProcess = GetCurrentProcess();
	// SymInitialize was called in mainhelper_init_win32()
	DWORD64 displacement;
	char symbol_info_buf[sizeof(SYMBOL_INFO) + 256];
	PSYMBOL_INFO pInfo = (SYMBOL_INFO *)symbol_info_buf;
	pInfo->MaxNameLen = 256;
	pInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
#endif

	for(size_t i = 0; i < size; i++)
	{
		std::ostringstream output;
		output << "Stack frame " << i << ": ";

#if defined WIN32
		if(!SymFromAddr(hProcess, (DWORD64)array[i], &displacement, pInfo))
#elif defined HAVE_DLADDR
		Dl_info info;
		int result = dladdr(array[i], &info);
		if(result == 0)
#endif
		{
			// This can also happen if HAVE_DLADDR is not defined (dladdr not found),
			// in which case the reported error is likely to be 0 (Success).
			BOX_LOG_NATIVE_WARNING("Failed to resolve "
				"backtrace address " << array[i]);
			output << "unresolved address " << array[i];
			continue;
		}

		const char* mangled_name = NULL;
		void* start_addr;
#if defined WIN32
		mangled_name = &(pInfo->Name[0]);
		start_addr   = (void *)(pInfo->Address);
#elif defined HAVE_DLADDR
		mangled_name = info.dli_sname;
		start_addr   = info.dli_saddr;
#else
		output << "address " << array[i];
		BOX_TRACE(output.str());
		continue;
#endif

		if(mangled_name == NULL)
		{
			output << "unknown address " << array[i];
		}
		else
		{
			uint64_t diff = (uint64_t) array[i];
			diff -= (uint64_t) start_addr;
			output << demangle(mangled_name) << "+" <<
				(void *)diff;
		}

		// Instead of calling BOX_TRACE, we call Logging::Log directly in order to pass
		// filename as the source file. This allows exception backtraces to be turned on
		// and off by file, instead of all of them originating in Utils.cpp.
		Logging::Log(Log::TRACE, filename, 0, // line
			__FUNCTION__, BACKTRACE, output.str());

		// We don't really care about frames after main() (e.g. CRT startup), and sometimes
		// they contain invalid data (not function pointers) such as 0x7.
		if(mangled_name != NULL && strcmp(mangled_name, "main") == 0)
		{
			break;
		}
	}
}

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    FileExists(const std::string& rFilename)
//		Purpose: Does a file exist?
//		Created: 20/11/03
//
// --------------------------------------------------------------------------
bool FileExists(const std::string& rFilename, int64_t *pFileSize,
	bool TreatLinksAsNotExisting)
{
	EMU_STRUCT_STAT st;
	if(EMU_LSTAT(rFilename.c_str(), &st) != 0)
	{
		if(errno == ENOENT)
		{
			return false;
		}
		else
		{
			THROW_EXCEPTION(CommonException, OSFileError);
		}
	}

	// is it a file?
	if(!S_ISDIR(st.st_mode))
	{
		if(TreatLinksAsNotExisting && S_ISLNK(st.st_mode))
		{
			return false;
		}

		// Yes. Tell caller the size?
		if(pFileSize != 0)
		{
			*pFileSize = st.st_size;
		}

		return true;
	}
	else
	{
		return false;
	}
}


std::string GetTempDirPath()
{
#ifdef WIN32
	char buffer[PATH_MAX+1];
	int len = GetTempPath(sizeof(buffer), buffer);
	if(len > sizeof(buffer))
	{
		THROW_EXCEPTION(CommonException, TempDirPathTooLong);
	}
	if(len == 0)
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			"Failed to get temporary directory path");
	}
	if(buffer[len - 1] == DIRECTORY_SEPARATOR_ASCHAR)
	{
		// Ensure that the returned string does not end with a backslash, so that we can append
		// one without creating a syntax error
		buffer[len - 1] = 0;
	}
	return std::string(buffer);
#else
	const char* result = getenv("TMPDIR");
	if(result == NULL)
	{
		result = getenv("TEMP");
	}
	if(result == NULL)
	{
		result = getenv("TMP");
	}
	if(result == NULL)
	{
		BOX_WARNING("TMPDIR/TEMP/TMP not set, falling back to /tmp");
		result = "/tmp";
	}
	return RemoveSuffix(DIRECTORY_SEPARATOR, std::string(result), result); // default_value
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ObjectExists(const std::string& rFilename)
//		Purpose: Does a object exist, and if so, is it a file or a directory?
//		Created: 23/11/03
//
// --------------------------------------------------------------------------
object_exists_t ObjectExists(const std::string& rFilename)
{
	EMU_STRUCT_STAT st;
	if(EMU_STAT(rFilename.c_str(), &st) != 0)
	{
		if(errno == ENOENT)
		{
			return ObjectExists_NoObject;
		}
		else
		{
			THROW_EMU_ERROR(
				BOX_FILE_MESSAGE(rFilename, "Failed to check for existence and type of file"),
				CommonException, OSFileError);
		}
	}

	// is it a file or a dir?
	return ((st.st_mode & S_IFDIR) == 0)?ObjectExists_File:ObjectExists_Dir;
}

std::string HumanReadableSize(int64_t Bytes)
{
	double readableValue = Bytes;
	std::string units = " B";

	if (readableValue > 1024)
	{
		readableValue /= 1024;
		units = "kB";
	}
  
	if (readableValue > 1024)
	{
		readableValue /= 1024;
		units = "MB";
	}
  
	if (readableValue > 1024)
	{
		readableValue /= 1024;
		units = "GB";
	}
  
	std::ostringstream result;
	result << std::fixed << std::setprecision(2) << readableValue <<
		" " << units;
	return result.str();
}

std::string FormatUsageBar(int64_t Blocks, int64_t Bytes, int64_t Max,
	bool MachineReadable)
{
	std::ostringstream result;


	if (MachineReadable)
	{
		result << (Bytes >> 10) << " kB, " <<
			std::setprecision(0) << ((Bytes*100)/Max) << "%";
	}
	else
	{
		// Bar graph
		char bar[17];
		unsigned int b = (int)((Bytes * (sizeof(bar)-1)) / Max);
		if(b > sizeof(bar)-1) {b = sizeof(bar)-1;}
		for(unsigned int l = 0; l < b; l++)
		{
			bar[l] = '*';
		}
		for(unsigned int l = b; l < sizeof(bar) - 1; l++)
		{
			bar[l] = ' ';
		}
		bar[sizeof(bar)-1] = '\0';

		result << std::fixed <<
			std::setw(10) << Blocks << " blocks, " <<
			std::setw(10) << HumanReadableSize(Bytes) << ", " << 
			std::setw(3) << std::setprecision(0) <<
			((Bytes*100)/Max) << "% |" << bar << "|";
	}

	return result.str();
}

std::string FormatUsageLineStart(const std::string& rName,
	bool MachineReadable)
{
	std::ostringstream result;

	if (MachineReadable)
	{
		result << rName << ": ";
	}
	else
	{
		result << std::setw(20) << std::right << rName << ": ";
	}

	return result.str();
}

std::map<std::string, str_pair_t> compare_str_maps(const str_map_t& expected,
	const str_map_t& actual)
{
	str_map_diff_t differences;

	// First check that every key in the expected map is present in the actual map,
	// with the correct value.
	for(str_map_t::const_iterator i = expected.begin(); i != expected.end(); i++)
	{
		std::string name = i->first;
		std::string value = i->second;
		str_map_t::const_iterator found = actual.find(name);
		if(found == actual.end())
		{
			differences[name] = str_pair_t(value, "");
		}
		else if(found->second != value)
		{
			differences[name] = str_pair_t(value, found->second);
		}
	}

	// Now try the other way around: check that every key in the actual map is present
	// in the expected map. We don't need to check values here, because if the key is
	// present in both maps but with different values, the first pass above will
	// already have recorded it.
	for(str_map_t::const_iterator i = actual.begin(); i != actual.end(); i++)
	{
		std::string name = i->first;
		std::string value = i->second;
		str_map_t::const_iterator found = expected.find(name);
		if(found == expected.end())
		{
			differences[name] = str_pair_t("", value);
		}
	}

	return differences;
}

bool process_is_running(int pid)
{
#ifdef WIN32
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION,
		false, pid);
	if (hProcess == NULL)
	{
		if (GetLastError() != ERROR_INVALID_PARAMETER)
		{
			BOX_LOG_WIN_ERROR("Failed to open process " << pid);
		}
		return false;
	}

	DWORD exitCode;
	BOOL result = GetExitCodeProcess(hProcess, &exitCode);
	CloseHandle(hProcess);

	if (result == 0)
	{
		BOX_LOG_WIN_ERROR("Failed to get exit code for process " << pid);
		return false;
	}

	if (exitCode == STILL_ACTIVE)
	{
		return true;
	}

	return false;
#else // !WIN32
	if(pid == 0) return false;
	return ::kill(pid, 0) != -1;
#endif // WIN32
}
