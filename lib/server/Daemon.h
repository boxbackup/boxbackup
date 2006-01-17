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
//		Name:    Daemon.h
//		Purpose: Basic daemon functionality
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------

/*  NOTE: will log to local6: include a line like
	local6.info                                             /var/log/box
	in /etc/syslog.conf
*/


#ifndef DAEMON__H
#define DAEMON__H

class Configuration;
class ConfigurationVerify;

// --------------------------------------------------------------------------
//
// Class
//		Name:    Daemon
//		Purpose: Basic daemon functionality
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------
class Daemon
{
public:
	Daemon();
	virtual ~Daemon();
private:
	Daemon(const Daemon &rToCopy);
public:

	int Main(const char *DefaultConfigFile, int argc, const char *argv[]);
	
	virtual void Run();
	const Configuration &GetConfiguration() const;
	
	virtual const char *DaemonName() const;
	virtual const char *DaemonBanner() const;
	virtual const ConfigurationVerify *GetConfigVerify() const;
	
	bool StopRun() {return mReloadConfigWanted | mTerminateWanted;}
	bool IsReloadConfigWanted() {return mReloadConfigWanted;}
	bool IsTerminateWanted() {return mTerminateWanted;}

	// To allow derived classes to get these signals in other ways
	void SetReloadConfigWanted() {mReloadConfigWanted = true;}
	void SetTerminateWanted() {mTerminateWanted = true;}
	
	virtual void SetupInInitialProcess();
	virtual void EnterChild();
	
	static void SetProcessTitle(const char *format, ...);
	
private:
	static void SignalHandler(int sigraised);
	
private:
	Configuration *mpConfiguration;
	bool mReloadConfigWanted;
	bool mTerminateWanted;
	static Daemon *spDaemon;
};

#define DAEMON_VERIFY_SERVER_KEYS 	{"PidFile", 0, ConfigTest_Exists, 0}, \
									{"User", 0, ConfigTest_LastEntry, 0}

#endif // DAEMON__H

