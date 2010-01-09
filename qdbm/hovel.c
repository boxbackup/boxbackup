/*************************************************************************************************
 * Implementation of Hovel
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

#include "hovel.h"
#include "myconf.h"

#define HV_INITBNUM    32749             /* initial bucket number */
#define HV_ALIGNSIZ    16                /* size of alignment */


/* private function prototypes */
static int gdbm_geterrno(int ecode);



/*************************************************************************************************
 * public objects
 *************************************************************************************************/


/* String containing the version information. */
char *gdbm_version = "Powered by QDBM";


/* Get a message string corresponding to an error code. */
char *gdbm_strerror(gdbm_error gdbmerrno){
  switch(gdbmerrno){
  case GDBM_NO_ERROR: return "No error";
  case GDBM_MALLOC_ERROR: return "Malloc error";
  case GDBM_BLOCK_SIZE_ERROR: return "Block size error";
  case GDBM_FILE_OPEN_ERROR: return "File open error";
  case GDBM_FILE_WRITE_ERROR: return "File write error";
  case GDBM_FILE_SEEK_ERROR: return "File seek error";
  case GDBM_FILE_READ_ERROR: return "File read error";
  case GDBM_BAD_MAGIC_NUMBER: return "Bad magic number";
  case GDBM_EMPTY_DATABASE: return "Empty database";
  case GDBM_CANT_BE_READER: return "Can't be reader";
  case GDBM_CANT_BE_WRITER: return "Can't be writer";
  case GDBM_READER_CANT_DELETE: return "Reader can't delete";
  case GDBM_READER_CANT_STORE: return "Reader can't store";
  case GDBM_READER_CANT_REORGANIZE: return "Reader can't reorganize";
  case GDBM_UNKNOWN_UPDATE: return "Unknown update";
  case GDBM_ITEM_NOT_FOUND: return "Item not found";
  case GDBM_REORGANIZE_FAILED: return "Reorganize failed";
  case GDBM_CANNOT_REPLACE: return "Cannot replace";
  case GDBM_ILLEGAL_DATA: return "Illegal data";
  case GDBM_OPT_ALREADY_SET: return "Option already set";
  case GDBM_OPT_ILLEGAL: return "Illegal option";
  }
  return "(invalid ecode)";
}


/* Get a database handle after the fashion of GDBM. */
GDBM_FILE gdbm_open(char *name, int block_size, int read_write, int mode,
                    void (*fatal_func)(void)){
  GDBM_FILE dbf;
  int dpomode;
  DEPOT *depot;
  int flags, fd;
  assert(name);
  dpomode = DP_OREADER;
  flags = O_RDONLY;
  if(read_write & GDBM_READER){
    dpomode = GDBM_READER;
    if(read_write & GDBM_NOLOCK) dpomode |= DP_ONOLCK;
    if(read_write & GDBM_LOCKNB) dpomode |= DP_OLCKNB;
    flags = O_RDONLY;
  } else if(read_write & GDBM_WRITER){
    dpomode = DP_OWRITER;
    if(read_write & GDBM_NOLOCK) dpomode |= DP_ONOLCK;
    if(read_write & GDBM_LOCKNB) dpomode |= DP_OLCKNB;
    flags = O_RDWR;
  } else if(read_write & GDBM_WRCREAT){
    dpomode = DP_OWRITER | DP_OCREAT;
    if(read_write & GDBM_NOLOCK) dpomode |= DP_ONOLCK;
    if(read_write & GDBM_LOCKNB) dpomode |= DP_OLCKNB;
    if(read_write & GDBM_SPARSE) dpomode |= DP_OSPARSE;
    flags = O_RDWR | O_CREAT;
  } else if(read_write & GDBM_NEWDB){
    dpomode = DP_OWRITER | DP_OCREAT | DP_OTRUNC;
    if(read_write & GDBM_NOLOCK) dpomode |= DP_ONOLCK;
    if(read_write & GDBM_LOCKNB) dpomode |= DP_OLCKNB;
    if(read_write & GDBM_SPARSE) dpomode |= DP_OSPARSE;
    flags = O_RDWR | O_CREAT | O_TRUNC;
  } else {
    gdbm_errno = GDBM_ILLEGAL_DATA;
    return NULL;
  }
  mode |= 00600;
  if((fd = open(name, flags, mode)) == -1 || close(fd) == -1){
    gdbm_errno = GDBM_FILE_OPEN_ERROR;
    return NULL;
  }
  if(!(depot = dpopen(name, dpomode, HV_INITBNUM))){
    gdbm_errno = gdbm_geterrno(dpecode);
    if(dpecode == DP_ESTAT) gdbm_errno = GDBM_FILE_OPEN_ERROR;
    return NULL;
  }
  if(dpomode & DP_OWRITER){
    if(!dpsetalign(depot, HV_ALIGNSIZ)){
      gdbm_errno = gdbm_geterrno(dpecode);
      dpclose(depot);
    }
  }
  if((dpomode & DP_OWRITER) && (read_write & GDBM_SYNC)){
    if(!dpsync(depot)){
      gdbm_errno = gdbm_geterrno(dpecode);
      dpclose(depot);
    }
  }
  if(!(dbf = malloc(sizeof(GDBM)))){
    gdbm_errno = GDBM_MALLOC_ERROR;
    dpclose(depot);
    return NULL;
  }
  dbf->depot = depot;
  dbf->curia = NULL;
  dbf->syncmode = (dpomode & DP_OWRITER) && (read_write & GDBM_SYNC) ? TRUE : FALSE;
  return dbf;
}


