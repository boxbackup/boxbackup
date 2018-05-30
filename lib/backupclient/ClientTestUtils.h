// --------------------------------------------------------------------------
//
// File
//		Name:    ClientTestUtils.h
//		Purpose: Utilities for specialised client tests
//		Created: 2018-05-29
//
// --------------------------------------------------------------------------

#ifndef CLIENTTESTUTILS__H
#define CLIENTTESTUTILS__H

#include <string>

#include "Test.h"

#define SETUP_TEST_SPECIALISED(spec) \
	SETUP_SPECIALISED(spec.name()); \
	TEST_THAT_OR(setup_test_specialised(spec.name(), spec.control()), FAIL); \
	try \
	{ // left open for TEARDOWN_TEST_SPECIALISED()

#define _TEARDOWN_TEST_SPECIALISED(spec, check_for_errors) \
		TEST_THAT_OR(teardown_test_specialised(spec.name(), spec.control(), \
			check_for_errors), FAIL); \
	} \
	catch (BoxException &e) \
	{ \
		BOX_WARNING("Specialised test failed with exception, cleaning up: " << \
			spec.name() << ": " << e.what()); \
		TEST_THAT_OR(teardown_test_specialised(spec.name(), spec.control(), \
			check_for_errors), FAIL); \
		throw; \
	} \
	TEARDOWN();

#define TEARDOWN_TEST_SPECIALISED(spec) \
	_TEARDOWN_TEST_SPECIALISED(spec, true)

#define TEARDOWN_TEST_SPECIALISED_NO_CHECK(spec) \
	_TEARDOWN_TEST_SPECIALISED(spec, false)

class RaidAndS3TestSpecs
{
public:
	class Specialisation
	{
	private:
		std::string mName;
		std::auto_ptr<Configuration> mapConfig;
		std::auto_ptr<BackupAccountControl> mapControl;
	public:
		Specialisation(const std::string& name, std::auto_ptr<Configuration> config,
			std::auto_ptr<BackupAccountControl> control)
		: mName(name),
		  mapConfig(config),
		  mapControl(control)
		{ }
		Specialisation(Specialisation &&other) // move constructor
		: mName(other.mName),
		  mapConfig(other.mapConfig),
		  mapControl(other.mapControl)
		{ }
		const std::string& name() const { return mName; }
		Configuration& config() const { return *mapConfig; }
		BackupAccountControl& control() const { return *mapControl; }
		S3BackupAccountControl& s3_control() const
		{
			return dynamic_cast<S3BackupAccountControl &>(*mapControl);
		}
		BackupStoreAccountControl& store_control() const
		{
			return dynamic_cast<BackupStoreAccountControl &>(*mapControl);
		}
	};

private:
	std::list<Specialisation> mSpecialisations;
	Specialisation* mpS3;
	Specialisation* mpStore;

public:
	std::list<Specialisation>& specs() { return mSpecialisations; }
	Specialisation& s3() const { return *mpS3; }
	Specialisation& store() const { return *mpStore; }

	RaidAndS3TestSpecs(); // constructor
};

#endif // CLIENTTESTUTILS__H

