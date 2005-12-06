// Box Backup service daemon implementation by Nick Knight

#ifndef SERVICEBACKUPDAEMON__H
#define SERVICEBACKUPDAEMON__H

#ifdef WIN32

class Configuration;
class ConfigurationVerify;
class BackupDaemon;

class ServiceBackupDaemon : public BackupDaemon
{
public:
	DWORD WinService(void);
};

#endif // WIN32

#endif // SERVICEBACKUPDAEMON__H

