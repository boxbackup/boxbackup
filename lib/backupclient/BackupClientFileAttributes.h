// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientFileAttributes.h
//		Purpose: Storage of file attributes
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------

#ifndef BACKUPCLIENTFILEATTRIBUTES__H
#define BACKUPCLIENTFILEATTRIBUTES__H

#include <string>

#include "StreamableMemBlock.h"
#include "BoxTime.h"

EMU_STRUCT_STAT; // declaration

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupClientFileAttributes
//		Purpose: Storage, streaming and application of file attributes
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
class BackupClientFileAttributes : public StreamableMemBlock
{
public:
	BackupClientFileAttributes();
	BackupClientFileAttributes(const BackupClientFileAttributes &rToCopy);
	BackupClientFileAttributes(const StreamableMemBlock &rToCopy);
	~BackupClientFileAttributes();
	BackupClientFileAttributes &operator=(const BackupClientFileAttributes &rAttr);
	BackupClientFileAttributes &operator=(const StreamableMemBlock &rAttr);
	bool operator==(const BackupClientFileAttributes &rAttr) const;
//	bool operator==(const StreamableMemBlock &rAttr) const; // too dangerous?

	bool Compare(const BackupClientFileAttributes &rAttr, bool IgnoreAttrModTime = false, bool IgnoreModTime = false) const;
	
	// Prevent access to base class members accidently
	void Set();

	void ReadAttributes(const char *Filename, bool ZeroModificationTimes = false,
		box_time_t *pModTime = 0, box_time_t *pAttrModTime = 0, int64_t *pFileSize = 0,
		InodeRefType *pInodeNumber = 0, bool *pHasMultipleLinks = 0);
	void WriteAttributes(const char *Filename, 
		bool MakeUserWritable = false) const;

	bool IsSymLink() const;

	static void SetBlowfishKey(const void *pKey, int KeyLength);
	static void SetAttributeHashSecret(const void *pSecret, int SecretLength);
	
	static uint64_t GenerateAttributeHash(EMU_STRUCT_STAT &st, const std::string &filename, const std::string &leafname);
	static void FillExtendedAttr(StreamableMemBlock &outputBlock, const char *Filename);

private:
	static void FillAttributes(StreamableMemBlock &outputBlock,
		const char *Filename, EMU_STRUCT_STAT &st,
		bool ZeroModificationTimes);
	static void FillAttributesLink(StreamableMemBlock &outputBlock, const char *Filename, struct stat &st);
	void WriteExtendedAttr(const char *Filename, int xattrOffset) const;

	void RemoveClear() const;
	void EnsureClearAvailable() const;
	static StreamableMemBlock *MakeClear(const StreamableMemBlock &rEncrypted);
	void EncryptAttr(const StreamableMemBlock &rToEncrypt);

private:
	mutable StreamableMemBlock *mpClearAttributes;
};

#endif // BACKUPCLIENTFILEATTRIBUTES__H

