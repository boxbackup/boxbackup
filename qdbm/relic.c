/*************************************************************************************************
 * Implementation of Relic
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

#include "relic.h"
#include "myconf.h"

#define RL_NAMEMAX     512               /* max size of a database name */
#define RL_DIRFSUF     MYEXTSTR "dir"    /* suffix of a directory file */
#define RL_DATAFSUF    MYEXTSTR "pag"    /* suffix of a page file */
#define RL_PATHBUFSIZ  1024              /* size of a path buffer */
#define RL_INITBNUM    1913              /* initial bucket number */
#define RL_ALIGNSIZ    16                /* size of alignment */
#define RL_MAXLOAD     1.25              /* max ratio of bucket loading */
#define RL_DIRMAGIC    "[depot]\0\v"     /* magic number of a directory file */


/* private function prototypes */
static void dbm_writedummy(int fd);
static int dbm_writestr(int fd, const char *str);



/*************************************************************************************************
 * public objects
 *************************************************************************************************/


/* Get a database handle. */
DBM *dbm_open(char *name, int flags, int mode){
  DBM *db;
  DEPOT *depot;
  int dpomode;
  char path[RL_PATHBUFSIZ];
  int dfd, fd;
  assert(name);
  if(strlen(name) > RL_NAMEMAX) return NULL;
  dpomode = DP_OREADER;
  if((flags & O_WRONLY) || (flags & O_RDWR)){
    dpomode = DP_OWRITER;
    if(flags & O_CREAT) dpomode |= DP_OCREAT;
    if(flags & O_TRUNC) dpomode |= DP_OTRUNC;
  }
  mode |= 00600;
  sprintf(path, "%s%s", name, RL_DIRFSUF);
  if((dfd = open(path, flags, mode)) == -1) return NULL;
  dbm_writedummy(dfd);
  sprintf(path, "%s%s", name, RL_DATAFSUF);
  if((fd = open(path, flags, mode)) == -1 || close(fd) == -1){
    close(dfd);
    return NULL;
  }
  if(!(depot = dpopen(path, dpomode, RL_INITBNUM))){
    close(dfd);
    return NULL;
  }
  if(dpomode & DP_OWRITER){
    if(!dpsetalign(depot, RL_ALIGNSIZ)){
      close(dfd);
      dpclose(depot);
      return NULL;
    }
  }
  if(!(db = malloc(sizeof(DBM)))){
    close(dfd);
    dpclose(depot);
    return NULL;
  }
  db->depot = depot;
  db->dfd = dfd;
  db->dbm_fetch_vbuf = NULL;
  db->dbm_nextkey_kbuf = NULL;
  return db;
}


/* Close a database handle. */
void dbm_close(DBM *db){
  assert(db);
  free(db->dbm_fetch_vbuf);
  free(db->dbm_nextkey_kbuf);
  close(db->dfd);
  dpclose(db->depot);
  free(db);
}


/* Store a record. */
int dbm_store(DBM *db, datum key, datum content, int flags){
  int dmode;
  int bnum, rnum;
  assert(db);
  if(!key.dptr || key.dsize < 0 || !content.dptr || content.dsize < 0) return -1;
  dmode = (flags == DBM_INSERT) ? DP_DKEEP : DP_DOVER;
  if(!dpput(db->depot, key.dptr, key.dsize, content.dptr, content.dsize, dmode)){
    if(dpecode == DP_EKEEP) return 1;
    return -1;
  }
  bnum = dpbnum(db->depot);
  rnum = dprnum(db->depot);
  if(bnum > 0 && rnum > 0 && ((double)rnum / (double)bnum > RL_MAXLOAD)){
    if(!dpoptimize(db->depot, -1)) return -1;
  }
  return 0;
}


/* Delete a record. */
int dbm_delete(DBM *db, datum key){
  assert(db);
  if(!key.dptr || key.dsize < 0) return -1;
  if(!dpout(db->depot, key.dptr, key.dsize)) return -1;
  return 0;
}


/* Retrieve a record. */
datum dbm_fetch(DBM *db, datum key){
  datum content;
  char *vbuf;
  int vsiz;
  assert(db);
  if(!key.dptr || key.dsize < 0 ||
     !(vbuf = dpget(db->depot, key.dptr, key.dsize, 0, -1, &vsiz))){
    content.dptr = NULL;
    content.dsize = 0;
    return content;
  }
  free(db->dbm_fetch_vbuf);
  db->dbm_fetch_vbuf = vbuf;
  content.dptr = vbuf;
  content.dsize = vsiz;
  return content;
}


