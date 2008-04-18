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
#include <stdlib.h>

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

	MAINHELPER_SETUP_MEMORY_LEAK_EXIT_REPORT("bbackupctl.memleaks", 
		"bbackupctl")

	MAINHELPER_START

#if defined WIN32 && ! defined NDEBUG
	::openlog("Box Backup (bbackupctl)", 0, 0);
#endif

	// Filename for configuration file?
	std::string configFilename;

	#ifdef WIN32
		configFilename = BOX_GET_DEFAULT_BBACKUPD_CONFIG_FILE;
	#else
		configFilename = BOX_FILE_BBACKUPD_DEFAULT_CONFIG;
	#endif
	
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
	if(!quiet) BOX_NOTICE("Using configuration file " << configFilename);

	std::string errs;
	std::auto_ptr<Configuration> config(
		Configuration::LoadAndVerify
			(configFilename, &BackupDaemonConfigVerify, errs));

	if(config.get() == 0 || !errs.empty())
	{
		BOX_ERROR("Invalid configuration file: " << errs);
		return 1;
	}
	// Easier coding
	const Configuration &conf(*config);

	// Check there's a socket defined in the config file
	if(!conf.KeyExists("CommandSocket"))
	{
		BOX_ERROR("Daemon isn't using a control socket, "
			"could not execute command.\n"
			"Add a CommandSocket declaration to the "
			"bbackupd.conf file.");
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
		std::string socket = conf.GetKeyValue("CommandSocket");
		connection.Connect(socket);
#else
		connection.Open(Socket::TypeUNIX, conf.GetKeyValue("CommandSocket").c_str());
#endif
	}
	catch(...)
	{
		BOX_ERROR("Failed to connect to daemon control socket.\n"
			"Possible causes:\n"
			"  * Daemon not running\n"
			"  * Daemon busy syncing with store server\n"
			"  * Another bbackupctl process is communicating with the daemon\n"
			"  * Daemon is waiting to recover from an error"
		);

		return 1;
	}
	
	// For receiving data
	IOStreamGetLine getLine(connection);
	
	// Wait for the configuration summary
	std::string configSummary;
	if(!getLine.GetLine(configSummary))
	{
		BOX_ERROR("Failed to receive configuration summary "
			"from daemon");
		return 1;
	}

	// Was the connection rejected by the server?
	if(getLine.IsEOF())
	{
		BOX_ERROR("Server rejected the connection. Are you running "
			"bbackupctl as the same user as the daemon?");
		return 1;
	}

	// Decode it
	int autoBackup, updateStoreInterval, minimumFileAge, maxUploadWait;
	if(::sscanf(configSummary.c_str(), "bbackupd: %d %d %d %d", &autoBackup,
			&updateStoreInterval, &minimumFileAge, &maxUploadWait) != 4)
	{
		BOX_ERROR("Config summary didn't decode.");
		return 1;
	}
	// Print summary?
	if(!quiet)
	{
		BOX_INFO("Daemon configuration summary:\n"
			"  AutomaticBackup = " << 
			(autoBackup?"true":"false") << "\n"
			"  UpdateStoreInterval = " << updateStoreInterval << 
			" seconds\n"
			"  MinimumFileAge = " << minimumFileAge << " seconds\n"
			"  MaxUploadWait = " << maxUploadWait << " seconds");
	}

	std::string stateLine;
	if(!getLine.GetLine(stateLine) || getLine.IsEOF())
	{
		BOX_ERROR("Failed to receive state line from daemon");
		return 1;
	}

	// Decode it
	int currentState;
	if(::sscanf(stateLine.c_str(), "state %d", &currentState) != 1)
	{
		BOX_ERROR("Received invalid state line from daemon");
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
			// Check that it's in automatic mode, 
			// because otherwise it'll never start

			if(!autoBackup)
			{
				BOX_ERROR("Daemon is not in automatic mode, "
					"sync will never start!");
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
				BOX_INFO("Waiting for current sync/error state "
					"to finish...");
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
					if (!quiet) BOX_INFO("Sync started...");
					syncIsRunning = true;
				}
				else if(line == "finish-sync")
				{
					if (syncIsRunning)
					{
						if (!quiet) BOX_INFO("Sync finished.");
						// Send a quit command to finish nicely
						connection.Write("quit\n", 5);
					
						// And we're done
						finished = true;
					}
					else
					{
						if (!quiet) BOX_INFO("Previous sync finished.");
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
						BOX_INFO("Succeeded.");
					}
					finished = true;
				}
				else if(line == "error")
				{
					BOX_ERROR("Check command spelling");
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
