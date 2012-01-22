// --------------------------------------------------------------------------
//
// File
//		Name:    FileModificationTime.h
//		Purpose: Function for getting file modification time.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------

#ifndef FILEMODIFICATIONTIME__H
#define FILEMODIFICATIONTIME__H

#include <sys/stat.h>

#include "BoxTime.h"

box_time_t FileModificationTime(const EMU_STRUCT_STAT &st);
box_time_t FileAttrModificationTime(const EMU_STRUCT_STAT &st);
box_time_t FileModificationTimeMaxModAndAttr(const EMU_STRUCT_STAT &st);

#endif // FILEMODIFICATIONTIME__H

