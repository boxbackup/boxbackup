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
#include "autogen_BackupProtocol.h"
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
		"  -w         Read/write mode, allow changes to store\n"
#ifdef WIN32
		"  -u         Enable Unicode console, requires font change to Lucida Console\n"
#endif
#ifdef HAVE_LIBREADLINE
		"  -E         Disable interactive command editing, may fix entering intl chars\n"
#endif
		"  -c <file>  Use the specified configuration file. If -c is omitted, the last\n"
		"             argument is the configuration file, or else the default \n"
		"             [" << BOX_GET_DEFAULT_BBACKUPD_CONFIG_FILE << "]\n"
		"  -o <file>  Write logging output to specified file as well as console\n"
		"  -O <level> Set file verbosity to error/warning/notice/info/trace/everything\n"
		"  -l <file>  Write protocol debugging logs to specified file\n"
		<<
		Logging::OptionParser::GetUsageString()
		<<
		"\n"
		"Parameters: as many commands as you like. If commands are multiple words,\n"
		"remember to enclose the command in quotes. Remember to use the quit command\n"
		"unless you want to end up in interactive mode.\n";
	printf("%s", out.str().c_str());
	exit(1);
}

#ifdef HAVE_LIBREADLINE
static BackupProtocolClient* pProtocol;
static const Configuration* pConfig;
static BackupQueries* pQueries;
static std::vector<std::string> completions;
static std::auto_ptr<BackupQueries::ParsedCommand> sapCmd;

char * completion_generator(const char *text, int state)
{
	if(state == 0)
	{
		completions.clear();

		std::string partialCommand(rl_line_buffer, rl_point);
		sapCmd.reset(new BackupQueries::ParsedCommand(partialCommand,
			false));
		int currentArg = sapCmd->mCompleteArgCount;

		if(currentArg == 0) // incomplete command
		{
			completions = CompleteCommand(*sapCmd, text, *pProtocol,
				*pConfig, *pQueries);
		}
		else if(sapCmd->mInOptions)
		{
			completions = CompleteOptions(*sapCmd, text, *pProtocol,
				*pConfig, *pQueries);
		}
		else if(currentArg - 1 < MAX_COMPLETION_HANDLERS)
		// currentArg must be at least 1 if we're here
		{
			CompletionHandler handler =
				sapCmd->pSpec->complete[currentArg - 1];

			if(handler != NULL)
			{
				completions = handler(*sapCmd, text, *pProtocol,
					*pConfig, *pQueries);
			}

			if(std::string(text) == "")
			{
				// additional options are also allowed here
				std::vector<std::string> addOpts =
					CompleteOptions(*sapCmd, text,
						*pProtocol, *pConfig,
						*pQueries);

				for(std::vector<std::string>::iterator
					i =  addOpts.begin();
					i != addOpts.end(); i++)
				{
					completions.push_back(*i);
				}
			}
		}
	}

	if(state < 0 || state >= (int) completions.size())
	{
		rl_attempted_completion_over = 1;
		sapCmd.reset();
		return NULL;
	}

	return strdup(completions[state].c_str());
	// string must be allocated with malloc() and will be freed
	// by rl_completion_matches().
}

#ifdef HAVE_RL_COMPLETION_MATCHES
	#define RL_COMPLETION_MATCHES rl_completion_matches
#elif defined HAVE_COMPLETION_MATCHES
	#define RL_COMPLETION_MATCHES completion_matches
#else
	char* no_matches[] = {NULL};
	char** bbackupquery_completion_dummy(const char *text, 
		char * (completion_generator)(const char *text, int state))
	{
		return no_matches;
	}
	#define RL_COMPLETION_MATCHES bbackupquery_completion_dummy
#endif

