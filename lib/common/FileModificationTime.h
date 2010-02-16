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

box_time_t FileModificationTime(EMU_STRUCT_STAT &st);
box_time_t FileAttrModificationTime(EMU_STRUCT_STAT &st);
box_time_t FileModificationTimeMaxModAndAttr(EMU_STRUCT_STAT &st);

#endif // FILEMODIFICATIONTIME__H

