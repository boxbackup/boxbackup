/*************************************************************************************************
 * The extended API of QDBM
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


#ifndef _CURIA_H                         /* duplication check */
#define _CURIA_H

#if defined(__cplusplus)                 /* export for C++ */
extern "C" {
#endif


#include <depot.h>
#include <stdlib.h>
#include <time.h>


#if defined(_MSC_VER) && !defined(QDBM_INTERNAL) && !defined(QDBM_STATIC)
#define MYEXTERN extern __declspec(dllimport)
#else
#define MYEXTERN extern
#endif



/*************************************************************************************************
 * API
 *************************************************************************************************/


typedef struct {                         /* type of structure for the database handle */
  char *name;                            /* name of the database directory */
  int wmode;                             /* whether to be writable */
  int inode;                             /* inode of the database directory */
  DEPOT *attr;                           /* database handle for attributes */
  DEPOT **depots;                        /* handles of the record database */
  int dnum;                              /* number of record database handles */
  int inum;                              /* number of the database of the using iterator */
  int lrnum;                             /* number of large objects */
} CURIA;

enum {                                   /* enumeration for open modes */
  CR_OREADER = 1 << 0,                   /* open as a reader */
  CR_OWRITER = 1 << 1,                   /* open as a writer */
  CR_OCREAT = 1 << 2,                    /* a writer creating */
  CR_OTRUNC = 1 << 3,                    /* a writer truncating */
  CR_ONOLCK = 1 << 4,                    /* open without locking */
  CR_OLCKNB = 1 << 5,                    /* lock without blocking */
  CR_OSPARSE = 1 << 6                    /* create as sparse files */
};

enum {                                   /* enumeration for write modes */
  CR_DOVER,                              /* overwrite an existing value */
  CR_DKEEP,                              /* keep an existing value */
  CR_DCAT                                /* concatenate values */
};


/* Get a database handle.
   `name' specifies the name of a database directory.
   `omode' specifies the connection mode: `CR_OWRITER' as a writer, `CR_OREADER' as a reader.
   If the mode is `CR_OWRITER', the following may be added by bitwise or: `CR_OCREAT', which
   means it creates a new database if not exist, `CR_OTRUNC', which means it creates a new
   database regardless if one exists.  Both of `CR_OREADER' and `CR_OWRITER' can be added to by
   bitwise or: `CR_ONOLCK', which means it opens a database directory without file locking, or
   `CR_OLCKNB', which means locking is performed without blocking.  `CR_OCREAT' can be added to
   by bitwise or: `CR_OSPARSE', which means it creates database files as sparse files.
   `bnum' specifies the number of elements of each bucket array.  If it is not more than 0,
   the default value is specified.  The size of each bucket array is determined on creating,
   and can not be changed except for by optimization of the database.  Suggested size of each
   bucket array is about from 0.5 to 4 times of the number of all records to store.
   `dnum' specifies the number of division of the database.  If it is not more than 0, the
   default value is specified.  The number of division can not be changed from the initial value.
   The max number of division is 512.
   The return value is the database handle or `NULL' if it is not successful.
   While connecting as a writer, an exclusive lock is invoked to the database directory.
   While connecting as a reader, a shared lock is invoked to the database directory.
   The thread blocks until the lock is achieved.  If `CR_ONOLCK' is used, the application is
   responsible for exclusion control. */
CURIA *cropen(const char *name, int omode, int bnum, int dnum);


/* Close a database handle.
   `curia' specifies a database handle.
   If successful, the return value is true, else, it is false.
   Because the region of a closed handle is released, it becomes impossible to use the handle.
   Updating a database is assured to be written when the handle is closed.  If a writer opens
   a database but does not close it appropriately, the database will be broken. */
int crclose(CURIA *curia);


/* Store a record.
   `curia' specifies a database handle connected as a writer.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   `vbuf' specifies the pointer to the region of a value.
   `vsiz' specifies the size of the region of the value.  If it is negative, the size is
   assigned with `strlen(vbuf)'.
   `dmode' specifies behavior when the key overlaps, by the following values: `CR_DOVER',
   which means the specified value overwrites the existing one, `CR_DKEEP', which means the
   existing value is kept, `CR_DCAT', which means the specified value is concatenated at the
   end of the existing value.
   If successful, the return value is true, else, it is false. */
int crput(CURIA *curia, const char *kbuf, int ksiz, const char *vbuf, int vsiz, int dmode);


/* Delete a record.
   `curia' specifies a database handle connected as a writer.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   If successful, the return value is true, else, it is false.  False is returned when no
   record corresponds to the specified key. */
int crout(CURIA *curia, const char *kbuf, int ksiz);


/* Retrieve a record.
   `curia' specifies a database handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   `start' specifies the offset address of the beginning of the region of the value to be read.
   `max' specifies the max size to be read.  If it is negative, the size to read is unlimited.
   `sp' specifies the pointer to a variable to which the size of the region of the return value
   is assigned.  If it is `NULL', it is not used.
   If successful, the return value is the pointer to the region of the value of the
   corresponding record, else, it is `NULL'.  `NULL' is returned when no record corresponds to
   the specified key or the size of the value of the corresponding record is less than `start'.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if it
   is no longer in use. */
char *crget(CURIA *curia, const char *kbuf, int ksiz, int start, int max, int *sp);


/* Retrieve a record and write the value into a buffer.
   `curia' specifies a database handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   `start' specifies the offset address of the beginning of the region of the value to be read.
   `max' specifies the max size to be read.  It shuld be equal to or less than the size of the
   writing buffer.
   `vbuf' specifies the pointer to a buffer into which the value of the corresponding record is
   written.
   If successful, the return value is the size of the written data, else, it is -1.  -1 is
   returned when no record corresponds to the specified key or the size of the value of the
   corresponding record is less than `start'.
   Note that no additional zero code is appended at the end of the region of the writing buffer. */
int crgetwb(CURIA *curia, const char *kbuf, int ksiz, int start, int max, char *vbuf);


/* Get the size of the value of a record.
   `curia' specifies a database handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   If successful, the return value is the size of the value of the corresponding record, else,
   it is -1.
   Because this function does not read the entity of a record, it is faster than `crget'. */
int crvsiz(CURIA *curia, const char *kbuf, int ksiz);


/* Initialize the iterator of a database handle.
   `curia' specifies a database handle.
   If successful, the return value is true, else, it is false.
   The iterator is used in order to access the key of every record stored in a database. */
int criterinit(CURIA *curia);


/* Get the next key of the iterator.
   `curia' specifies a database handle.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   If successful, the return value is the pointer to the region of the next key, else, it is
   `NULL'.  `NULL' is returned when no record is to be get out of the iterator.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if it
   is no longer in use.  It is possible to access every record by iteration of calling this
   function.  However, it is not assured if updating the database is occurred while the
   iteration.  Besides, the order of this traversal access method is arbitrary, so it is not
   assured that the order of storing matches the one of the traversal access. */
char *criternext(CURIA *curia, int *sp);


/* Set alignment of a database handle.
   `curia' specifies a database handle connected as a writer.
   `align' specifies the size of alignment.
   If successful, the return value is true, else, it is false.
   If alignment is set to a database, the efficiency of overwriting values is improved.
   The size of alignment is suggested to be average size of the values of the records to be
   stored.  If alignment is positive, padding whose size is multiple number of the alignment
   is placed.  If alignment is negative, as `vsiz' is the size of a value, the size of padding
   is calculated with `(vsiz / pow(2, abs(align) - 1))'.  Because alignment setting is not
   saved in a database, you should specify alignment every opening a database. */
int crsetalign(CURIA *curia, int align);


/* Set the size of the free block pool of a database handle.
   `curia' specifies a database handle connected as a writer.
   `size' specifies the size of the free block pool of a database.
   If successful, the return value is true, else, it is false.
   The default size of the free block pool is 16.  If the size is greater, the space efficiency
   of overwriting values is improved with the time efficiency sacrificed. */
int crsetfbpsiz(CURIA *curia, int size);


/* Synchronize updating contents with the files and the devices.
   `curia' specifies a database handle connected as a writer.
   If successful, the return value is true, else, it is false.
   This function is useful when another process uses the connected database directory. */
int crsync(CURIA *curia);


/* Optimize a database.
   `curia' specifies a database handle connected as a writer.
   `bnum' specifies the number of the elements of each bucket array.  If it is not more than 0,
   the default value is specified.
   If successful, the return value is true, else, it is false.
   In an alternating succession of deleting and storing with overwrite or concatenate,
   dispensable regions accumulate.  This function is useful to do away with them. */
int croptimize(CURIA *curia, int bnum);

/* Get the name of a database.
   `curia' specifies a database handle.
   If successful, the return value is the pointer to the region of the name of the database,
   else, it is `NULL'.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
char *crname(CURIA *curia);


/* Get the total size of database files.
   `curia' specifies a database handle.
   If successful, the return value is the total size of the database files, else, it is -1.
   If the total size is more than 2GB, the return value overflows. */
int crfsiz(CURIA *curia);


/* Get the total size of database files as double-precision floating-point number.
   `curia' specifies a database handle.
   If successful, the return value is the total size of the database files, else, it is -1.0. */
double crfsizd(CURIA *curia);


/* Get the total number of the elements of each bucket array.
   `curia' specifies a database handle.
   If successful, the return value is the total number of the elements of each bucket array,
   else, it is -1. */
int crbnum(CURIA *curia);


/* Get the total number of the used elements of each bucket array.
   `curia' specifies a database handle.
   If successful, the return value is the total number of the used elements of each bucket
   array, else, it is -1.
   This function is inefficient because it accesses all elements of each bucket array. */
int crbusenum(CURIA *curia);


/* Get the number of the records stored in a database.
   `curia' specifies a database handle.
   If successful, the return value is the number of the records stored in the database, else,
   it is -1. */
int crrnum(CURIA *curia);


/* Check whether a database handle is a writer or not.
   `curia' specifies a database handle.
   The return value is true if the handle is a writer, false if not. */
int crwritable(CURIA *curia);


/* Check whether a database has a fatal error or not.
   `curia' specifies a database handle.
   The return value is true if the database has a fatal error, false if not. */
int crfatalerror(CURIA *curia);


/* Get the inode number of a database directory.
   `curia' specifies a database handle.
   The return value is the inode number of the database directory. */
int crinode(CURIA *curia);


/* Get the last modified time of a database.
   `curia' specifies a database handle.
   The return value is the last modified time of the database. */
time_t crmtime(CURIA *curia);


/* Remove a database directory.
   `name' specifies the name of a database directory.
   If successful, the return value is true, else, it is false. */
int crremove(const char *name);


/* Repair a broken database directory.
   `name' specifies the name of a database directory.
   If successful, the return value is true, else, it is false.
   There is no guarantee that all records in a repaired database directory correspond to the
   original or expected state. */
int crrepair(const char *name);


/* Dump all records as endian independent data.
   `curia' specifies a database handle.
   `name' specifies the name of an output directory.
   If successful, the return value is true, else, it is false.
   Note that large objects are ignored. */
int crexportdb(CURIA *curia, const char *name);


/* Load all records from endian independent data.
   `curia' specifies a database handle connected as a writer.  The database of the handle must
   be empty.
   `name' specifies the name of an input directory.
   If successful, the return value is true, else, it is false.
   Note that large objects are ignored. */
int crimportdb(CURIA *curia, const char *name);


/* Retrieve a record directly from a database directory.
   `name' specifies the name of a database directory.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   If successful, the return value is the pointer to the region of the value of the
   corresponding record, else, it is `NULL'.  `NULL' is returned when no record corresponds to
   the specified key.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if it
   is no longer in use.  Although this function can be used even while the database directory is
   locked by another process, it is not assured that recent updated is reflected. */
char *crsnaffle(const char *name, const char *kbuf, int ksiz, int *sp);


/* Store a large object.
   `curia' specifies a database handle connected as a writer.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   `vbuf' specifies the pointer to the region of a value.
   `vsiz' specifies the size of the region of the value.  If it is negative, the size is
   assigned with `strlen(vbuf)'.
   `dmode' specifies behavior when the key overlaps, by the following values: `CR_DOVER',
   which means the specified value overwrites the existing one, `CR_DKEEP', which means the
   existing value is kept, `CR_DCAT', which means the specified value is concatenated at the
   end of the existing value.
   If successful, the return value is true, else, it is false. */
int crputlob(CURIA *curia, const char *kbuf, int ksiz, const char *vbuf, int vsiz, int dmode);


/* Delete a large object.
   `curia' specifies a database handle connected as a writer.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   If successful, the return value is true, else, it is false.  false is returned when no large
   object corresponds to the specified key. */
int croutlob(CURIA *curia, const char *kbuf, int ksiz);


/* Retrieve a large object.
   `curia' specifies a database handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   `start' specifies the offset address of the beginning of the region of the value to be read.
   `max' specifies the max size to be read.  If it is negative, the size to read is unlimited.
   `sp' specifies the pointer to a variable to which the size of the region of the return value
   is assigned.  If it is `NULL', it is not used.
   If successful, the return value is the pointer to the region of the value of the
   corresponding large object, else, it is `NULL'.  `NULL' is returned when no large object
   corresponds to the specified key or the size of the value of the corresponding large object
   is less than `start'.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if it
   is no longer in use. */
char *crgetlob(CURIA *curia, const char *kbuf, int ksiz, int start, int max, int *sp);


/* Get the file descriptor of a large object.
   `curia' specifies a database handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   If successful, the return value is the file descriptor of the corresponding large object,
   else, it is -1.  -1 is returned when no large object corresponds to the specified key.  The
   returned file descriptor is opened with the `open' call.  If the database handle was opened
   as a writer, the descriptor is writable (O_RDWR), else, it is not writable (O_RDONLY).  The
   descriptor should be closed with the `close' call if it is no longer in use. */
int crgetlobfd(CURIA *curia, const char *kbuf, int ksiz);


/* Get the size of the value of a large object.
   `curia' specifies a database handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   If successful, the return value is the size of the value of the corresponding large object,
   else, it is -1.
   Because this function does not read the entity of a large object, it is faster than
   `crgetlob'. */
int crvsizlob(CURIA *curia, const char *kbuf, int ksiz);


/* Get the number of the large objects stored in a database.
   `curia' specifies a database handle.
   If successful, the return value is the number of the large objects stored in the database,
   else, it is -1. */
int crrnumlob(CURIA *curia);



/*************************************************************************************************
 * features for experts
 *************************************************************************************************/


/* Synchronize updating contents on memory.
   `curia' specifies a database handle connected as a writer.
   If successful, the return value is true, else, it is false. */
int crmemsync(CURIA *curia);


/* Synchronize updating contents on memory, not physically.
   `curia' specifies a database handle connected as a writer.
   If successful, the return value is true, else, it is false. */
int crmemflush(CURIA *curia);


/* Get flags of a database.
   `curia' specifies a database handle.
   The return value is the flags of a database. */
int crgetflags(CURIA *curia);


/* Set flags of a database.
   `curia' specifies a database handle connected as a writer.
   `flags' specifies flags to set.  Least ten bits are reserved for internal use.
   If successful, the return value is true, else, it is false. */
int crsetflags(CURIA *curia, int flags);



#undef MYEXTERN

#if defined(__cplusplus)                 /* export for C++ */
}
#endif

#endif                                   /* duplication check */


/* END OF FILE */
