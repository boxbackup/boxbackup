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

#ifdef SHOW_BACKTRACE_ON_EXCEPTION
	#include <execinfo.h>
	#include <stdlib.h>
#endif

#ifdef HAVE_CXXABI_H
	#include <cxxabi.h>
#endif

#ifdef HAVE_DLFCN_H
	#include <dlfcn.h>
#endif

#ifdef NEED_BOX_VERSION_H
#	include "BoxVersion.h"
#endif

#include "CommonException.h"
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
void SplitString(const std::string &String, char SplitOn, std::vector<std::string> &rOutput)
{
	// Split it up.
	std::string::size_type b = 0;
	std::string::size_type e = 0;
	while(e = String.find_first_of(SplitOn, b), e != String.npos)
	{
		// Get this string
		unsigned int len = e - b;
		if(len >= 1)
		{
			rOutput.push_back(String.substr(b, len));
		}
		b = e + 1;
	}
	// Last string
	if(b < String.size())
	{
		rOutput.push_back(String.substr(b));
	}
/*#ifndef BOX_RELEASE_BUILD
	BOX_TRACE("Splitting string '" << String << " on " << (char)SplitOn);
	for(unsigned int l = 0; l < rOutput.size(); ++l)
	{
		BOX_TRACE(l << " = '" << rOutput[l] << "'");
	}
#endif*/
}

#ifdef SHOW_BACKTRACE_ON_EXCEPTION
static std::string demangle(const std::string& mangled_name)
{
	#ifdef HAVE_CXXABI_H
	int status;
	
#include "MemLeakFindOff.h"
	char* result = abi::__cxa_demangle(mangled_name.c_str(),
		NULL, NULL, &status);
#include "MemLeakFindOn.h"

	if (result == NULL)
	{
		if (status == 0)
		{
			BOX_WARNING("Demangle failed but no error: " <<
				mangled_name);
		}
		else if (status == -1)
		{
			BOX_WARNING("Demangle failed with "
				"memory allocation error: " <<
				mangled_name);
		}
		else if (status == -2)
		{
			// Probably non-C++ name, don't demangle
			/*
			BOX_WARNING("Demangle failed with "
				"with invalid name: " <<
				mangled_name);
			*/
		}
		else if (status == -3)
		{
			BOX_WARNING("Demangle failed with "
				"with invalid argument: " <<
				mangled_name);
		}
		else
		{
			BOX_WARNING("Demangle failed with "
				"with unknown error " << status <<
				": " << mangled_name);
		}

		return std::string(mangled_name);
	}
	else
	{
		std::string output = result;
#include "MemLeakFindOff.h"
		std::free(result);
#include "MemLeakFindOn.h"
		return output;
	}
	#else // !HAVE_CXXABI_H
	return mangled_name;
	#endif // HAVE_CXXABI_H
}

void DumpStackBacktrace()
{
	void  *array[10];
	size_t size = backtrace(array, 10);
	BOX_TRACE("Obtained " << size << " stack frames.");

	for(size_t i = 0; i < size; i++)
	{
		std::ostringstream output;
		output << "Stack frame " << i << ": ";

		#ifdef HAVE_DLADDR
			Dl_info info;
			int result = dladdr(array[i], &info);

			if(result == 0)
			{
				BOX_LOG_SYS_WARNING("Failed to resolve "
					"backtrace address " << array[i]);
				output << "unresolved address " << array[i];
			}
			else if(info.dli_sname == NULL)
			{
				output << "unknown address " << array[i];
			}
			else
			{
				uint64_t diff = (uint64_t) array[i];
				diff -= (uint64_t) info.dli_saddr;
				output << demangle(info.dli_sname) << "+" <<
					(void *)diff;
			}
		#else
			output << "address " << array[i];
		#endif // HAVE_DLADDR

		BOX_TRACE(output.str());
	}
}
#endif // SHOW_BACKTRACE_ON_EXCEPTION



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
	if((st.st_mode & S_IFDIR) == 0)
	{
		if(TreatLinksAsNotExisting && ((st.st_mode & S_IFLNK) != 0))
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

// --------------------------------------------------------------------------
//
// Function
//		Name:    ObjectExists(const std::string& rFilename)
//		Purpose: Does a object exist, and if so, is it a file or a directory?
//		Created: 23/11/03
//
// --------------------------------------------------------------------------
int ObjectExists(const std::string& rFilename)
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
			THROW_EXCEPTION(CommonException, OSFileError);
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

std::string BoxGetTemporaryDirectoryName()
{
#ifdef WIN32
	// http://msdn.microsoft.com/library/default.asp?
	// url=/library/en-us/fileio/fs/creating_and_using_a_temporary_file.asp

	DWORD dwRetVal;
	char lpPathBuffer[1024];
	DWORD dwBufSize = sizeof(lpPathBuffer);
	
	// Get the temp path.
	dwRetVal = GetTempPath(dwBufSize,     // length of the buffer
						   lpPathBuffer); // buffer for path 
	if (dwRetVal > dwBufSize)
	{
		THROW_EXCEPTION(CommonException, TempDirPathTooLong)
	}
	
	return std::string(lpPathBuffer);
#elif defined TEMP_DIRECTORY_NAME
	return std::string(TEMP_DIRECTORY_NAME);
#else	
	#error non-static temporary directory names not supported yet
#endif
}


