// Box Backup service daemon implementation by Nick Knight

#ifndef WIN32BACKUPSERVICE_H
#define WIN32BACKUPSERVICE_H

#ifdef WIN32

class Configuration;
class ConfigurationVerify;
class BackupDaemon;

class Win32BackupService : public BackupDaemon
{
public:
	DWORD WinService(void);
};

#endif // WIN32

#endif // WIN32BACKUPSERVICE_H

