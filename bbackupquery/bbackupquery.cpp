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
#include <stdio.h>
#include <sys/types.h>
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

#include "MemLeakFindOn.h"

void PrintUsageAndExit()
{
	printf("Usage: bbackupquery [-q] [-w] "
#ifdef WIN32
	"[-u] "
#endif
	"\n\t[-c config_file] [-l log_file] [commands]\n"
	"As many commands as you require.\n"
	"If commands are multiple words, remember to enclose the command in quotes.\n"
	"Remember to use quit command if you don't want to drop into interactive mode.\n");
	exit(1);
}

int main(int argc, const char *argv[])
{
	MAINHELPER_SETUP_MEMORY_LEAK_EXIT_REPORT("bbackupquery.memleaks", "bbackupquery")

#ifdef WIN32
	WSADATA info;
	
	// Under Win32 we must initialise the Winsock library
	// before using it.
	
	if (WSAStartup(0x0101, &info) == SOCKET_ERROR) 
	{
		// throw error?    perhaps give it its own id in the furture
		THROW_EXCEPTION(BackupStoreException, Internal)
	}
#endif

	// Really don't want trace statements happening, even in debug mode
	#ifndef NDEBUG
		BoxDebugTraceOn = false;
	#endif
	
	int returnCode = 0;

	MAINHELPER_START
	
	FILE *logFile = 0;

	// Filename for configuration file?
	const char *configFilename = BOX_FILE_BBACKUPD_DEFAULT_CONFIG;
	
	// Flags
	bool quiet = false;
	bool readWrite = false;

#ifdef WIN32
	const char* validOpts = "qwuc:l:";
	bool unicodeConsole = false;
#else
	const char* validOpts = "qwc:l:";
#endif

	// See if there's another entry on the command line
	int c;
	while((c = getopt(argc, (char * const *)argv, validOpts)) != -1)
	{
		switch(c)
		{
		case 'q':
			// Quiet mode
			quiet = true;
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
				printf("Can't open log file '%s'\n", optarg);
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
	
	// Print banner?
	if(!quiet)
	{
		const char *banner = BANNER_TEXT("Backup Query Tool");
		printf(banner);
	}

#ifdef WIN32
	if (unicodeConsole)
	{
		if (!SetConsoleCP(CP_UTF8))
		{
			fprintf(stderr, "Failed to set input codepage: "
				"error %d\n", GetLastError());
		}

		if (!SetConsoleOutputCP(CP_UTF8))
		{
			fprintf(stderr, "Failed to set output codepage: "
				"error %d\n", GetLastError());
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
	if(!quiet) printf("Using configuration file %s\n", configFilename);
	std::string errs;
	std::auto_ptr<Configuration> config(Configuration::LoadAndVerify(configFilename, &BackupDaemonConfigVerify, errs));
	if(config.get() == 0 || !errs.empty())
	{
		printf("Invalid configuration file:\n%s", errs.c_str());
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
	if(!quiet) printf("Connecting to store...\n");
	SocketStreamTLS socket;
	socket.Open(tlsContext, Socket::TypeINET, conf.GetKeyValue("StoreHostname").c_str(), BOX_PORT_BBSTORED);
	
	// 3. Make a protocol, and handshake
	if(!quiet) printf("Handshake with store...\n");
	BackupProtocolClient connection(socket);
	connection.Handshake();
	
	// logging?
	if(logFile != 0)
	{
		connection.SetLogToFile(logFile);
	}
	
	// 4. Log in to server
	if(!quiet) printf("Login to store...\n");
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
	BackupQueries context(connection, conf);
	
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
	if(!quiet) printf("Logging off...\n");
	connection.QueryFinished();
	if(!quiet) printf("Session finished.\n");
	
	// Return code
	returnCode = context.GetReturnCode();
	
	// Close log file?
	if(logFile)
	{
		::fclose(logFile);
	}
	
	// Let everything be cleaned up on exit.
	
	MAINHELPER_END
	
#ifdef WIN32
	// Clean up our sockets
	WSACleanup();
#endif

	return returnCode;
}

