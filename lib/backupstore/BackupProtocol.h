// --------------------------------------------------------------------------
//
// File
//		Name:    BackupProtocol.h
//		Purpose: A thin wrapper around autogen_BackupProtocol.h
//		Created: 2014/01/05
//
// --------------------------------------------------------------------------

#ifndef BACKUPPROTOCOL__H
#define BACKUPPROTOCOL__H

#include <string>

#include "autogen_BackupProtocol.h"
#include "BackupStoreConstants.h"
#include "BackupStoreContext.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupProtocolLocal2
//		Purpose: BackupProtocolLocal with a few more IQ points
//		Created: 2014/09/20
//
// --------------------------------------------------------------------------
class BackupProtocolLocal2 : public BackupProtocolLocal
{
private:
	std::auto_ptr<BackupStoreContext> mapLocalContext;
	int32_t mAccountNumber;
	bool mReadOnly;
	int64_t mClientStoreMarker;

public:
	BackupProtocolLocal2(int32_t AccountNumber,
		const std::string& ConnectionDetails,
		const std::string& AccountRootDir, int DiscSetNumber,
		bool ReadOnly)
	// This is rather ugly: we need to pass a reference to a context to
	// BackupProtocolLocal(), and we want it to be one that we've created ourselves,
	// so we create one with new(), dereference it to pass the reference to the
	// superclass, and then get the reference out again, take its address and stick
	// that into the auto_ptr, which will delete it when we are destroyed.
	: BackupProtocolLocal(
		*(new BackupStoreContext(AccountNumber, (HousekeepingInterface *)NULL,
			ConnectionDetails))
		),
	  mapLocalContext(&GetContext()),
	  mAccountNumber(AccountNumber),
	  mReadOnly(ReadOnly)
	{
		GetContext().SetClientHasAccount(AccountRootDir, DiscSetNumber);
		QueryVersion(BACKUP_STORE_SERVER_VERSION);
		mClientStoreMarker = QueryLogin(AccountNumber,
			ReadOnly ? BackupProtocolLogin::Flags_ReadOnly : 0)->GetClientStoreMarker();
	}

	BackupProtocolLocal2(BackupStoreContext& rContext, int32_t AccountNumber,
		bool ReadOnly, bool login = true)
	: BackupProtocolLocal(rContext),
	  mAccountNumber(AccountNumber),
	  mReadOnly(ReadOnly)
	{
		GetContext().SetClientHasAccount();
		QueryVersion(BACKUP_STORE_SERVER_VERSION);
		if(login)
		{
			mClientStoreMarker = QueryLogin(AccountNumber,
				ReadOnly ? BackupProtocolLogin::Flags_ReadOnly : 0
			)->GetClientStoreMarker();
		}
	}

	std::auto_ptr<BackupProtocolFinished> Query(const BackupProtocolFinished &rQuery)
	{
		std::auto_ptr<BackupProtocolFinished> finished =
			BackupProtocolLocal::Query(rQuery);
		GetContext().CleanUp();
		return finished;
	}
	using BackupProtocolLocal::Query;

	BackupStoreContext& GetContext() // make public, for tests
	{
		return BackupProtocolLocal::GetContext();
	}

	void Reopen()
	{
		QueryVersion(BACKUP_STORE_SERVER_VERSION);
		QueryLogin(mAccountNumber,
			mReadOnly ? BackupProtocolLogin::Flags_ReadOnly : 0);
	}

	// Returns the initial client store marker (at login time), not the current one if changed
	// using the SetClientStoreMarker command!
	int64_t GetClientStoreMarker()
	{
		return mClientStoreMarker;
	}
};

#endif // BACKUPPROTOCOL__H
