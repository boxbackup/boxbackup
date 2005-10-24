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

// BOX_PRIVATE_BEGIN

// Locator daemon
#define BOX_PORT_BLOCATORD					(BOX_PORT_BASE+0)
#define BOX_FILE_BLOCATORD					"/etc/box/blocatord.conf"

// BOX_PRIVATE_END

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

// RaidFile conf location efault
#define BOX_FILE_RAIDFILE_DEFAULT_CONFIG	"/etc/box/raidfile.conf"

// BOX_PRIVATE_BEGIN

// smbpasswd authentication daemon (courier-imap authdaemon compatible)
#define BOX_PORT_BAUTHSMBPWD				(BOX_PORT_BASE+2)
#define BOX_FILE_BAUTHSMBPWD_DEFAULT_CONFIG	"/etc/box/bauthsmbpwd.conf"

// Message default config location
#define BOX_FILE_BOXMSG_DEFAULT_CONFIG		"/etc/box/boxmsg.conf"

// Message daemons
#define BOX_FILE_BMSGD_DEFAULT_CONFIG		"/etc/box/bmsgd.conf"
#define BOX_FILE_BMSGRECVD_DEFAULT_CONFIG	"/etc/box/bmsgrecvd.conf"
#define BOX_PORT_BMSGRECVD					(BOX_PORT_BASE+3)

// BOX_PRIVATE_END

#endif // BOXPORTSANDFILES__H

