//***************************************************************
// From the book "Win32 System Services: The Heart of Windows 98
// and Windows 2000"
// by Marshall Brain
// Published by Prentice Hall
// Copyright 1995 Prentice Hall.
//
// This code implements the Windows API Service interface 
// for the Box Backup for Windows native port.
// Adapted for Box Backup by Nick Knight.
//***************************************************************

#ifdef WIN32

#include "Box.h"

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif
#ifdef HAVE_PROCESS_H
	#include <process.h>
#endif

extern void TerminateService(void);
extern unsigned int WINAPI RunService(LPVOID lpParameter);

// Global variables

TCHAR* gServiceName = TEXT("Box Backup Service");
SERVICE_STATUS gServiceStatus;
SERVICE_STATUS_HANDLE gServiceStatusHandle = 0;
HANDLE gStopServiceEvent = 0;
DWORD gServiceReturnCode = 0;

#define SERVICE_NAME "boxbackup"

void ShowMessage(char *s)
{
	MessageBox(0, s, "Box Backup Message", 
		MB_OK | MB_SETFOREGROUND | MB_DEFAULT_DESKTOP_ONLY);
}

void ErrorHandler(char *s, DWORD err)
{
	char buf[256];
	memset(buf, 0, sizeof(buf));
	_snprintf(buf, sizeof(buf)-1, "%s: %s", s,
		GetErrorMessage(err).c_str());
	BOX_ERROR(buf);
	MessageBox(0, buf, "Error", 
		MB_OK | MB_SETFOREGROUND | MB_DEFAULT_DESKTOP_ONLY);
	ExitProcess(err);
}

void WINAPI ServiceControlHandler( DWORD controlCode )
{
	switch ( controlCode )
	{
		case SERVICE_CONTROL_INTERROGATE:
			break;

		case SERVICE_CONTROL_SHUTDOWN:
		case SERVICE_CONTROL_STOP:
			Beep(1000,100);
			TerminateService();
			gServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
			SetServiceStatus(gServiceStatusHandle, &gServiceStatus);

			SetEvent(gStopServiceEvent);
			return;

		case SERVICE_CONTROL_PAUSE:
			break;

		case SERVICE_CONTROL_CONTINUE:
			break;

		default:
			if ( controlCode >= 128 && controlCode <= 255 )
				// user defined control code
				break;
			else
				// unrecognised control code
				break;
	}

	SetServiceStatus( gServiceStatusHandle, &gServiceStatus );
}

// ServiceMain is called when the SCM wants to
// start the service. When it returns, the service
// has stopped. It therefore waits on an event
// just before the end of the function, and
// that event gets set when it is time to stop. 
// It also returns on any error because the
// service cannot start if there is an eror.

static char* spConfigFileName;

VOID ServiceMain(DWORD argc, LPTSTR *argv) 
{
	// initialise service status
	gServiceStatus.dwServiceType = SERVICE_WIN32;
	gServiceStatus.dwCurrentState = SERVICE_STOPPED;
	gServiceStatus.dwControlsAccepted = 0;
	gServiceStatus.dwWin32ExitCode = NO_ERROR;
	gServiceStatus.dwServiceSpecificExitCode = NO_ERROR;
	gServiceStatus.dwCheckPoint = 0;
	gServiceStatus.dwWaitHint = 0;

	gServiceStatusHandle = RegisterServiceCtrlHandler(gServiceName, 
		ServiceControlHandler);

	if (gServiceStatusHandle)
	{
		// service is starting
		gServiceStatus.dwCurrentState = SERVICE_START_PENDING;
		SetServiceStatus(gServiceStatusHandle, &gServiceStatus);

		// do initialisation here
		gStopServiceEvent = CreateEvent(0, TRUE, FALSE, 0);
		if (!gStopServiceEvent)
		{
			gServiceStatus.dwControlsAccepted &= 
				~(SERVICE_ACCEPT_STOP | 
				  SERVICE_ACCEPT_SHUTDOWN);
			gServiceStatus.dwCurrentState = SERVICE_STOPPED;
			SetServiceStatus(gServiceStatusHandle, &gServiceStatus);
			return;
		}

		HANDLE ourThread = (HANDLE)_beginthreadex(
			NULL,
			0,
			RunService,
			spConfigFileName,
			CREATE_SUSPENDED,
			NULL);

		SetThreadPriority(ourThread, THREAD_PRIORITY_LOWEST);
		ResumeThread(ourThread);

		// we are now running so tell the SCM
		gServiceStatus.dwControlsAccepted |= 
			(SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
		gServiceStatus.dwCurrentState = SERVICE_RUNNING;
		SetServiceStatus(gServiceStatusHandle, &gServiceStatus);

		// do cleanup here
		WaitForSingleObject(gStopServiceEvent, INFINITE);
		CloseHandle(gStopServiceEvent);
		gStopServiceEvent = 0;

		// service was stopped
		gServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
		SetServiceStatus(gServiceStatusHandle, &gServiceStatus);

		// service is now stopped
		gServiceStatus.dwControlsAccepted &= 
			~(SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);

		gServiceStatus.dwCurrentState = SERVICE_STOPPED;

		if (gServiceReturnCode != 0)
		{
			gServiceStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
			gServiceStatus.dwServiceSpecificExitCode = gServiceReturnCode;
		}

		SetServiceStatus(gServiceStatusHandle, &gServiceStatus);
	}
}

int OurService(const char* pConfigFileName)
{
	spConfigFileName = strdup(pConfigFileName);

	SERVICE_TABLE_ENTRY serviceTable[] = 
	{ 
		{ SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION) ServiceMain },
		{ NULL, NULL }
	};
	BOOL success;

	// Register with the SCM
	success = StartServiceCtrlDispatcher(serviceTable);

	free(spConfigFileName);
	spConfigFileName = NULL;

	if (!success)
	{
		ErrorHandler("Failed to start service. Did you start "
			"Box Backup from the Service Control Manager? "
			"(StartServiceCtrlDispatcher)", GetLastError());
		return 1;
	}

	return 0;
}

