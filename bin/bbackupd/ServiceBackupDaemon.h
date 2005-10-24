

#ifndef SERVICEBACKUPDAEMON__H
#define SERVICEBACKUPDAEMON__H

class Configuration;
class ConfigurationVerify;
class BackupDaemon;

class ServiceBackupDaemon : public BackupDaemon
{
public:
	DWORD WinService(void);
};


#endif