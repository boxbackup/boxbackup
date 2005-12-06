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

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

extern void terminateService(void);
extern DWORD WINAPI runService(LPVOID lpParameter);

// Global variables

TCHAR* serviceName = TEXT("Beeper Service");
SERVICE_STATUS serviceStatus;
SERVICE_STATUS_HANDLE serviceStatusHandle = 0;
HANDLE stopServiceEvent = 0;

#define SERVICE_NAME "boxbackup"

void ErrorHandler(char *s, DWORD err)
{
	MessageBox(0, s, "Error", 
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
			terminateService();
			serviceStatus.dwCurrentState = SERVICE_STOP_PENDING;
			SetServiceStatus( serviceStatusHandle, &serviceStatus );

			SetEvent( stopServiceEvent );
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

	SetServiceStatus( serviceStatusHandle, &serviceStatus );
}

// ServiceMain is called when the SCM wants to
// start the service. When it returns, the service
// has stopped. It therefore waits on an event
// just before the end of the function, and
// that event gets set when it is time to stop. 
// It also returns on any error because the
// service cannot start if there is an eror.

VOID ServiceMain(DWORD argc, LPTSTR *argv) 
{
   // initialise service status
    serviceStatus.dwServiceType = SERVICE_WIN32;
    serviceStatus.dwCurrentState = SERVICE_STOPPED;
    serviceStatus.dwControlsAccepted = 0;
    serviceStatus.dwWin32ExitCode = NO_ERROR;
    serviceStatus.dwServiceSpecificExitCode = NO_ERROR;
    serviceStatus.dwCheckPoint = 0;
    serviceStatus.dwWaitHint = 0;

    serviceStatusHandle = RegisterServiceCtrlHandler( serviceName, ServiceControlHandler );

    if ( serviceStatusHandle )
    {
        // service is starting
        serviceStatus.dwCurrentState = SERVICE_START_PENDING;
        SetServiceStatus( serviceStatusHandle, &serviceStatus );

        // do initialisation here
        stopServiceEvent = CreateEvent( 0, TRUE, FALSE, 0 );
		if (!stopServiceEvent)
		{
			serviceStatus.dwControlsAccepted &= ~(SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
			serviceStatus.dwCurrentState = SERVICE_STOPPED;
			SetServiceStatus( serviceStatusHandle, &serviceStatus );
			return;
		}

		HANDLE ourThread = CreateThread(NULL,0,runService,0,CREATE_SUSPENDED ,NULL);
		SetThreadPriority(ourThread, THREAD_PRIORITY_LOWEST);
		ResumeThread(ourThread);

        // running we are now runnint so tell the SCM
        serviceStatus.dwControlsAccepted |= (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
        serviceStatus.dwCurrentState = SERVICE_RUNNING;
        SetServiceStatus( serviceStatusHandle, &serviceStatus );

		// do cleanup here
		WaitForSingleObject(stopServiceEvent, INFINITE);
        CloseHandle( stopServiceEvent );
        stopServiceEvent = 0;

        // service was stopped
        serviceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus( serviceStatusHandle, &serviceStatus );

        // service is now stopped
        serviceStatus.dwControlsAccepted &= ~(SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
        serviceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus( serviceStatusHandle, &serviceStatus );
    }
}

void ourService(void)
{
	SERVICE_TABLE_ENTRY serviceTable[] = 
	{ 
		{ SERVICE_NAME,(LPSERVICE_MAIN_FUNCTION) ServiceMain},{ NULL, NULL }
	};
	BOOL success;

	// Register with the SCM
	success = StartServiceCtrlDispatcher(serviceTable);

	if (!success)
	{
		ErrorHandler("In StartServiceCtrlDispatcher", GetLastError());
	}
}

void installService(void)
{
	SC_HANDLE newService, scm;

	scm = OpenSCManager(0,0,SC_MANAGER_CREATE_SERVICE);

	if (!scm) return;

	char buf[MAX_PATH];
	
	GetModuleFileName(NULL, buf, sizeof(buf));
	newService = CreateService(scm, SERVICE_NAME, "Box Backup", SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, buf, 0,0,0,0,0);

	if ( newService ) CloseServiceHandle(newService);
	CloseServiceHandle(scm);
}

void removeService(void)
{
	SC_HANDLE service, scm;
	SERVICE_STATUS status;

	scm = OpenSCManager(0,0,SC_MANAGER_CREATE_SERVICE);

	if (!scm) return;

	service = OpenService(scm, SERVICE_NAME, SERVICE_ALL_ACCESS|DELETE);
	ControlService(service, SERVICE_CONTROL_STOP, &status);

	if (!service)
	{
		printf("Failed to open service manager");
		return;
	}
	if (DeleteService(service))
	{
		printf("Service removed");
	}
	else
	{
		printf("Failed to remove service");
	}

	CloseServiceHandle(service);
	CloseServiceHandle(scm);
}

#endif // WIN32
