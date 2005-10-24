// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreObjectMagic.h
//		Purpose: Magic values for the start of objects in the backup store
//		Created: 19/11/03
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREOBJECTMAGIC__H
#define BACKUPSTOREOBJECTMAGIC__H

// Each of these values is the first 4 bytes of the object file.
// Remember to swap from network to host byte order.

// Magic value for file streams
#define OBJECTMAGIC_FILE_MAGIC_VALUE_V1		0x66696C65
// Do not use v0 in any new code!
#define OBJECTMAGIC_FILE_MAGIC_VALUE_V0		0x46494C45

// Magic for the block index at the file stream -- used to
// ensure streams are reordered as expected
#define OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1 0x62696478
// Do not use v0 in any new code!
#define OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V0 0x46426C6B

// Magic value for directory streams
#define OBJECTMAGIC_DIR_MAGIC_VALUE 		0x4449525F

#endif // BACKUPSTOREOBJECTMAGIC__H

