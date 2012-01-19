/*************************************************************************************************
 * The NDBM-compatible API of QDBM
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


#ifndef _RELIC_H                         /* duplication check */
#define _RELIC_H

#if defined(__cplusplus)                 /* export for C++ */
extern "C" {
#endif


#include <depot.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#if defined(_MSC_VER) && !defined(QDBM_INTERNAL) && !defined(QDBM_STATIC)
#define MYEXTERN extern __declspec(dllimport)
#else
#define MYEXTERN extern
#endif



/*************************************************************************************************
 * API
 *************************************************************************************************/


typedef struct {                         /* type of structure for a database handle */
  DEPOT *depot;                          /* internal database handle */
  int dfd;                               /* file descriptor of a dummy file */
  char *dbm_fetch_vbuf;                  /* buffer for dbm_fetch */
  char *dbm_nextkey_kbuf;                /* buffer for dbm_nextkey */
} DBM;

typedef struct {                         /* type of structure for a key or a value */
  void *dptr;                            /* pointer to the region */
  size_t dsize;                          /* size of the region */
} datum;

enum {                                   /* enumeration for write modes */
  DBM_INSERT,                            /* keep an existing value */
  DBM_REPLACE                            /* overwrite an existing value */
};


/* Get a database handle.
   `name' specifies the name of a database.  The file names are concatenated with suffixes.
   `flags' is the same as the one of `open' call, although `O_WRONLY' is treated as `O_RDWR'
   and additional flags except for `O_CREAT' and `O_TRUNC' have no effect.
   `mode' specifies the mode of the database file as the one of `open' call does.
   The return value is the database handle or `NULL' if it is not successful. */
DBM *dbm_open(char *name, int flags, int mode);


/* Close a database handle.
   `db' specifies a database handle.
   Because the region of the closed handle is released, it becomes impossible to use the
   handle. */
void dbm_close(DBM *db);


/* Store a record.
   `db' specifies a database handle.
   `key' specifies a structure of a key.
   `content' specifies a structure of a value.
   `flags' specifies behavior when the key overlaps, by the following values: `DBM_REPLACE',
   which means the specified value overwrites the existing one, `DBM_INSERT', which means the
   existing value is kept.
   The return value is 0 if it is successful, 1 if it gives up because of overlaps of the key,
   -1 if other error occurs. */
int dbm_store(DBM *db, datum key, datum content, int flags);


/* Delete a record.
   `db' specifies a database handle.
   `key' specifies a structure of a key.
   The return value is 0 if it is successful, -1 if some errors occur. */
int dbm_delete(DBM *db, datum key);


/* Retrieve a record.
   `db' specifies a database handle.
   `key' specifies a structure of a key.
   The return value is a structure of the result.
   If a record corresponds, the member `dptr' of the structure is the pointer to the region of
   the value.  If no record corresponds or some errors occur, `dptr' is `NULL'.  `dptr' points
   to the region related with the handle.  The region is available until the next time of
   calling this function with the same handle. */
datum dbm_fetch(DBM *db, datum key);


/* Get the first key of a database.
   `db' specifies a database handle.
   The return value is a structure of the result.
   If a record corresponds, the member `dptr' of the structure is the pointer to the region of
   the first key.  If no record corresponds or some errors occur, `dptr' is `NULL'.  `dptr'
   points to the region related with the handle.  The region is available until the next time
   of calling this function or the function `dbm_nextkey' with the same handle. */
datum dbm_firstkey(DBM *db);


/* Get the next key of a database.
   `db' specifies a database handle.
   The return value is a structure of the result.
   If a record corresponds, the member `dptr' of the structure is the pointer to the region of
   the next key.  If no record corresponds or some errors occur, `dptr' is `NULL'.  `dptr'
   points to the region related with the handle.  The region is available until the next time
   of calling this function or the function `dbm_firstkey' with the same handle. */
datum dbm_nextkey(DBM *db);


/* Check whether a database has a fatal error or not.
   `db' specifies a database handle.
   The return value is true if the database has a fatal error, false if not. */
int dbm_error(DBM *db);


/* No effect.
   `db' specifies a database handle.
   The return value is 0.
   The function is only for compatibility. */
int dbm_clearerr(DBM *db);


/* Check whether a handle is read-only or not.
   `db' specifies a database handle.
   The return value is true if the handle is read-only, or false if not read-only. */
int dbm_rdonly(DBM *db);


/* Get the file descriptor of a directory file.
   `db' specifies a database handle.
   The return value is the file descriptor of the directory file. */
int dbm_dirfno(DBM *db);


/* Get the file descriptor of a data file.
   `db' specifies a database handle.
   The return value is the file descriptor of the data file. */
int dbm_pagfno(DBM *db);



#undef MYEXTERN

#if defined(__cplusplus)                 /* export for C++ */
}
#endif

#endif                                   /* duplication check */


/* END OF FILE */
