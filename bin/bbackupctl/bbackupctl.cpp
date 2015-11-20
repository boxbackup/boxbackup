// --------------------------------------------------------------------------
//
// File
//		Name:    bbackupctl.cpp
//		Purpose: bbackupd daemon control program
//		Created: 18/2/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include "box_getopt.h"
#include "MainHelper.h"
#include "BackupDaemon.h"
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
	NoCommand,
};

void PrintUsageAndExit(int ret)
{
	std::cout << 
	"Usage: bbackupctl [options] <command>\n"
	"\n"
	"Options:\n" <<
	Logging::OptionParser::GetUsageString() <<
	"\n"
	"Commands are:\n"
	"  status -- report daemon status without changing anything\n"
	"  sync -- start a synchronisation (backup) run now\n"
	"  force-sync -- force the start of a synchronisation run, "
	"even if SyncAllowScript says no\n"
	"  reload -- reload daemon configuration\n"
	"  terminate -- terminate daemon now\n"
    "  stop-sync -- cancel the sync now\n"
	"  wait-for-sync -- wait until the next sync starts, then exit\n"
	"  wait-for-end  -- wait until the next sync finishes, then exit\n"
	"  sync-and-wait -- start sync, wait until it finishes, then exit\n"
	;
	exit(ret);
}

int main(int argc, const char *argv[])
{
	int returnCode = 0;

	MAINHELPER_SETUP_MEMORY_LEAK_EXIT_REPORT("bbackupctl.memleaks", 
		"bbackupctl")

	MAINHELPER_START

	Logging::SetProgramName("bbackupctl");

	// Filename for configuration file?
	std::string configFilename = BOX_GET_DEFAULT_BBACKUPD_CONFIG_FILE;
	
	// See if there's another entry on the command line
	int c;
	std::string options("c:");
	options += Logging::OptionParser::GetOptionString();
	Logging::OptionParser LogLevel;

	while((c = getopt(argc, (char * const *)argv, options.c_str())) != -1)
	{
		switch(c)
		{
		case 'c':
			// store argument
			configFilename = optarg;
			break;
		
		default:
			int ret = LogLevel.ProcessOption(c);
			if(ret != 0)
			{
				PrintUsageAndExit(ret);
			}
		}
	}
	// Adjust arguments
	argc -= optind;
	argv += optind;
	
	// Check there's a command
	if(argc != 1)
	{
		PrintUsageAndExit(2);
	}

	Logging::FilterConsole(LogLevel.GetCurrentLevel());

	// Read in the configuration file
	BOX_INFO("Using configuration file " << configFilename);

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
	if(!getLine.GetLine(configSummary, false, PROTOCOL_DEFAULT_TIMEOUT))
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
	BOX_TRACE("Daemon configuration summary:\n"
		"  AutomaticBackup = " << 
		(autoBackup?"true":"false") << "\n"
		"  UpdateStoreInterval = " << updateStoreInterval << 
		" seconds\n"
		"  MinimumFileAge = " << minimumFileAge << " seconds\n"
		"  MaxUploadWait = " << maxUploadWait << " seconds");



	std::string stateLine;
	if(!getLine.GetLine(stateLine, false, PROTOCOL_DEFAULT_TIMEOUT) || getLine.IsEOF())
	{
		BOX_ERROR("Failed to receive state line from daemon");
		return 1;
	}

	// Decode it
	int currentState;
    if(::sscanf(stateLine.c_str(), "state: %d", &currentState) != 1)
	{
		BOX_ERROR("Received invalid state line from daemon");
		return 1;
	}

	BOX_TRACE("Current state: " <<
		BackupDaemon::GetStateName(currentState));


    std::string statsLine;
    if(!getLine.GetLine(statsLine, false, PROTOCOL_DEFAULT_TIMEOUT) || getLine.IsEOF())
    {
        BOX_ERROR("Failed to receive stats line from daemon");
        return 1;
    }

    // Decode it
    int statsState;
    box_time_t statsStartTime, statsEndTime;
    uint64_t statsFileCount, statsSizeUploaded;
    if(::sscanf(statsLine.c_str(), "stats: %d %lu %lu %llu %llu", &statsState, &statsStartTime,
                &statsEndTime, &statsFileCount, &statsSizeUploaded) != 5)
    {
        BOX_ERROR("Received invalid stats line from daemon");
        return 1;
    }


	Command command = Default;
	std::string commandName(argv[0]);

	if(commandName == "wait-for-sync")
	{
		command = WaitForSyncStart;
	}
	else if(commandName == "wait-for-end")
	{
		command = WaitForSyncEnd;
	}
	else if(commandName == "sync-and-wait")
	{
		command = SyncAndWaitForEnd;
	}
	else if(commandName == "status")
	{
		BOX_NOTICE("state " <<
			BackupDaemon::GetStateName(currentState));
        BOX_NOTICE("lastSync " << statsState
                   <<" "<<statsStartTime
                   <<" "<<statsEndTime
                   <<" "<<statsFileCount
                   <<" "<<statsSizeUploaded);
		command = NoCommand;
	}

	switch(command)
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
			commandName = "force-sync";
			std::string cmd = commandName + "\n";
			connection.Write(cmd, PROTOCOL_DEFAULT_TIMEOUT);
			connection.WriteAllBuffered();

			if(currentState != 0)
			{
				BOX_INFO("Waiting for current sync/error state "
					"to finish...");
			}
		}
		break;

		default:
		{
			// Normal case, just send the command given, plus a
			// quit command.
			std::string cmd = commandName + "\n";
			connection.Write(cmd, PROTOCOL_DEFAULT_TIMEOUT);
		}
		// fall through

		case NoCommand:
		{
			// Normal case, just send the command given plus a
			// quit command.
			std::string cmd = "quit\n";
			connection.Write(cmd, PROTOCOL_DEFAULT_TIMEOUT);
		}
	}
	
	// Read the response
	std::string line;
	bool syncIsRunning = false;
	bool finished = false;

	while(command != NoCommand && !finished && !getLine.IsEOF() &&
		getLine.GetLine(line, false, PROTOCOL_DEFAULT_TIMEOUT))
	{
		BOX_TRACE("Received line: " << line);

		if(line.substr(0, 6) == "state ")
		{
			std::string state_str = line.substr(6);
			int state_num;
			if(sscanf(state_str.c_str(), "%d", &state_num) == 1)
			{
				BOX_INFO("Daemon state changed to: " <<
					BackupDaemon::GetStateName(state_num));
			}
			else
			{
				BOX_WARNING("Failed to parse line: " << line);
			}
		}

		switch(command)
		{
			case WaitForSyncStart:
			{
				// Need to wait for the state change...
				if(line == "start-sync")
				{
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
					BOX_TRACE("Sync started...");
					syncIsRunning = true;
				}
				else if(line == "finish-sync")
				{
					if (syncIsRunning)
					{
						// And we're done
						BOX_TRACE("Sync finished.");
						finished = true;
					}
					else
					{
						BOX_TRACE("Previous sync finished.");
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
					BOX_TRACE("Control command "
						"sent: " <<
						commandName);
					finished = true;
				}
				else if(line == "error")
				{
					BOX_ERROR("Control command failed: " <<
						commandName << ". Check "
						"command spelling.");
					returnCode = 1;
					finished = true;
				}
			}
		}
	}

	// Send a quit command to finish nicely
	connection.Write("quit\n", 5, PROTOCOL_DEFAULT_TIMEOUT);

	MAINHELPER_END

#if defined WIN32 && ! defined BOX_RELEASE_BUILD
	closelog();
#endif
	
	return returnCode;
}
