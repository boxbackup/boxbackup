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

#include "Utils.h"
#include "CommonException.h"
#include "Logging.h"

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
void DumpStackBacktrace()
{
	void  *array[10];
	size_t size = backtrace (array, 10);
	char **strings = backtrace_symbols (array, size);

	BOX_TRACE("Obtained " << size << " stack frames.");

	for(size_t i = 0; i < size; i++)
	{
		// Demangling code copied from 
		// cctbx_sources/boost_adaptbx/meta_ext.cpp, BSD license
		
		std::string mangled_frame = strings[i];
		std::string output_frame  = strings[i]; // default

		#ifdef HAVE_CXXABI_H
		int start = mangled_frame.find('(');
		int end   = mangled_frame.find('+', start);
		std::string mangled_func = mangled_frame.substr(start + 1,
			end - start - 1);

		int status;
		
#include "MemLeakFindOff.h"
		char* result = abi::__cxa_demangle(mangled_func.c_str(),
			NULL, NULL, &status);
#include "MemLeakFindOn.h"

		if (result == NULL)
		{
			if (status == 0)
			{
				BOX_WARNING("Demangle failed but no error: " <<
					mangled_func);
			}
			else if (status == -1)
			{
				BOX_WARNING("Demangle failed with "
					"memory allocation error: " <<
					mangled_func);
			}
			else if (status == -2)
			{
				// Probably non-C++ name, don't demangle
				/*
				BOX_WARNING("Demangle failed with "
					"with invalid name: " <<
					mangled_func);
				*/
			}
			else if (status == -3)
			{
				BOX_WARNING("Demangle failed with "
					"with invalid argument: " <<
					mangled_func);
			}
			else
			{
				BOX_WARNING("Demangle failed with "
					"with unknown error " << status <<
					": " << mangled_func);
			}
		}
		else
		{
			output_frame = mangled_frame.substr(0, start + 1) +
				result + mangled_frame.substr(end);
#include "MemLeakFindOff.h"
			std::free(result);
#include "MemLeakFindOn.h"
		}
		#endif // HAVE_CXXABI_H

		BOX_TRACE("Stack frame " << i << ": " << output_frame);
	}

#include "MemLeakFindOff.h"
	std::free (strings);
#include "MemLeakFindOn.h"
}
#endif



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
