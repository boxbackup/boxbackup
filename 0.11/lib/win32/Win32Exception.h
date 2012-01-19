#ifdef WIN32
#	pragma once

#	include "autogen_Win32BaseException.h"

	class Win32Exception : public Win32BaseException
	{
	public:
		Win32Exception(unsigned int SubType, const std::string& rMessage = "") : Win32BaseException(SubType, rMessage)
		{
			mGLE = GetLastError();
			errno = win2errno(mGLE);
		}
	
		Win32Exception(const Win32Exception &rToCopy) : Win32BaseException(rToCopy.mSubType, rToCopy.mMessage)
		{
			mGLE = GetLastError();
			errno = win2errno(mGLE);
		}

		DWORD GetLastError()
		{
			return mGLE;
		}

		static int win2errno(DWORD gle) throw()
		{
			switch(gle) {
			case ERROR_ACCESS_DENIED:
				return EACCES;
			case ERROR_FILE_NOT_FOUND:
			case ERROR_PATH_NOT_FOUND:
				return ENOENT;
			case ERROR_INVALID_PARAMETER:
				return EINVAL;
			case ERROR_SHARING_VIOLATION:
				return EBUSY;
			default:
				return ENOSYS;
			}
		}

	protected:
		static DWORD mGLE;
	};

#endif
