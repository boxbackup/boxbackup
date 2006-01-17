// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientFileAttributes.cpp
//		Purpose: Storage of file attributes
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "BackupClientFileAttributes.h"
#include "CommonException.h"
#include "FileModificationTime.h"
#include "BoxTimeToUnix.h"
#include "BackupStoreException.h"
#include "CipherContext.h"
#include "CipherBlowfish.h"
#include "MD5Digest.h"

#include "MemLeakFindOn.h"

// set packing to one byte
#ifdef STRUCTURE_PATCKING_FOR_WIRE_USE_HEADERS
#include "BeginStructPackForWire.h"
#else
BEGIN_STRUCTURE_PACKING_FOR_WIRE
#endif

#define ATTRIBUTETYPE_GENERIC_UNIX	1

#define ATTRIBUTE_ENCODING_BLOWFISH	2

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
} attr_StreamFormat;

// This has wire packing so it's compatible across platforms
// Use wider than necessary sizes, just to be careful.
typedef struct
{
	int32_t uid, gid, mode;
} attributeHashData;

// Use default packing
#ifdef STRUCTURE_PATCKING_FOR_WIRE_USE_HEADERS
#include "EndStructPackForWire.h"
#else
END_STRUCTURE_PACKING_FOR_WIRE
#endif


#define MAX_ATTRIBUTE_HASH_SECRET_LENGTH	256