/* Get a database handle after the fashion of QDBM. */
GDBM_FILE gdbm_open2(char *name, int read_write, int mode, int bnum, int dnum, int align){
  GDBM_FILE dbf;
  int dpomode, cromode, flags, fd;
  struct stat sbuf;
  DEPOT *depot;
  CURIA *curia;
  assert(name);
  dpomode = DP_OREADER;
  cromode = CR_OREADER;
  flags = O_RDONLY;
  if(read_write & GDBM_READER){
    dpomode = DP_OREADER;
    cromode = CR_OREADER;
    if(read_write & GDBM_NOLOCK){
      dpomode |= DP_ONOLCK;
      cromode |= CR_ONOLCK;
    }
    if(read_write & GDBM_LOCKNB){
      dpomode |= DP_OLCKNB;
      cromode |= CR_OLCKNB;
    }
    flags = O_RDONLY;
  } else if(read_write & GDBM_WRITER){
    dpomode = DP_OWRITER;
    cromode = CR_OWRITER;
    if(read_write & GDBM_NOLOCK){
      dpomode |= DP_ONOLCK;
      cromode |= CR_ONOLCK;
    }
    if(read_write & GDBM_LOCKNB){
      dpomode |= DP_OLCKNB;
      cromode |= CR_OLCKNB;
    }
    flags = O_RDWR;
  } else if(read_write & GDBM_WRCREAT){
    dpomode = DP_OWRITER | DP_OCREAT;
    cromode = CR_OWRITER | CR_OCREAT;
    if(read_write & GDBM_NOLOCK){
      dpomode |= DP_ONOLCK;
      cromode |= CR_ONOLCK;
    }
    if(read_write & GDBM_LOCKNB){
      dpomode |= DP_OLCKNB;
      cromode |= CR_OLCKNB;
    }
    if(read_write & GDBM_SPARSE){
      dpomode |= DP_OSPARSE;
      cromode |= CR_OSPARSE;
    }
    flags = O_RDWR | O_CREAT;
  } else if(read_write & GDBM_NEWDB){
    dpomode = DP_OWRITER | DP_OCREAT | DP_OTRUNC;
    cromode = CR_OWRITER | CR_OCREAT | CR_OTRUNC;
    if(read_write & GDBM_NOLOCK){
      dpomode |= DP_ONOLCK;
      cromode |= CR_ONOLCK;
    }
    if(read_write & GDBM_LOCKNB){
      dpomode |= DP_OLCKNB;
      cromode |= CR_OLCKNB;
    }
    if(read_write & GDBM_SPARSE){
      dpomode |= DP_OSPARSE;
      cromode |= CR_OSPARSE;
    }
    flags = O_RDWR | O_CREAT | O_TRUNC;
  } else {
    gdbm_errno = GDBM_ILLEGAL_DATA;
    return NULL;
  }
  if(lstat(name, &sbuf) != -1){
    if(S_ISDIR(sbuf.st_mode)){
      if(dnum < 1) dnum = 1;
    } else {
      dnum = 0;
    }
  }
  depot = NULL;
  curia = NULL;
  if(dnum > 0){
    mode |= 00700;
    if((cromode & CR_OCREAT)){
      if(mkdir(name, mode) == -1 && errno != EEXIST){
        gdbm_errno = GDBM_FILE_OPEN_ERROR;
        return NULL;
      }
    }
    if(!(curia = cropen(name, cromode, bnum, dnum))){
      gdbm_errno = gdbm_geterrno(dpecode);
      return NULL;
    }
    if(cromode & CR_OWRITER) crsetalign(curia, align);
    if((cromode & CR_OWRITER) && (read_write & GDBM_SYNC)) crsync(curia);
  } else {
    mode |= 00600;
    if(dpomode & DP_OWRITER){
      if((fd = open(name, flags, mode)) == -1 || close(fd) == -1){
        gdbm_errno = GDBM_FILE_OPEN_ERROR;
        return NULL;
      }
    }
    if(!(depot = dpopen(name, dpomode, bnum))){
      gdbm_errno = gdbm_geterrno(dpecode);
      return NULL;
    }
    if(dpomode & DP_OWRITER) dpsetalign(depot, align);
    if((dpomode & DP_OWRITER) && (read_write & GDBM_SYNC)) dpsync(depot);
  }
  if(!(dbf = malloc(sizeof(GDBM)))){
    gdbm_errno = GDBM_MALLOC_ERROR;
    if(depot) dpclose(depot);
    if(curia) crclose(curia);
    return NULL;
  }
  dbf->depot = depot;
  dbf->curia = curia;
  dbf->syncmode = (dpomode & DP_OWRITER) && (read_write & GDBM_SYNC) ? TRUE : FALSE;
  return dbf;
}


