// --------------------------------------------------------------------------
//
// File
//		Name:    ProtocolWire.h
//		Purpose: On the wire structures for Protocol
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------

#ifndef PROTOCOLWIRE__H
#define PROTOCOLWIRE__H

#include <sys/types.h>

// set packing to one byte
#ifdef STRUCTURE_PATCKING_FOR_WIRE_USE_HEADERS
#include "BeginStructPackForWire.h"
#else
BEGIN_STRUCTURE_PACKING_FOR_WIRE
#endif

typedef struct
{
	char mIdent[32];
} PW_Handshake;

typedef struct
{
	u_int32_t	mObjSize;
	u_int32_t	mObjType;
} PW_ObjectHeader;

#define SPECIAL_STREAM_OBJECT_TYPE		0xffffffff

// Use default packing
#ifdef STRUCTURE_PATCKING_FOR_WIRE_USE_HEADERS
#include "EndStructPackForWire.h"
#else
END_STRUCTURE_PACKING_FOR_WIRE
#endif

#endif // PROTOCOLWIRE__H

