#include "stdafx.h"


UINT __stdcall GenerateKeyFile(MSIHANDLE hInstall)
{
	HRESULT hr;

	if(FAILED(hr = WcaInitialize(hInstall, "GenerateKeyFile")))
	{
		WcaLogError(hr, "Failed to initialize");
	}
	else
	{
		DWORD err;
		HKEY hKey;

		if(ERROR_SUCCESS != (err = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Box Backup", 0, KEY_QUERY_VALUE|KEY_SET_VALUE, &hKey)))
		{
			hr = HRESULT_FROM_WIN32(err);
			WcaLogError(hr, "Failed to open registry key");
		}
		else
		{
			// Make sure we don't stomp on an existing key
			if(ERROR_SUCCESS == (err = RegQueryValueEx(hKey, "FileEncKeys", NULL, NULL, NULL, NULL)))
			{
				hr = HRESULT_FROM_WIN32(err);
			}
			else
			{
				HCRYPTPROV hCryptProv;

				if(!CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
				{
					hr = HRESULT_FROM_WIN32(GetLastError());
					WcaLogError(hr, "Failed to aquire crypt context");
				}
				else
				{
					BYTE RandomData[BACKUPCRYPTOKEYS_FILE_SIZE];

					if(!CryptGenRandom(hCryptProv, BACKUPCRYPTOKEYS_FILE_SIZE, RandomData))
					{
						hr = HRESULT_FROM_WIN32(GetLastError());
						WcaLogError(hr, "Can't get random data");
					}
					else
					{
						if(ERROR_SUCCESS != (err = RegSetValueEx(hKey, "FileEncKeys", 0, REG_BINARY, RandomData, BACKUPCRYPTOKEYS_FILE_SIZE)))
						{
							hr = HRESULT_FROM_WIN32(err);
							WcaLogError(hr, "Failed to set registry value");
						}

						SecureZeroMemory(RandomData, BACKUPCRYPTOKEYS_FILE_SIZE);
					}

					CryptReleaseContext(hCryptProv, 0);
				}
			}

			RegCloseKey(hKey);
		}
	}

	return WcaFinalize( (SUCCEEDED(hr)) ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE );
}
