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
//		Name:    BackupClientCryptoKeys.cpp
//		Purpose: function for setting up all the backup client keys
//		Created: 1/12/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <string.h>

#include "BackupClientCryptoKeys.h"
#include "FileStream.h"
#include "BackupStoreFilenameClear.h"
#include "BackupStoreException.h"
#include "BackupClientFileAttributes.h"
#include "BackupStoreFile.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientCryptoKeys_Setup(const char *)
//		Purpose: Read in the key material file, and set keys to all the backup elements required.
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
void BackupClientCryptoKeys_Setup(const char *KeyMaterialFilename)
{
	// Read in the key material
	unsigned char KeyMaterial[BACKUPCRYPTOKEYS_FILE_SIZE];
	
	// Open the file
	FileStream file(KeyMaterialFilename);
	// Read in data
	if(!file.ReadFullBuffer(KeyMaterial, BACKUPCRYPTOKEYS_FILE_SIZE, 0))
	{
		THROW_EXCEPTION(BackupStoreException, CouldntLoadClientKeyMaterial)
	}
	
	// Tell the filename how to encrypt
	BackupStoreFilenameClear::SetBlowfishKey(KeyMaterial + BACKUPCRYPTOKEYS_FILENAME_KEY_START, BACKUPCRYPTOKEYS_FILENAME_KEY_LENGTH,
		KeyMaterial + BACKUPCRYPTOKEYS_FILENAME_IV_START, BACKUPCRYPTOKEYS_FILENAME_IV_LENGTH);
	BackupStoreFilenameClear::SetEncodingMethod(BackupStoreFilename::Encoding_Blowfish);

	// Tell the attributes how to encrypt
	BackupClientFileAttributes::SetBlowfishKey(KeyMaterial + BACKUPCRYPTOKEYS_ATTRIBUTES_KEY_START, BACKUPCRYPTOKEYS_ATTRIBUTES_KEY_LENGTH);
	// and the secret for hashing
	BackupClientFileAttributes::SetAttributeHashSecret(KeyMaterial + BACKUPCRYPTOKEYS_ATTRIBUTE_HASH_SECRET_START, BACKUPCRYPTOKEYS_ATTRIBUTE_HASH_SECRET_LENGTH);

	// Tell the files how to encrypt
	BackupStoreFile::SetBlowfishKeys(KeyMaterial + BACKUPCRYPTOKEYS_ATTRIBUTES_KEY_START, BACKUPCRYPTOKEYS_ATTRIBUTES_KEY_LENGTH,
		KeyMaterial + BACKUPCRYPTOKEYS_FILE_BLOCK_ENTRY_KEY_START, BACKUPCRYPTOKEYS_FILE_BLOCK_ENTRY_KEY_LENGTH);
#ifndef PLATFORM_OLD_OPENSSL
	// Use AES where available
	BackupStoreFile::SetAESKey(KeyMaterial + BACKUPCRYPTOKEYS_FILE_AES_KEY_START, BACKUPCRYPTOKEYS_FILE_AES_KEY_LENGTH);
#endif

	// Wipe the key material from memory
	::memset(KeyMaterial, 0, BACKUPCRYPTOKEYS_FILE_SIZE);
}