int InstallService(const char* pConfigFileName, const std::string& rServiceName)
{
	if (pConfigFileName != NULL)
	{
		EMU_STRUCT_STAT st;

		if (emu_stat(pConfigFileName, &st) != 0)
		{
			BOX_LOG_SYS_ERROR("Failed to open configuration file "
				"'" << pConfigFileName << "'");
			return 1;
		}

		if (!(st.st_mode & S_IFREG))
		{
	
			BOX_ERROR("Failed to open configuration file '" <<
				pConfigFileName << "': not a file");
			return 1;
		}
	}

	SC_HANDLE scm = OpenSCManager(0, 0, SC_MANAGER_CREATE_SERVICE);

	if (!scm) 
	{
		BOX_ERROR("Failed to open service control manager: " <<
			GetErrorMessage(GetLastError()));
		return 1;
	}

	char cmd[MAX_PATH];
	GetModuleFileName(NULL, cmd, sizeof(cmd)-1);
	cmd[sizeof(cmd)-1] = 0;

	std::string cmdWithArgs(cmd);
	cmdWithArgs += " -s -S \"" + rServiceName + "\"";

	if (pConfigFileName != NULL)
	{
		cmdWithArgs += " \"";
		cmdWithArgs += pConfigFileName;
		cmdWithArgs += "\"";
	}

	std::string serviceDesc = "Box Backup (" + rServiceName + ")";

	SC_HANDLE newService = CreateService(
		scm, 
		rServiceName.c_str(),
		serviceDesc.c_str(),
		SERVICE_ALL_ACCESS, 
		SERVICE_WIN32_OWN_PROCESS, 
		SERVICE_AUTO_START, 
		SERVICE_ERROR_NORMAL, 
		cmdWithArgs.c_str(),
		0,0,0,0,0);

	DWORD err = GetLastError();
	CloseServiceHandle(scm);

	if (!newService) 
	{
		switch (err)
		{
			case ERROR_SERVICE_EXISTS:
			{
				BOX_ERROR("Failed to create Box Backup "
					"service: it already exists");
			}
			break;

			case ERROR_SERVICE_MARKED_FOR_DELETE:
			{
				BOX_ERROR("Failed to create Box Backup "
					"service: it is waiting to be deleted");
			}
			break;

			case ERROR_DUPLICATE_SERVICE_NAME:
			{
				BOX_ERROR("Failed to create Box Backup "
					"service: a service with this name "
					"already exists");
			}
			break;

			default:
			{
				BOX_ERROR("Failed to create Box Backup "
					"service: error " <<
					GetErrorMessage(GetLastError()));
			}
		}

		return 1;
	}

	BOX_INFO("Created Box Backup service");
	
	SERVICE_DESCRIPTION desc;
	desc.lpDescription = "Backs up your data files over the Internet";
	
	if (!ChangeServiceConfig2(newService, SERVICE_CONFIG_DESCRIPTION,
		&desc))
	{
		BOX_WARNING("Failed to set description for Box Backup "
			"service: " << GetErrorMessage(GetLastError()));
	}

	CloseServiceHandle(newService);

	return 0;
}

int RemoveService(const std::string& rServiceName)
{
	SC_HANDLE scm = OpenSCManager(0,0,SC_MANAGER_CREATE_SERVICE);

	if (!scm) 
	{
		BOX_ERROR("Failed to open service control manager: " <<
			GetErrorMessage(GetLastError()));
		return 1;
	}

	SC_HANDLE service = OpenService(scm, rServiceName.c_str(), 
		SERVICE_ALL_ACCESS|DELETE);
	DWORD err = GetLastError();
	CloseServiceHandle(scm);

	if (!service)
	{
		if (err == ERROR_SERVICE_DOES_NOT_EXIST ||
			err == ERROR_IO_PENDING) 
			// hello microsoft? anyone home?
		{
			BOX_ERROR("Failed to open Box Backup service: "
				"not installed or not found");
		}
		else
		{
			BOX_ERROR("Failed to open Box Backup service: " <<
				GetErrorMessage(err));
		}
		return 1;
	}

	SERVICE_STATUS status;
	if (!ControlService(service, SERVICE_CONTROL_STOP, &status))
	{
		err = GetLastError();
		if (err != ERROR_SERVICE_NOT_ACTIVE)
		{
			BOX_WARNING("Failed to stop Box Backup service: " <<
				GetErrorMessage(err));
		}
	}

	BOOL deleted = DeleteService(service);
	err = GetLastError();
	CloseServiceHandle(service);

	if (deleted)
	{
		BOX_INFO("Box Backup service deleted");
		return 0;
	}
	else if (err == ERROR_SERVICE_MARKED_FOR_DELETE)
	{
		BOX_ERROR("Failed to remove Box Backup service: "
			"it is already being deleted");
	}
	else
	{
		BOX_ERROR("Failed to remove Box Backup service: " <<
			GetErrorMessage(err));
	}

	return 1;
}

#endif // WIN32
