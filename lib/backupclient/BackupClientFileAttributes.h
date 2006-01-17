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
		ino_t *pInodeNumber = 0, bool *pHasMultipleLinks = 0);
	void WriteAttributes(const char *Filename) const;

	bool IsSymLink() const;

	static void SetBlowfishKey(const void *pKey, int KeyLength);
	static void SetAttributeHashSecret(const void *pSecret, int SecretLength);
	
	static uint64_t GenerateAttributeHash(struct stat &st, const std::string &rFilename);

private:
	void ReadAttributesLink(const char *Filename, void *pst, bool ZeroModificationTimes);

	void RemoveClear() const;
	void EnsureClearAvailable() const;
	static StreamableMemBlock *MakeClear(const StreamableMemBlock &rEncrypted);
	void EncryptAttr(const StreamableMemBlock &rToEncrypt);

private:
	mutable StreamableMemBlock *mpClearAttributes;
};

#endif // BACKUPCLIENTFILEATTRIBUTES__H

