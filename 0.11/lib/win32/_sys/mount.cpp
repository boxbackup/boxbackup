#include "Box.h"


using namespace Win32;


// --------------------------------------------------------------------------
//
// Function
//		Name:    statfs
//		Purpose: returns the mount point of where a file is located -
//			in this case the volume serial number
//		Created: 25th October 2004
//
// --------------------------------------------------------------------------
int statfs(const char * pName, struct statfs * s) throw()
{
	HANDLE hFile;
	
	try
	{
		hFile = OpenFileByNameUtf8(pName, FILE_READ_ATTRIBUTES | FILE_READ_EA);

		BY_HANDLE_FILE_INFORMATION fi;
		if (!GetFileInformationByHandle(hFile, &fi))
			throw Win32Exception(Win32Exception::API_GetFileInformationByHandle, pName);

		// convert volume serial number to a string
		_ui64toa(fi.dwVolumeSerialNumber, s->f_mntonname + 1, 16);

		// pseudo unix mount point
		s->f_mntonname[0] = '\\';

		CloseHandle(hFile);   // close the handle

		return 0;
	}
	EMU_EXCEPTION_HANDLING_RETURN(-1)
}