/* Close a database handle. */
void gdbm_close(GDBM_FILE dbf){
  assert(dbf);
  if(dbf->depot){
    if(dbf->syncmode) dpsync(dbf->depot);
    dpclose(dbf->depot);
  } else {
    if(dbf->syncmode) crsync(dbf->curia);
    crclose(dbf->curia);
  }
  free(dbf);
}


/* Store a record. */
int gdbm_store(GDBM_FILE dbf, datum key, datum content, int flag){
  int dmode;
  assert(dbf);
  if(!key.dptr || key.dsize < 0 || !content.dptr || content.dsize < 0){
    gdbm_errno = GDBM_ILLEGAL_DATA;
    return -1;
  }
  if(dbf->depot){
    if(!dpwritable(dbf->depot)){
      gdbm_errno = GDBM_READER_CANT_STORE;
      return -1;
    }
    dmode = (flag == GDBM_INSERT) ? DP_DKEEP : DP_DOVER;
    if(!dpput(dbf->depot, key.dptr, key.dsize, content.dptr, content.dsize, dmode)){
      gdbm_errno = gdbm_geterrno(dpecode);
      if(dpecode == DP_EKEEP) return 1;
      return -1;
    }
    if(dbf->syncmode && !dpsync(dbf->depot)){
      gdbm_errno = gdbm_geterrno(dpecode);
      return -1;
    }
  } else {
    if(!crwritable(dbf->curia)){
      gdbm_errno = GDBM_READER_CANT_STORE;
      return -1;
    }
    dmode = (flag == GDBM_INSERT) ? CR_DKEEP : CR_DOVER;
    if(!crput(dbf->curia, key.dptr, key.dsize, content.dptr, content.dsize, dmode)){
      gdbm_errno = gdbm_geterrno(dpecode);
      if(dpecode == DP_EKEEP) return 1;
      return -1;
    }
    if(dbf->syncmode && !crsync(dbf->curia)){
      gdbm_errno = gdbm_geterrno(dpecode);
      return -1;
    }
  }
  return 0;
}


