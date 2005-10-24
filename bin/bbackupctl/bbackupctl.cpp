// --------------------------------------------------------------------------
//
// File
//		Name:    bbackupctl.cpp
//		Purpose: bbackupd daemon control program
//		Created: 18/2/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>
#include <unistd.h>

#include "MainHelper.h"
#include "BoxPortsAndFiles.h"
#include "BackupDaemonConfigVerify.h"
#include "Socket.h"
#include "SocketStream.h"
#include "IOStreamGetLine.h"

#include "MemLeakFindOn.h"

void PrintUsageAndExit()
{
	printf("Usage: bbackupctl [-q] [-c config_file] <command>\n"
	"Commands are:\n"
	"  sync -- start a syncronisation run now\n"
	"  force-sync -- force the start of a syncronisation run, even if SyncAllowScript says no\n"
	"  reload -- reload daemon configuration\n"
	"  terminate -- terminate daemon now\n"
	"  wait-for-sync -- wait until the next sync starts, then exit\n"
	);
	exit(1);
}

int main(int argc, const char *argv[])
{
	int returnCode = 0;
	::openlog("Box Backup", 0, 0);

	MAINHELPER_SETUP_MEMORY_LEAK_EXIT_REPORT("bbackupctl.memleaks", "bbackupctl")

	MAINHELPER_START

	// Filename for configuraiton file?
	const char *configFilename = BOX_FILE_BBACKUPD_DEFAULT_CONFIG;
	
	// Quiet?
	bool quiet = false;
	
	// See if there's another entry on the command line
	int c;
	while((c = getopt(argc, (char * const *)argv, "qc:l:")) != -1)
	{
		switch(c)
		{
		case 'q':
			// Quiet mode
			quiet = true;
			break;
		
		case 'c':
			// store argument
			configFilename = optarg;
			break;
		
		case '?':
		default:
			PrintUsageAndExit();
		}
	}
	// Adjust arguments
	argc -= optind;
	argv += optind;
	
	// Check there's a command
	if(argc != 1)
	{
		PrintUsageAndExit();
	}

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

#ifndef WIN32
	// Check there's a socket defined in the config file
	if(!conf.KeyExists("CommandSocket"))
	{
		printf("Daemon isn't using a control socket, could not execute command.\nAdd a CommandSocket declaration to the bbackupd.conf file.\n");
		return 1;
	}

	// Connect to socket
	SocketStream connection;
#else
	WinNamedPipe connection;
#endif
	

	try
	{
#ifdef WIN32	
		connection.open();
#else
		connection.Open(Socket::TypeUNIX, conf.GetKeyValue("CommandSocket").c_str());
#endif
	}
	catch(...)
	{
		printf("Failed to connect to daemon control socket.\n"	\
			"Possible causes:\n"								\
			"  * Daemon not running\n"							\
			"  * Daemon busy syncing with store server\n"		\
			"  * Another bbackupctl process is communicating with the daemon\n"	\
			"  * Daemon is waiting to recover from an error\n"
		);
		syslog(LOG_ERR,"Failed to connect to the command socket");
		return 1;
	}
	
	// For receiving data
#ifdef WIN32
	PipeGetLine getLine(connection);
#else
	IOStreamGetLine getLine(connection);
#endif
	
	// Wait for the configuration summary
	std::string configSummary;
	if(!getLine.GetLine(configSummary))
	{
		printf("Failed to receive configuration summary from daemon\n");
		syslog(LOG_ERR,"Failed to receive configuration summary from daemon");
		return 1;
	}

	// Was the connection rejected by the server?
	if(getLine.IsEOF())
	{
		printf("Server rejected the connection. Are you running bbackupctl as the same user as the daemon?\n");
		syslog(LOG_ERR,"Server rejected the connection. Are you running bbackupctl as the same user as the daemon?");
		return 1;
	}

	// Decode it
	int autoBackup, updateStoreInterval, minimumFileAge, maxUploadWait;
	if(::sscanf(configSummary.c_str(), "bbackupd: %d %d %d %d", &autoBackup,
			&updateStoreInterval, &minimumFileAge, &maxUploadWait) != 4)
	{
		printf("Config summary didn't decode\n");
		return 1;
	}
	// Print summary?
	if(!quiet)
	{
		printf("Daemon configuration summary:\n"	\
			   "  AutomaticBackup = %s\n"			\
			   "  UpdateStoreInterval = %d seconds\n"	\
			   "  MinimumFileAge = %d seconds\n"	\
			   "  MaxUploadWait = %d seconds\n",
			   autoBackup?"true":"false", updateStoreInterval, minimumFileAge, maxUploadWait);
	}

	// Is the command the "wait for sync to start" command?
	bool areWaitingForSync = false;
	if(::strcmp(argv[0], "wait-for-sync") == 0)
	{
		// Check that it's not in non-automatic mode, because then it'll never start
		if(!autoBackup)
		{
			printf("ERROR: Daemon is not in automatic mode -- sync will never start!\n");
			return 1;
		}
	
		// Yes... set the flag so we know what we're waiting for a sync to start
		areWaitingForSync = true;
	}
	else
	{
		// No? Just send the command given plus a quit command.
		std::string cmd(argv[0]);
		cmd += "\nquit\n";
		connection.Write(cmd.c_str(), cmd.size());		
	}
	
	// Read the response
	std::string line;
	while(!getLine.IsEOF() && getLine.GetLine(line))
	{
		if(areWaitingForSync)
		{
			// Need to wait for the state change...
			if(line == "start-sync")
			{
				// Send a quit command to finish nicely
				connection.Write("quit\n", 5);
				
				// And we're done
				break;
			}		
		}
		else
		{
			// Is this an OK or error line?
			if(line == "ok")
			{
				if(!quiet)
				{
					printf("Succeeded.\n");
				}
				break;
			}
			else if(line == "error")
			{
				printf("ERROR. (Check command spelling)\n");
				returnCode = 1;
				break;
			}
		}
	}

	MAINHELPER_END
	closelog();
	
	return returnCode;
}
