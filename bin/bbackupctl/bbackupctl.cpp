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

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include "MainHelper.h"
#include "BoxPortsAndFiles.h"
#include "BackupDaemonConfigVerify.h"
#include "Socket.h"
#include "SocketStream.h"
#include "IOStreamGetLine.h"

#ifdef WIN32
	#include "WinNamedPipeStream.h"
#endif

#include "MemLeakFindOn.h"

enum Command
{
	Default,
	WaitForSyncStart,
	WaitForSyncEnd,
	SyncAndWaitForEnd,
};

void PrintUsageAndExit()
{
	printf("Usage: bbackupctl [-q] [-c config_file] <command>\n"
	"Commands are:\n"
	"  sync -- start a synchronisation (backup) run now\n"
	"  force-sync -- force the start of a synchronisation run, "
	"even if SyncAllowScript says no\n"
	"  reload -- reload daemon configuration\n"
	"  terminate -- terminate daemon now\n"
	"  wait-for-sync -- wait until the next sync starts, then exit\n"
	"  wait-for-end  -- wait until the next sync finishes, then exit\n"
	"  sync-and-wait -- start sync, wait until it finishes, then exit\n"
	);
	exit(1);
}

int main(int argc, const char *argv[])
{
	int returnCode = 0;

#if defined WIN32 && ! defined NDEBUG
	::openlog("Box Backup (bbackupctl)", 0, 0);
#endif

	MAINHELPER_SETUP_MEMORY_LEAK_EXIT_REPORT("bbackupctl.memleaks", 
		"bbackupctl")

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

	// Check there's a socket defined in the config file
	if(!conf.KeyExists("CommandSocket"))
	{
		printf("Daemon isn't using a control socket, "
			"could not execute command.\n"
			"Add a CommandSocket declaration to the "
			"bbackupd.conf file.\n");
		return 1;
	}
	
	// Connect to socket

#ifndef WIN32
	SocketStream connection;
#else /* WIN32 */
	WinNamedPipeStream connection;
#endif /* ! WIN32 */
	
	try
	{
#ifdef WIN32
		connection.Connect(BOX_NAMED_PIPE_NAME);
#else
		connection.Open(Socket::TypeUNIX, conf.GetKeyValue("CommandSocket").c_str());
#endif
	}
	catch(...)
	{
		printf("Failed to connect to daemon control socket.\n"
			"Possible causes:\n"
			"  * Daemon not running\n"
			"  * Daemon busy syncing with store server\n"
			"  * Another bbackupctl process is communicating with the daemon\n"
			"  * Daemon is waiting to recover from an error\n"
		);

#if defined WIN32 && ! defined NDEBUG
		syslog(LOG_ERR,"Failed to connect to the command socket");
#endif

		return 1;
	}
	
	// For receiving data
	IOStreamGetLine getLine(connection);
	
	// Wait for the configuration summary
	std::string configSummary;
	if(!getLine.GetLine(configSummary))
	{
#if defined WIN32 && ! defined NDEBUG
		syslog(LOG_ERR, "Failed to receive configuration summary "
			"from daemon");
#else
		printf("Failed to receive configuration summary from daemon\n");
#endif

		return 1;
	}

	// Was the connection rejected by the server?
	if(getLine.IsEOF())
	{
#if defined WIN32 && ! defined NDEBUG
		syslog(LOG_ERR, "Server rejected the connection. "
			"Are you running bbackupctl as the same user "
			"as the daemon?");
#else
		printf("Server rejected the connection. "
			"Are you running bbackupctl as the same user "
			"as the daemon?\n");
#endif

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
		printf("Daemon configuration summary:\n"
			"  AutomaticBackup = %s\n"
			"  UpdateStoreInterval = %d seconds\n"
			"  MinimumFileAge = %d seconds\n"
			"  MaxUploadWait = %d seconds\n",
			autoBackup?"true":"false", updateStoreInterval, 
			minimumFileAge, maxUploadWait);
	}

	std::string stateLine;
	if(!getLine.GetLine(stateLine) || getLine.IsEOF())
	{
#if defined WIN32 && ! defined NDEBUG
		syslog(LOG_ERR, "Failed to receive state line from daemon");
#else
		printf("Failed to receive state line from daemon\n");
#endif
		return 1;
	}

	// Decode it
	int currentState;
	if(::sscanf(stateLine.c_str(), "state %d", &currentState) != 1)
	{
		printf("State line didn't decode\n");
		return 1;
	}

	Command command = Default;
	std::string commandName(argv[0]);

	if (commandName == "wait-for-sync")
	{
		command = WaitForSyncStart;
	}
	else if (commandName == "wait-for-end")
	{
		command = WaitForSyncEnd;
	}
	else if (commandName == "sync-and-wait")
	{
		command = SyncAndWaitForEnd;
	}

	switch (command)
	{
		case WaitForSyncStart:
		case WaitForSyncEnd:
		{
			// Check that it's not in non-automatic mode, 
			// because then it'll never start

			if(!autoBackup)
			{
				printf("ERROR: Daemon is not in automatic mode -- "
					"sync will never start!\n");
				return 1;
			}

		}
		break;

		case SyncAndWaitForEnd:
		{
			// send a sync command
			std::string cmd("force-sync\n");
			connection.Write(cmd.c_str(), cmd.size());
			connection.WriteAllBuffered();

			if (currentState != 0)
			{
				printf("Waiting for current sync/error state "
					"to finish...\n");
			}
		}
		break;

		default:
		{
			// Normal case, just send the command given 
			// plus a quit command.
			std::string cmd = commandName;
			cmd += "\nquit\n";
			connection.Write(cmd.c_str(), cmd.size());
		}
	}
	
	// Read the response
	std::string line;
	bool syncIsRunning = false;
	bool finished = false;

	while(!finished && !getLine.IsEOF() && getLine.GetLine(line))
	{
		switch (command)
		{
			case WaitForSyncStart:
			{
				// Need to wait for the state change...
				if(line == "start-sync")
				{
					// Send a quit command to finish nicely
					connection.Write("quit\n", 5);
					
					// And we're done
					finished = true;
				}
			}
			break;

			case WaitForSyncEnd:
			case SyncAndWaitForEnd:
			{
				if(line == "start-sync")
				{
					if (!quiet) printf("Sync started...\n");
					syncIsRunning = true;
				}
				else if(line == "finish-sync")
				{
					if (syncIsRunning)
					{
						if (!quiet) printf("Sync finished.\n");
						// Send a quit command to finish nicely
						connection.Write("quit\n", 5);
					
						// And we're done
						finished = true;
					}
					else
					{
						if (!quiet) printf("Previous sync finished.\n");
					}
					// daemon must still be busy
				}
			}
			break;

			default:
			{
				// Is this an OK or error line?
				if(line == "ok")
				{
					if(!quiet)
					{
						printf("Succeeded.\n");
					}
					finished = true;
				}
				else if(line == "error")
				{
					printf("ERROR. (Check command spelling)\n");
					returnCode = 1;
					finished = true;
				}
			}
		}
	}

	MAINHELPER_END

#if defined WIN32 && ! defined NDEBUG
	closelog();
#endif
	
	return returnCode;
}
