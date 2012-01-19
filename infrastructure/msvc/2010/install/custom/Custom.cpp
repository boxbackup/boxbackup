#include "stdafx.h"


// DllMain - Initialize and cleanup WiX custom action utils.
extern "C" BOOL WINAPI DllMain(
	__in HINSTANCE hInst,
	__in ULONG ulReason,
	__in LPVOID
	)
{
	switch(ulReason)
	{
	case DLL_PROCESS_ATTACH:
		WcaGlobalInitialize(hInst);
		break;

	case DLL_PROCESS_DETACH:
		WcaGlobalFinalize();
		break;
	}

	return TRUE;
}


HCRYPTPROV GetCryptContext(void)
{
	HCRYPTPROV		hCryptProv		= NULL;

	// Force creation of a new container
	CryptAcquireContext(&hCryptProv,"BoxBackup",NULL,0,CRYPT_DELETEKEYSET);
	CryptAcquireContext(&hCryptProv,"BoxBackup",NULL,PROV_RSA_FULL,CRYPT_MACHINE_KEYSET|CRYPT_NEWKEYSET);

	return hCryptProv;
}


char* GetCustomActionData(MSIHANDLE hInstall)
{
	DWORD	lenValueBuf = 0;
	char* valueBuf = NULL;
	UINT uiStat =  MsiGetProperty(hInstall, "CustomActionData", "", &lenValueBuf);
	//cchValueBuf now contains the size of the property's string, without null termination
	if (ERROR_MORE_DATA == uiStat)
	{
		++lenValueBuf; // add 1 for null termination
		valueBuf = new char[lenValueBuf];
		if (valueBuf)
		{
			uiStat = MsiGetProperty(hInstall, "CustomActionData", valueBuf, &lenValueBuf);
		}
	}
	if (ERROR_SUCCESS != uiStat && valueBuf != NULL)
	{
		delete[] valueBuf;
		valueBuf = NULL;
		SetLastError(uiStat);
	}

	return valueBuf;
}
