/*************************************************************************************************
 * The extended advanced API of QDBM
 *                                                      Copyright (C) 2000-2007 Mikio Hirabayashi
 * This file is part of QDBM, Quick Database Manager.
 * QDBM is free software; you can redistribute it and/or modify it under the terms of the GNU
 * Lesser General Public License as published by the Free Software Foundation; either version
 * 2.1 of the License or any later version.  QDBM is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 * You should have received a copy of the GNU Lesser General Public License along with QDBM; if
 * not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA.
 *************************************************************************************************/


#ifndef _VISTA_H                         /* duplication check */
#define _VISTA_H

#if defined(__cplusplus)                 /* export for C++ */
extern "C" {
#endif



/*************************************************************************************************
 * macros to borrow symbols from Villa
 *************************************************************************************************/


#include <depot.h>
#include <curia.h>
#include <cabin.h>
#include <stdlib.h>

#define VLREC          VSTREC
#define VLIDX          VSTIDX
#define VLLEAF         VSTLEAF
#define VLNODE         VSTNODE
#define VLCFUNC        VSTCFUNC

#define VL_CMPLEX      VST_CMPLEX
#define VL_CMPINT      VST_CMPINT
#define VL_CMPNUM      VST_CMPNUM
#define VL_CMPDEC      VST_CMPDEC

#define VILLA          VISTA

#define VL_OREADER     VST_OREADER
#define VL_OWRITER     VST_OWRITER
#define VL_OCREAT      VST_OCREAT
#define VL_OTRUNC      VST_OTRUNC
#define VL_ONOLCK      VST_ONOLCK
#define VL_OLCKNB      VST_OLCKNB
#define VL_OZCOMP      VST_OZCOMP

#define VL_DOVER       VST_DOVER
#define VL_DKEEP       VST_DKEEP
#define VL_DCAT        VST_DCAT
#define VL_DDUP        VST_DDUP

#define VL_JFORWARD    VST_JFORWARD
#define VL_JBACKWARD   VST_JBACKWARD

#define vlopen         vstopen
#define vlclose        vstclose
#define vlput          vstput
#define vlout          vstout
#define vlget          vstget
#define vlvsiz         vstvsiz
#define vlvnum         vstvnum
#define vlputlist      vstputlist
#define vloutlist      vstoutlist
#define vlgetlist      vstgetlist
#define vlgetcat       vstgetcat
#define vlcurfirst     vstcurfirst
#define vlcurlast      vstcurlast
#define vlcurprev      vstcurprev
#define vlcurnext      vstcurnext
#define vlcurjump      vstcurjump
#define vlcurkey       vstcurkey
#define vlcurval       vstcurval
#define vlcurput       vstcurput
#define vlcurout       vstcurout
#define vlsettuning    vstsettuning
#define vlsync         vstsync
#define vloptimize     vstoptimize
#define vlname         vstname
#define vlfsiz         vstfsiz
#define vllnum         vstlnum
#define vlnnum         vstnnum
#define vlrnum         vstrnum
#define vlwritable     vstwritable
#define vlfatalerror   vstfatalerror
#define vlinode        vstinode
#define vlmtime        vstmtime
#define vltranbegin    vsttranbegin
#define vltrancommit   vsttrancommit
#define vltranabort    vsttranabort
#define vlremove       vstremove
#define vlrepair       vstrepair
#define vlexportdb     vstexportdb
#define vlimportdb     vstimportdb
#define vlcrdnumptr    vstcrdnumptr
#define vlmemsync      vstmemsync
#define vlmemflush     vstmemflush
#define vlgetcache     vstgetcache
#define vlcurkeycache  vstcurkeycache
#define vlcurvalcache  vstcurvalcache
#define vlmulcuropen   vstmulcuropen
#define vlmulcurclose  vstmulcurclose
#define vlmulcurfirst  vstmulcurfirst
#define vlmulcurlast   vstmulcurlast
#define vlmulcurprev   vstmulcurprev
#define vlmulcurnext   vstmulcurnext
#define vlmulcurjump   vstmulcurjump
#define vlmulcurkey    vstmulcurkey
#define vlmulcurval    vstmulcurval
#define vlmulcurkeycache  vstmulcurkeycache
#define vlmulcurvalcache  vstmulcurvalcache
#define vlsetfbpsiz    vstsetfbpsiz
#define vlgetflags     vstgetflags
#define vlsetflags     vstsetflags

#if !defined(_VISTA_C)
#include <villa.h>
#endif



#if defined(__cplusplus)                 /* export for C++ */
}
#endif

#endif                                   /* duplication check */


/* END OF FILE */
