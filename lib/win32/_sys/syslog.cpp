#include "Box.h"

#include "messages.h"


using namespace Win32;


// copied from MSDN: http://msdn.microsoft.com/library/default.asp?url=/library/en-us/eventlog/base/adding_a_source_to_the_registry.asp

BOOL AddEventSource
(
	const char* pszSrcName, // event source name
	DWORD  dwNum       // number of categories
)
{
	// Work out the executable file name, to register ourselves
	// as the event source

	WCHAR cmd[MAX_PATH];
	DWORD len = GetModuleFileNameW(NULL, cmd, MAX_PATH);

	if (len == 0)
	{
		::syslog(LOG_ERR, "Failed to get the program file name: %s",
			GetErrorMessage(GetLastError()).c_str());
		return FALSE;
	}

	// Create the event source as a subkey of the log. 

	std::string regkey("SYSTEM\\CurrentControlSet\\Services\\EventLog\\"
		"Box Backup\\");
	regkey += pszSrcName; 
 
	HKEY hk;
	DWORD dwDisp;

	if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, regkey.c_str(), 
			 0, NULL, REG_OPTION_NON_VOLATILE,
			 KEY_WRITE, NULL, &hk, &dwDisp)) 
	{
		::syslog(LOG_ERR, "Failed to create the registry key: %s",
			GetErrorMessage(GetLastError()).c_str());
		return FALSE;
	}

	// Set the name of the message file. 
 
	if (RegSetValueExW(hk,                 // subkey handle 
			   L"EventMessageFile", // value name 
			   0,                  // must be zero 
			   REG_EXPAND_SZ,      // value type 
			   (LPBYTE)cmd,        // pointer to value data 
			   len*sizeof(WCHAR))) // data size
	{
		::syslog(LOG_ERR, "Failed to set the event message file: %s",
			GetErrorMessage(GetLastError()).c_str());
		RegCloseKey(hk); 
		return FALSE;
	}
 
	// Set the supported event types. 
 
	DWORD dwData = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | 
		  EVENTLOG_INFORMATION_TYPE; 
 
	if (RegSetValueEx(hk,               // subkey handle 
			  "TypesSupported", // value name 
			  0,                // must be zero 
			  REG_DWORD,        // value type 
			  (LPBYTE) &dwData, // pointer to value data 
			  sizeof(DWORD)))   // length of value data 
	{
		::syslog(LOG_ERR, "Failed to set the supported types: %s",
			GetErrorMessage(GetLastError()).c_str());
		RegCloseKey(hk); 
		return FALSE;
	}
 
	// Set the category message file and number of categories.

	if (RegSetValueExW(hk,                    // subkey handle 
			   L"CategoryMessageFile", // value name 
			   0,                     // must be zero 
			   REG_EXPAND_SZ,         // value type 
			   (LPBYTE)cmd,           // pointer to value data 
			   len*sizeof(WCHAR)))    // data size
	{
		::syslog(LOG_ERR, "Failed to set the category message file: "
			"%s", GetErrorMessage(GetLastError()).c_str());
		RegCloseKey(hk); 
		return FALSE;
	}
 
	if (RegSetValueEx(hk,              // subkey handle 
			  "CategoryCount", // value name 
			  0,               // must be zero 
			  REG_DWORD,       // value type 
			  (LPBYTE) &dwNum, // pointer to value data 
			  sizeof(DWORD)))  // length of value data 
	{
		::syslog(LOG_ERR, "Failed to set the category count: %s",
			GetErrorMessage(GetLastError()).c_str());
		RegCloseKey(hk); 
		return FALSE;
	}

	RegCloseKey(hk); 
	return TRUE;
}


static HANDLE gSyslogH = 0;
static bool sHaveWarnedEventLogFull = false;

