// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFileCryptVar.cpp
//		Purpose: Cryptographic keys for backup store files
//		Created: 12/1/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "BackupStoreFileCryptVar.h"
#include "BackupStoreFileWire.h"

#include "MemLeakFindOn.h"

CipherContext BackupStoreFileCryptVar::sBlowfishEncrypt;
CipherContext BackupStoreFileCryptVar::sBlowfishDecrypt;

#ifndef PLATFORM_OLD_OPENSSL
	CipherContext BackupStoreFileCryptVar::sAESEncrypt;
	CipherContext BackupStoreFileCryptVar::sAESDecrypt;
#endif

// Default to blowfish
CipherContext *BackupStoreFileCryptVar::spEncrypt = &BackupStoreFileCryptVar::sBlowfishEncrypt;
uint8_t BackupStoreFileCryptVar::sEncryptCipherType = HEADER_BLOWFISH_ENCODING;

CipherContext BackupStoreFileCryptVar::sBlowfishEncryptBlockEntry;
CipherContext BackupStoreFileCryptVar::sBlowfishDecryptBlockEntry;

