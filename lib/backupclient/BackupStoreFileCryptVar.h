// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFileCryptVar.h
//		Purpose: Cryptographic keys for backup store files
//		Created: 12/1/04
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREFILECRYPTVAR__H
#define BACKUPSTOREFILECRYPTVAR__H

#include "CipherContext.h"

// Hide private static variables from the rest of the world by putting them
// as static variables in a namespace.
// -- don't put them as static class variables to avoid openssl/evp.h being
// included all over the project.
namespace BackupStoreFileCryptVar
{
	// Keys for the main file data
	extern CipherContext sBlowfishEncrypt;
	extern CipherContext sBlowfishDecrypt;
	// Use AES when available
#ifndef PLATFORM_OLD_OPENSSL
	extern CipherContext sAESEncrypt;
	extern CipherContext sAESDecrypt;
#endif
	// How encoding will be done
	extern CipherContext *spEncrypt;
	extern uint8_t sEncryptCipherType;

	// Keys for the block indicies
	extern CipherContext sBlowfishEncryptBlockEntry;
	extern CipherContext sBlowfishDecryptBlockEntry;
}

#endif // BACKUPSTOREFILECRYPTVAR__H

