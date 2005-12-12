// --------------------------------------------------------------------------
//
// File
//		Name:    BoxPortsAndFiles.h
//		Purpose: Central list of which tcp/ip ports and hardcoded file locations
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#ifndef BOXPORTSANDFILES__H
#define BOXPORTSANDFILES__H

#define BOX_PORT_BASE		2200


// Backup store daemon
#define BOX_PORT_BBSTORED					(BOX_PORT_BASE+1)
#define BOX_FILE_BBSTORED_DEFAULT_CONFIG	"/etc/box/bbstored.conf"
// directory within the RAIDFILE root for the backup store daemon
#define BOX_RAIDFILE_ROOT_BBSTORED			"backup"

// Backup client daemon
#ifdef WIN32
#define BOX_FILE_BBACKUPD_DEFAULT_CONFIG	"C:\\Program Files\\Box Backup\\bbackupd.conf"
#else
#define BOX_FILE_BBACKUPD_DEFAULT_CONFIG	"/etc/box/bbackupd.conf"
#endif

// RaidFile conf location default
#define BOX_FILE_RAIDFILE_DEFAULT_CONFIG	"/etc/box/raidfile.conf"

// Default name of the named pipe
#define BOX_NAMED_PIPE_NAME L"\\\\.\\pipe\\boxbackup"

#endif // BOXPORTSANDFILES__H

