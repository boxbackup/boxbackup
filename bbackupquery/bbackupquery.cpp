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
#include <cstdio>
#include <cstdlib>

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
	std::ostringstream out;
	out << 
		"Usage: bbackupquery [options] [command]...\n"
		"\n"
		"Options:\n"
		"  -q         Run more quietly, reduce verbosity level by one, can repeat\n"
		"  -Q         Run at minimum verbosity, log nothing\n"
		"  -v         Run more verbosely, increase verbosity level by one, can repeat\n"
		"  -V         Run at maximum verbosity, log everything\n"
		"  -W <level> Set verbosity to error/warning/notice/info/trace/everything\n"
		"  -w         Read/write mode, allow changes to store\n"
#ifdef WIN32
		"  -u         Enable Unicode console, requires font change to Lucida Console\n"
#else // !WIN32
		"  -E         Disable interactive command editing, may fix entering intl chars\n"
#endif
		"  -c <file>  Use the specified configuration file. If -c is omitted, the last\n"
		"             argument is the configuration file, or else the default \n"
		"             [" << BOX_GET_DEFAULT_BBACKUPD_CONFIG_FILE << 
		"]\n"
		"  -o <file>  Write logging output to specified file as well as console\n"
		"  -O <level> Set file verbosity to error/warning/notice/info/trace/everything\n"
		"  -l <file>  Write protocol debugging logs to specified file\n"
		"\n"
		"Parameters: as many commands as you like. If commands are multiple words,\n"
		"remember to enclose the command in quotes. Remember to use the quit command\n"
		"unless you want to end up in interactive mode.\n";
	printf("%s", out.str().c_str());
	exit(1);
}

#ifdef HAVE_LIBREADLINE
// copied from: http://tiswww.case.edu/php/chet/readline/readline.html#SEC44

char * command_generator(const char *text, int state)
{
	static int list_index, len;
	const char *name;

	/* 
	 * If this is a new word to complete, initialize now.  This includes
	 * saving the length of TEXT for efficiency, and initializing the index
	 * variable to 0.
	 */
	if(!state)
	{
		list_index = 0;
		len = strlen(text);
	}

	/* Return the next name which partially matches from the command list. */
	while((name = commands[list_index].name))
	{
		list_index++;

		if(::strncmp(name, text, len) == 0 && !(state--))
		{
			return ::strdup(name);
		}
	}

	list_index = 0;

	while((name = alias[list_index]))
	{
		list_index++;

		if(::strncmp(name, text, len) == 0 && !(state--))
		{
			return ::strdup(name);
		}
	}

	/* If no names matched, then return NULL. */
	return (char *) NULL;
}

#ifdef HAVE_RL_COMPLETION_MATCHES
	#define RL_COMPLETION_MATCHES rl_completion_matches
#elif defined HAVE_COMPLETION_MATCHES
	#define RL_COMPLETION_MATCHES completion_matches
#endif

char ** bbackupquery_completion(const char *text, int start, int end)
{
	char **matches;

	matches = (char **)NULL;

	/* If this word is at the start of the line, then it is a command
	 * to complete.  Otherwise it is the name of a file in the current
	 * directory.
	 */
	#ifdef RL_COMPLETION_MATCHES
	if (start == 0)
	{
		matches = RL_COMPLETION_MATCHES(text, command_generator);
	}
	#endif

	return matches;
}

#endif // HAVE_LIBREADLINE

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
	std::string configFilename = BOX_GET_DEFAULT_BBACKUPD_CONFIG_FILE;
	
	// Flags
	bool readWrite = false;

	Logging::SetProgramName("bbackupquery");

	#ifdef BOX_RELEASE_BUILD
	int masterLevel = Log::NOTICE; // need an int to do math with
	#else
	int masterLevel = Log::INFO; // need an int to do math with
	#endif

#ifdef WIN32
	const char* validOpts = "qvVwuc:l:o:O:W:";
	bool unicodeConsole = false;
#elif defined HAVE_LIBREADLINE // && !WIN32
	const char* validOpts = "qvVwEc:l:o:O:W:";
	bool useReadline = true;
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

		case 'V':
			{
				masterLevel = Log::EVERYTHING;
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
#elif defined HAVE_LIBREADLINE // && !WIN32
		case 'E':
			useReadline = false;
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
	std::auto_ptr<BackupProtocolClient>
		apConnection(new BackupProtocolClient(socket));
	BackupProtocolClient& connection(*(apConnection.get()));
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
	connection.QueryLogin(conf.GetKeyValueUint32("AccountNumber"),
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
			BackupQueries::ParsedCommand cmd(
				context.ParseCommand(argv[c++], true));
			context.DoCommand(cmd);
		}
	}
	
	// Get commands from input

#ifdef HAVE_LIBREADLINE
	if (useReadline)
	{
#else
	if (false)
	{
#endif
		// Must initialise the locale before using editline's
		// readline(), otherwise cannot enter international characters.
		if (setlocale(LC_ALL, "") == NULL)
		{
			BOX_ERROR("Failed to initialise locale. International "
				"character support may not work.");
		}

		#ifdef HAVE_READLINE_HISTORY
			using_history();
		#endif

		/* Allow conditional parsing of the ~/.inputrc file. */
		rl_readline_name = strdup("bbackupquery");

		/* Tell the completer that we want a crack first. */
		rl_attempted_completion_function = bbackupquery_completion;
		
		char *last_cmd = 0;
		while(!context.Stop())
		{
			char *command = readline("query > ");
			
			if(command == NULL)
			{
				// Ctrl-D pressed -- terminate now
				break;
			}
			
			BackupQueries::ParsedCommand cmd(
				context.ParseCommand(command, false));
			context.DoCommand(cmd);
			
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
	}
	else // !HAVE_LIBREADLINE || !useReadline
	{
		// Version for platforms which don't have readline by default
		if(fileno(stdin) >= 0)
		{
			FdGetLine getLine(fileno(stdin));
			while(!context.Stop())
			{
				printf("query > ");
				fflush(stdout);
				std::string command(getLine.GetLine());
				BackupQueries::ParsedCommand cmd(
					context.ParseCommand(command, false));
				context.DoCommand(cmd);
			}
		}
	}
	
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

