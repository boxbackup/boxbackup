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
void BackupClientCryptoKeys_Setup(const std::string& rKeyMaterialFilename)
{
	// Read in the key material
	unsigned char KeyMaterial[BACKUPCRYPTOKEYS_FILE_SIZE];
	
#ifdef WIN32
	DWORD gle;
	HKEY hKey;

	(void)rKeyMaterialFilename;

	if(ERROR_SUCCESS != (gle = RegOpenKeyEx(HKEY_LOCAL_MACHINE,"Software\\Box Backup",0,KEY_QUERY_VALUE,&hKey)))
	{
		THROW_EXCEPTION(BackupStoreException, CouldntLoadClientKeyMaterial)
	}
	else
	{
		DWORD lenKeyMaterial = BACKUPCRYPTOKEYS_FILE_SIZE;

		gle = RegQueryValueEx(hKey,"FileEncKeys",NULL,NULL,KeyMaterial,&lenKeyMaterial);
		printf("gle: %d\n",gle);
		RegCloseKey(hKey);

		if(ERROR_SUCCESS != gle)
		{
			THROW_EXCEPTION(BackupStoreException, CouldntLoadClientKeyMaterial)
		}
	}
#else
	// Open the file
	FileStream file(rKeyMaterialFilename);

	// Read in data
	if(!file.ReadFullBuffer(KeyMaterial, BACKUPCRYPTOKEYS_FILE_SIZE, 0))
	{
		THROW_EXCEPTION(BackupStoreException, CouldntLoadClientKeyMaterial)
	}
#endif

	// Setup keys and encoding method for filename encryption
	BackupStoreFilenameClear::SetBlowfishKey(
		KeyMaterial + BACKUPCRYPTOKEYS_FILENAME_KEY_START,
		BACKUPCRYPTOKEYS_FILENAME_KEY_LENGTH,
		KeyMaterial + BACKUPCRYPTOKEYS_FILENAME_IV_START,
		BACKUPCRYPTOKEYS_FILENAME_IV_LENGTH);
	BackupStoreFilenameClear::SetEncodingMethod(
		BackupStoreFilename::Encoding_Blowfish);

	// Setup key for attributes encryption
	BackupClientFileAttributes::SetBlowfishKey(
		KeyMaterial + BACKUPCRYPTOKEYS_ATTRIBUTES_KEY_START, 
		BACKUPCRYPTOKEYS_ATTRIBUTES_KEY_LENGTH);

	// Setup secret for attribute hashing
	BackupClientFileAttributes::SetAttributeHashSecret(
		KeyMaterial + BACKUPCRYPTOKEYS_ATTRIBUTE_HASH_SECRET_START,
		BACKUPCRYPTOKEYS_ATTRIBUTE_HASH_SECRET_LENGTH);

	// Setup keys for file data encryption
	BackupStoreFile::SetBlowfishKeys(
		KeyMaterial + BACKUPCRYPTOKEYS_ATTRIBUTES_KEY_START,
		BACKUPCRYPTOKEYS_ATTRIBUTES_KEY_LENGTH,
		KeyMaterial + BACKUPCRYPTOKEYS_FILE_BLOCK_ENTRY_KEY_START,
		BACKUPCRYPTOKEYS_FILE_BLOCK_ENTRY_KEY_LENGTH);

#ifndef HAVE_OLD_SSL
	// Use AES where available
	BackupStoreFile::SetAESKey(
		KeyMaterial + BACKUPCRYPTOKEYS_FILE_AES_KEY_START,
		BACKUPCRYPTOKEYS_FILE_AES_KEY_LENGTH);
#endif

	// Wipe the key material from memory
	#ifdef _MSC_VER // not defined on MinGW
		SecureZeroMemory(KeyMaterial, BACKUPCRYPTOKEYS_FILE_SIZE);
	#else
		::memset(KeyMaterial, 0, BACKUPCRYPTOKEYS_FILE_SIZE);
	#endif
}

