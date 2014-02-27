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
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
class BackupProtocolLocal2 : public BackupProtocolLocal
{
private:
	BackupStoreContext mContext;

public:
	BackupProtocolLocal2(int32_t AccountNumber,
		const std::string& ConnectionDetails, 
		const std::string& AccountRootDir, int DiscSetNumber,
		bool ReadOnly)
	// This is rather ugly: the BackupProtocolLocal constructor must not
	// touch the Context, because it's not initialised yet!
	: BackupProtocolLocal(mContext),
	  mContext(AccountNumber, (HousekeepingInterface *)NULL,
		ConnectionDetails)
	{
		mContext.SetClientHasAccount(AccountRootDir, DiscSetNumber);
		QueryVersion(BACKUP_STORE_SERVER_VERSION);
		QueryLogin(0x01234567,
			ReadOnly ? BackupProtocolLogin::Flags_ReadOnly : 0);
	}
	virtual ~BackupProtocolLocal2() { }

	std::auto_ptr<BackupProtocolFinished> Query(const BackupProtocolFinished &rQuery)
	{
		std::auto_ptr<BackupProtocolFinished> finished =
			BackupProtocolLocal::Query(rQuery);
		mContext.ReleaseWriteLock();
		return finished;
	}
};

#endif // BACKUPPROTOCOL__H
