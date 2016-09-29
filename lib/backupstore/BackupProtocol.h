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

#include <autogen_BackupProtocol.h>
#include <BackupStoreConstants.h>
#include <BackupStoreContext.h>

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
		QueryLogin(AccountNumber,
			ReadOnly ? BackupProtocolLogin::Flags_ReadOnly : 0);
	}

	BackupProtocolLocal2(BackupStoreContext& rContext, int32_t AccountNumber,
		bool ReadOnly)
	: BackupProtocolLocal(rContext),
	  mAccountNumber(AccountNumber),
	  mReadOnly(ReadOnly)
	{
		GetContext().SetClientHasAccount();
		QueryVersion(BACKUP_STORE_SERVER_VERSION);
		QueryLogin(AccountNumber,
			ReadOnly ? BackupProtocolLogin::Flags_ReadOnly : 0);
	}
	virtual ~BackupProtocolLocal2() { }

	std::auto_ptr<BackupProtocolFinished> Query(const BackupProtocolFinished &rQuery)
	{
		std::auto_ptr<BackupProtocolFinished> finished =
			BackupProtocolLocal::Query(rQuery);
		GetContext().CleanUp();
		return finished;
	}

	void Reopen()
	{
		QueryVersion(BACKUP_STORE_SERVER_VERSION);
		QueryLogin(mAccountNumber,
			mReadOnly ? BackupProtocolLogin::Flags_ReadOnly : 0);
	}
};

#endif // BACKUPPROTOCOL__H
