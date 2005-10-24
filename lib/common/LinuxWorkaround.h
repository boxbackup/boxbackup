// --------------------------------------------------------------------------
//
// File
//		Name:    LinuxWorkaround.h
//		Purpose: Workarounds for Linux
//		Created: 2003/10/31
//
// --------------------------------------------------------------------------

#ifndef LINUXWORKAROUND__H
#define LINUXWORKAROUND__H

#ifdef PLATFORM_LINUX

void LinuxWorkaround_FinishDirentStruct(struct dirent *entry, const char *DirectoryName);

#endif // PLATFORM_LINUX

#endif // LINUXWORKAROUND__H

