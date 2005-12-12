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
	Configuration(const std::string &rName);
public:
	Configuration(const Configuration &rToCopy);
	~Configuration();
	
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
	
private:
	static bool LoadInto(Configuration &rConfig, FdGetLine &rGetLine, std::string &rErrorMsg, bool RootLevel);
	static bool Verify(Configuration &rConfig, const ConfigurationVerify &rVerify, const std::string &rLevel, std::string &rErrorMsg);
};

#endif // CONFIGURATION__H

