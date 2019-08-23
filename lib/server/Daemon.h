// --------------------------------------------------------------------------
//
// File
//		Name:    Daemon.h
//		Purpose: Basic daemon functionality
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------

/*  NOTE: will log to local6: include a line like
	local6.info                                             /var/log/box
	in /etc/syslog.conf
*/


#ifndef DAEMON__H
#define DAEMON__H

#include <string>

#include "BoxTime.h"
#include "Configuration.h"

class ConfigurationVerify;

// --------------------------------------------------------------------------
//
// Class
//		Name:    Daemon
//		Purpose: Basic daemon functionality
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------
class Daemon
{
public:
	Daemon();
	virtual ~Daemon();
private:
	Daemon(const Daemon &rToCopy);
public:
	virtual int Main(const std::string& rDefaultConfigFile, int argc,
		const char *argv[]);
	virtual int ProcessOptions(int argc, const char *argv[]);

	/* override this Main() if you want custom option processing: */
	virtual int Main(const std::string &rConfigFile);
	
	virtual void Run();
	const Configuration &GetConfiguration() const;
	const std::string &GetConfigFileName() const {return mConfigFileName;}

	virtual const char *DaemonName() const;
	virtual std::string DaemonBanner() const;
	virtual const ConfigurationVerify *GetConfigVerify() const;
	virtual void Usage();

	virtual bool Configure(const std::string& rConfigFileName);
	virtual bool Configure(const Configuration& rConfig);

	bool StopRun() {return mReloadConfigWanted | mTerminateWanted;}
	bool IsReloadConfigWanted() {return mReloadConfigWanted;}
	bool IsTerminateWanted() {return mTerminateWanted;}

	// To allow derived classes to get these signals in other ways
	void SetReloadConfigWanted() {mReloadConfigWanted = true;}
	void SetTerminateWanted() {mTerminateWanted = true;}
	
	virtual void EnterChild();
	
	static void SetProcessTitle(const char *format, ...);
	bool IsForkPerClient()
	{
		return mForkPerClient;
	}
	void SetForkPerClient(bool fork_per_client)
	{
		mForkPerClient = fork_per_client;
	}
	bool IsDaemonize()
	{
		return mDaemonize;
	}
	void SetDaemonize(bool daemonize)
	{
		mDaemonize = daemonize;
	}

protected:
	// Shouldn't really expose mapPidFile here, but need a way for bbstored to close it after
	// forking the housekeeping process:
	std::auto_ptr<FileStream> mapPidFile;

	virtual void SetupInInitialProcess();
	box_time_t GetLoadedConfigModifiedTime() const;
	virtual std::string GetOptionString();
	virtual int ProcessOption(signed int option);
	void ResetLogFile()
	{
		if(mapLogFileLogger.get())
		{
			mapLogFileLogger.reset(
				new FileLogger(mLogFile, mLogFileLevel,
					!mLogLevel.mTruncateLogFile));
		}
	}
	virtual void ServerIsReady()
	{
		// Normally we write the PID file at this point. Subclasses may wish to override
		// this to delay writing.
		WritePidFile();
	}

	// TODO: remove the unused wait_for_shared_lock argument
	void WritePidFile(bool wait_for_shared_lock = false);

private:
	static void SignalHandler(int sigraised);
	box_time_t GetConfigFileModifiedTime() const;
	
	std::string mConfigFileName;
	std::auto_ptr<Configuration> mapConfiguration;
	box_time_t mLoadedConfigModifiedTime;
	bool mReloadConfigWanted;
	bool mTerminateWanted;
	bool mDaemonize;
	bool mForkPerClient;
	bool mKeepConsoleOpenAfterFork;
	bool mHaveConfigFile;
	Logging::OptionParser mLogLevel;
	std::string mLogFile;
	Log::Level mLogFileLevel;
	std::auto_ptr<FileLogger> mapLogFileLogger;
	static Daemon *spDaemon;
	std::string mAppName;
	bool mPidFileWritten;
};

#define DAEMON_VERIFY_SERVER_KEYS \
	ConfigurationVerifyKey("PidFile", ConfigTest_Exists), \
	ConfigurationVerifyKey("LogFacility", 0), \
	ConfigurationVerifyKey("User", ConfigTest_LastEntry)

#endif // DAEMON__H

