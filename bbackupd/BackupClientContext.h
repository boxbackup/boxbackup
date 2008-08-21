// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientContext.h
//		Purpose: Keep track of context
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#ifndef BACKUPCLIENTCONTEXT__H
#define BACKUPCLIENTCONTEXT__H

#include "BoxTime.h"
#include "BackupClientDeleteList.h"
#include "BackupClientDirectoryRecord.h"
#include "BackupStoreFile.h"
#include "ExcludeList.h"
#include "Timer.h"

class TLSContext;
class BackupProtocolClient;
class SocketStreamTLS;
class BackupClientInodeToIDMap;
class BackupDaemon;
class BackupStoreFilenameClear;

#include <string>

// --------------------------------------------------------------------------
//
// Class
//		Name:    LocationResolver
//		Purpose: Interface for classes that can resolve locations to paths,
//		         like BackupDaemon
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
class LocationResolver
{
public:
	virtual ~LocationResolver() { }
	virtual bool FindLocationPathName(const std::string &rLocationName, 
		std::string &rPathOut) const = 0;
};


// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupClientContext
//		Purpose: 
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
class BackupClientContext : public DiffTimer
{
public:
	BackupClientContext
	(
		LocationResolver &rResolver, 
		TLSContext &rTLSContext, 
		const std::string &rHostname,
		int32_t Port,
		int32_t AccountNumber, 
		bool ExtendedLogging,
		bool ExtendedLogToFile,
		std::string ExtendedLogFile,
		ProgressNotifier &rProgressNotifier
	);
	virtual ~BackupClientContext();
private:
	BackupClientContext(const BackupClientContext &);
public:

	BackupProtocolClient &GetConnection();
	
	void CloseAnyOpenConnection();
	
	int GetTimeout() const;
	
	BackupClientDeleteList &GetDeleteList();
	void PerformDeletions();

	enum
	{
		ClientStoreMarker_NotKnown = 0
	};

	void SetClientStoreMarker(int64_t ClientStoreMarker) {mClientStoreMarker = ClientStoreMarker;}
	int64_t GetClientStoreMarker() const {return mClientStoreMarker;}
	
	bool StorageLimitExceeded() {return mStorageLimitExceeded;}
	void SetStorageLimitExceeded() {mStorageLimitExceeded = true;}

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::SetIDMaps(const BackupClientInodeToIDMap *, BackupClientInodeToIDMap *)
	//		Purpose: Store pointers to the Current and New ID maps
	//		Created: 11/11/03
	//
	// --------------------------------------------------------------------------
	void SetIDMaps(const BackupClientInodeToIDMap *pCurrent, BackupClientInodeToIDMap *pNew)
	{
		ASSERT(pCurrent != 0);
		ASSERT(pNew != 0);
		mpCurrentIDMap = pCurrent;
		mpNewIDMap = pNew;
	}
	const BackupClientInodeToIDMap &GetCurrentIDMap() const;
	BackupClientInodeToIDMap &GetNewIDMap() const;
	
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::SetExcludeLists(ExcludeList *, ExcludeList *)
	//		Purpose: Sets the exclude lists for the operation. Can be 0.
	//		Created: 28/1/04
	//
	// --------------------------------------------------------------------------
	void SetExcludeLists(ExcludeList *pExcludeFiles, ExcludeList *pExcludeDirs)
	{
		mpExcludeFiles = pExcludeFiles;
		mpExcludeDirs = pExcludeDirs;
	}
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::ExcludeFile(const std::string &)
	//		Purpose: Returns true is this file should be excluded from the backup
	//		Created: 28/1/04
	//
	// --------------------------------------------------------------------------
	inline bool ExcludeFile(const std::string &rFullFilename)
	{
		if(mpExcludeFiles != 0)
		{
			return mpExcludeFiles->IsExcluded(rFullFilename);
		}
		// If no list, don't exclude anything
		return false;
	}
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::ExcludeDir(const std::string &)
	//		Purpose: Returns true is this directory should be excluded from the backup
	//		Created: 28/1/04
	//
	// --------------------------------------------------------------------------
	inline bool ExcludeDir(const std::string &rFullDirName)
	{
		if(mpExcludeDirs != 0)
		{
			return mpExcludeDirs->IsExcluded(rFullDirName);
		}
		// If no list, don't exclude anything
		return false;
	}

	// Utility functions -- may do a lot of work
	bool FindFilename(int64_t ObjectID, int64_t ContainingDirectory, std::string &rPathOut, bool &rIsDirectoryOut,
		bool &rIsCurrentVersionOut, box_time_t *pModTimeOnServer = 0, box_time_t *pAttributesHashOnServer = 0,
		BackupStoreFilenameClear *pLeafname = 0); // not const as may connect to server

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::SetMaximumDiffingTime()
	//		Purpose: Sets the maximum time that will be spent diffing a file
	//		Created: 04/19/2005
	//
	// --------------------------------------------------------------------------
	void SetMaximumDiffingTime(int iSeconds);

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::SetKeepAliveTime()
	//		Purpose: Sets the time interval for repetitive keep-alive operation
	//		Created: 04/19/2005
	//
	// --------------------------------------------------------------------------
	void SetKeepAliveTime(int iSeconds);

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::ManageDiffProcess()
	//		Purpose: Initiates an SSL connection/session keep-alive process
	//		Created: 04/19/2005
	//
	// --------------------------------------------------------------------------
	void ManageDiffProcess();

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::UnManageDiffProcess()
	//		Purpose: Suspends an SSL connection/session keep-alive process
	//		Created: 04/19/2005
	//
	// --------------------------------------------------------------------------
	void UnManageDiffProcess();

	// -------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::DoKeepAlive()
	//		Purpose: Check whether it's time to send a KeepAlive
	//			 message over the SSL link, and if so, send it.
	//		Created: 04/19/2005
	//
	// -------------------------------------------------------------------
	virtual void   DoKeepAlive();
	virtual int    GetMaximumDiffingTime();
	virtual bool   IsManaged() { return mbIsManaged; }
	
	ProgressNotifier& GetProgressNotifier() const 
	{ 
		return mrProgressNotifier;
	}

private:
	LocationResolver &mrResolver;
	TLSContext &mrTLSContext;
	std::string mHostname;
	int mPort;
	int32_t mAccountNumber;
	SocketStreamTLS *mpSocket;
	BackupProtocolClient *mpConnection;
	bool mExtendedLogging;
	bool mExtendedLogToFile;
	std::string mExtendedLogFile;
	FILE* mpExtendedLogFileHandle;
	int64_t mClientStoreMarker;
	BackupClientDeleteList *mpDeleteList;
	const BackupClientInodeToIDMap *mpCurrentIDMap;
	BackupClientInodeToIDMap *mpNewIDMap;
	bool mStorageLimitExceeded;
	ExcludeList *mpExcludeFiles;
	ExcludeList *mpExcludeDirs;
	Timer mKeepAliveTimer;
	bool mbIsManaged;
	int mKeepAliveTime;
	int mMaximumDiffingTime;
	ProgressNotifier &mrProgressNotifier;
};

#endif // BACKUPCLIENTCONTEXT__H