void openlog(const char * daemonName, int, int)
{
	// register a default event source, so that we can
	// log errors with the process of adding or registering our own.
	gSyslogH = RegisterEventSource(
		NULL,        // uses local computer
		daemonName); // source name
	if (gSyslogH == NULL)
	{
	}

	BOOL success = AddEventSource(daemonName, 0);

	if (!success)
	{
		::syslog(LOG_ERR, "Failed to add our own event source");
		return;
	}

	HANDLE newSyslogH = RegisterEventSource(NULL, daemonName);
	if (newSyslogH == NULL)
	{
		::syslog(LOG_ERR, "Failed to register our own event source: "
			"%s", GetErrorMessage(GetLastError()).c_str());
		return;
	}

	DeregisterEventSource(gSyslogH);
	gSyslogH = newSyslogH;
}

void closelog(void)
{
	DeregisterEventSource(gSyslogH);
}

void syslog(int loglevel, const char *frmt, ...)
{
	WORD errinfo;
	char buffer[1024];
	std::string sixfour(frmt);

	switch (loglevel)
	{
	case LOG_INFO:
		errinfo = EVENTLOG_INFORMATION_TYPE;
		break;
	case LOG_ERR:
		errinfo = EVENTLOG_ERROR_TYPE;
		break;
	case LOG_WARNING:
	default:
		errinfo = EVENTLOG_WARNING_TYPE;
		break;
	}

	// taken from MSDN
	int sixfourpos;
	while ( (sixfourpos = (int)sixfour.find("%ll")) != -1 )
	{
		// maintain portability - change the 64 bit formater...
		std::string temp = sixfour.substr(0,sixfourpos);
		temp += "%I64";
		temp += sixfour.substr(sixfourpos+3, sixfour.length());
		sixfour = temp;
	}

	// printf("parsed string is:%s\r\n", sixfour.c_str());

	va_list args;
	va_start(args, frmt);

	int len = vsnprintf(buffer, sizeof(buffer)-1, sixfour.c_str(), args);
	if (len < 0)
	{
		printf("%s\r\n", buffer);
		fflush(stdout);
		return;
	}

	buffer[sizeof(buffer)-1] = 0;

	va_end(args);

	if (gSyslogH == 0)
	{
		printf("%s\r\n", buffer);
		fflush(stdout);
		return;
	}

	std::wstring wide = multi2wide(buffer);

	DWORD result;

/*	if (wide == NULL)
	{
		std::string buffer2 = buffer;
		buffer2 += " (failed to convert string encoding)";
		LPCSTR strings[] = { buffer2.c_str(), NULL };

		result = ReportEventA(gSyslogH, // event log handle
			errinfo,               // event type
			0,                     // category zero
			MSG_ERR,	       // event identifier -
					       // we will call them all the same
			NULL,                  // no user security identifier
			1,                     // one substitution string
			0,                     // no data
			strings,               // pointer to string array
			NULL);                 // pointer to data
	}
	else*/
	{
		LPCWSTR strings[] = { wide.c_str(), NULL };
		result = ReportEventW(gSyslogH, // event log handle
			errinfo,               // event type
			0,                     // category zero
			MSG_ERR,	       // event identifier -
					       // we will call them all the same
			NULL,                  // no user security identifier
			1,                     // one substitution string
			0,                     // no data
			strings,               // pointer to string array
			NULL);                 // pointer to data
	}

	if (result == 0)
	{
		DWORD err = GetLastError();
		if (err == ERROR_LOG_FILE_FULL)
		{
			if (!sHaveWarnedEventLogFull)
			{
				printf("Unable to send message to Event Log "
					"(Event Log is full):\r\n");
				fflush(stdout);
				sHaveWarnedEventLogFull = TRUE;
			}
		}
		else
		{
			printf("Unable to send message to Event Log: %s:\r\n",
				GetErrorMessage(err).c_str());
			fflush(stdout);
		}
	}
	else
	{
		sHaveWarnedEventLogFull = false;
	}
}