/* Get the first key of a database. */
datum dbm_firstkey(DBM *db){
  assert(db);
  dpiterinit(db->depot);
  return dbm_nextkey(db);
}


/* Get the next key of a database. */
datum dbm_nextkey(DBM *db){
  datum key;
  char *kbuf;
  int ksiz;
  if(!(kbuf = dpiternext(db->depot, &ksiz))){
    key.dptr = NULL;
    key.dsize = 0;
    return key;
  }
  free(db->dbm_nextkey_kbuf);
  db->dbm_nextkey_kbuf = kbuf;
  key.dptr = kbuf;
  key.dsize = ksiz;
  return key;
}


/* Check whether a database has a fatal error or not. */
int dbm_error(DBM *db){
  assert(db);
  return dpfatalerror(db->depot) ? TRUE : FALSE;
}


/* No effect. */
int dbm_clearerr(DBM *db){
  assert(db);
  return 0;
}


/* Check whether a handle is read-only or not. */
int dbm_rdonly(DBM *db){
  assert(db);
  return dpwritable(db->depot) ? FALSE : TRUE;
}


/* Get the file descriptor of a directory file. */
int dbm_dirfno(DBM *db){
  assert(db);
  return db->dfd;
}


/* Get the file descriptor of a data file. */
int dbm_pagfno(DBM *db){
  assert(db);
  return dpfdesc(db->depot);
}



/*************************************************************************************************
 * private objects
 *************************************************************************************************/


/* Write dummy data into a dummy file.
   `fd' specifies a file descriptor. */
static void dbm_writedummy(int fd){
  struct stat sbuf;
  if(fstat(fd, &sbuf) == -1 || sbuf.st_size > 0) return;
  write(fd, RL_DIRMAGIC, sizeof(RL_DIRMAGIC) - 1);
  dbm_writestr(fd, "\n\n");
  dbm_writestr(fd, "\x20\x20\xa2\xca\xa1\xb2\xa2\xca\x20\x20\x20\x20\x20\xa1\xbf\xa1");
  dbm_writestr(fd, "\xb1\xa1\xb1\xa1\xb1\xa1\xb1\xa1\xb1\xa1\xb1\xa1\xb1\xa1\xb1\xa1");
  dbm_writestr(fd, "\xb1\x0a\xa1\xca\x20\xa1\xad\xa2\xcf\xa1\xae\xa1\xcb\xa1\xe3\x20");
  dbm_writestr(fd, "\x20\x4e\x44\x42\x4d\x20\x43\x6f\x6d\x70\x61\x74\x69\x62\x69\x6c");
  dbm_writestr(fd, "\x69\x74\x79\x0a\xa1\xca\x20\x20\x20\x20\x20\x20\x20\xa1\xcb\x20");
  dbm_writestr(fd, "\x20\xa1\xc0\xa1\xb2\xa1\xb2\xa1\xb2\xa1\xb2\xa1\xb2\xa1\xb2\xa1");
  dbm_writestr(fd, "\xb2\xa1\xb2\xa1\xb2\x0a\x20\xa1\xc3\x20\x20\xa1\xc3\x20\xa1\xc3");
  dbm_writestr(fd, "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20");
  dbm_writestr(fd, "\x20\x20\x20\x20\x20\x20\x20\x0a\xa1\xca\x5f\x5f\xa1\xb2\xa1\xcb");
  dbm_writestr(fd, "\x5f\xa1\xcb\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20");
  dbm_writestr(fd, "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x0a");
}


/* Write a string into a file.
   `fd' specifies a file descriptor.
   `str' specifies a string. */
static int dbm_writestr(int fd, const char *str){
  const char *lbuf;
  int size, rv, wb;
  assert(fd >= 0 && str);
  lbuf = str;
  size = strlen(str);
  rv = 0;
  do {
    wb = write(fd, lbuf, size);
    switch(wb){
    case -1: if(errno != EINTR) return -1;
    case 0: break;
    default:
      lbuf += wb;
      size -= wb;
      rv += wb;
      break;
    }
  } while(size > 0);
  return rv;
}



/* END OF FILE */
