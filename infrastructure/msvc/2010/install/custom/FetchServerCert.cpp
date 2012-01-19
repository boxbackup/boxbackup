#include "stdafx.h"


UINT __stdcall FetchServerCert(MSIHANDLE hInstall)
{
	HRESULT hr;

	if(FAILED(hr = WcaInitialize(hInstall, "FetchServerCert")))
	{
		WcaLogError(hr, "Failed to initialize");
	}
	else
	{
		char *HostName = GetCustomActionData(hInstall);


	}

}

