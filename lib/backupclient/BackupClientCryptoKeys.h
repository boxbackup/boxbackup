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
//		Name:    BackupClientCryptoKeys.h
//		Purpose: Format of crypto keys file, and function for setting everything up
//		Created: 1/12/03
//
// --------------------------------------------------------------------------

#ifndef BACKUPCLIENTCRYTOKEYS__H
#define BACKUPCLIENTCRYTOKEYS__H


// All keys are the maximum size that Blowfish supports. Since only the
// setup time is affected by key length (encryption same speed whatever)
// there is no disadvantage to using long keys as they are never
// transmitted and are static over long periods of time.


// All sizes in bytes. Some gaps deliberately left in the used material.

// How long the key material file is expected to be
#define BACKUPCRYPTOKEYS_FILE_SIZE						1024

// key for encrypting filenames (448 bits)
#define BACKUPCRYPTOKEYS_FILENAME_KEY_START				0
#define BACKUPCRYPTOKEYS_FILENAME_KEY_LENGTH			56
#define BACKUPCRYPTOKEYS_FILENAME_IV_START				(0 + BACKUPCRYPTOKEYS_FILENAME_KEY_LENGTH)
#define BACKUPCRYPTOKEYS_FILENAME_IV_LENGTH				8

// key for encrypting attributes (448 bits)
#define BACKUPCRYPTOKEYS_ATTRIBUTES_KEY_START			(BACKUPCRYPTOKEYS_FILENAME_KEY_START+64)
#define BACKUPCRYPTOKEYS_ATTRIBUTES_KEY_LENGTH			56

// Blowfish key for encrypting file data (448 bits (max blowfish key length))
#define BACKUPCRYPTOKEYS_FILE_KEY_START					(BACKUPCRYPTOKEYS_ATTRIBUTES_KEY_START+64)
#define BACKUPCRYPTOKEYS_FILE_KEY_LENGTH				56

// key for encrypting file block index entries
#define BACKUPCRYPTOKEYS_FILE_BLOCK_ENTRY_KEY_START		(BACKUPCRYPTOKEYS_FILE_KEY_START+64)
#define BACKUPCRYPTOKEYS_FILE_BLOCK_ENTRY_KEY_LENGTH	56

// Secret for hashing attributes
#define BACKUPCRYPTOKEYS_ATTRIBUTE_HASH_SECRET_START	(BACKUPCRYPTOKEYS_FILE_BLOCK_ENTRY_KEY_START+64)
#define BACKUPCRYPTOKEYS_ATTRIBUTE_HASH_SECRET_LENGTH	128

// AES key for encrypting file data (256 bits (max AES key length))
#define BACKUPCRYPTOKEYS_FILE_AES_KEY_START				(BACKUPCRYPTOKEYS_ATTRIBUTE_HASH_SECRET_START+128)
#define BACKUPCRYPTOKEYS_FILE_AES_KEY_LENGTH			32


void BackupClientCryptoKeys_Setup(const char *KeyMaterialFilename);

#endif // BACKUPCLIENTCRYTOKEYS__H

