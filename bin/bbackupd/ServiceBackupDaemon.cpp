
#include "Box.h"
#include "BackupDaemon.h"
#include "MainHelper.h"
#include "BoxPortsAndFiles.h"

#include "MemLeakFindOn.h"

#include "ServiceBackupDaemon.h"

ServiceBackupDaemon daemonService;
extern HANDLE stopServiceEvent;

DWORD WINAPI runService(LPVOID lpParameter)
{
	DWORD retVal = daemonService.WinService();
	SetEvent( stopServiceEvent );
	return retVal;
}
void terminateService(void)
{
	daemonService.SetTerminateWanted();
}

DWORD ServiceBackupDaemon::WinService(void)
{
	

	WSADATA info;
	//First off initialize sockets - which we have to do under Win32
    if (WSAStartup(MAKELONG(1, 1), &info) == SOCKET_ERROR) {
        //throw error?    perhaps give it its own id in the furture
        //THROW_EXCEPTION(BackupStoreException, Internal)
    }

	int argc = 2;
	//first off get the path name for the default 
	char buf[MAX_PATH];
	
	GetModuleFileName(NULL, buf, sizeof(buf));
	std::string buffer(buf);
	std::string conf( "-c");
	std::string cfile(buffer.substr(0,(buffer.find("bbackupd.exe"))) + "bbackupd.conf");

	const char *argv[] = {conf.c_str(), cfile.c_str()};

	MAINHELPER_START
	return this->Main(BOX_FILE_BBACKUPD_DEFAULT_CONFIG, argc, argv);

	//Clean up our sockets
    WSACleanup();

	MAINHELPER_END
}