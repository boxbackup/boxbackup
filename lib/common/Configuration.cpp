// --------------------------------------------------------------------------
//
// File
//		Name:    Configuration.cpp
//		Purpose: Reading configuration files
//		Created: 2003/07/23
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <sstream>

#include "Configuration.h"
#include "CommonException.h"
#include "Guards.h"
#include "FdGetLine.h"

#include "MemLeakFindOn.h"

#include <cstring>

// utility whitespace function
inline bool iw(int c)
{
	return (c == ' ' || c == '\t' || c == '\v' || c == '\f'); // \r, \n are already excluded
}

// boolean values
static const char *sValueBooleanStrings[] = {"yes", "true", "no", "false", 0};
static const bool sValueBooleanValue[] = {true, true, false, false};

const ConfigurationCategory ConfigurationVerify::VERIFY_ERROR("VerifyError");

ConfigurationVerifyKey::ConfigurationVerifyKey
(
	std::string name,
	int flags,
	void *testFunction
)
: mName(name),
  mHasDefaultValue(false),
  mFlags(flags),
  mTestFunction(testFunction)
{ }

// to allow passing NULL for default ListenAddresses

ConfigurationVerifyKey::ConfigurationVerifyKey
(
	std::string name,
	int flags,
	NoDefaultValue_t t,
	void *testFunction
)
: mName(name),
  mHasDefaultValue(false),
  mFlags(flags),
  mTestFunction(testFunction)
{ }

ConfigurationVerifyKey::ConfigurationVerifyKey
(
	std::string name,
	int flags,
	std::string defaultValue,
	void *testFunction
)
: mName(name),
  mDefaultValue(defaultValue),
  mHasDefaultValue(true),
  mFlags(flags),
  mTestFunction(testFunction)
{ }

ConfigurationVerifyKey::ConfigurationVerifyKey
(
	std::string name,
	int flags,
	const char *defaultValue,
	void *testFunction
)
: mName(name),
  mDefaultValue(defaultValue),
  mHasDefaultValue(true),
  mFlags(flags),
  mTestFunction(testFunction)
{ }

ConfigurationVerifyKey::ConfigurationVerifyKey
(
	std::string name,
	int flags,
	int defaultValue,
	void *testFunction
)
: mName(name),
  mHasDefaultValue(true),
  mFlags(flags),
  mTestFunction(testFunction)
{
	ASSERT(flags & ConfigTest_IsInt);
	std::ostringstream val;
	val << defaultValue;
	mDefaultValue = val.str();
}

ConfigurationVerifyKey::ConfigurationVerifyKey
(
	std::string name,
	int flags,
	bool defaultValue,
	void *testFunction
)
: mName(name),
  mHasDefaultValue(true),
  mFlags(flags),
  mTestFunction(testFunction)
{
	ASSERT(flags & ConfigTest_IsBool);
	mDefaultValue = defaultValue ? "yes" : "no";
}

ConfigurationVerifyKey::ConfigurationVerifyKey
(
	const ConfigurationVerifyKey& rToCopy
)
: mName(rToCopy.mName),
  mDefaultValue(rToCopy.mDefaultValue),
  mHasDefaultValue(rToCopy.mHasDefaultValue),
  mFlags(rToCopy.mFlags),
  mTestFunction(rToCopy.mTestFunction)
{ }

// --------------------------------------------------------------------------
//
// Function
//		Name:    Configuration::Configuration(const std::string &)
//		Purpose: Constructor
//		Created: 2003/07/23
//
// --------------------------------------------------------------------------
Configuration::Configuration(const std::string &rName)
	: mName(rName)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Configuration::Configuration(const Configuration &)
//		Purpose: Copy constructor
//		Created: 2003/07/23
//
// --------------------------------------------------------------------------
Configuration::Configuration(const Configuration &rToCopy)
	: mName(rToCopy.mName),
	  mKeys(rToCopy.mKeys),
	  mSubConfigurations(rToCopy.mSubConfigurations)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Configuration::~Configuration()
