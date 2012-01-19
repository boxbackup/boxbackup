#include "Box.h"


namespace BoxBackup
{
	namespace Win32
	{
		void wide2multi(const std::wstring& wide, std::string& multi, const UINT CodePage) throw(Win32Exception)
		{
			int len = WideCharToMultiByte(CodePage,0,wide.c_str(),static_cast<int>(wide.size()+1),NULL,0,NULL,NULL);
			if (0 == len)
				throw Win32Exception(Win32Exception::API_WideCharToMultiByte);
			std::unique_ptr<char[]> buf(new char[len]);
			if (0 == WideCharToMultiByte(CodePage,0,wide.c_str(),static_cast<int>(wide.size()+1),buf.get(),len,NULL,NULL))
				throw Win32Exception(Win32Exception::API_WideCharToMultiByte);
			multi.assign(buf.get());
		}

		std::string wide2multi(const std::wstring& wide, const UINT CodePage) throw(Win32Exception)
		{
			std::string multi;
			wide2multi(wide,multi,CodePage);
			return multi;
		}

		void multi2wide(const std::string& multi, std::wstring& wide, const UINT CodePage) throw(Win32Exception)
		{
			int len = MultiByteToWideChar(CodePage,0,multi.c_str(),static_cast<int>(multi.size()+1),NULL,0);
			if (0 == len)
				throw Win32Exception(Win32Exception::API_MultiByteToWideChar);
			std::unique_ptr<wchar_t[]> buf(new wchar_t[len]);
			if (0 == MultiByteToWideChar(CodePage,0,multi.c_str(),static_cast<int>(multi.size()+1),buf.get(),len))
				throw Win32Exception(Win32Exception::API_MultiByteToWideChar);
			wide = buf.get();
		}

		std::wstring multi2wide(const std::string& multi, const UINT CodePage) throw(Win32Exception)
		{
			std::wstring wide;
			multi2wide(multi,wide,CodePage);
			return wide;
		}

		std::string GetCurrentDirectory() throw(Win32Exception)
		{
			DWORD len = GetCurrentDirectoryW(0, NULL);
			if (0 == len)
				throw Win32Exception(Win32Exception::API_GetCurrentDirectory);
			std::unique_ptr<wchar_t[]> wide(new wchar_t[len]);
			if (0 == GetCurrentDirectoryW(len, wide.get()))
				throw Win32Exception(Win32Exception::API_GetCurrentDirectory);
			return wide2multi(wide.get());
		}

		std::string GetErrorMessage(DWORD errorCode) throw()
		{
			char* pMsgBuf = NULL;

			DWORD chars = FormatMessage
			(
				FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM,
				NULL,
				errorCode,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(char *)(&pMsgBuf),
				0, NULL
			);

			if (chars == 0 || pMsgBuf == NULL)
			{
				return std::string("failed to get error message");
			}

			// remove embedded newline
			pMsgBuf[chars - 1] = 0;
			pMsgBuf[chars - 2] = 0;

			std::ostringstream line;
			line << pMsgBuf << " (" << errorCode << ")";
			LocalFree(pMsgBuf);

			return line.str();
		}

