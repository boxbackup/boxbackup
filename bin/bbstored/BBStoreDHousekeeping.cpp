// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
// --------------------------------------------------------------------------
//
// File
//		Name:    BBStoreDHousekeeping.cpp
//		Purpose: Implementation of housekeeping functions for bbstored
//		Created: 11/12/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>
#include <syslog.h>

#include "BackupStoreDaemon.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreAccounts.h"
#include "HousekeepStoreAccount.h"
#include "BoxTime.h"
#include "Configuration.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDaemon::HousekeepingProcess()
//		Purpose: Do housekeeping
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
void BackupStoreDaemon::HousekeepingProcess()
{
	// Get the time between housekeeping runs
	const Configuration &rconfig(GetConfiguration());
	int64_t housekeepingInterval = SecondsToBoxTime((uint32_t)rconfig.GetKeyValueInt("TimeBetweenHousekeeping"));
	
	int64_t lastHousekeepingRun = 0;

	while(!StopRun())
	{
		// Time now
		int64_t timeNow = GetCurrentBoxTime();
		// Do housekeeping if the time interval has elapsed since the last check
		if((timeNow - lastHousekeepingRun) >= housekeepingInterval)
		{
			// Store the time
			lastHousekeepingRun = timeNow;
			::syslog(LOG_INFO, "Starting housekeeping");

			// Get the list of accounts
			std::vector<int32_t> accounts;
			if(mpAccountDatabase)
			{
				mpAccountDatabase->GetAllAccountIDs(accounts);
			}
			
			SetProcessTitle("housekeeping, active");
			
			// Check them all
			for(std::vector<int32_t>::const_iterator i = accounts.begin(); i != accounts.end(); ++i)
			{
				try
				{
					if(mpAccounts)
					{
						// Get the account root
						std::string rootDir;
						int discSet = 0;
						mpAccounts->GetAccountRoot(*i, rootDir, discSet);
						
						// Do housekeeping on this account
						HousekeepStoreAccount housekeeping(*i, rootDir, discSet, *this);
						housekeeping.DoHousekeeping();
					}
				}
				catch(BoxException &e)
				{
					::syslog(LOG_ERR, "while housekeeping account %08X, exception %s (%d/%d) -- aborting housekeeping run for this account",
						*i, e.what(), e.GetType(), e.GetSubType());
				}
				catch(std::exception &e)
				{
					::syslog(LOG_ERR, "while housekeeping account %08X, exception %s -- aborting housekeeping run for this account",
						*i, e.what());
				}
				catch(...)
				{
					::syslog(LOG_ERR, "while housekeeping account %08X, unknown exception -- aborting housekeeping run for this account",
						*i);
				}
				
				// Check to see if there's any message pending
				CheckForInterProcessMsg(0 /* no account */);
		
				// Stop early?
				if(StopRun())
				{
					break;
				}
			}
			
			::syslog(LOG_INFO, "Finished housekeeping");
		}

		// Placed here for accuracy, if StopRun() is true, for example.
		SetProcessTitle("housekeeping, idle");
		
		// Calculate how long should wait before doing the next housekeeping run
		timeNow = GetCurrentBoxTime();
		int64_t secondsToGo = BoxTimeToSeconds((lastHousekeepingRun + housekeepingInterval) - timeNow);
		if(secondsToGo < 1) secondsToGo = 1;
		if(secondsToGo > 60) secondsToGo = 60;
		int32_t millisecondsToGo = ((int)secondsToGo) * 1000;
	
		// Check to see if there's any message pending
		CheckForInterProcessMsg(0 /* no account */, millisecondsToGo);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDaemon::CheckForInterProcessMsg(int, int)
//		Purpose: Process a message, returning true if the housekeeping process
//				 should abort for the specified account.
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
bool BackupStoreDaemon::CheckForInterProcessMsg(int AccountNum, int MaximumWaitTime)
{
	// First, check to see if it's EOF -- this means something has gone wrong, and the housekeeping should terminate.
	if(mInterProcessComms.IsEOF())
	{
		SetTerminateWanted();
		return true;
	}

	// Get a line, and process the message
	std::string line;
	if(mInterProcessComms.GetLine(line, false /* no pre-processing */, MaximumWaitTime))
	{
		TRACE1("housekeeping received command '%s' over interprocess comms\n", line.c_str());
	
		int account = 0;
	
		if(line == "h")
		{
			// HUP signal received by main process
			SetReloadConfigWanted();
			return true;
		}
		else if(line == "t")
		{
			// Terminate signal received by main process
			SetTerminateWanted();
			return true;
		}
		else if(sscanf(line.c_str(), "r%x", &account) == 1)
		{
			// Main process is trying to lock an account -- are we processing it?
			if(account == AccountNum)
			{
				// Yes! -- need to stop now so when it retries to get the lock, it will succeed
				::syslog(LOG_INFO, "Housekeeping giving way to connection for account 0x%08x", AccountNum);
				return true;
			}
		}
	}
	
	return false;
}