char ** bbackupquery_completion(const char *text, int start, int end)
{
	return RL_COMPLETION_MATCHES(text, completion_generator);
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

#ifdef WIN32
	#define WIN32_OPTIONS "u"
	bool unicodeConsole = false;
#else
	#define WIN32_OPTIONS
#endif

#ifdef HAVE_LIBREADLINE
	#define READLINE_OPTIONS "E"
	bool useReadline = true;
#else
	#define READLINE_OPTIONS
#endif

	std::string options("wc:l:o:O:" WIN32_OPTIONS READLINE_OPTIONS);
	options += Logging::OptionParser::GetOptionString();
	Logging::OptionParser LogLevel;

	std::string fileLogFile;
	Log::Level fileLogLevel = Log::INVALID;

	// See if there's another entry on the command line
	int c;
	while((c = getopt(argc, (char * const *)argv, options.c_str())) != -1)
	{
		switch(c)
		{
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

#ifdef HAVE_LIBREADLINE
		case 'E':
			useReadline = false;
			break;
#endif
		
		default:
			int ret = LogLevel.ProcessOption(c);
			if (ret != 0)
			{
				PrintUsageAndExit();
			}
		}
	}
	// Adjust arguments
	argc -= optind;
	argv += optind;
	
	Logging::GetConsole().Filter(LogLevel.GetCurrentLevel());

	std::auto_ptr<FileLogger> fileLogger;
	if (fileLogLevel != Log::INVALID)
	{
		fileLogger.reset(new FileLogger(fileLogFile, fileLogLevel));
	}

	BOX_NOTICE(BANNER_TEXT("Backup Query Tool"));

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
	BOX_INFO("Using configuration file " << configFilename);

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
	BOX_INFO("Connecting to store...");
	SocketStreamTLS *socket = new SocketStreamTLS;
	std::auto_ptr<SocketStream> apSocket(socket);
	socket->Open(tlsContext, Socket::TypeINET,
		conf.GetKeyValue("StoreHostname").c_str(),
		conf.GetKeyValueInt("StorePort"));
	
	// 3. Make a protocol, and handshake
	BOX_INFO("Handshake with store...");
	std::auto_ptr<BackupProtocolClient>
		apConnection(new BackupProtocolClient(apSocket));
	BackupProtocolClient& connection(*(apConnection.get()));
	connection.Handshake();
	
	// logging?
	if(logFile != 0)
	{
		connection.SetLogToFile(logFile);
	}
	
	// 4. Log in to server
	BOX_INFO("Login to store...");
	// Check the version of the server
	{
		std::auto_ptr<BackupProtocolVersion> serverVersion(connection.QueryVersion(BACKUP_STORE_SERVER_VERSION));
		if(serverVersion->GetVersion() != BACKUP_STORE_SERVER_VERSION)
		{
			THROW_EXCEPTION(BackupStoreException, WrongServerVersion)
		}
	}
	// Login -- if this fails, the Protocol will exception
	connection.QueryLogin(conf.GetKeyValueUint32("AccountNumber"),
		(readWrite)?0:(BackupProtocolLogin::Flags_ReadOnly));

	// 5. Tell user.
	BOX_INFO("Login complete.");
	BOX_INFO("Type \"help\" for a list of commands.");
	
	// Set up a context for our work
	BackupQueries context(connection, conf, readWrite);
	
	// Start running commands... first from the command line
	{
		int c = 0;
		while(c < argc && !context.Stop())
		{
			BackupQueries::ParsedCommand cmd(argv[c++], true);
			context.DoCommand(cmd);
		}
	}
	
	// Get commands from input

#ifdef HAVE_LIBREADLINE
	if(useReadline)
	{
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
		
		pProtocol = &connection;
		pConfig = &conf;
		pQueries = &context;
	}

	std::string last_cmd;
#endif

	std::auto_ptr<FdGetLine> apGetLine;
	if(fileno(stdin) >= 0)
	{
		apGetLine.reset(new FdGetLine(fileno(stdin)));
	}

	while(!context.Stop() && fileno(stdin) >= 0)
	{
		std::string cmd_str;

		#ifdef HAVE_LIBREADLINE
		if(useReadline)
		{
			char *cmd_ptr = readline("query > ");
			
			if(cmd_ptr == NULL)
			{
				// Ctrl-D pressed -- terminate now
				puts("");
				break;
			}

			cmd_str = cmd_ptr;
			free(cmd_ptr);
		}
		else
		#endif // HAVE_LIBREADLINE
		{
			printf("query > ");
			fflush(stdout);

			try
			{
				cmd_str = apGetLine->GetLine();
			}
			catch(CommonException &e)
			{
				if(e.GetSubType() == CommonException::GetLineEOF)
				{
					break;
				}
				throw;
			}
		}

		BackupQueries::ParsedCommand cmd_parsed(cmd_str, false);
		if (cmd_parsed.IsEmpty())
		{
			continue;
		}

		context.DoCommand(cmd_parsed);

		#ifdef HAVE_READLINE_HISTORY
		if(last_cmd != cmd_str)
		{
			add_history(cmd_str.c_str());
			last_cmd = cmd_str;
		}
		#endif // HAVE_READLINE_HISTORY
	}
	
	// Done... stop nicely
	BOX_INFO("Logging off...");
	connection.QueryFinished();
	BOX_INFO("Session finished.");
	
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