/* Delete a record. */
int gdbm_delete(GDBM_FILE dbf, datum key){
  assert(dbf);
  if(!key.dptr || key.dsize < 0){
    gdbm_errno = GDBM_ILLEGAL_DATA;
    return -1;
  }
  if(dbf->depot){
    if(!dpwritable(dbf->depot)){
      gdbm_errno = GDBM_READER_CANT_DELETE;
      return -1;
    }
    if(!dpout(dbf->depot, key.dptr, key.dsize)){
      gdbm_errno = gdbm_geterrno(dpecode);
      return -1;
    }
    if(dbf->syncmode && !dpsync(dbf->depot)){
      gdbm_errno = gdbm_geterrno(dpecode);
      return -1;
    }
  } else {
    if(!crwritable(dbf->curia)){
      gdbm_errno = GDBM_READER_CANT_DELETE;
      return -1;
    }
    if(!crout(dbf->curia, key.dptr, key.dsize)){
      gdbm_errno = gdbm_geterrno(dpecode);
      return -1;
    }
    if(dbf->syncmode && !crsync(dbf->curia)){
      gdbm_errno = gdbm_geterrno(dpecode);
      return -1;
    }
  }
  return 0;
}


/* Retrieve a record. */
datum gdbm_fetch(GDBM_FILE dbf, datum key){
  datum content;
  char *vbuf;
  int vsiz;
  assert(dbf);
  if(!key.dptr || key.dsize < 0){
    gdbm_errno = GDBM_ILLEGAL_DATA;
    content.dptr = NULL;
    content.dsize = 0;
    return content;
  }
  if(dbf->depot){
    if(!(vbuf = dpget(dbf->depot, key.dptr, key.dsize, 0, -1, &vsiz))){
      gdbm_errno = gdbm_geterrno(dpecode);
      content.dptr = NULL;
      content.dsize = 0;
      return content;
    }
  } else {
    if(!(vbuf = crget(dbf->curia, key.dptr, key.dsize, 0, -1, &vsiz))){
      gdbm_errno = gdbm_geterrno(dpecode);
      content.dptr = NULL;
      content.dsize = 0;
      return content;
    }
  }
  content.dptr = vbuf;
  content.dsize = vsiz;
  return content;
}


/* Check whether a record exists or not. */
int gdbm_exists(GDBM_FILE dbf, datum key){
  assert(dbf);
  if(!key.dptr || key.dsize < 0){
    gdbm_errno = GDBM_ILLEGAL_DATA;
    return FALSE;
  }
  if(dbf->depot){
    if(dpvsiz(dbf->depot, key.dptr, key.dsize) == -1){
      gdbm_errno = gdbm_geterrno(dpecode);
      return FALSE;
    }
  } else {
    if(crvsiz(dbf->curia, key.dptr, key.dsize) == -1){
      gdbm_errno = gdbm_geterrno(dpecode);
      return FALSE;
    }
  }
  return TRUE;
}


/* Get the first key of a database. */
datum gdbm_firstkey(GDBM_FILE dbf){
  datum key;
  assert(dbf);
  memset(&key, 0, sizeof(datum));
  if(dbf->depot){
    if(dprnum(dbf->depot) < 1){
      gdbm_errno = GDBM_EMPTY_DATABASE;
      key.dptr = NULL;
      key.dsize = 0;
      return key;
    }
    dpiterinit(dbf->depot);
    return gdbm_nextkey(dbf, key);
  } else {
    if(crrnum(dbf->curia) < 1){
      gdbm_errno = GDBM_EMPTY_DATABASE;
      key.dptr = NULL;
      key.dsize = 0;
      return key;
    }
    criterinit(dbf->curia);
    return gdbm_nextkey(dbf, key);
  }
}


