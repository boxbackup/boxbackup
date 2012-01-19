#include "Box.h"


using namespace Win32;


int chown(const char * Filename, uint32_t uid, uint32_t gid) throw()
{
	//important - this needs implementing
	//If a large restore is required then 
	//it needs to restore files AND permissions
	//reference AdjustTokenPrivileges
	//GetAccountSid
	//InitializeSecurityDescriptor
	//SetSecurityDescriptorOwner
	//The next function looks like the guy to use...
	//SetFileSecurity

	//indicate success
	return 0;
}


// Windows and Unix owners and groups are pretty fundamentally different.
// Ben prefers that we kludge here rather than litter the code with #ifdefs.
// Pretend to be root, and pretend that set...() operations succeed.
int setegid(int) throw()
{
	return true;
}
int seteuid(int) throw()
{
	return true;
}
int setgid(int) throw()
{
	return true;
}
int setuid(int) throw()
{
	return true;
}
int getgid(void) throw()
{
	return 0;
}
int getuid(void) throw()
{
	return 0;
}
int geteuid(void) throw()
{
	return 0;
}

unsigned int sleep(unsigned int secs) throw()
{
	Sleep(secs*1000);
	return(ERROR_SUCCESS);
}



int emu_chdir(const char* pDirName) throw()
{
	try
	{
		std::string multiDir = ConvertPathToAbsoluteUnicode(pDirName);
		std::wstring wideDir = multi2wide(multiDir);

		if (!SetCurrentDirectoryW(wideDir.c_str()))
			throw Win32Exception(Win32Exception::API_SetCurrentDirectory, pDirName);

		return 0;
	}
	EMU_EXCEPTION_HANDLING_RETURN(-1)
}

int emu_unlink(const char* pFileName)
{
	try
	{
		std::string multiPath = ConvertPathToAbsoluteUnicode(pFileName);
		std::wstring widePath = multi2wide(multiPath);

		if (!DeleteFileW(widePath.c_str()))
			throw Win32Exception(Win32Exception::API_DeleteFile, pFileName);

		return 0;
	}
	EMU_EXCEPTION_HANDLING_RETURN(-1)
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    emu_chmod
//		Purpose: replacement for the POSIX chmod function,
//			works with unicode filenames supplied in utf8 format
//		Created: 26th July 2006
//
// --------------------------------------------------------------------------
int emu_chmod(const char * pName, mode_t mode)
{
	try
	{
		std::string multiPath = ConvertPathToAbsoluteUnicode(pName);
		std::wstring widePath = multi2wide(multiPath);

		DWORD attribs;
	
		if (INVALID_FILE_ATTRIBUTES == (attribs = GetFileAttributesW(widePath.c_str())))
			throw Win32Exception(Win32Exception::API_GetFileAttributes, pName);

		if (mode & S_IWRITE)
		{
			attribs &= ~FILE_ATTRIBUTE_READONLY;
		}
		else
		{
			attribs |= FILE_ATTRIBUTE_READONLY;
		}

		if (!SetFileAttributesW(widePath.c_str(), attribs))
			throw Win32Exception(Win32Exception::API_SetFileAttributes);

		return 0;
	}
	EMU_EXCEPTION_HANDLING_RETURN(-1)
}

char* emu_getcwd(char* pBuffer, size_t BufSize)
{
	try
	{
		std::string curDir = GetCurrentDirectory();

		if (curDir.size() >= BufSize) {
			errno = ENAMETOOLONG;
		} else {
			return strcpy(pBuffer, curDir.c_str());
		}
	}
	EMU_EXCEPTION_HANDLING_RETURN(NULL)
}

int emu_rename(const char* pOldFileName, const char* pNewFileName)
{
	try
	{
		std::string oldMultiPath = ConvertPathToAbsoluteUnicode(pOldFileName);
		std::wstring oldWidePath = multi2wide(oldMultiPath);
		std::string newMultiPath = ConvertPathToAbsoluteUnicode(pNewFileName);
		std::wstring newWidePath = multi2wide(newMultiPath);

		if (!MoveFileW(oldWidePath.c_str(), newWidePath.c_str()))
			throw Win32Exception(Win32Exception::API_MoveFile);

		return 0;
	}
	EMU_EXCEPTION_HANDLING_RETURN(-1)
}

