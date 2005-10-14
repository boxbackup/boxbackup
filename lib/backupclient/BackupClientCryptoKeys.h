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