/* Get the next key of a database. */
datum gdbm_nextkey(GDBM_FILE dbf, datum key){
  char *kbuf;
  int ksiz;
  assert(dbf);
  if(dbf->depot){
    if(!(kbuf = dpiternext(dbf->depot, &ksiz))){
      gdbm_errno = gdbm_geterrno(dpecode);
      key.dptr = NULL;
      key.dsize = 0;
      return key;
    }
  } else {
    if(!(kbuf = criternext(dbf->curia, &ksiz))){
      gdbm_errno = gdbm_geterrno(dpecode);
      key.dptr = NULL;
      key.dsize = 0;
      return key;
    }
  }
  key.dptr = kbuf;
  key.dsize = ksiz;
  return key;
}


/* Synchronize contents of updating with the file and the device. */
void gdbm_sync(GDBM_FILE dbf){
  assert(dbf);
  if(dbf->depot){
    if(!dpsync(dbf->depot)) gdbm_errno = gdbm_geterrno(dpecode);
  } else {
    if(!crsync(dbf->curia)) gdbm_errno = gdbm_geterrno(dpecode);
  }
}


/* Reorganize a database. */
int gdbm_reorganize(GDBM_FILE dbf){
  assert(dbf);
  if(dbf->depot){
    if(!dpwritable(dbf->depot)){
      gdbm_errno = GDBM_READER_CANT_REORGANIZE;
      return -1;
    }
    if(!dpoptimize(dbf->depot, dprnum(dbf->depot) >= HV_INITBNUM ? -1 : HV_INITBNUM)){
      gdbm_errno = gdbm_geterrno(dpecode);
      return -1;
    }
  } else {
    if(!crwritable(dbf->curia)){
      gdbm_errno = GDBM_READER_CANT_REORGANIZE;
      return -1;
    }
    if(!croptimize(dbf->curia, crrnum(dbf->curia) >= HV_INITBNUM ? -1 : HV_INITBNUM)){
      gdbm_errno = gdbm_geterrno(dpecode);
      return -1;
    }
  }
  return 0;
}


/* Get the file descriptor of a database file. */
int gdbm_fdesc(GDBM_FILE dbf){
  assert(dbf);
  if(dbf->depot){
    return dpfdesc(dbf->depot);
  } else {
    return -1;
  }
}


/* No effect. */
int gdbm_setopt(GDBM_FILE dbf, int option, int *value, int size){
  assert(dbf);
  return 0;
}



/*************************************************************************************************
 * features for experts
 *************************************************************************************************/


/* Get the pointer of the last happened error code. */
int *gdbm_errnoptr(void){
  static int deferrno = GDBM_NO_ERROR;
  void *ptr;
  if(_qdbm_ptsafe){
    if(!(ptr = _qdbm_settsd(&deferrno, sizeof(int), &deferrno))){
      deferrno = GDBM_ILLEGAL_DATA;
      return &deferrno;
    }
    return (int *)ptr;
  }
  return &deferrno;
}



/*************************************************************************************************
 * private objects
 *************************************************************************************************/


/* Get the error code of GDBM corresponding to an error code of Depot.
   `ecode' specifies an error code of Depot.
   The return value is the error code of GDBM. */
static int gdbm_geterrno(int ecode){
  switch(ecode){
  case DP_ENOERR: return GDBM_NO_ERROR;
  case DP_EBROKEN: return GDBM_BAD_MAGIC_NUMBER;
  case DP_EKEEP: return GDBM_CANNOT_REPLACE;
  case DP_ENOITEM: return GDBM_ITEM_NOT_FOUND;
  case DP_EALLOC: return GDBM_MALLOC_ERROR;
  case DP_EOPEN: return GDBM_FILE_OPEN_ERROR;
  case DP_ESEEK: return GDBM_FILE_SEEK_ERROR;
  case DP_EREAD: return GDBM_FILE_READ_ERROR;
  case DP_EWRITE: return GDBM_FILE_WRITE_ERROR;
  case DP_EMKDIR: return GDBM_FILE_OPEN_ERROR;
  default: break;
  }
  return GDBM_ILLEGAL_DATA;
}



/* END OF FILE */
