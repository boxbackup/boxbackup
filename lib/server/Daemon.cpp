// --------------------------------------------------------------------------
//
// File
//		Name:    Daemon.cpp
//		Purpose: Basic daemon functionality
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>

#ifdef HAVE_SYSLOG_H
	#include <syslog.h>
#endif

#include "Daemon.h"
#include "Configuration.h"
#include "ServerException.h"
#include "Guards.h"
#include "UnixUser.h"
#include "FileModificationTime.h"

#include "MemLeakFindOn.h"

Daemon *Daemon::spDaemon = 0;


// --------------------------------------------------------------------------
//
// Function
//		Name:    Daemon::Daemon()
//		Purpose: Constructor
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------
Daemon::Daemon()
	: mpConfiguration(0),
	  mReloadConfigWanted(false),
	  mTerminateWanted(false)
{
	if(spDaemon != 0)
	{
		THROW_EXCEPTION(ServerException, AlreadyDaemonConstructed)
	}
	spDaemon = this;
	
	// And in debug builds, we'll switch on assert failure logging to syslog
	ASSERT_FAILS_TO_SYSLOG_ON
	// And trace goes to syslog too
	TRACE_TO_SYSLOG(true)
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Daemon::~Daemon()
//		Purpose: Destructor
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------
Daemon::~Daemon()
{
	if(mpConfiguration)
	{
		delete mpConfiguration;
		mpConfiguration = 0;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Daemon::Main(const char *, int, const char *[])
//		Purpose: Starts the daemon off -- equivalent of C main() function
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------
int Daemon::Main(const char *DefaultConfigFile, int argc, const char *argv[])
{
	// Banner (optional)
	{
		const char *banner = DaemonBanner();
		if(banner != 0)
		{
			printf("%s", banner);
		}
	}

	std::string pidFileName;

	try
	{
		// Find filename of config file
		mConfigFileName = DefaultConfigFile;
		if(argc >= 2)
		{
			// First argument is config file, or it's -c and the next arg is the config file
			if(::strcmp(argv[1], "-c") == 0 && argc >= 3)
			{
				mConfigFileName = argv[2];
			}
			else
			{
				mConfigFileName = argv[1];
			}
		}
		
		// Test mode with no daemonisation?
		bool asDaemon = true;
		if(argc >= 3)
		{
			if(::strcmp(argv[2], "SINGLEPROCESS") == 0)
			{
				asDaemon = false;
			}
		}

		// Load the configuration file.
		std::string errors;
		std::auto_ptr<Configuration> pconfig = 
			Configuration::LoadAndVerify(
				mConfigFileName.c_str(), 
				GetConfigVerify(), errors);

		// Got errors?
		if(pconfig.get() == 0 || !errors.empty())
		{
			// Tell user about errors
			fprintf(stderr, "%s: Errors in config file %s:\n%s", 
				DaemonName(), mConfigFileName.c_str(), 
				errors.c_str());
			// And give up
			return 1;
		}
		
		// Store configuration
		mpConfiguration = pconfig.release();
		mLoadedConfigModifiedTime = GetConfigFileModifiedTime();
		
		// Server configuration
		const Configuration &serverConfig(mpConfiguration->GetSubConfiguration("Server"));
		
		// Let the derived class have a go at setting up stuff in the initial process
		SetupInInitialProcess();
		
#ifndef WIN32		
		// Set signal handler
		struct sigaction sa;
		sa.sa_handler = SignalHandler;
		sa.sa_flags = 0;
		sigemptyset(&sa.sa_mask);		// macro
		if(::sigaction(SIGHUP, &sa, NULL) != 0 || ::sigaction(SIGTERM, &sa, NULL) != 0)
		{
			THROW_EXCEPTION(ServerException, DaemoniseFailed)
		}
		
		// Open PID file for writing
		pidFileName = serverConfig.GetKeyValue("PidFile");
		FileHandleGuard<(O_WRONLY | O_CREAT | O_TRUNC), (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)> pidFile(pidFileName.c_str());
		
		// Handle changing to a different user
		if(serverConfig.KeyExists("User"))
		{
			// Config file specifies an user -- look up
			UnixUser daemonUser(serverConfig.GetKeyValue("User").c_str());
			
			// Change the owner on the PID file, so it can be deleted properly on termination
			if(::fchown(pidFile, daemonUser.GetUID(), daemonUser.GetGID()) != 0)
			{
				THROW_EXCEPTION(ServerException, CouldNotChangePIDFileOwner)
			}
			
			// Change the process ID
			daemonUser.ChangeProcessUser();
		}
	
		if(asDaemon)
		{
			// Let's go... Daemonise...
			switch(::fork())
			{
			case -1:
				// error
				THROW_EXCEPTION(ServerException, DaemoniseFailed)
				break;

			default:
				// parent
				_exit(0);
				return 0;
				break;

			case 0:
				// child
				break;
			}

			// In child

			// Set new session
			if(::setsid() == -1)
			{
				::syslog(LOG_ERR, "can't setsid");
				THROW_EXCEPTION(ServerException, DaemoniseFailed)
			}

			// Fork again...
			switch(::fork())
			{
			case -1:
				// error
				THROW_EXCEPTION(ServerException, DaemoniseFailed)
				break;

			default:
				// parent
				_exit(0);
				return 0;
				break;

			case 0:
				// child
				break;
			}
		}
#endif // ! WIN32

		// open the log
		::openlog(DaemonName(), LOG_PID, LOG_LOCAL6);
		// Log the start message
		::syslog(LOG_INFO, "Starting daemon (config: %s) (version " 
			BOX_VERSION ")", mConfigFileName.c_str());

#ifndef WIN32
		// Write PID to file
		char pid[32];
		int pidsize = sprintf(pid, "%d", (int)getpid());
		if(::write(pidFile, pid, pidsize) != pidsize)
		{
			::syslog(LOG_ERR, "can't write pid file");
			THROW_EXCEPTION(ServerException, DaemoniseFailed)
		}
#endif
		
		// Set up memory leak reporting
		#ifdef BOX_MEMORY_LEAK_TESTING
		{
			char filename[256];
			sprintf(filename, "%s.memleaks", DaemonName());
			memleakfinder_setup_exit_report(filename, DaemonName());
		}
		#endif // BOX_MEMORY_LEAK_TESTING
	
		if(asDaemon)
		{
#ifndef WIN32
			// Close standard streams
			::close(0);
			::close(1);
			::close(2);
			
			// Open and redirect them into /dev/null
			int devnull = ::open(PLATFORM_DEV_NULL, O_RDWR, 0);
			if(devnull == -1)
			{
				THROW_EXCEPTION(CommonException, OSFileError);
			}
			// Then duplicate them to all three handles
			if(devnull != 0) dup2(devnull, 0);
			if(devnull != 1) dup2(devnull, 1);
			if(devnull != 2) dup2(devnull, 2);
			// Close the original handle if it was opened above the std* range
			if(devnull > 2)
			{
				::close(devnull);
			}
#endif // ! WIN32

			// And definitely don't try and send anything to those file descriptors
			// -- this has in the past sent text to something which isn't expecting it.
			TRACE_TO_STDOUT(false);
		}		
	}
	catch(BoxException &e)
	{
		fprintf(stderr, "%s: exception %s (%d/%d)\n", DaemonName(), e.what(), e.GetType(), e.GetSubType());
		return 1;
	}
	catch(std::exception &e)
	{
		fprintf(stderr, "%s: exception %s\n", DaemonName(), e.what());
		return 1;
	}
	catch(...)
	{
		fprintf(stderr, "%s: unknown exception\n", DaemonName());
		return 1;
	}
	
	// Main Daemon running
	try
	{
		while(!mTerminateWanted)
		{
			Run();
			
			if(mReloadConfigWanted && !mTerminateWanted)
			{
				// Need to reload that config file...
				::syslog(LOG_INFO, "Reloading configuration "
					"(config: %s)", 
					mConfigFileName.c_str());
				std::string errors;
				std::auto_ptr<Configuration> pconfig = 
					Configuration::LoadAndVerify(
						mConfigFileName.c_str(),
						GetConfigVerify(), errors);

				// Got errors?
				if(pconfig.get() == 0 || !errors.empty())
				{
					// Tell user about errors
					::syslog(LOG_ERR, "Errors in config "
						"file %s:\n%s", 
						mConfigFileName.c_str(),
						errors.c_str());
					// And give up
					return 1;
				}
				
				// delete old configuration
				delete mpConfiguration;
				mpConfiguration = 0;

				// Store configuration
				mpConfiguration = pconfig.release();
				mLoadedConfigModifiedTime =
					GetConfigFileModifiedTime();
				
				// Stop being marked for loading config again
				mReloadConfigWanted = false;
			}
		}
		
		// Delete the PID file
		::unlink(pidFileName.c_str());
		
		// Log
		::syslog(LOG_INFO, "Terminating daemon");
	}
	catch(BoxException &e)
	{
		::syslog(LOG_ERR, "exception %s (%d/%d) -- terminating", e.what(), e.GetType(), e.GetSubType());
		return 1;
	}
	catch(std::exception &e)
	{
		::syslog(LOG_ERR, "exception %s -- terminating", e.what());
		return 1;
	}
	catch(...)
	{
		::syslog(LOG_ERR, "unknown exception -- terminating");
		return 1;
	}
	
	return 0;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Daemon::EnterChild()
//		Purpose: Sets up for a child task of the main server. Call just after fork()
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void Daemon::EnterChild()
{
#ifndef WIN32
	// Unset signal handlers
	struct sigaction sa;
	sa.sa_handler = SIG_DFL;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);			// macro
	::sigaction(SIGHUP, &sa, NULL);
	::sigaction(SIGTERM, &sa, NULL);
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Daemon::SignalHandler(int)
//		Purpose: Signal handler
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------
void Daemon::SignalHandler(int sigraised)
{
#ifndef WIN32
	if(spDaemon != 0)
	{
		switch(sigraised)
		{
		case SIGHUP:
			spDaemon->mReloadConfigWanted = true;
			break;
			
		case SIGTERM:
			spDaemon->mTerminateWanted = true;
			break;
		
		default:
			break;
		}
	}
#endif
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Daemon::DaemonName() 
//		Purpose: Returns name of the daemon
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------
const char *Daemon::DaemonName() const
{
	return "generic-daemon";
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Daemon::DaemonBanner()
//		Purpose: Returns the text banner for this daemon's startup
//		Created: 1/1/04
//
// --------------------------------------------------------------------------
const char *Daemon::DaemonBanner() const
{
	return 0;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Daemon::Run()
//		Purpose: Main run function after basic Daemon initialisation
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------
void Daemon::Run()
{
	while(!StopRun())
	{
		::sleep(10);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Daemon::GetConfigVerify()
//		Purpose: Returns the configuration file verification structure for this daemon
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------
const ConfigurationVerify *Daemon::GetConfigVerify() const
{
	static ConfigurationVerifyKey verifyserverkeys[] = 
	{
		DAEMON_VERIFY_SERVER_KEYS
	};

	static ConfigurationVerify verifyserver[] = 
	{
		{
			"Server",
			0,
			verifyserverkeys,
			ConfigTest_Exists | ConfigTest_LastEntry,
			0
		}
	};

	static ConfigurationVerify verify =
	{
		"root",
		verifyserver,
		0,
		ConfigTest_Exists | ConfigTest_LastEntry,
		0
	};

	return &verify;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Daemon::GetConfiguration()
//		Purpose: Returns the daemon configuration object
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------
const Configuration &Daemon::GetConfiguration() const
{
	if(mpConfiguration == 0)
	{
		// Shouldn't get anywhere near this if a configuration file can't be loaded
		THROW_EXCEPTION(ServerException, Internal)
	}
	
	return *mpConfiguration;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Daemon::SetupInInitialProcess()
//		Purpose: A chance for the daemon to do something initial setting up in the process which
//				 initiates everything, and after the configuration file has been read and verified.
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
void Daemon::SetupInInitialProcess()
{
	// Base class doesn't do anything.
}


void Daemon::SetProcessTitle(const char *format, ...)
{
	// On OpenBSD, setproctitle() sets the process title to imagename: <text> (imagename)
	// -- make sure other platforms include the image name somewhere so ps listings give
	// useful information.

#ifdef HAVE_SETPROCTITLE
	// optional arguments
	va_list args;
	va_start(args, format);

	// Make the string
	char title[256];
	::vsnprintf(title, sizeof(title), format, args);
	
	// Set process title
	::setproctitle("%s", title);
	
#endif // HAVE_SETPROCTITLE
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Daemon::GetConfigFileModifiedTime()
//		Purpose: Returns the timestamp when the configuration file
//			 was last modified
//
//		Created: 2006/01/29
//
// --------------------------------------------------------------------------

box_time_t Daemon::GetConfigFileModifiedTime() const
{
	struct stat st;

	if(::stat(GetConfigFileName().c_str(), &st) != 0)
	{
		if (errno == ENOENT)
		{
			return 0;
		}
		THROW_EXCEPTION(CommonException, OSFileError)
	}
	
	return FileModificationTime(st);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Daemon::GetLoadedConfigModifiedTime()
//		Purpose: Returns the timestamp when the configuration file
//			 had been last modified, at the time when it was 
//			 loaded
//
//		Created: 2006/01/29
//
// --------------------------------------------------------------------------

box_time_t Daemon::GetLoadedConfigModifiedTime() const
{
	return mLoadedConfigModifiedTime;
}

