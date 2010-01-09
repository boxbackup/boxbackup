/*************************************************************************************************
 * Implementation of Vista
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


#define QDBM_INTERNAL  1
#define _VISTA_C       1

#include "vista.h"
#include "myconf.h"



/*************************************************************************************************
 * macros to convert Depot to Curia
 *************************************************************************************************/


#define DEPOT          CURIA

#define \
  dpopen(name, omode, bnum) \
  cropen(name, omode, ((bnum / vlcrdnum) * 2), vlcrdnum)

#define \
  dpclose(db) \
  crclose(db)

#define \
  dpput(db, kbuf, ksiz, vbuf, vsiz, dmode) \
  crput(db, kbuf, ksiz, vbuf, vsiz, dmode)

#define \
  dpout(db, kbuf, ksiz) \
  crout(db, kbuf, ksiz)

#define \
  dpget(db, kbuf, ksiz, start, max, sp) \
  crget(db, kbuf, ksiz, start, max, sp)

#define \
  dpgetwb(db, kbuf, ksiz, start, max, vbuf) \
  crgetwb(db, kbuf, ksiz, start, max, vbuf)

#define \
  dpvsiz(db, kbuf, ksiz) \
  crvsiz(db, kbuf, ksiz)

#define \
  dpiterinit(db) \
  criterinit(db)

#define \
  dpiternext(db, sp) \
  criternext(db, sp)

#define \
  dpsetalign(db, align) \
  crsetalign(db, align)

#define \
  dpsetfbpsiz(db, size) \
  crsetfbpsiz(db, size)

#define \
  dpsync(db) \
  crsync(db)

#define \
  dpoptimize(db, bnum) \
  croptimize(db, bnum)

#define \
  dpname(db) \
  crname(db)

#define \
  dpfsiz(db) \
  crfsiz(db)

#define \
  dpbnum(db) \
  crbnum(db)

#define \
  dpbusenum(db) \
  crbusenum(db)

#define \
  dprnum(db) \
  crrnum(db)

#define \
  dpwritable(db) \
  crwritable(db)

#define \
  dpfatalerror(db) \
  crfatalerror(db)

#define \
  dpinode(db) \
  crinode(db)

#define \
  dpmtime(db) \
  crmtime(db)

#define \
  dpfdesc(db) \
  crfdesc(db)

#define \
  dpremove(db) \
  crremove(db)

#define \
  dprepair(db) \
  crrepair(db)

#define \
  dpexportdb(db, name) \
  crexportdb(db, name)

#define \
  dpimportdb(db, name) \
  crimportdb(db, name)

#define \
  dpsnaffle(db, name) \
  crsnaffle(db, name)

#define \
  dpmemsync(db) \
  crmemsync(db)

#define \
  dpmemflush(db) \
  crmemflush(db)

#define \
  dpgetflags(db) \
  crgetflags(db)

#define \
  dpsetflags(db, flags) \
  crsetflags(db, flags)



/*************************************************************************************************
 * including real implementation
 *************************************************************************************************/


#include "villa.c"



/* END OF FILE */