		// --------------------------------------------------------------------------
		//
		// Function
		//		Name:    ConvertPathToAbsoluteUnicode
		//		Purpose: Converts relative paths to absolute (with unicode marker)
		//		Created: 4th February 2006
		//
		// --------------------------------------------------------------------------
		std::string ConvertPathToAbsoluteUnicode(const char *pFileName) throw(Win32Exception)
		{
			std::string filename;
			for (int i = 0; pFileName[i] != 0; i++)
			{
				if (pFileName[i] == '/')
				{
					filename += '\\';
				}
				else
				{
					filename += pFileName[i];
				}
			}

			std::string tmpStr("\\\\?\\");

			// Is the path relative or absolute?
			// Absolute paths on Windows are always a drive letter
			// followed by ':'

			if (filename.length() > 2 && filename[0] == '\\' &&
				filename[1] == '\\')
			{
				tmpStr += "UNC\\";
				filename.replace(0, 2, "");
				// \\?\UNC\<server>\<share>
				// see http://msdn2.microsoft.com/en-us/library/aa365247.aspx
			}
			else if (filename.length() >= 1 && filename[0] == '\\')
			{
				// root directory of current drive.
				tmpStr = GetCurrentDirectory();
				tmpStr.resize(2); // drive letter and colon
			}
			else if (filename.length() >= 2 && filename[1] != ':')
			{
				// Must be relative. We need to get the
				// current directory to make it absolute.
				tmpStr += GetCurrentDirectory();
				if (tmpStr[tmpStr.length()] != '\\')
				{
					tmpStr += '\\';
				}
			}

			tmpStr += filename;

			// We are using direct filename access, which does not support ..,
			// so we need to implement it ourselves.

			for (std::string::size_type i = 1; i < tmpStr.size() - 3; i++)
			{
				if (tmpStr.substr(i, 3) == "\\..")
				{
					std::string::size_type lastSlash =
						tmpStr.rfind('\\', i - 1);

					if (lastSlash == std::string::npos)
					{
						// no previous directory, ignore it,
						// CreateFile will fail with error 123
					}
					else
					{
						tmpStr.replace(lastSlash, i + 3 - lastSlash,
							"");
					}

					i = lastSlash;
				}
			}

			return tmpStr;
		}

		// --------------------------------------------------------------------------
		//
		// Function
		//		Name:    OpenFileByNameUtf8
		//		Purpose: Converts filename to Unicode and returns
		//			a handle to it. In case of error, sets errno,
		//			logs the error and returns NULL.
		//		Created: 10th December 2004
		//
		// --------------------------------------------------------------------------
		HANDLE OpenFileByNameUtf8(const char* pFileName, DWORD flags) throw(Win32Exception)
		{
			std::string multiPath = ConvertPathToAbsoluteUnicode(pFileName);
			std::wstring widePath = multi2wide(multiPath);

			HANDLE hFile;

			if (INVALID_HANDLE_VALUE == (hFile = CreateFileW(widePath.c_str(),
																				flags,
																				FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE,
																				NULL,
																				OPEN_EXISTING,
																				FILE_FLAG_BACKUP_SEMANTICS,
																				NULL)))
			{
				// if our open fails we should always be able to
				// open in this mode - to get the inode information
				// at least one process must have the file open -
				// in this case someone else does.
				if (INVALID_HANDLE_VALUE == (hFile = CreateFileW(widePath.c_str(),
																					READ_CONTROL,
																					FILE_SHARE_READ,
																					NULL,
																					OPEN_EXISTING,
																					FILE_FLAG_BACKUP_SEMANTICS,
																					NULL)))
				{
					throw Win32Exception(Win32Exception::API_CreateFile, pFileName);
				}
			}

			return hFile;
		}


		bool EnableBackupRights()
		{
			HANDLE hToken;
			TOKEN_PRIVILEGES token_priv;

			//open current process to adjust privileges
			if(!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES,
				&hToken))
			{
				BOX_LOG_WIN_ERROR("Failed to open process token");
				return false;
			}

			//let's build the token privilege struct -
			//first, look up the LUID for the backup privilege

			if (!LookupPrivilegeValue(
				NULL, //this system
				SE_BACKUP_NAME, //the name of the privilege
				&( token_priv.Privileges[0].Luid ))) //result
			{
				BOX_LOG_WIN_ERROR("Failed to lookup backup privilege");
				CloseHandle(hToken);
				return false;
			}

			token_priv.PrivilegeCount = 1;
			token_priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

			// now set the privilege
			// because we're going exit right after dumping the streams, there isn't
			// any need to save current state

			if (!AdjustTokenPrivileges(
				hToken, //our process token
				false,  //we're not disabling everything
				&token_priv, //address of structure
				sizeof(token_priv), //size of structure
				NULL, NULL)) //don't save current state
			{
				//this function is a little tricky - if we were adjusting
				//more than one privilege, it could return success but not
				//adjust them all - in the general case, you need to trap this
				BOX_LOG_WIN_ERROR("Failed to enable backup privilege");
				CloseHandle(hToken);
				return false;

			}

			CloseHandle(hToken);
			return true;
		}

	}
}