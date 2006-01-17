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


