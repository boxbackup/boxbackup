// --------------------------------------------------------------------------
//
// File
//		Name:    bbackupquery.cpp
//		Purpose: Backup query utility
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include <errno.h>
#include <stdio.h>

#ifdef HAVE_SYS_TYPES_H
	#include <sys/types.h>
#endif

#ifdef HAVE_LIBREADLINE
	#ifdef HAVE_READLINE_READLINE_H
		#include <readline/readline.h>
	#elif defined(HAVE_EDITLINE_READLINE_H)
		#include <editline/readline.h>
	#elif defined(HAVE_READLINE_H)
		#include <readline.h>
	#endif
#endif
#ifdef HAVE_READLINE_HISTORY
	#ifdef HAVE_READLINE_HISTORY_H
		#include <readline/history.h>
	#elif defined(HAVE_HISTORY_H)
		#include <history.h>
	#endif
#endif

#include <cstdlib>

#include "MainHelper.h"
#include "BoxPortsAndFiles.h"
#include "BackupDaemonConfigVerify.h"
#include "SocketStreamTLS.h"
#include "Socket.h"
#include "TLSContext.h"
#include "SSLLib.h"
#include "BackupStoreConstants.h"
#include "BackupStoreException.h"
#include "autogen_BackupProtocolClient.h"
#include "BackupQueries.h"
#include "FdGetLine.h"
#include "BackupClientCryptoKeys.h"
#include "BannerText.h"
#include "Logging.h"

#include "MemLeakFindOn.h"

void PrintUsageAndExit()
{
	printf("Usage: bbackupquery [-q] [-w] "
#ifdef WIN32
	"[-u] "
#endif
	"\n"
	"\t[-c config_file] [-o log_file] [-O log_file_level]\n"
	"\t[-l protocol_log_file] [commands]\n"
	"\n"
	"As many commands as you require.\n"
	"If commands are multiple words, remember to enclose the command in quotes.\n"
	"Remember to use the quit command unless you want to end up in interactive mode.\n");
	exit(1);
}

