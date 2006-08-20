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

struct stat;

// set packing to one byte
#ifdef STRUCTURE_PACKING_FOR_WIRE_USE_HEADERS
#include "BeginStructPackForWire.h"
#else
BEGIN_STRUCTURE_PACKING_FOR_WIRE
#endif

#define ATTRIBUTETYPE_GENERIC_UNIX	1
#define ATTRIBUTETYPE_GENERIC_WINDOWS	2

#ifdef WIN32
#define ACL_FORMAT_MAGIC   0x31415927
#define ACL_FORMAT_VERSION 0x1
#endif

typedef struct 
{
	int32_t		AttributeType;
} attr_StreamFormat_Generic;

typedef struct 
{
	int32_t		AttributeType;
	u_int32_t	UID;
	u_int32_t	GID;
	u_int64_t	ModificationTime;
	u_int64_t	AttrModificationTime;
	u_int32_t	UserDefinedFlags;
	u_int32_t	FileGenerationNumber;
	u_int16_t	Mode;
	// Symbolic link filename may follow
	// Extended attribute (xattr) information may follow, format is:
	//   u_int32_t     Size of extended attribute block (excluding this word)
	// For each of NumberOfAttributes (sorted by AttributeName):
	//   u_int16_t     AttributeNameLength
	//   char          AttributeName[AttributeNameLength]
	//   u_int32_t     AttributeValueLength
	//   unsigned char AttributeValue[AttributeValueLength]
	// AttributeName is 0 terminated, AttributeValue is not (and may be binary data)
} attr_StreamFormat_Unix;

typedef struct 
{
	int32_t		AttributeType;
	u_int32_t	Attributes;
	u_int64_t	CreationTime;
	u_int64_t	LastWriteTime;
} attr_StreamFormat_Windows;
	
// This has wire packing so it's compatible across platforms
// Use wider than necessary sizes, just to be careful.
typedef struct
{
	int32_t uid, gid, mode;
} attributeHashData;

// Use default packing
#ifdef STRUCTURE_PACKING_FOR_WIRE_USE_HEADERS
#include "EndStructPackForWire.h"
#else
END_STRUCTURE_PACKING_FOR_WIRE
#endif

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
	void WriteAttributes(const char *Filename) const;

	bool IsSymLink() const;

	static void SetBlowfishKey(const void *pKey, int KeyLength);
	static void SetAttributeHashSecret(const void *pSecret, int SecretLength);
	
	static uint64_t GenerateAttributeHash(struct stat &st, const std::string &filename, const std::string &leafname);
	static void FillExtendedAttr(StreamableMemBlock &outputBlock, const char *Filename);

private:
	static void FillAttributes(StreamableMemBlock &outputBlock, const char *Filename, struct stat &st, bool ZeroModificationTimes);
	static void FillAttributesLink(StreamableMemBlock &outputBlock, const char *Filename, struct stat &st);

#ifdef WIN32
	static void FillAttributesWindows(StreamableMemBlock &rOutputBlock, 
		const char* pFilename, struct stat &rStat,
		bool ZeroModificationTimes);
	void WriteAttributesWindows(const char *Filename) const;
#endif

	void WriteExtendedAttr(const char *Filename, int xattrOffset) const;

	void RemoveClear() const;
	void EnsureClearAvailable() const;
	static StreamableMemBlock *MakeClear(const StreamableMemBlock &rEncrypted);
	void EncryptAttr(const StreamableMemBlock &rToEncrypt);

public:
	const StreamableMemBlock& GetAttributes()
	{
		EnsureClearAvailable();
		return *mpClearAttributes;
	}

private:
	mutable StreamableMemBlock *mpClearAttributes;
};

#endif // BACKUPCLIENTFILEATTRIBUTES__H

