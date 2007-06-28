// --------------------------------------------------------------------------
//
// File
//		Name:    Daemon.cpp
//		Purpose: Basic daemon functionality
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>

#ifdef HAVE_SYSLOG_H
	#include <syslog.h>
#endif

#ifdef WIN32
	#include <ws2tcpip.h>
#endif

#include "Daemon.h"
#include "Configuration.h"
#include "ServerException.h"
#include "Guards.h"
#include "UnixUser.h"
#include "FileModificationTime.h"
#include "Logging.h"

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
	: mpConfiguration(NULL),
	  mReloadConfigWanted(false),
	  mTerminateWanted(false),
	  mSingleProcess(false),
	  mRunInForeground(false),
	  mKeepConsoleOpenAfterFork(false)
{
	if(spDaemon != NULL)
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

	ASSERT(spDaemon == this);
	spDaemon = NULL;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Daemon::Main(const char *, int, const char *[])
//		Purpose: Parses command-line options, and then calls
//			Main(std::string& configFile, bool singleProcess)
//			to start the daemon.
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------
int Daemon::Main(const char *DefaultConfigFile, int argc, const char *argv[])
{
	// Find filename of config file
	mConfigFileName = DefaultConfigFile;
	bool haveConfigFile = false;

	#ifdef NDEBUG
	int masterLevel = Log::NOTICE; // need an int to do math with
	#else
	int masterLevel = Log::INFO; // need an int to do math with
	#endif

	char c;

	// reset getopt, just in case anybody used it before.
	// unfortunately glibc and BSD differ on this point!
	// http://www.ussg.iu.edu/hypermail/linux/kernel/0305.3/0262.html
	#ifdef __GLIBC__
		optind = 0;
	#else
		optind = 1;
		optreset = 1;
	#endif

	while((c = getopt(argc, (char * const *)argv, "c:DFqvVt:Tk")) != -1)
	{
		switch(c)
		{
			case 'c':
			{
				mConfigFileName = optarg;
				haveConfigFile = true;
			}
			break;

			case 'D':
			{
				mSingleProcess = true;
			}
			break;

			case 'F':
			{
				mRunInForeground = true;
			}
			break;

			case 'q':
			{
				if(masterLevel == Log::NOTHING)
				{
					BOX_FATAL("Too many '-q': "
						"Cannot reduce logging "
						"level any more");
					return 2;
				}
				masterLevel--;
			}
			break;

			case 'v':
			{
				if(masterLevel == Log::EVERYTHING)
				{
					BOX_FATAL("Too many '-v': "
						"Cannot increase logging "
						"level any more");
					return 2;
				}
				masterLevel++;
			}
			break;

			case 'V':
			{
				masterLevel = Log::EVERYTHING;
			}
			break;

			case 't':
			{
				Console::SetTag(optarg);
			}
			break;

			case 'T':
			{
				Console::SetShowTime(true);
			}
			break;

			case 'k':
			{
				mKeepConsoleOpenAfterFork = true;
			}
			break;

			case '?':
			{
				BOX_FATAL("Unknown option on command line: " 
					<< "'" << (char)optopt << "'");
				return 2;
			}
			break;

			default:
			{
				BOX_FATAL("Unknown error in getopt: returned "
					<< "'" << c << "'");
				return 1;
			}
		}
	}

	if (argc > optind && !haveConfigFile)
	{
		mConfigFileName = argv[optind]; optind++;
	}

	if (argc > optind && ::strcmp(argv[optind], "SINGLEPROCESS") == 0)
	{
		mSingleProcess = true; optind++;
	}

	if (argc > optind)
	{
		BOX_FATAL("Unknown parameter on command line: "
			<< "'" << std::string(argv[optind]) << "'");
		return 2;
	}

	Logging::SetGlobalLevel((Log::Level)masterLevel);

	return Main(mConfigFileName);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Daemon::Main(const std::string& rConfigFileName)
//		Purpose: Starts the daemon off -- equivalent of C main() function
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------
int Daemon::Main(const std::string &rConfigFileName)
{
	// Banner (optional)
	{
		const char *banner = DaemonBanner();
		if(banner != 0)
		{
			BOX_NOTICE(banner);
		}
	}

	std::string pidFileName;

	mConfigFileName = rConfigFileName;
	
	bool asDaemon   = !mSingleProcess && !mRunInForeground;

	try
	{
		// Load the configuration file.
		std::string errors;
		std::auto_ptr<Configuration> pconfig;

		try
		{
			pconfig = Configuration::LoadAndVerify(
				mConfigFileName.c_str(), 
				GetConfigVerify(), errors);
		}
		catch(BoxException &e)
		{
			if(e.GetType() == CommonException::ExceptionType &&
				e.GetSubType() == CommonException::OSFileOpenError)
			{
				BOX_FATAL("Failed to start: failed to open "
					"configuration file: " 
					<< mConfigFileName);
				return 1;
			}

			throw;
		}

		// Got errors?
		if(pconfig.get() == 0 || !errors.empty())
		{
			// Tell user about errors
			BOX_FATAL("Failed to start: errors in configuration "
				"file: " << mConfigFileName << ": " << errors);
			// And give up
			return 1;
		}
		
		// Store configuration
		mpConfiguration = pconfig.release();
		mLoadedConfigModifiedTime = GetConfigFileModifiedTime();
		
		// Let the derived class have a go at setting up stuff in the initial process
		SetupInInitialProcess();
		
		// Server configuration
		const Configuration &serverConfig(
			mpConfiguration->GetSubConfiguration("Server"));

		// Open PID file for writing
		pidFileName = serverConfig.GetKeyValue("PidFile");
		FileHandleGuard<(O_WRONLY | O_CREAT | O_TRUNC), (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)> pidFile(pidFileName.c_str());
	
#ifndef WIN32	
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
				// _exit(0);
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

		// Set signal handler
		// Don't do this in the parent, since it might be anything
		// (e.g. test/bbackupd)
		
		struct sigaction sa;
		sa.sa_handler = SignalHandler;
		sa.sa_flags = 0;
		sigemptyset(&sa.sa_mask);		// macro
		if(::sigaction(SIGHUP, &sa, NULL) != 0 || ::sigaction(SIGTERM, &sa, NULL) != 0)
		{
			THROW_EXCEPTION(ServerException, DaemoniseFailed)
		}
#endif // !WIN32

		// Log the start message
		BOX_NOTICE("Starting daemon, version " << BOX_VERSION
			<< ", config: " << mConfigFileName);

		// Write PID to file
		char pid[32];

#ifdef WIN32
		int pidsize = sprintf(pid, "%d", (int)GetCurrentProcessId());
#else
		int pidsize = sprintf(pid, "%d", (int)getpid());
#endif

		if(::write(pidFile, pid, pidsize) != pidsize)
		{
			BOX_FATAL("can't write pid file");
			THROW_EXCEPTION(ServerException, DaemoniseFailed)
		}
		
		// Set up memory leak reporting
		#ifdef BOX_MEMORY_LEAK_TESTING
		{
			char filename[256];
			sprintf(filename, "%s.memleaks", DaemonName());
			memleakfinder_setup_exit_report(filename, DaemonName());
		}
		#endif // BOX_MEMORY_LEAK_TESTING
	
		if(asDaemon && !mKeepConsoleOpenAfterFork)
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
			Logging::ToConsole(false);
		}		
	}
	catch(BoxException &e)
	{
		BOX_FATAL("Failed to start: exception " << e.what() 
			<< " (" << e.GetType() 
			<< "/"  << e.GetSubType() << ")");
		return 1;
	}
	catch(std::exception &e)
	{
		BOX_FATAL("Failed to start: exception " << e.what());
		return 1;
	}
	catch(...)
	{
		BOX_FATAL("Failed to start: unknown error");
		return 1;
	}

#ifdef WIN32
	// Under win32 we must initialise the Winsock library
	// before using sockets

	WSADATA info;

	if (WSAStartup(0x0101, &info) == SOCKET_ERROR)
	{
		// will not run without sockets
		BOX_FATAL("Failed to initialise Windows Sockets");
		THROW_EXCEPTION(CommonException, Internal)
	}
#endif

	int retcode = 0;
	
	// Main Daemon running
	try
	{
		while(!mTerminateWanted)
		{
			Run();
			
			if(mReloadConfigWanted && !mTerminateWanted)
			{
				// Need to reload that config file...
				BOX_NOTICE("Reloading configuration file: "
					<< mConfigFileName);
				std::string errors;
				std::auto_ptr<Configuration> pconfig = 
					Configuration::LoadAndVerify(
						mConfigFileName.c_str(),
						GetConfigVerify(), errors);

				// Got errors?
				if(pconfig.get() == 0 || !errors.empty())
				{
					// Tell user about errors
					BOX_FATAL("Error in configuration "
						<< "file: " << mConfigFileName
						<< ": " << errors);
					// And give up
					retcode = 1;
					break;
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
		BOX_NOTICE("Terminating daemon");
	}
	catch(BoxException &e)
	{
		BOX_FATAL("Terminating due to exception " << e.what() 
			<< " (" << e.GetType() 
			<< "/"  << e.GetSubType() << ")");
		retcode = 1;
	}
	catch(std::exception &e)
	{
		BOX_FATAL("Terminating due to exception " << e.what());
		retcode = 1;
	}
	catch(...)
	{
		BOX_FATAL("Terminating due to unknown exception");
		retcode = 1;
	}

#ifdef WIN32
	WSACleanup();
#endif
	
	return retcode;
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