//		Purpose: Destructor
//		Created: 2003/07/23
//
// --------------------------------------------------------------------------
Configuration::~Configuration()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Configuration::LoadAndVerify(const std::string &, const ConfigurationVerify *, std::string &)
//		Purpose: Loads a configuration file from disc, checks it. Returns NULL if it was faulting, in which
//				 case they'll be an error message.
//		Created: 2003/07/23
//
// --------------------------------------------------------------------------
std::auto_ptr<Configuration> Configuration::LoadAndVerify(
	const std::string& rFilename,
	const ConfigurationVerify *pVerify,
	std::string &rErrorMsg)
{
	// Just to make sure
	rErrorMsg.erase();

	// Open the file
	FileHandleGuard<O_RDONLY> file(rFilename);

	// GetLine object
	FdGetLine getline(file);

	// Object to create
	std::auto_ptr<Configuration> apConfig(
		new Configuration(std::string("<root>")));

	try
	{
		// Load
		LoadInto(*apConfig, getline, rErrorMsg, true);

		if(!rErrorMsg.empty())
		{
			// An error occured, return now
			BOX_LOG_CATEGORY(Log::ERROR, ConfigurationVerify::VERIFY_ERROR,
				"Error in Configuration::LoadInto: " << rErrorMsg);
			return std::auto_ptr<Configuration>(0);
		}

		// Verify?
		if(pVerify)
		{
			if(!apConfig->Verify(*pVerify, std::string(), rErrorMsg))
			{
				BOX_LOG_CATEGORY(Log::ERROR,
					ConfigurationVerify::VERIFY_ERROR,
					"Error verifying configuration: " <<
					rErrorMsg.substr(0, rErrorMsg.size() > 0
						? rErrorMsg.size() - 1 : 0));
				return std::auto_ptr<Configuration>(0);
			}
		}
	}
	catch(...)
	{
		// Clean up
		throw;
	}

	// Success. Return result.
	return apConfig;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    LoadInto(Configuration &, FdGetLine &, std::string &, bool)
//		Purpose: Private. Load configuration information from the file into the config object.
//				 Returns 'abort' flag, if error, will be appended to rErrorMsg.
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
bool Configuration::LoadInto(Configuration &rConfig, FdGetLine &rGetLine, std::string &rErrorMsg, bool RootLevel)
{
	bool startBlockExpected = false;
	std::string blockName;

	//TRACE1("BLOCK: |%s|\n", rConfig.mName.c_str());

	while(!rGetLine.IsEOF())
	{
		std::string line(rGetLine.GetLine(true));	/* preprocess out whitespace and comments */

		if(line.empty())
		{
			// Ignore blank lines
			continue;
		}

		// Line an open block string?
		if(line == "{")
		{
			if(startBlockExpected)
			{
				// New config object
				Configuration subConfig(blockName);

				// Continue processing into this block
				if(!LoadInto(subConfig, rGetLine, rErrorMsg, false))
				{
					// Abort error
					return false;
				}

				startBlockExpected = false;

				// Store...
				rConfig.AddSubConfig(blockName, subConfig);
			}
			else
			{
				rErrorMsg += "Unexpected start block in " +
					rConfig.mName + "\n";
			}
		}
		else
		{
			// Close block?
			if(line == "}")
			{
				if(RootLevel)
				{
					// error -- root level doesn't have a close
					rErrorMsg += "Root level has close block -- forgot to terminate subblock?\n";
					// but otherwise ignore
				}
				else
				{
					//TRACE0("ENDBLOCK\n");
					return true;		// All very good and nice
				}
			}
			// Either a key, or a sub block beginning
			else
			{
				// Can't be a start block
				if(startBlockExpected)
				{
					rErrorMsg += "Block " + blockName + " wasn't started correctly (no '{' on line of it's own)\n";
					startBlockExpected = false;
				}

				// Has the line got an = in it?
				unsigned int equals = 0;
				for(; equals < line.size(); ++equals)
				{
					if(line[equals] == '=')
					{
						// found!
						break;
					}
				}
				if(equals < line.size())
				{
					// Make key value pair
					unsigned int keyend = equals;
					while(keyend > 0 && iw(line[keyend-1]))
					{
						keyend--;
					}
					unsigned int valuestart = equals+1;
					while(valuestart < line.size() && iw(line[valuestart]))
					{
						valuestart++;
					}
					if(keyend > 0 && valuestart <= line.size())
					{
						std::string key(line.substr(0, keyend));
						std::string value(line.substr(valuestart));
						rConfig.AddKeyValue(key, value);
					}
					else
					{
						rErrorMsg += "Invalid configuration key: " + line + "\n";
					}
				}
				else
				{
					// Start of sub block
					blockName = line;
					startBlockExpected = true;
				}
			}
		}
	}

	// End of file?
	if(!RootLevel && rGetLine.IsEOF())
	{
		// Error if EOF and this isn't the root level
		rErrorMsg += "File ended without terminating all subblocks\n";
	}

	return true;
}

void Configuration::AddKeyValue(const std::string& rKey,
	const std::string& rValue)
{
	// Check for duplicate values
	if(mKeys.find(rKey) != mKeys.end())
	{
		// Multi-values allowed here, but checked later on
		mKeys[rKey] += MultiValueSeparator;
		mKeys[rKey] += rValue;
	}
	else
	{
		// Store
		mKeys[rKey] = rValue;
	}
}

void Configuration::AddSubConfig(const std::string& rName,
	const Configuration& rSubConfig)
{
	mSubConfigurations.push_back(
		std::pair<std::string, Configuration>(rName, rSubConfig));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Configuration::KeyExists(const std::string&)
//		Purpose: Checks to see if a key exists
//		Created: 2003/07/23
//
// --------------------------------------------------------------------------
bool Configuration::KeyExists(const std::string& rKeyName) const
{
	return mKeys.find(rKeyName) != mKeys.end();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Configuration::GetKeyValue(const std::string&)
//		Purpose: Returns the value of a configuration variable
//		Created: 2003/07/23
//
// --------------------------------------------------------------------------
const std::string &Configuration::GetKeyValue(const std::string& rKeyName) const
{
	std::map<std::string, std::string>::const_iterator i(mKeys.find(rKeyName));

	if(i == mKeys.end())
	{
		THROW_EXCEPTION_MESSAGE(CommonException, ConfigNoKey,
			"Missing configuration key: " << rKeyName);
	}

	return i->second;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Configuration::GetKeyValueInt(const std::string& rKeyName)
//		Purpose: Gets a key value as an integer
//		Created: 2003/07/23
//
// --------------------------------------------------------------------------
int Configuration::GetKeyValueInt(const std::string& rKeyName) const
{
	std::string value_str = GetKeyValue(rKeyName);
	long value = ::strtol(value_str.c_str(), NULL, 0 /* C style handling */);

	if(value == LONG_MAX || value == LONG_MIN)
	{
		THROW_EXCEPTION_MESSAGE(CommonException, ConfigBadIntValue,
			"Invalid integer value for configuration key: " <<
			rKeyName << ": '" << value_str << "'");
	}

	return (int)value;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Configuration::GetKeyValueUint32(const std::string& rKeyName)
//		Purpose: Gets a key value as a 32-bit unsigned integer
//		Created: 2003/07/23
//
// --------------------------------------------------------------------------
uint32_t Configuration::GetKeyValueUint32(const std::string& rKeyName) const
{
	std::string value_str = GetKeyValue(rKeyName);
	errno = 0;
	long value = ::strtoul(value_str.c_str(), NULL, 0 /* C style handling */);

	if(errno != 0)
	{
		THROW_EXCEPTION_MESSAGE(CommonException, ConfigBadIntValue,
			"Invalid integer value for configuration key: " <<
			rKeyName << ": '" << value_str << "'");
	}

	return (int)value;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Configuration::GetKeyValueBool(const std::string&)
//		Purpose: Gets a key value as a boolean
//		Created: 17/2/04
//
// --------------------------------------------------------------------------
bool Configuration::GetKeyValueBool(const std::string& rKeyName) const
{
	std::string value_str = GetKeyValue(rKeyName);
	bool value = false;

	// Anything this is called for should have been verified as having a correct
	// string in the verification section. However, this does default to false
	// if it isn't in the string table.

	for(int l = 0; sValueBooleanStrings[l] != 0; ++l)
	{
		if(::strcasecmp(value_str.c_str(), sValueBooleanStrings[l]) == 0)
		{
			// Found.
			value = sValueBooleanValue[l];
			break;
		}
	}

	return value;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    Configuration::GetKeyNames()
//		Purpose: Returns list of key names
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
std::vector<std::string> Configuration::GetKeyNames() const
{
	std::map<std::string, std::string>::const_iterator i(mKeys.begin());
	std::vector<std::string> r;

	for(; i != mKeys.end(); ++i)
	{
		r.push_back(i->first);
	}

	return r;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Configuration::SubConfigurationExists(const
//			 std::string&)
//		Purpose: Checks to see if a sub configuration exists
//		Created: 2003/07/23
//
// --------------------------------------------------------------------------
bool Configuration::SubConfigurationExists(const std::string& rSubName) const
{
	// Attempt to find it...
	std::list<std::pair<std::string, Configuration> >::const_iterator i(mSubConfigurations.begin());

	for(; i != mSubConfigurations.end(); ++i)
	{
		// This the one?
		if(i->first == rSubName)
		{
			// Yes.
			return true;
		}
	}

	// didn't find it.
	return false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Configuration::GetSubConfiguration(const
//			 std::string&)
//		Purpose: Gets a sub configuration
//		Created: 2003/07/23
//
// --------------------------------------------------------------------------
const Configuration &Configuration::GetSubConfiguration(const std::string&
	rSubName) const
{
	// Attempt to find it...
	std::list<std::pair<std::string, Configuration> >::const_iterator i(mSubConfigurations.begin());

	for(; i != mSubConfigurations.end(); ++i)
	{
		// This the one?
		if(i->first == rSubName)
		{
			// Yes.
			return i->second;
		}
	}

	THROW_EXCEPTION_MESSAGE(CommonException, ConfigNoSubConfig,
		"Missing sub-configuration section: " << rSubName);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Configuration::GetSubConfiguration(const
//			 std::string&)
//		Purpose: Gets a sub configuration for editing
//		Created: 2008/08/12
//
// --------------------------------------------------------------------------
Configuration &Configuration::GetSubConfigurationEditable(const std::string&
	rSubName)
{
	// Attempt to find it...

	for(SubConfigListType::iterator
		i  = mSubConfigurations.begin();
		i != mSubConfigurations.end(); ++i)
	{
		// This the one?
		if(i->first == rSubName)
		{
			// Yes.
			return i->second;
		}
	}

	THROW_EXCEPTION_MESSAGE(CommonException, ConfigNoSubConfig,
		"Missing sub-configuration section: " << rSubName);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Configuration::GetSubConfigurationNames()
//		Purpose: Return list of sub configuration names
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
std::vector<std::string> Configuration::GetSubConfigurationNames() const
{
	std::list<std::pair<std::string, Configuration> >::const_iterator
		i(mSubConfigurations.begin());
	std::vector<std::string> r;

	for(; i != mSubConfigurations.end(); ++i)
	{
		r.push_back(i->first);
	}

	return r;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Configuration::Verify(const ConfigurationVerify &,
//			 const std::string &, std::string &)
//		Purpose: Checks that the configuration is valid according to the
//			 supplied verifier
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
bool Configuration::Verify(const ConfigurationVerify &rVerify,
	const std::string &rLevel, std::string &rErrorMsg)
{
	bool ok = true;

	// First... check the keys
	if(rVerify.mpKeys != 0)
	{
		const ConfigurationVerifyKey *pvkey = rVerify.mpKeys;

		bool todo = true;
		do
		{
			// Can the key be found?
			if(KeyExists(pvkey->Name()))
			{
				// Get value
				const std::string &rval = GetKeyValue(pvkey->Name());
				const char *val = rval.c_str();

				// Check it's a number?
				if((pvkey->Flags() & ConfigTest_IsInt) == ConfigTest_IsInt)
				{
					// Test it...
					char *end;
					long r = ::strtol(val, &end, 0);
					if(r == LONG_MIN || r == LONG_MAX || end != (val + rval.size()))
					{
						// not a good value
						ok = false;
						rErrorMsg += rLevel + mName + "." + pvkey->Name() + " (key) is not a valid integer.\n";
					}
				}

				// Check it's a number?
				if(pvkey->Flags() & ConfigTest_IsUint32)
				{
					// Test it...
					char *end;
					errno = 0;
					uint32_t r = ::strtoul(val, &end, 0);
					if(errno != 0 || end != (val + rval.size()))
					{
						// not a good value
						ok = false;
						rErrorMsg += rLevel + mName + "." + pvkey->Name() + " (key) is not a valid unsigned 32-bit integer.\n";
					}
				}

				// Check it's a bool?
				if((pvkey->Flags() & ConfigTest_IsBool) == ConfigTest_IsBool)
				{
					// See if it's one of the allowed strings.
					bool found = false;
					for(int l = 0; sValueBooleanStrings[l] != 0; ++l)
					{
						if(::strcasecmp(val, sValueBooleanStrings[l]) == 0)
						{
							// Found.
							found = true;
							break;
						}
					}

					// Error if it's not one of them.
					if(!found)
					{
						ok = false;
						rErrorMsg += rLevel + mName + "." + pvkey->Name() + " (key) is not a valid boolean value.\n";
					}
				}

				// Check for multi valued statments where they're not allowed
				if((pvkey->Flags() & ConfigTest_MultiValueAllowed) == 0)
				{
					// Check to see if this key is a multi-value -- it shouldn't be
					if(rval.find(MultiValueSeparator) != rval.npos)
					{
						ok = false;
						rErrorMsg += rLevel + mName +"." + pvkey->Name() + " (key) multi value not allowed (duplicated key?).\n";
					}
				}
			}
			else
			{
				// Is it required to exist?
				if((pvkey->Flags() & ConfigTest_Exists) == ConfigTest_Exists)
				{
					// Should exist, but doesn't.
					ok = false;
					rErrorMsg += rLevel + mName + "." + pvkey->Name() + " (key) is missing.\n";
				}
				else if(pvkey->HasDefaultValue())
				{
					mKeys[pvkey->Name()] =
						pvkey->DefaultValue();
				}
			}

			if((pvkey->Flags() & ConfigTest_LastEntry) == ConfigTest_LastEntry)
			{
				// No more!
				todo = false;
			}

			// next
			pvkey++;

		} while(todo);

		// Check for additional keys
		for(std::map<std::string, std::string>::const_iterator i = mKeys.begin();
			i != mKeys.end(); ++i)
		{
			// Is the name in the list?
			const ConfigurationVerifyKey *scan = rVerify.mpKeys;
			bool found = false;
			while(scan)
			{
				if(scan->Name() == i->first)
				{
					found = true;
					break;
				}

				// Next?
				if((scan->Flags() & ConfigTest_LastEntry) == ConfigTest_LastEntry)
				{
					break;
				}
				scan++;
			}

			if(!found)
			{
				// Shouldn't exist, but does.
				ok = false;
				rErrorMsg += rLevel + mName + "." + i->first + " (key) is not a known key. Check spelling and placement.\n";
			}
		}
	}

	// Then the sub configurations
	if(rVerify.mpSubConfigurations)
	{
		// Find the wildcard entry, if it exists, and check that required subconfigs are there
		const ConfigurationVerify *wildcardverify = 0;

		const ConfigurationVerify *scan = rVerify.mpSubConfigurations;
		while(scan)
		{
			if(scan->mName.length() > 0 && scan->mName[0] == '*')
			{
				wildcardverify = scan;
			}

			// Required?
			if((scan->Tests & ConfigTest_Exists) == ConfigTest_Exists)
			{
				if(scan->mName.length() > 0 &&
					scan->mName[0] == '*')
				{
					// Check something exists
					if(mSubConfigurations.size() < 1)
					{
						// A sub config should exist, but doesn't.
						ok = false;
						rErrorMsg += rLevel + mName + ".* (block) is missing (a block must be present).\n";
					}
				}
				else
				{
					// Check real thing exists
					if(!SubConfigurationExists(scan->mName))
					{
						// Should exist, but doesn't.
						ok = false;
						rErrorMsg += rLevel + mName + "." + scan->mName + " (block) is missing.\n";
					}
				}
			}

			// Next?
			if((scan->Tests & ConfigTest_LastEntry) == ConfigTest_LastEntry)
			{
				break;
			}
			scan++;
		}

		// Go through the sub configurations, one by one
		for(SubConfigListType::iterator
			i  = mSubConfigurations.begin();
			i != mSubConfigurations.end(); ++i)
		{
			// Can this be found?
			const ConfigurationVerify *subverify = 0;

			const ConfigurationVerify *scan = rVerify.mpSubConfigurations;
			const char *name = i->first.c_str();
			ASSERT(name);
			while(scan)
			{
				if(scan->mName == name)
				{
					// found it!
					subverify = scan;
				}

				// Next?
				if((scan->Tests & ConfigTest_LastEntry) == ConfigTest_LastEntry)
				{
					break;
				}
				scan++;
			}

			// Use wildcard?
			if(subverify == 0)
			{
				subverify = wildcardverify;
			}

			// Verify
			if(subverify)
			{
				// override const-ness here...
				if(!i->second.Verify(*subverify, mName + '.',
					rErrorMsg))
				{
					ok = false;
				}
			}
		}
	}

	return ok;
}


