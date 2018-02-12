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

#include "Logging.h" // for Log::Category

// For defining tests
enum
{
	ConfigTest_LastEntry = 1,
	ConfigTest_Exists = 2,
	ConfigTest_IsInt = 4,
	ConfigTest_IsUint32 = 8, 
	ConfigTest_MultiValueAllowed = 16,
	ConfigTest_IsBool = 32
};

class ConfigurationCategory : public Log::Category
{
	public:
		ConfigurationCategory(const std::string& name)
		: Log::Category(std::string("Configuration/") + name)
		{ }
};

class ConfigurationVerifyKey
{
public:
	typedef enum
	{
		NoDefaultValue = 1
	} NoDefaultValue_t;

	ConfigurationVerifyKey(std::string name, int flags,
		void *testFunction = NULL);
	// to allow passing ConfigurationVerifyKey::NoDefaultValue
	// for default ListenAddresses
	ConfigurationVerifyKey(std::string name, int flags,
		NoDefaultValue_t t, void *testFunction = NULL);
	ConfigurationVerifyKey(std::string name, int flags,
		std::string defaultValue, void *testFunction = NULL);
	ConfigurationVerifyKey(std::string name, int flags,
		const char* defaultValue, void *testFunction = NULL);
	ConfigurationVerifyKey(std::string name, int flags,
		int defaultValue, void *testFunction = NULL);
	ConfigurationVerifyKey(std::string name, int flags,
		bool defaultValue, void *testFunction = NULL);
	const std::string& Name() const { return mName; }
	const std::string& DefaultValue() const { return mDefaultValue; }
	const bool HasDefaultValue() const { return mHasDefaultValue; }
	const int Flags() const { return mFlags; }
	const void* TestFunction() const { return mTestFunction; }
	ConfigurationVerifyKey(const ConfigurationVerifyKey& rToCopy);

private:
	ConfigurationVerifyKey& operator=(const ConfigurationVerifyKey&
		noAssign);

	std::string mName;         // "*" for all other keys (not implemented yet)
	std::string mDefaultValue; // default for when it's not present
	bool mHasDefaultValue;
	int mFlags;
	void *mTestFunction; // set to zero for now, will implement later
};

class ConfigurationVerify
{
public:
	std::string mName; // "*" for all other sub config names
	const ConfigurationVerify *mpSubConfigurations;
	const ConfigurationVerifyKey *mpKeys;
	int Tests;
	void *TestFunction; // set to zero for now, will implement later
	static const ConfigurationCategory VERIFY_ERROR;
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
public:
	Configuration(const std::string &rName);
	Configuration(const Configuration &rToCopy);
	~Configuration();

	enum
	{
		// The character to separate multi-values
		MultiValueSeparator = '\x01'
	};

	static std::auto_ptr<Configuration> LoadAndVerify(
		const std::string& rFilename,
		const ConfigurationVerify *pVerify,
		std::string &rErrorMsg);

	static std::auto_ptr<Configuration> Load(
		const std::string& rFilename,
		std::string &rErrorMsg)
	{ return LoadAndVerify(rFilename, 0, rErrorMsg); }

	bool KeyExists(const std::string& rKeyName) const;
	const std::string &GetKeyValue(const std::string& rKeyName) const;
	const std::string &GetKeyValueDefault(const std::string& rKeyName,
		const std::string& rDefaultValue) const
	{
		// Don't call this for an item that has a default value defined,
		// because rDefaultValue will never be used.
		std::map<std::string, std::string>::const_iterator i =
			mKeys.find(rKeyName);
		return (i != mKeys.end()) ? i->second : rDefaultValue;
	}
	int GetKeyValueInt(const std::string& rKeyName) const;
	uint32_t GetKeyValueUint32(const std::string& rKeyName) const;
	bool GetKeyValueBool(const std::string& rKeyName) const;
	std::vector<std::string> GetKeyNames() const;

	bool SubConfigurationExists(const std::string& rSubName) const;
	const Configuration &GetSubConfiguration(const std::string& rSubName) const;
	Configuration &GetSubConfigurationEditable(const std::string& rSubName);
	std::vector<std::string> GetSubConfigurationNames() const;

	void AddKeyValue(const std::string& rKey, const std::string& rValue);
	void AddSubConfig(const std::string& rName, const Configuration& rSubConfig);

	bool Verify(const ConfigurationVerify &rVerify, std::string &rErrorMsg)
	{
		return Verify(rVerify, std::string(), rErrorMsg);
	}

private:
	std::string mName;
	// Order of keys not preserved
	std::map<std::string, std::string> mKeys;
	// Order of sub blocks preserved
	typedef std::list<std::pair<std::string, Configuration> > SubConfigListType;
	SubConfigListType mSubConfigurations;

	static bool LoadInto(Configuration &rConfig, FdGetLine &rGetLine, std::string &rErrorMsg, bool RootLevel);
	bool Verify(const ConfigurationVerify &rVerify, const std::string &rLevel,
		std::string &rErrorMsg);
};

#endif // CONFIGURATION__H