// Hide private static variables from the rest of the world
// -- don't put them as static class variables to avoid openssl/evp.h being
// included all over the project.
namespace
{
	CipherContext sBlowfishEncrypt;
	CipherContext sBlowfishDecrypt;
	uint8_t sAttributeHashSecret[MAX_ATTRIBUTE_HASH_SECRET_LENGTH];
	int sAttributeHashSecretLength = 0;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::BackupClientFileAttributes()
//		Purpose: Default constructor
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
BackupClientFileAttributes::BackupClientFileAttributes()
	: mpClearAttributes(0)
{
	ASSERT(sizeof(u_int64_t) == sizeof(box_time_t));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::BackupClientFileAttributes(const BackupClientFileAttributes &)
//		Purpose: Copy constructor
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
BackupClientFileAttributes::BackupClientFileAttributes(const BackupClientFileAttributes &rToCopy)
	: StreamableMemBlock(rToCopy), // base class does the hard work
	  mpClearAttributes(0)
{
}
BackupClientFileAttributes::BackupClientFileAttributes(const StreamableMemBlock &rToCopy)
	: StreamableMemBlock(rToCopy), // base class does the hard work
	  mpClearAttributes(0)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::~BackupClientFileAttributes()
//		Purpose: Destructor
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
BackupClientFileAttributes::~BackupClientFileAttributes()
{
	if(mpClearAttributes)
	{
		delete mpClearAttributes;
		mpClearAttributes = 0;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes &operator=(const BackupClientFileAttributes &)
//		Purpose: Assignment operator
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
BackupClientFileAttributes &BackupClientFileAttributes::operator=(const BackupClientFileAttributes &rAttr)
{
	StreamableMemBlock::Set(rAttr);
	RemoveClear();	// make sure no decrypted version held
	return *this;
}
// Assume users play nice
BackupClientFileAttributes &BackupClientFileAttributes::operator=(const StreamableMemBlock &rAttr)
{
	StreamableMemBlock::Set(rAttr);
	RemoveClear();	// make sure no decrypted version held
	return *this;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::operator==(const BackupClientFileAttributes &)
//		Purpose: Comparison operator
//		Created: 2003/10/09
//
// --------------------------------------------------------------------------
bool BackupClientFileAttributes::operator==(const BackupClientFileAttributes &rAttr) const
{
	EnsureClearAvailable();
	rAttr.EnsureClearAvailable();

	return mpClearAttributes->operator==(*rAttr.mpClearAttributes);
}
// Too dangerous to allow -- put the two names the wrong way round, and it compares encrypted data.
/*bool BackupClientFileAttributes::operator==(const StreamableMemBlock &rAttr) const
{
	StreamableMemBlock *pDecoded = 0;

	try
	{
		EnsureClearAvailable();
		StreamableMemBlock *pDecoded = MakeClear(rAttr);

		// Compare using clear version
		bool compared = mpClearAttributes->operator==(rAttr);
		
		// Delete temporary
		delete pDecoded;
	
		return compared;
	}
	catch(...)
	{
		delete pDecoded;
		throw;
	}
}*/


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::Compare(const BackupClientFileAttributes &, bool)
//		Purpose: Compare, optionally ignoring the attribute modification time and/or modification time, and some data which is
//				 irrelevant in practise (eg file generation number)
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
bool BackupClientFileAttributes::Compare(const BackupClientFileAttributes &rAttr, bool IgnoreAttrModTime, bool IgnoreModTime) const
{
	EnsureClearAvailable();
	rAttr.EnsureClearAvailable();

	// Check sizes are the same, as a first check
	if(mpClearAttributes->GetSize() != rAttr.mpClearAttributes->GetSize())
	{
		return false;
	}
	
	// Then check the elements of the two things
	// Bytes are checked in network order, but this doesn't matter as we're only checking for equality.
	attr_StreamFormat *a1 = (attr_StreamFormat*)mpClearAttributes->GetBuffer();
	attr_StreamFormat *a2 = (attr_StreamFormat*)rAttr.mpClearAttributes->GetBuffer();
	
	if(a1->AttributeType != a2->AttributeType
		|| a1->UID != a2->UID
		|| a1->GID != a2->GID
		|| a1->UserDefinedFlags != a2->UserDefinedFlags
		|| a1->Mode != a2->Mode)
	{
		return false;
	}
	
	if(!IgnoreModTime)
	{
		if(a1->ModificationTime != a2->ModificationTime)
		{
			return false;
		}
	}

	if(!IgnoreAttrModTime)
	{
		if(a1->AttrModificationTime != a2->AttrModificationTime)
		{
			return false;
		}
	}
	
	// Check symlink string?
	unsigned int size = mpClearAttributes->GetSize();
	if(size > sizeof(attr_StreamFormat))
	{
		// Symlink strings don't match
		if(::memcmp(a1 + 1, a2 + 1, size - sizeof(attr_StreamFormat)) != 0)
		{
			return false;
		}
	}
	
	// Passes all test, must be OK
	return true;
}




// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::ReadAttributes(const char *)
//		Purpose: Read the attributes of the file, and store them ready for streaming.
//				 Optionally retrieve the modification time and attribute modification time.
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::ReadAttributes(const char *Filename, bool ZeroModificationTimes, box_time_t *pModTime,
	box_time_t *pAttrModTime, int64_t *pFileSize, ino_t *pInodeNumber, bool *pHasMultipleLinks)
{
	StreamableMemBlock *pnewAttr = 0;
	try
	{
		struct stat st;
		if(::lstat(Filename, &st) != 0)
		{
			THROW_EXCEPTION(CommonException, OSFileError)
		}
		
		// Modification times etc
		if(pModTime) {*pModTime = FileModificationTime(st);}
		if(pAttrModTime) {*pAttrModTime = FileAttrModificationTime(st);}
		if(pFileSize) {*pFileSize = st.st_size;}
		if(pInodeNumber) {*pInodeNumber = st.st_ino;}
		if(pHasMultipleLinks) {*pHasMultipleLinks = (st.st_nlink > 1);}
		
		// Is it a link?
		if((st.st_mode & S_IFMT) == S_IFLNK)
		{
			ReadAttributesLink(Filename, &st, ZeroModificationTimes);
			return;
		}
		ASSERT((st.st_mode & S_IFMT) != S_IFLNK);
	
		// Now, can allocate the block
		pnewAttr = new StreamableMemBlock(sizeof(attr_StreamFormat));
		
		// Fill in the entries
		attr_StreamFormat *pattr = (attr_StreamFormat*)pnewAttr->GetBuffer();
		
#define FILL_IN_ATTRIBUTES	\
		::memset(pattr, 0, sizeof(attr_StreamFormat));				\
		ASSERT(pattr != 0);											\
		pattr->AttributeType = htonl(ATTRIBUTETYPE_GENERIC_UNIX);	\
		pattr->UID = htonl(st.st_uid);								\
		pattr->GID = htonl(st.st_gid);								\
		if(ZeroModificationTimes)									\
		{															\
			pattr->ModificationTime = 0;							\
			pattr->AttrModificationTime = 0;						\
		}															\
		else														\
		{															\
			pattr->ModificationTime = hton64(FileModificationTime(st));	\
			pattr->AttrModificationTime = hton64(FileAttrModificationTime(st));	\
		}															\
		pattr->Mode = htons(st.st_mode);
#ifdef PLATFORM_stat_NO_st_flags
	#define FILL_IN_ATTRIBUTES_2	\
		pattr->UserDefinedFlags = 0;				\
		pattr->FileGenerationNumber = 0;
#else
	#define FILL_IN_ATTRIBUTES_2	\
		pattr->UserDefinedFlags = htonl(st.st_flags);				\
		pattr->FileGenerationNumber = htonl(st.st_gen);
#endif
		
		FILL_IN_ATTRIBUTES
		FILL_IN_ATTRIBUTES_2
		
		// Attributes ready. Encrypt into this block
		EncryptAttr(*pnewAttr);
		
		// Store the new attributes
		RemoveClear();
		mpClearAttributes = pnewAttr;
		pnewAttr = 0;
	}
	catch(...)
	{
		// clean up
		delete pnewAttr;
		pnewAttr = 0;
		throw;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::ReadAttributesLink()
//		Purpose: Private function, handles the case where a symbolic link is needed
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::ReadAttributesLink(const char *Filename, void *pst, bool ZeroModificationTimes)
{
	StreamableMemBlock *pnewAttr = 0;
	try
	{
		// Avoid needing to have struct stat available when including header file
		struct stat &st = *((struct stat *)pst);
		// Make sure we're only called for symbolic links
		ASSERT((st.st_mode & S_IFMT) == S_IFLNK);
	
		// Get the filename the link is linked to
		char linkedTo[PATH_MAX+4];
		int linkedToSize = ::readlink(Filename, linkedTo, PATH_MAX);
		if(linkedToSize == -1)
		{
			THROW_EXCEPTION(CommonException, OSFileError)
		}
	
		// Now, can allocate the block
		pnewAttr = new StreamableMemBlock(sizeof(attr_StreamFormat) + linkedToSize + 1);
		
		// Fill in the entries
		attr_StreamFormat *pattr = (attr_StreamFormat*)pnewAttr->GetBuffer();
	
		FILL_IN_ATTRIBUTES
		FILL_IN_ATTRIBUTES_2
	
		// Add the path name for the symbolic link
		::memcpy(pattr + 1, linkedTo, linkedToSize);
		// Make sure it's neatly terminated
		((char*)(pattr + 1))[linkedToSize] = '\0';
		
		// Attributes ready. Encrypt into this block
		EncryptAttr(*pnewAttr);
		
		// Store the new attributes
		RemoveClear();
		mpClearAttributes = pnewAttr;
		pnewAttr = 0;
	}
	catch(...)
	{
		// clean up
		delete pnewAttr;
		pnewAttr = 0;
		throw;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::WriteAttributes(const char *)
//		Purpose: Apply the stored attributes to the file
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::WriteAttributes(const char *Filename) const
{
	// Got something loaded
	if(GetSize() <= 0)
	{
		THROW_EXCEPTION(BackupStoreException, AttributesNotLoaded);
	}
	
	// Make sure there are clear attributes to use
	EnsureClearAvailable();
	ASSERT(mpClearAttributes != 0);

	// Check if the decrypted attributes are small enough, and the type of attributes stored
	if(mpClearAttributes->GetSize() < (int)sizeof(int32_t))
	{
		THROW_EXCEPTION(BackupStoreException, AttributesNotUnderstood);
	}
	int32_t *type = (int32_t*)mpClearAttributes->GetBuffer();
	ASSERT(type != 0);
	if(ntohl(*type) != ATTRIBUTETYPE_GENERIC_UNIX)
	{
		// Don't know what to do with these
		THROW_EXCEPTION(BackupStoreException, AttributesNotUnderstood);
	}
	
	// Check there is enough space for an attributes block
	if(mpClearAttributes->GetSize() < (int)sizeof(attr_StreamFormat))
	{
		// Too small
		THROW_EXCEPTION(BackupStoreException, AttributesNotLoaded);
	}

	// Get pointer to structure
	attr_StreamFormat *pattr = (attr_StreamFormat*)mpClearAttributes->GetBuffer();
	
	// is it a symlink?
	int16_t mode = ntohs(pattr->Mode);
	if((mode & S_IFMT) == S_IFLNK)
	{
		// Check things are sensible
		if(mpClearAttributes->GetSize() < (int)sizeof(attr_StreamFormat) + 1)
		{
			// Too small
			THROW_EXCEPTION(BackupStoreException, AttributesNotLoaded);
		}
	
		// Make a symlink, first deleting anything in the way
		::unlink(Filename);
		if(::symlink((char*)(pattr + 1), Filename) != 0)
		{
			THROW_EXCEPTION(CommonException, OSFileError)
		}
	}
	
	// If working as root, set user IDs
	if(::geteuid() == 0)
	{
		#ifdef PLATFORM_LCHOWN_NOT_SUPPORTED
			// only if not a link, can't set their owner on this platform
			if((mode & S_IFMT) != S_IFLNK)
			{
				// Not a link, use normal chown
				if(::chown(Filename, ntohl(pattr->UID), ntohl(pattr->GID)) != 0)
				{
					THROW_EXCEPTION(CommonException, OSFileError)
				}
			}
		#else
			if(::lchown(Filename, ntohl(pattr->UID), ntohl(pattr->GID)) != 0)	// use the version which sets things on symlinks
			{
				THROW_EXCEPTION(CommonException, OSFileError)
			}
		#endif
	}
	
	// Stop now if symlink, because otherwise it'll just be applied to the target
	if((mode & S_IFMT) == S_IFLNK)
	{
		return;
	}

	// Set modification time?
	box_time_t modtime = ntoh64(pattr->ModificationTime);
	if(modtime != 0)
	{
		// Work out times as timevals
		struct timeval times[2];
		BoxTimeToTimeval(modtime, times[1]);
		// Copy access time as well, why not, got to set it to something
		times[0] = times[1];
		// Attr modification time will be changed anyway, nothing that can be done about it
		
		// Try to apply
		if(::utimes(Filename, times) != 0)
		{
			THROW_EXCEPTION(CommonException, OSFileError)
		}
	}
	
	// Apply everything else... (allowable mode flags only)
	if(::chmod(Filename, mode & (S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID | S_ISGID | S_ISVTX)) != 0)	// mode must be done last (think setuid)
	{
		THROW_EXCEPTION(CommonException, OSFileError)
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::IsSymLink()
//		Purpose: Do these attributes represent a symbolic link?
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
bool BackupClientFileAttributes::IsSymLink() const
{
	EnsureClearAvailable();

	// Got the right kind of thing?
	if(mpClearAttributes->GetSize() < (int)sizeof(int32_t))
	{
		THROW_EXCEPTION(BackupStoreException, AttributesNotLoaded);
	}
	
	// Get the type of attributes stored
	int32_t *type = (int32_t*)mpClearAttributes->GetBuffer();
	ASSERT(type != 0);
	if(ntohl(*type) == ATTRIBUTETYPE_GENERIC_UNIX && mpClearAttributes->GetSize() > (int)sizeof(attr_StreamFormat))
	{
		// Check link
		attr_StreamFormat *pattr = (attr_StreamFormat*)mpClearAttributes->GetBuffer();
		return ((ntohs(pattr->Mode)) & S_IFMT) == S_IFLNK;
	}
	
	return false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::RemoveClear()
//		Purpose: Private. Deletes any clear version of the attributes that may be held
//		Created: 3/12/03
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::RemoveClear() const
{
	if(mpClearAttributes)
	{
		delete mpClearAttributes;
	}
	mpClearAttributes = 0;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::EnsureClearAvailable()
//		Purpose: Private. Makes sure the clear version is available
//		Created: 3/12/03
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::EnsureClearAvailable() const
{
	if(mpClearAttributes == 0)
	{
		mpClearAttributes = MakeClear(*this);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::MakeClear(const StreamableMemBlock &)
//		Purpose: Static. Decrypts stored attributes.
//		Created: 3/12/03
//
// --------------------------------------------------------------------------
StreamableMemBlock *BackupClientFileAttributes::MakeClear(const StreamableMemBlock &rEncrypted)
{
	// New block
	StreamableMemBlock *pdecrypted = 0;

	try
	{
		// Check the block is big enough for IV and header
		int ivSize = sBlowfishEncrypt.GetIVLength();
		if(rEncrypted.GetSize() <= (ivSize + 1))
		{
			THROW_EXCEPTION(BackupStoreException, BadEncryptedAttributes);
		}
		
		// How much space is needed for the output?
		int maxDecryptedSize = sBlowfishDecrypt.MaxOutSizeForInBufferSize(rEncrypted.GetSize() - ivSize);
		
		// Allocate it
		pdecrypted = new StreamableMemBlock(maxDecryptedSize);
	
		// ptr to block	
		uint8_t *encBlock = (uint8_t*)rEncrypted.GetBuffer();

		// Check that the header has right type
		if(encBlock[0] != ATTRIBUTE_ENCODING_BLOWFISH)
		{
			THROW_EXCEPTION(BackupStoreException, EncryptedAttributesHaveUnknownEncoding);
		}

		// Set IV
		sBlowfishDecrypt.SetIV(encBlock + 1);
		
		// Decrypt
		int decryptedSize = sBlowfishDecrypt.TransformBlock(pdecrypted->GetBuffer(), maxDecryptedSize, encBlock + 1 + ivSize, rEncrypted.GetSize() - (ivSize + 1));

		// Resize block to fit
		pdecrypted->ResizeBlock(decryptedSize);
	}
	catch(...)
	{
		delete pdecrypted;
		pdecrypted = 0;
	}

	return pdecrypted;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::SetBlowfishKey(const void *, int)
//		Purpose: Static. Sets the key to use for encryption and decryption.
//		Created: 3/12/03
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::SetBlowfishKey(const void *pKey, int KeyLength)
{
	// IVs set later
	sBlowfishEncrypt.Reset();
	sBlowfishEncrypt.Init(CipherContext::Encrypt, CipherBlowfish(CipherDescription::Mode_CBC, pKey, KeyLength));
	sBlowfishDecrypt.Reset();
	sBlowfishDecrypt.Init(CipherContext::Decrypt, CipherBlowfish(CipherDescription::Mode_CBC, pKey, KeyLength));
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::EncryptAttr(const StreamableMemBlock &)
//		Purpose: Private. Encrypt the given attributes into this block. 
//		Created: 3/12/03
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::EncryptAttr(const StreamableMemBlock &rToEncrypt)
{
	// Free any existing block
	FreeBlock();
	
	// Work out the maximum amount of space we need
	int maxEncryptedSize = sBlowfishEncrypt.MaxOutSizeForInBufferSize(rToEncrypt.GetSize());
	// And the size of the IV
	int ivSize = sBlowfishEncrypt.GetIVLength();
	
	// Allocate this space
	AllocateBlock(maxEncryptedSize + ivSize + 1);
	
	// Store the encoding byte
	uint8_t *block = (uint8_t*)GetBuffer();
	block[0] = ATTRIBUTE_ENCODING_BLOWFISH;
	
	// Generate and store an IV for this attribute block
	int ivSize2 = 0;
	const void *iv = sBlowfishEncrypt.SetRandomIV(ivSize2);
	ASSERT(ivSize == ivSize2);
	
	// Copy into the encrypted block
	::memcpy(block + 1, iv, ivSize);
	
	// Do the transform
	int encrytedSize = sBlowfishEncrypt.TransformBlock(block + 1 + ivSize, maxEncryptedSize, rToEncrypt.GetBuffer(), rToEncrypt.GetSize());

	// Resize this block
	ResizeBlock(encrytedSize + ivSize + 1);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::SetAttributeHashSecret(const void *, int)
//		Purpose: Set the secret for the filename attribute hash
//		Created: 25/4/04
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::SetAttributeHashSecret(const void *pSecret, int SecretLength)
{
	if(SecretLength > (int)sizeof(sAttributeHashSecret))
	{
		SecretLength = sizeof(sAttributeHashSecret);
	}
	if(SecretLength < 0)
	{
		THROW_EXCEPTION(BackupStoreException, Internal)
	}
	
	// Copy
	::memcpy(sAttributeHashSecret, pSecret, SecretLength);
	sAttributeHashSecretLength = SecretLength;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::GenerateAttributeHash(struct stat &, const std::string &)
//		Purpose: Generate a 64 bit hash from the attributes, used to detect changes.
//				 Include filename in the hash, so that it changes from one file to another,
//				 so don't reveal identical attributes.
//		Created: 25/4/04
//
// --------------------------------------------------------------------------
uint64_t BackupClientFileAttributes::GenerateAttributeHash(struct stat &st, const std::string &rFilename)
{
	if(sAttributeHashSecretLength == 0)
	{
		THROW_EXCEPTION(BackupStoreException, AttributeHashSecretNotSet)
	}
	
	// Assemble stuff we're interested in
	attributeHashData hashData;
	memset(&hashData, 0, sizeof(hashData));
	// Use network byte order and large sizes to be cross platform
	hashData.uid = htonl(st.st_uid);
	hashData.gid = htonl(st.st_gid);
	hashData.mode = htonl(st.st_mode);

	// Create a MD5 hash of the data, filename, and secret
	MD5Digest digest;
	digest.Add(&hashData, sizeof(hashData));
	digest.Add(rFilename.c_str(), rFilename.size());
	digest.Add(sAttributeHashSecret, sAttributeHashSecretLength);
	digest.Finish();
	
	// Return the first 64 bits of the hash
	uint64_t result = *((uint64_t *)(digest.DigestAsData()));
	return result;
}