int main(int argc, const char *argv[])
{
	int returnCode = 0;

	MAINHELPER_SETUP_MEMORY_LEAK_EXIT_REPORT("bbackupquery.memleaks",
		"bbackupquery")
	MAINHELPER_START

#ifdef WIN32
	WSADATA info;
	
	// Under Win32 we must initialise the Winsock library
	// before using it.
	
	if (WSAStartup(0x0101, &info) == SOCKET_ERROR) 
	{
		// throw error? perhaps give it its own id in the future
		THROW_EXCEPTION(BackupStoreException, Internal)
	}
#endif

	// Really don't want trace statements happening, even in debug mode
	#ifndef BOX_RELEASE_BUILD
		BoxDebugTraceOn = false;
	#endif
	
	FILE *logFile = 0;

	// Filename for configuration file?
	std::string configFilename;

	#ifdef WIN32
		configFilename = BOX_GET_DEFAULT_BBACKUPD_CONFIG_FILE;
	#else
		configFilename = BOX_FILE_BBACKUPD_DEFAULT_CONFIG;
	#endif
	
	// Flags
	bool readWrite = false;

	Logging::SetProgramName("bbackupquery");

	#ifdef BOX_RELEASE_BUILD
	int masterLevel = Log::NOTICE; // need an int to do math with
	#else
	int masterLevel = Log::INFO; // need an int to do math with
	#endif

#ifdef WIN32
	const char* validOpts = "qvwuc:l:o:O:W:";
	bool unicodeConsole = false;
#else
	const char* validOpts = "qvwc:l:o:O:W:";
#endif

	std::string fileLogFile;
	Log::Level fileLogLevel = Log::INVALID;

	// See if there's another entry on the command line
	int c;
	while((c = getopt(argc, (char * const *)argv, validOpts)) != -1)
	{
		switch(c)
		{
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

		case 'W':
			{
				masterLevel = Logging::GetNamedLevel(optarg);
				if (masterLevel == Log::INVALID)
				{
					BOX_FATAL("Invalid logging level");
					return 2;
				}
			}
			break;

		case 'w':
			// Read/write mode
			readWrite = true;
			break;
		
		case 'c':
			// store argument
			configFilename = optarg;
			break;
		
		case 'l':
			// open log file
			logFile = ::fopen(optarg, "w");
			if(logFile == 0)
			{
				BOX_LOG_SYS_ERROR("Failed to open log file "
					"'" << optarg << "'");
			}
			break;

		case 'o':
			fileLogFile = optarg;
			fileLogLevel = Log::EVERYTHING;
			break;

		case 'O':
			{
				fileLogLevel = Logging::GetNamedLevel(optarg);
				if (fileLogLevel == Log::INVALID)
				{
					BOX_FATAL("Invalid logging level");
					return 2;
				}
			}
			break;

#ifdef WIN32
		case 'u':
			unicodeConsole = true;
			break;
#endif
		
		case '?':
		default:
			PrintUsageAndExit();
		}
	}
	// Adjust arguments
	argc -= optind;
	argv += optind;
	
	Logging::SetGlobalLevel((Log::Level)masterLevel);

	std::auto_ptr<FileLogger> fileLogger;
	if (fileLogLevel != Log::INVALID)
	{
		fileLogger.reset(new FileLogger(fileLogFile, fileLogLevel));
	}

	bool quiet = false;
	if (masterLevel < Log::NOTICE)
	{
		// Quiet mode
		quiet = true;
	}

	// Print banner?
	if(!quiet)
	{
		const char *banner = BANNER_TEXT("Backup Query Tool");
		BOX_NOTICE(banner);
	}

#ifdef WIN32
	if (unicodeConsole)
	{
		if (!SetConsoleCP(CP_UTF8))
		{
			BOX_ERROR("Failed to set input codepage: " <<
				GetErrorMessage(GetLastError()));
		}

		if (!SetConsoleOutputCP(CP_UTF8))
		{
			BOX_ERROR("Failed to set output codepage: " <<
				GetErrorMessage(GetLastError()));
		}

		// enable input of Unicode characters
		if (_fileno(stdin) != -1 &&
			_setmode(_fileno(stdin), _O_TEXT) == -1)
		{
			perror("Failed to set the console input to "
				"binary mode");
		}
	}
#endif // WIN32

	// Read in the configuration file
	if(!quiet) BOX_INFO("Using configuration file " << configFilename);

	std::string errs;
	std::auto_ptr<Configuration> config(
		Configuration::LoadAndVerify
			(configFilename, &BackupDaemonConfigVerify, errs));

	if(config.get() == 0 || !errs.empty())
	{
		BOX_FATAL("Invalid configuration file: " << errs);
		return 1;
	}
	// Easier coding
	const Configuration &conf(*config);
	
	// Setup and connect
	// 1. TLS context
	SSLLib::Initialise();
	// Read in the certificates creating a TLS context
	TLSContext tlsContext;
	std::string certFile(conf.GetKeyValue("CertificateFile"));
	std::string keyFile(conf.GetKeyValue("PrivateKeyFile"));
	std::string caFile(conf.GetKeyValue("TrustedCAsFile"));
	tlsContext.Initialise(false /* as client */, certFile.c_str(), keyFile.c_str(), caFile.c_str());
	
	// Initialise keys
	BackupClientCryptoKeys_Setup(conf.GetKeyValue("KeysFile").c_str());

	// 2. Connect to server
	if(!quiet) BOX_INFO("Connecting to store...");
	SocketStreamTLS socket;
	socket.Open(tlsContext, Socket::TypeINET,
		conf.GetKeyValue("StoreHostname").c_str(),
		conf.GetKeyValueInt("StorePort"));
	
	// 3. Make a protocol, and handshake
	if(!quiet) BOX_INFO("Handshake with store...");
	BackupProtocolClient connection(socket);
	connection.Handshake();
	
	// logging?
	if(logFile != 0)
	{
		connection.SetLogToFile(logFile);
	}
	
	// 4. Log in to server
	if(!quiet) BOX_INFO("Login to store...");
	// Check the version of the server
	{
		std::auto_ptr<BackupProtocolClientVersion> serverVersion(connection.QueryVersion(BACKUP_STORE_SERVER_VERSION));
		if(serverVersion->GetVersion() != BACKUP_STORE_SERVER_VERSION)
		{
			THROW_EXCEPTION(BackupStoreException, WrongServerVersion)
		}
	}
	// Login -- if this fails, the Protocol will exception
	connection.QueryLogin(conf.GetKeyValueInt("AccountNumber"),
		(readWrite)?0:(BackupProtocolClientLogin::Flags_ReadOnly));

	// 5. Tell user.
	if(!quiet) printf("Login complete.\n\nType \"help\" for a list of commands.\n\n");
	
	// Set up a context for our work
	BackupQueries context(connection, conf, readWrite);
	
	// Start running commands... first from the command line
	{
		int c = 0;
		while(c < argc && !context.Stop())
		{
			context.DoCommand(argv[c++], true);
		}
	}
	
	// Get commands from input

#ifdef HAVE_LIBREADLINE
	// Must initialise the locale before using editline's readline(),
	// otherwise cannot enter international characters.
	if (setlocale(LC_ALL, "") == NULL)
	{
		BOX_ERROR("Failed to initialise locale. International "
			"character support may not work.");
	}

#ifdef HAVE_READLINE_HISTORY
	using_history();
#endif
	char *last_cmd = 0;
	while(!context.Stop())
	{
		char *command = readline("query > ");
		if(command == NULL)
		{
			// Ctrl-D pressed -- terminate now
			break;
		}
		context.DoCommand(command, false);
		if(last_cmd != 0 && ::strcmp(last_cmd, command) == 0)
		{
			free(command);
		}
		else
		{
#ifdef HAVE_READLINE_HISTORY
			add_history(command);
#else
			free(last_cmd);
#endif
			last_cmd = command;
		}
	}
#ifndef HAVE_READLINE_HISTORY
	free(last_cmd);
	last_cmd = 0;
#endif
#else
	// Version for platforms which don't have readline by default
	if(fileno(stdin) >= 0)
	{
		FdGetLine getLine(fileno(stdin));
		while(!context.Stop())
		{
			printf("query > ");
			fflush(stdout);
			std::string command(getLine.GetLine());
			context.DoCommand(command.c_str(), false);
		}
	}
#endif
	
	// Done... stop nicely
	if(!quiet) BOX_INFO("Logging off...");
	connection.QueryFinished();
	if(!quiet) BOX_INFO("Session finished.");
	
	// Return code
	returnCode = context.GetReturnCode();
	
	// Close log file?
	if(logFile)
	{
		::fclose(logFile);
	}
	
	// Let everything be cleaned up on exit.
	
#ifdef WIN32
	// Clean up our sockets
	// FIXME we should do this, but I get an abort() when I try
	// WSACleanup();
#endif

	MAINHELPER_END
	
	return returnCode;
}

