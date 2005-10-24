// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFilenameClear.h
//		Purpose: BackupStoreFilenames in the clear
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREFILENAMECLEAR__H
#define BACKUPSTOREFILENAMECLEAR__H

#include "BackupStoreFilename.h"

class CipherContext;

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreFilenameClear
//		Purpose: BackupStoreFilenames, handling conversion from and to the in the clear version
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
class BackupStoreFilenameClear : public BackupStoreFilename
{
public:
	BackupStoreFilenameClear();
	BackupStoreFilenameClear(const std::string &rToEncode);
	BackupStoreFilenameClear(const BackupStoreFilenameClear &rToCopy);
	BackupStoreFilenameClear(const BackupStoreFilename &rToCopy);
	virtual ~BackupStoreFilenameClear();

	// Because we need to use a different allocator for this class to avoid
	// nasty things happening, can't return this as a reference. Which is a
	// pity. But probably not too bad.
#ifdef BACKUPSTOREFILEAME_MALLOC_ALLOC_BASE_TYPE
	const std::string GetClearFilename() const;
#else
	const std::string &GetClearFilename() const;
#endif
	void SetClearFilename(const std::string &rToEncode);

	// Setup for encryption of filenames	
	static void SetBlowfishKey(const void *pKey, int KeyLength, const void *pIV, int IVLength);
	static void SetEncodingMethod(int Method);
	
protected:
	void MakeClearAvailable() const;
	virtual void EncodedFilenameChanged();
	void EncryptClear(const std::string &rToEncode, CipherContext &rCipherContext, int StoreAsEncoding);
	void DecryptEncoded(CipherContext &rCipherContext) const;

private:
	mutable BackupStoreFilename_base mClearFilename;
};

#endif // BACKUPSTOREFILENAMECLEAR__H


