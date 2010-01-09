/*************************************************************************************************
 * The basic API of QDBM
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


#ifndef _DEPOT_H                         /* duplication check */
#define _DEPOT_H

#if defined(__cplusplus)                 /* export for C++ */
extern "C" {
#endif


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


typedef struct {                         /* type of structure for a database handle */
  char *name;                            /* name of the database file */
  int wmode;                             /* whether to be writable */
  int inode;                             /* inode of the database file */
  time_t mtime;                          /* last modified time of the database */
  int fd;                                /* file descriptor of the database file */
  int fsiz;                              /* size of the database file */
  char *map;                             /* pointer to the mapped memory */
  int msiz;                              /* size of the mapped memory */
  int *buckets;                          /* pointer to the bucket array */
  int bnum;                              /* number of the bucket array */
  int rnum;                              /* number of records */
  int fatal;                             /* whether a fatal error occured */
  int ioff;                              /* offset of the iterator */
  int *fbpool;                           /* free block pool */
  int fbpsiz;                            /* size of the free block pool */
  int fbpinc;                            /* incrementor of update of the free block pool */
  int align;                             /* basic size of alignment */
} DEPOT;

enum {                                   /* enumeration for error codes */
  DP_ENOERR,                             /* no error */
  DP_EFATAL,                             /* with fatal error */
  DP_EMODE,                              /* invalid mode */
  DP_EBROKEN,                            /* broken database file */
  DP_EKEEP,                              /* existing record */
  DP_ENOITEM,                            /* no item found */
  DP_EALLOC,                             /* memory allocation error */
  DP_EMAP,                               /* memory mapping error */
  DP_EOPEN,                              /* open error */
  DP_ECLOSE,                             /* close error */
  DP_ETRUNC,                             /* trunc error */
  DP_ESYNC,                              /* sync error */
  DP_ESTAT,                              /* stat error */
  DP_ESEEK,                              /* seek error */
  DP_EREAD,                              /* read error */
  DP_EWRITE,                             /* write error */
  DP_ELOCK,                              /* lock error */
  DP_EUNLINK,                            /* unlink error */
  DP_EMKDIR,                             /* mkdir error */
  DP_ERMDIR,                             /* rmdir error */
  DP_EMISC                               /* miscellaneous error */
};

enum {                                   /* enumeration for open modes */
  DP_OREADER = 1 << 0,                   /* open as a reader */
  DP_OWRITER = 1 << 1,                   /* open as a writer */
  DP_OCREAT = 1 << 2,                    /* a writer creating */
  DP_OTRUNC = 1 << 3,                    /* a writer truncating */
  DP_ONOLCK = 1 << 4,                    /* open without locking */
  DP_OLCKNB = 1 << 5,                    /* lock without blocking */
  DP_OSPARSE = 1 << 6                    /* create as a sparse file */
};

enum {                                   /* enumeration for write modes */
  DP_DOVER,                              /* overwrite an existing value */
  DP_DKEEP,                              /* keep an existing value */
  DP_DCAT                                /* concatenate values */
};


/* String containing the version information. */
MYEXTERN const char *dpversion;


/* Last happened error code. */
#define dpecode        (*dpecodeptr())


/* Get a message string corresponding to an error code.
   `ecode' specifies an error code.
   The return value is the message string of the error code. The region of the return value
   is not writable. */
const char *dperrmsg(int ecode);


/* Get a database handle.
   `name' specifies the name of a database file.
   `omode' specifies the connection mode: `DP_OWRITER' as a writer, `DP_OREADER' as a reader.
   If the mode is `DP_OWRITER', the following may be added by bitwise or: `DP_OCREAT', which
   means it creates a new database if not exist, `DP_OTRUNC', which means it creates a new
   database regardless if one exists.  Both of `DP_OREADER' and `DP_OWRITER' can be added to by
   bitwise or: `DP_ONOLCK', which means it opens a database file without file locking, or
   `DP_OLCKNB', which means locking is performed without blocking.  `DP_OCREAT' can be added to
   by bitwise or: `DP_OSPARSE', which means it creates a database file as a sparse file.
   `bnum' specifies the number of elements of the bucket array.  If it is not more than 0,
   the default value is specified.  The size of a bucket array is determined on creating,
   and can not be changed except for by optimization of the database.  Suggested size of a
   bucket array is about from 0.5 to 4 times of the number of all records to store.
   The return value is the database handle or `NULL' if it is not successful.
   While connecting as a writer, an exclusive lock is invoked to the database file.
   While connecting as a reader, a shared lock is invoked to the database file.  The thread
   blocks until the lock is achieved.  If `DP_ONOLCK' is used, the application is responsible
   for exclusion control. */
DEPOT *dpopen(const char *name, int omode, int bnum);


/* Close a database handle.
   `depot' specifies a database handle.
   If successful, the return value is true, else, it is false.
   Because the region of a closed handle is released, it becomes impossible to use the handle.
   Updating a database is assured to be written when the handle is closed.  If a writer opens
   a database but does not close it appropriately, the database will be broken. */
int dpclose(DEPOT *depot);


/* Store a record.
   `depot' specifies a database handle connected as a writer.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   `vbuf' specifies the pointer to the region of a value.
   `vsiz' specifies the size of the region of the value.  If it is negative, the size is
   assigned with `strlen(vbuf)'.
   `dmode' specifies behavior when the key overlaps, by the following values: `DP_DOVER',
   which means the specified value overwrites the existing one, `DP_DKEEP', which means the
   existing value is kept, `DP_DCAT', which means the specified value is concatenated at the
   end of the existing value.
   If successful, the return value is true, else, it is false. */
int dpput(DEPOT *depot, const char *kbuf, int ksiz, const char *vbuf, int vsiz, int dmode);


/* Delete a record.
   `depot' specifies a database handle connected as a writer.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   If successful, the return value is true, else, it is false.  False is returned when no
   record corresponds to the specified key. */
int dpout(DEPOT *depot, const char *kbuf, int ksiz);


/* Retrieve a record.
   `depot' specifies a database handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   `start' specifies the offset address of the beginning of the region of the value to be read.
   `max' specifies the max size to be read.  If it is negative, the size to read is unlimited.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   If successful, the return value is the pointer to the region of the value of the
   corresponding record, else, it is `NULL'.  `NULL' is returned when no record corresponds to
   the specified key or the size of the value of the corresponding record is less than `start'.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if it
   is no longer in use. */
char *dpget(DEPOT *depot, const char *kbuf, int ksiz, int start, int max, int *sp);


/* Retrieve a record and write the value into a buffer.
   `depot' specifies a database handle.
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
int dpgetwb(DEPOT *depot, const char *kbuf, int ksiz, int start, int max, char *vbuf);


/* Get the size of the value of a record.
   `depot' specifies a database handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   If successful, the return value is the size of the value of the corresponding record, else,
   it is -1.
   Because this function does not read the entity of a record, it is faster than `dpget'. */
int dpvsiz(DEPOT *depot, const char *kbuf, int ksiz);


/* Initialize the iterator of a database handle.
   `depot' specifies a database handle.
   If successful, the return value is true, else, it is false.
   The iterator is used in order to access the key of every record stored in a database. */
int dpiterinit(DEPOT *depot);


/* Get the next key of the iterator.
   `depot' specifies a database handle.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   If successful, the return value is the pointer to the region of the next key, else, it is
   `NULL'.  `NULL' is returned when no record is to be get out of the iterator.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if
   it is no longer in use.  It is possible to access every record by iteration of calling
   this function.  However, it is not assured if updating the database is occurred while the
   iteration.  Besides, the order of this traversal access method is arbitrary, so it is not
   assured that the order of storing matches the one of the traversal access. */
char *dpiternext(DEPOT *depot, int *sp);


/* Set alignment of a database handle.
   `depot' specifies a database handle connected as a writer.
   `align' specifies the size of alignment.
   If successful, the return value is true, else, it is false.
   If alignment is set to a database, the efficiency of overwriting values is improved.
   The size of alignment is suggested to be average size of the values of the records to be
   stored.  If alignment is positive, padding whose size is multiple number of the alignment
   is placed.  If alignment is negative, as `vsiz' is the size of a value, the size of padding
   is calculated with `(vsiz / pow(2, abs(align) - 1))'.  Because alignment setting is not
   saved in a database, you should specify alignment every opening a database. */
int dpsetalign(DEPOT *depot, int align);


/* Set the size of the free block pool of a database handle.
   `depot' specifies a database handle connected as a writer.
   `size' specifies the size of the free block pool of a database.
   If successful, the return value is true, else, it is false.
   The default size of the free block pool is 16.  If the size is greater, the space efficiency
   of overwriting values is improved with the time efficiency sacrificed. */
int dpsetfbpsiz(DEPOT *depot, int size);


/* Synchronize updating contents with the file and the device.
   `depot' specifies a database handle connected as a writer.
   If successful, the return value is true, else, it is false.
   This function is useful when another process uses the connected database file. */
int dpsync(DEPOT *depot);


/* Optimize a database.
   `depot' specifies a database handle connected as a writer.
   `bnum' specifies the number of the elements of the bucket array.  If it is not more than 0,
   the default value is specified.
   If successful, the return value is true, else, it is false.
   In an alternating succession of deleting and storing with overwrite or concatenate,
   dispensable regions accumulate.  This function is useful to do away with them. */
int dpoptimize(DEPOT *depot, int bnum);


/* Get the name of a database.
   `depot' specifies a database handle.
   If successful, the return value is the pointer to the region of the name of the database,
   else, it is `NULL'.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
char *dpname(DEPOT *depot);


/* Get the size of a database file.
   `depot' specifies a database handle.
   If successful, the return value is the size of the database file, else, it is -1. */
int dpfsiz(DEPOT *depot);


/* Get the number of the elements of the bucket array.
   `depot' specifies a database handle.
   If successful, the return value is the number of the elements of the bucket array, else, it
   is -1. */
int dpbnum(DEPOT *depot);


/* Get the number of the used elements of the bucket array.
   `depot' specifies a database handle.
   If successful, the return value is the number of the used elements of the bucket array,
   else, it is -1.
   This function is inefficient because it accesses all elements of the bucket array. */
int dpbusenum(DEPOT *depot);


/* Get the number of the records stored in a database.
   `depot' specifies a database handle.
   If successful, the return value is the number of the records stored in the database, else,
   it is -1. */
int dprnum(DEPOT *depot);


/* Check whether a database handle is a writer or not.
   `depot' specifies a database handle.
   The return value is true if the handle is a writer, false if not. */
int dpwritable(DEPOT *depot);


/* Check whether a database has a fatal error or not.
   `depot' specifies a database handle.
   The return value is true if the database has a fatal error, false if not. */
int dpfatalerror(DEPOT *depot);


/* Get the inode number of a database file.
   `depot' specifies a database handle.
   The return value is the inode number of the database file. */
int dpinode(DEPOT *depot);


/* Get the last modified time of a database.
   `depot' specifies a database handle.
   The return value is the last modified time of the database. */
time_t dpmtime(DEPOT *depot);


/* Get the file descriptor of a database file.
   `depot' specifies a database handle.
   The return value is the file descriptor of the database file.
   Handling the file descriptor of a database file directly is not suggested. */
int dpfdesc(DEPOT *depot);


/* Remove a database file.
   `name' specifies the name of a database file.
   If successful, the return value is true, else, it is false. */
int dpremove(const char *name);


/* Repair a broken database file.
   `name' specifies the name of a database file.
   If successful, the return value is true, else, it is false.
   There is no guarantee that all records in a repaired database file correspond to the original
   or expected state. */
int dprepair(const char *name);


/* Dump all records as endian independent data.
   `depot' specifies a database handle.
   `name' specifies the name of an output file.
   If successful, the return value is true, else, it is false. */
int dpexportdb(DEPOT *depot, const char *name);


/* Load all records from endian independent data.
   `depot' specifies a database handle connected as a writer.  The database of the handle must
   be empty.
   `name' specifies the name of an input file.
   If successful, the return value is true, else, it is false. */
int dpimportdb(DEPOT *depot, const char *name);


/* Retrieve a record directly from a database file.
   `name' specifies the name of a database file.
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
   is no longer in use.  Although this function can be used even while the database file is
   locked by another process, it is not assured that recent updated is reflected. */
char *dpsnaffle(const char *name, const char *kbuf, int ksiz, int *sp);


/* Hash function used inside Depot.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   The return value is the hash value of 31 bits length computed from the key.
   This function is useful when an application calculates the state of the inside bucket array. */
int dpinnerhash(const char *kbuf, int ksiz);


/* Hash function which is independent from the hash functions used inside Depot.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   The return value is the hash value of 31 bits length computed from the key.
   This function is useful when an application uses its own hash algorithm outside Depot. */
int dpouterhash(const char *kbuf, int ksiz);


/* Get a natural prime number not less than a number.
   `num' specified a natural number.
   The return value is a natural prime number not less than the specified number.
   This function is useful when an application determines the size of a bucket array of its
   own hash algorithm. */
int dpprimenum(int num);



/*************************************************************************************************
 * features for experts
 *************************************************************************************************/


#define _QDBM_VERSION  "1.8.77"
#define _QDBM_LIBVER   1413


/* Name of the operating system. */
MYEXTERN const char *dpsysname;


/* File descriptor for debugging output. */
MYEXTERN int dpdbgfd;


/* Whether this build is reentrant. */
MYEXTERN const int dpisreentrant;


/* Set the last happened error code.
   `ecode' specifies the error code.
   `line' specifies the number of the line where the error happened. */
void dpecodeset(int ecode, const char *file, int line);


/* Get the pointer of the variable of the last happened error code.
   The return value is the pointer of the variable. */
int *dpecodeptr(void);


/* Synchronize updating contents on memory.
   `depot' specifies a database handle connected as a writer.
   If successful, the return value is true, else, it is false. */
int dpmemsync(DEPOT *depot);


/* Synchronize updating contents on memory, not physically.
   `depot' specifies a database handle connected as a writer.
   If successful, the return value is true, else, it is false. */
int dpmemflush(DEPOT *depot);


/* Get flags of a database.
   `depot' specifies a database handle.
   The return value is the flags of a database. */
int dpgetflags(DEPOT *depot);


/* Set flags of a database.
   `depot' specifies a database handle connected as a writer.
   `flags' specifies flags to set.  Least ten bits are reserved for internal use.
   If successful, the return value is true, else, it is false. */
int dpsetflags(DEPOT *depot, int flags);



#undef MYEXTERN

#if defined(__cplusplus)                 /* export for C++ */
}
#endif

#endif                                   /* duplication check */


/* END OF FILE */
