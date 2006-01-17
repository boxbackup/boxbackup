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
//		Name:    Configuration
//		Purpose: Reading configuration files
//		Created: 2003/07/23
//
// --------------------------------------------------------------------------

#ifndef CONFIGURATION__H
#define CONFIGURATION__H

#include <map>
#include <list>
#include <vector>
#include <string>
#include <memory>

#include "BoxTime.h"

// For defining tests
enum
{
	ConfigTest_LastEntry = 1,
	ConfigTest_Exists = 2,
	ConfigTest_IsInt = 4,
	ConfigTest_MultiValueAllowed = 8,
	ConfigTest_IsBool = 16
};

class ConfigurationVerifyKey
{
public:
	const char *mpName;			// "*" for all other keys (not implemented yet)
	const char *mpDefaultValue;	// default for when it's not present
	int Tests;
	void *TestFunction;			// set to zero for now, will implement later
};

class ConfigurationVerify
{
public:
	const char *mpName;			// "*" for all other sub config names
	const ConfigurationVerify *mpSubConfigurations;
	const ConfigurationVerifyKey *mpKeys;
	int Tests;	
	void *TestFunction;			// set to zero for now, will implement later
};

class FdGetLine;

// --------------------------------------------------------------------------
//
// Class
//		Name:    Configuration
//		Purpose: Loading, checking, and representing configuration files
//		Created: 2003/07/23
//
// --------------------------------------------------------------------------
class Configuration
{
private:
	Configuration(const std::string &rName, box_time_t configModTime);
public:
	Configuration(const Configuration &rToCopy);
	~Configuration();
	
	box_time_t GetModTime() const { return mConfigModTime; }

	enum
	{
		// The character to separate multi-values
		MultiValueSeparator = '\x01'
	};
	
	static std::auto_ptr<Configuration> LoadAndVerify(const char *Filename, const ConfigurationVerify *pVerify, std::string &rErrorMsg);
	static std::auto_ptr<Configuration> Load(const char *Filename, std::string &rErrorMsg) { return LoadAndVerify(Filename, 0, rErrorMsg); }
	
	bool KeyExists(const char *pKeyName) const;
	const std::string &GetKeyValue(const char *pKeyName) const;
	int GetKeyValueInt(const char *pKeyName) const;
	bool GetKeyValueBool(const char *pKeyName) const;
	std::vector<std::string> GetKeyNames() const;
	
	bool SubConfigurationExists(const char *pSubName) const;
	const Configuration &GetSubConfiguration(const char *pSubName) const;
	std::vector<std::string> GetSubConfigurationNames() const;
	
	std::string mName;
	// Order of sub blocks preserved
	typedef std::list<std::pair<std::string, Configuration> > SubConfigListType;
	SubConfigListType mSubConfigurations;
	// Order of keys, not preserved
	std::map<std::string, std::string> mKeys;
protected:
	box_time_t mConfigModTime;
private:
	static bool LoadInto(Configuration &rConfig, FdGetLine &rGetLine, std::string &rErrorMsg, bool RootLevel);
	static bool Verify(Configuration &rConfig, const ConfigurationVerify &rVerify, const std::string &rLevel, std::string &rErrorMsg);
};

#endif // CONFIGURATION__H

