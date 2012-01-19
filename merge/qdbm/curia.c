/*************************************************************************************************
 * Implementation of Curia
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

#include "curia.h"
#include "myconf.h"

#define CR_NAMEMAX     512               /* max size of a database name */
#define CR_DPMAX       512               /* max number of division of a database */
#define CR_DIRMODE     00755             /* permission of a creating directory */
#define CR_FILEMODE    00644             /* permission of a creating file */
#define CR_PATHBUFSIZ  1024              /* size of a path buffer */
#define CR_DEFDNUM     5                 /* default number of division of a database */
#define CR_ATTRBNUM    16                /* bucket number of attrubute database */
#define CR_DPNAME      "depot"           /* name of each sub database */
#define CR_KEYDNUM     "dnum"            /* key of division number */
#define CR_KEYLRNUM    "lrnum"           /* key of the number of large objects */
#define CR_TMPFSUF     MYEXTSTR "crtmp"  /* suffix of a temporary directory */
#define CR_LOBDIR      "lob"             /* name of the directory of large objects */
#define CR_LOBDDEPTH   2                 /* depth of the directories of large objects */
#define CR_NUMBUFSIZ   32                /* size of a buffer for a number */
#define CR_IOBUFSIZ    8192              /* size of an I/O buffer */


/* private function prototypes */
static char *crstrdup(const char *str);
static int crdpgetnum(DEPOT *depot, const char *kbuf, int ksiz);
static char *crgetlobpath(CURIA *curia, const char *kbuf, int ksiz);
static int crmklobdir(const char *path);
static int crrmlobdir(const char *path);
static int crcplobdir(CURIA *curia, const char *path);
static int crwrite(int fd, const void *buf, int size);
static int crread(int fd, void *buf, int size);



/*************************************************************************************************
 * public objects
 *************************************************************************************************/


/* Get a database handle. */
CURIA *cropen(const char *name, int omode, int bnum, int dnum){
  DEPOT *attr, **depots;
  char path[CR_PATHBUFSIZ], *tname;
  int i, j, dpomode, inode, lrnum;
  struct stat sbuf;
  CURIA *curia;
  assert(name);
  if(dnum < 1) dnum = CR_DEFDNUM;
  if(dnum > CR_DPMAX) dnum = CR_DPMAX;
  if(strlen(name) > CR_NAMEMAX){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return NULL;
  }
  dpomode = DP_OREADER;
  if(omode & CR_OWRITER){
    dpomode = DP_OWRITER;
    if(omode & CR_OCREAT) dpomode |= DP_OCREAT;
    if(omode & CR_OTRUNC) dpomode |= DP_OTRUNC;
    if(omode & CR_OSPARSE) dpomode |= DP_OSPARSE;
  }
  if(omode & CR_ONOLCK) dpomode |= DP_ONOLCK;
  if(omode & CR_OLCKNB) dpomode |= DP_OLCKNB;
  attr = NULL;
  lrnum = 0;
  if((omode & CR_OWRITER) && (omode & CR_OCREAT)){
    if(mkdir(name, CR_DIRMODE) == -1 && errno != EEXIST){
      dpecodeset(DP_EMKDIR, __FILE__, __LINE__);
      return NULL;
    }
    sprintf(path, "%s%c%s", name, MYPATHCHR, CR_DPNAME);
    if(!(attr = dpopen(path, dpomode, CR_ATTRBNUM))) return NULL;
    if(dprnum(attr) > 0){
      if((dnum = crdpgetnum(attr, CR_KEYDNUM, -1)) < 1 ||
         (lrnum = crdpgetnum(attr, CR_KEYLRNUM, -1)) < 0){
        dpclose(attr);
        dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
        return NULL;
      }
    } else {
      if(!dpput(attr, CR_KEYDNUM, -1, (char *)&dnum, sizeof(int), DP_DOVER) ||
         !dpput(attr, CR_KEYLRNUM, -1, (char *)&lrnum, sizeof(int), DP_DOVER)){
        dpclose(attr);
        return NULL;
      }
      for(i = 0; i < dnum; i++){
        sprintf(path, "%s%c%04d", name, MYPATHCHR, i + 1);
        if(mkdir(path, CR_DIRMODE) == -1 && errno != EEXIST){
          dpclose(attr);
          dpecodeset(DP_EMKDIR, __FILE__, __LINE__);
          return NULL;
        }
      }
    }
  }
  if(!attr){
    sprintf(path, "%s%c%s", name, MYPATHCHR, CR_DPNAME);
    if(!(attr = dpopen(path, dpomode, 1))) return NULL;
    if(!(omode & CR_OTRUNC)){
      if((dnum = crdpgetnum(attr, CR_KEYDNUM, -1)) < 1 ||
         (lrnum = crdpgetnum(attr, CR_KEYLRNUM, -1)) < 0){
        dpclose(attr);
        dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
        return NULL;
      }
    }
  }
  if(omode & CR_OTRUNC){
    for(i = 0; i < CR_DPMAX; i++){
      sprintf(path, "%s%c%04d%c%s", name, MYPATHCHR, i + 1, MYPATHCHR, CR_DPNAME);
      if(unlink(path) == -1 && errno != ENOENT){
        dpclose(attr);
        dpecodeset(DP_EUNLINK, __FILE__, __LINE__);
        return NULL;
      }
      sprintf(path, "%s%c%04d%c%s", name, MYPATHCHR, i + 1, MYPATHCHR, CR_LOBDIR);
      if(!crrmlobdir(path)){
        dpclose(attr);
        return NULL;
      }
      if(i >= dnum){
        sprintf(path, "%s%c%04d", name, MYPATHCHR, i + 1);
        if(rmdir(path) == -1 && errno != ENOENT){
          dpclose(attr);
          dpecodeset(DP_ERMDIR, __FILE__, __LINE__);
          return NULL;
        }
      }
    }
    errno = 0;
  }
  if(lstat(name, &sbuf) == -1){
    dpclose(attr);
    dpecodeset(DP_ESTAT, __FILE__, __LINE__);
    return NULL;
  }
  inode = sbuf.st_ino;
  if(!(depots = malloc(dnum * sizeof(DEPOT *)))){
    dpclose(attr);
    dpecodeset(DP_EALLOC, __FILE__, __LINE__);
    return NULL;
  }
  for(i = 0; i < dnum; i++){
    sprintf(path, "%s%c%04d%c%s", name, MYPATHCHR, i + 1, MYPATHCHR, CR_DPNAME);
    if(!(depots[i] = dpopen(path, dpomode, bnum))){
      for(j = 0; j < i; j++){
        dpclose(depots[j]);
      }
      free(depots);
      dpclose(attr);
      return NULL;
    }
  }
  curia = malloc(sizeof(CURIA));
  tname = crstrdup(name);
  if(!curia || !tname){
    free(curia);
    free(tname);
    for(i = 0; i < dnum; i++){
      dpclose(depots[i]);
    }
    free(depots);
    dpclose(attr);
    dpecodeset(DP_EALLOC, __FILE__, __LINE__);
    return NULL;
  }
  curia->name = tname;
  curia->wmode = (omode & CR_OWRITER);
  curia->inode = inode;
  curia->attr = attr;
  curia->depots = depots;
  curia->dnum = dnum;
  curia->inum = 0;
  curia->lrnum = lrnum;
  return curia;
}


/* Close a database handle. */
int crclose(CURIA *curia){
  int i, err;
  assert(curia);
  err = FALSE;
  for(i = 0; i < curia->dnum; i++){
    if(!dpclose(curia->depots[i])) err = TRUE;
  }
  free(curia->depots);
  if(curia->wmode){
    if(!dpput(curia->attr, CR_KEYLRNUM, -1, (char *)&(curia->lrnum), sizeof(int), DP_DOVER))
      err = TRUE;
  }
  if(!dpclose(curia->attr)) err = TRUE;
  free(curia->name);
  free(curia);
  return err ? FALSE : TRUE;
}


/* Store a record. */
int crput(CURIA *curia, const char *kbuf, int ksiz, const char *vbuf, int vsiz, int dmode){
  int dpdmode;
  int tnum;
  assert(curia && kbuf && vbuf);
  if(!curia->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  switch(dmode){
  case CR_DKEEP: dpdmode = DP_DKEEP; break;
  case CR_DCAT: dpdmode = DP_DCAT; break;
  default: dpdmode = DP_DOVER; break;
  }
  tnum = dpouterhash(kbuf, ksiz) % curia->dnum;
  return dpput(curia->depots[tnum], kbuf, ksiz, vbuf, vsiz, dpdmode);
}


/* Delete a record. */
int crout(CURIA *curia, const char *kbuf, int ksiz){
  int tnum;
  assert(curia && kbuf);
  if(!curia->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  tnum = dpouterhash(kbuf, ksiz) % curia->dnum;
  return dpout(curia->depots[tnum], kbuf, ksiz);
}


/* Retrieve a record. */
char *crget(CURIA *curia, const char *kbuf, int ksiz, int start, int max, int *sp){
  int tnum;
  assert(curia && kbuf && start >= 0);
  if(ksiz < 0) ksiz = strlen(kbuf);
  tnum = dpouterhash(kbuf, ksiz) % curia->dnum;
  return dpget(curia->depots[tnum], kbuf, ksiz, start, max, sp);
}


/* Retrieve a record and write the value into a buffer. */
int crgetwb(CURIA *curia, const char *kbuf, int ksiz, int start, int max, char *vbuf){
  int tnum;
  assert(curia && kbuf && start >= 0 && max >= 0 && vbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  tnum = dpouterhash(kbuf, ksiz) % curia->dnum;
  return dpgetwb(curia->depots[tnum], kbuf, ksiz, start, max, vbuf);
}


/* Get the size of the value of a record. */
int crvsiz(CURIA *curia, const char *kbuf, int ksiz){
  int tnum;
  assert(curia && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  tnum = dpouterhash(kbuf, ksiz) % curia->dnum;
  return dpvsiz(curia->depots[tnum], kbuf, ksiz);
}


/* Initialize the iterator of a database handle. */
int criterinit(CURIA *curia){
  int i, err;
  assert(curia);
  err = FALSE;
  for(i = 0; i < curia->dnum; i++){
    if(!dpiterinit(curia->depots[i])){
      err = TRUE;
      break;
    }
  }
  curia->inum = 0;
  return err ? FALSE : TRUE;
}


/* Get the next key of the iterator. */
char *criternext(CURIA *curia, int *sp){
  char *kbuf;
  assert(curia);
  kbuf = NULL;
  while(curia->inum < curia->dnum && !(kbuf = dpiternext(curia->depots[curia->inum], sp))){
    if(dpecode != DP_ENOITEM) return NULL;
    (curia->inum)++;
  }
  return kbuf;
}


/* Set alignment of a database handle. */
int crsetalign(CURIA *curia, int align){
  int i, err;
  assert(curia);
  if(!curia->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  for(i = 0; i < curia->dnum; i++){
    if(!dpsetalign(curia->depots[i], align)){
      err = TRUE;
      break;
    }
  }
  return err ? FALSE : TRUE;
}


/* Set the size of the free block pool of a database handle. */
int crsetfbpsiz(CURIA *curia, int size){
  int i, err;
  assert(curia && size >= 0);
  if(!curia->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  for(i = 0; i < curia->dnum; i++){
    if(!dpsetfbpsiz(curia->depots[i], size)){
      err = TRUE;
      break;
    }
  }
  return err ? FALSE : TRUE;
}


/* Synchronize contents of updating a database with the files and the devices. */
int crsync(CURIA *curia){
  int i, err;
  assert(curia);
  if(!curia->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  if(!dpput(curia->attr, CR_KEYLRNUM, -1, (char *)&(curia->lrnum), sizeof(int), DP_DOVER) ||
     !dpsync(curia->attr)) err = TRUE;
  for(i = 0; i < curia->dnum; i++){
    if(!dpsync(curia->depots[i])){
      err = TRUE;
      break;
    }
  }
  return err ? FALSE : TRUE;
}


/* Optimize a database. */
int croptimize(CURIA *curia, int bnum){
  int i, err;
  assert(curia);
  if(!curia->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  for(i = 0; i < curia->dnum; i++){
    if(!dpoptimize(curia->depots[i], bnum)){
      err = TRUE;
      break;
    }
  }
  curia->inum = 0;
  return err ? FALSE : TRUE;
}


/* Get the name of a database. */
char *crname(CURIA *curia){
  char *name;
  assert(curia);
  if(!(name = crstrdup(curia->name))){
    dpecodeset(DP_EALLOC, __FILE__, __LINE__);
    return NULL;
  }
  return name;
}


/* Get the total size of database files. */
int crfsiz(CURIA *curia){
  int i, sum, rv;
  assert(curia);
  if((sum = dpfsiz(curia->attr)) == -1) return -1;
  for(i = 0; i < curia->dnum; i++){
    if((rv = dpfsiz(curia->depots[i])) == -1) return -1;
    sum += rv;
  }
  return sum;
}


/* Get the total size of database files as double-precision value. */
double crfsizd(CURIA *curia){
  double sum;
  int i, rv;
  assert(curia);
  sum = 0.0;
  if((sum = dpfsiz(curia->attr)) < 0) return -1.0;
  for(i = 0; i < curia->dnum; i++){
    if((rv = dpfsiz(curia->depots[i])) == -1) return -1.0;
    sum += rv;
  }
  return sum;
}


/* Get the total number of the elements of each bucket array. */
int crbnum(CURIA *curia){
  int i, sum, rv;
  assert(curia);
  sum = 0;
  for(i = 0; i < curia->dnum; i++){
    rv = dpbnum(curia->depots[i]);
    if(rv == -1) return -1;
    sum += rv;
  }
  return sum;
}


/* Get the total number of the used elements of each bucket array. */
int crbusenum(CURIA *curia){
  int i, sum, rv;
  assert(curia);
  sum = 0;
  for(i = 0; i < curia->dnum; i++){
    rv = dpbusenum(curia->depots[i]);
    if(rv == -1) return -1;
    sum += rv;
  }
  return sum;
}


/* Get the number of the records stored in a database. */
int crrnum(CURIA *curia){
  int i, sum, rv;
  assert(curia);
  sum = 0;
  for(i = 0; i < curia->dnum; i++){
    rv = dprnum(curia->depots[i]);
    if(rv == -1) return -1;
    sum += rv;
  }
  return sum;
}


/* Check whether a database handle is a writer or not. */
int crwritable(CURIA *curia){
  assert(curia);
  return curia->wmode;
}


/* Check whether a database has a fatal error or not. */
int crfatalerror(CURIA *curia){
  int i;
  assert(curia);
  if(dpfatalerror(curia->attr)) return TRUE;
  for(i = 0; i < curia->dnum; i++){
    if(dpfatalerror(curia->depots[i])) return TRUE;
  }
  return FALSE;
}


/* Get the inode number of a database directory. */
int crinode(CURIA *curia){
  assert(curia);
  return curia->inode;
}


/* Get the last modified time of a database. */
time_t crmtime(CURIA *curia){
  assert(curia);
  return dpmtime(curia->attr);
}


/* Remove a database directory. */
int crremove(const char *name){
  struct stat sbuf;
  CURIA *curia;
  char path[CR_PATHBUFSIZ];
  assert(name);
  if(lstat(name, &sbuf) == -1){
    dpecodeset(DP_ESTAT, __FILE__, __LINE__);
    return FALSE;
  }
  if((curia = cropen(name, CR_OWRITER | CR_OTRUNC, 1, 1)) != NULL) crclose(curia);
  sprintf(path, "%s%c0001%c%s", name, MYPATHCHR, MYPATHCHR, CR_DPNAME);
  dpremove(path);
  sprintf(path, "%s%c0001", name, MYPATHCHR);
  if(rmdir(path) == -1){
    dpecodeset(DP_ERMDIR, __FILE__, __LINE__);
    return FALSE;
  }
  sprintf(path, "%s%c%s", name, MYPATHCHR, CR_DPNAME);
  if(!dpremove(path)) return FALSE;
  if(rmdir(name) == -1){
    dpecodeset(DP_ERMDIR, __FILE__, __LINE__);
    return FALSE;
  }
  return TRUE;
}


/* Repair a broken database directory. */
int crrepair(const char *name){
  CURIA *tcuria;
  DEPOT *tdepot;
  char path[CR_PATHBUFSIZ], *kbuf, *vbuf;
  struct stat sbuf;
  int i, j, err, flags, bnum, dnum, ksiz, vsiz;
  assert(name);
  err = FALSE;
  flags = 0;
  bnum = 0;
  dnum = 0;
  sprintf(path, "%s%c%s", name, MYPATHCHR, CR_DPNAME);
  if(lstat(path, &sbuf) != -1){
    if((tdepot = dpopen(path, DP_OREADER, -1)) != NULL){
      flags = dpgetflags(tdepot);
      dpclose(tdepot);
    }
  }
  for(i = 1; i <= CR_DPMAX; i++){
    sprintf(path, "%s%c%04d%c%s", name, MYPATHCHR, i, MYPATHCHR, CR_DPNAME);
    if(lstat(path, &sbuf) != -1){
      dnum++;
      if(!dprepair(path)) err = TRUE;
      if((tdepot = dpopen(path, DP_OREADER, -1)) != NULL){
        bnum += dpbnum(tdepot);
        dpclose(tdepot);
      }
    }
  }
  if(dnum < CR_DEFDNUM) dnum = CR_DEFDNUM;
  bnum /= dnum;
  sprintf(path, "%s%s", name, CR_TMPFSUF);
  if((tcuria = cropen(path, CR_OWRITER | CR_OCREAT | CR_OTRUNC, bnum, dnum)) != NULL){
    if(!crsetflags(tcuria, flags)) err = TRUE;
    for(i = 1; i <= CR_DPMAX; i++){
      sprintf(path, "%s%c%04d%c%s", name, MYPATHCHR, i, MYPATHCHR, CR_DPNAME);
      if(lstat(path, &sbuf) != -1){
        if((tdepot = dpopen(path, DP_OREADER, -1)) != NULL){
          if(!dpiterinit(tdepot)) err = TRUE;
          while((kbuf = dpiternext(tdepot, &ksiz)) != NULL){
            if((vbuf = dpget(tdepot, kbuf, ksiz, 0, -1, &vsiz)) != NULL){
              if(!crput(tcuria, kbuf, ksiz, vbuf, vsiz, CR_DKEEP)) err = TRUE;
              free(vbuf);
            }
            free(kbuf);
          }
          dpclose(tdepot);
        } else {
          err = TRUE;
        }
      }
      for(j = 0; j <= CR_DPMAX; j++){
        sprintf(path, "%s%c%04d%c%s", name, MYPATHCHR, j, MYPATHCHR, CR_LOBDIR);
        if(lstat(path, &sbuf) != -1){
          if(!crcplobdir(tcuria, path)) err = TRUE;
        }
      }
    }
    if(!crclose(tcuria)) err = TRUE;
    if(!crremove(name)) err = TRUE;
    sprintf(path, "%s%s", name, CR_TMPFSUF);
    if(rename(path, name) == -1){
      if(!err) dpecodeset(DP_EMISC, __FILE__, __LINE__);
      err = TRUE;
    }
  } else {
    err = TRUE;
  }
  return err ? FALSE : TRUE;
}


/* Dump all records as endian independent data. */
int crexportdb(CURIA *curia, const char *name){
  char path[CR_PATHBUFSIZ], *kbuf, *vbuf, *pbuf;
  int i, err, *fds, ksiz, vsiz, psiz;
  assert(curia && name);
  if(!(criterinit(curia))) return FALSE;
  if(mkdir(name, CR_DIRMODE) == -1 && errno != EEXIST){
    dpecodeset(DP_EMKDIR, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  fds = malloc(sizeof(int) * curia->dnum);
  for(i = 0; i < curia->dnum; i++){
    sprintf(path, "%s%c%04d", name, MYPATHCHR, i + 1);
    if((fds[i] = open(path, O_RDWR | O_CREAT | O_TRUNC, CR_FILEMODE)) == -1){
      if(!err) dpecodeset(DP_EOPEN, __FILE__, __LINE__);
      err = TRUE;
      break;
    }
  }
  while(!err && (kbuf = criternext(curia, &ksiz)) != NULL){
    if((vbuf = crget(curia, kbuf, ksiz, 0, -1, &vsiz)) != NULL){
      if((pbuf = malloc(ksiz + vsiz + CR_NUMBUFSIZ * 2)) != NULL){
        psiz = 0;
        psiz += sprintf(pbuf + psiz, "%X\n%X\n", ksiz, vsiz);
        memcpy(pbuf + psiz, kbuf, ksiz);
        psiz += ksiz;
        pbuf[psiz++] = '\n';
        memcpy(pbuf + psiz, vbuf, vsiz);
        psiz += vsiz;
        pbuf[psiz++] = '\n';
        if(!crwrite(fds[curia->inum], pbuf, psiz)){
          dpecodeset(DP_EWRITE, __FILE__, __LINE__);
          err = TRUE;
        }
        free(pbuf);
      } else {
        dpecodeset(DP_EALLOC, __FILE__, __LINE__);
        err = TRUE;
      }
      free(vbuf);
    } else {
      err = TRUE;
    }
    free(kbuf);
  }
  for(i = 0; i < curia->dnum; i++){
    if(fds[i] != -1 && close(fds[i]) == -1){
      if(!err) dpecodeset(DP_ECLOSE, __FILE__, __LINE__);
      err = TRUE;
    }
  }
  free(fds);
  return !err && !crfatalerror(curia);
}


/* Load all records from endian independent data. */
int crimportdb(CURIA *curia, const char *name){
  DEPOT *depot;
  char ipath[CR_PATHBUFSIZ], opath[CR_PATHBUFSIZ], *kbuf, *vbuf;
  int i, err, ksiz, vsiz;
  struct stat sbuf;
  assert(curia && name);
  if(!curia->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(crrnum(curia) > 0){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  for(i = 0; !err && i < CR_DPMAX; i++){
    sprintf(ipath, "%s%c%04d", name, MYPATHCHR, i + 1);
    if(lstat(ipath, &sbuf) == -1 || !S_ISREG(sbuf.st_mode)) break;
    sprintf(opath, "%s%c%04d%s", curia->name, MYPATHCHR, i + 1, CR_TMPFSUF);
    if((depot = dpopen(opath, DP_OWRITER | DP_OCREAT | DP_OTRUNC, -1)) != NULL){
      if(dpimportdb(depot, ipath)){
        dpiterinit(depot);
        while((kbuf = dpiternext(depot, &ksiz)) != NULL){
          if((vbuf = dpget(depot, kbuf, ksiz, 0, -1, &vsiz)) != NULL){
            if(!crput(curia, kbuf, ksiz, vbuf, vsiz, CR_DKEEP)) err = TRUE;
            free(vbuf);
          } else {
            err = TRUE;
          }
          free(kbuf);
        }
      } else {
        err = TRUE;
      }
      if(!dpclose(depot)) err = TRUE;
      if(!dpremove(opath)) err = TRUE;
    } else {
      err = TRUE;
    }
  }
  return !err && !crfatalerror(curia);
}


/* Retrieve a record directly from a database directory. */
char *crsnaffle(const char *name, const char *kbuf, int ksiz, int *sp){
  char path[CR_PATHBUFSIZ], *vbuf;
  int dnum, vsiz, tnum;
  assert(name && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  sprintf(path, "%s%c%s", name, MYPATHCHR, CR_DPNAME);
  if(!(vbuf = dpsnaffle(path, CR_KEYDNUM, -1, &vsiz)) || vsiz != sizeof(int) ||
     (dnum = *(int *)vbuf) < 1){
    free(vbuf);
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return NULL;
  }
  free(vbuf);
  tnum = dpouterhash(kbuf, ksiz) % dnum;
  sprintf(path, "%s%c%04d%c%s", name, MYPATHCHR, tnum + 1, MYPATHCHR, CR_DPNAME);
  return dpsnaffle(path, kbuf, ksiz, sp);
}


/* Store a large object. */
int crputlob(CURIA *curia, const char *kbuf, int ksiz, const char *vbuf, int vsiz, int dmode){
  char *path;
  int mode, fd, err, be;
  struct stat sbuf;
  assert(curia && kbuf && vbuf);
  if(!curia->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(vsiz < 0) vsiz = strlen(vbuf);
  if(!(path = crgetlobpath(curia, kbuf, ksiz))) return FALSE;
  if(!crmklobdir(path)){
    free(path);
    return FALSE;
  }
  be = lstat(path, &sbuf) != -1 && S_ISREG(sbuf.st_mode);
  mode = O_RDWR | O_CREAT;
  if(dmode & CR_DKEEP) mode |= O_EXCL;
  if(dmode & CR_DCAT){
    mode |= O_APPEND;
  } else {
    mode |= O_TRUNC;
  }
  if((fd = open(path, mode, CR_FILEMODE)) == -1){
    free(path);
    dpecodeset(DP_EOPEN, __FILE__, __LINE__);
    if(dmode == CR_DKEEP) dpecodeset(DP_EKEEP, __FILE__, __LINE__);
    return FALSE;
  }
  free(path);
  err = FALSE;
  if(crwrite(fd, vbuf, vsiz) == -1){
    err = TRUE;
    dpecodeset(DP_EWRITE, __FILE__, __LINE__);
  }
  if(close(fd) == -1){
    err = TRUE;
    dpecodeset(DP_ECLOSE, __FILE__, __LINE__);
  }
  if(!err && !be) (curia->lrnum)++;
  return err ? FALSE : TRUE;
}


/* Delete a large object. */
int croutlob(CURIA *curia, const char *kbuf, int ksiz){
  char *path;
  int err, be;
  struct stat sbuf;
  assert(curia && kbuf);
  if(!curia->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(!(path = crgetlobpath(curia, kbuf, ksiz))) return FALSE;
  be = lstat(path, &sbuf) != -1 && S_ISREG(sbuf.st_mode);
  err = FALSE;
  if(unlink(path) == -1){
    err = TRUE;
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
  }
  free(path);
  if(!err && be) (curia->lrnum)--;
  return err ? FALSE : TRUE;
}


/* Retrieve a large object. */
char *crgetlob(CURIA *curia, const char *kbuf, int ksiz, int start, int max, int *sp){
  char *path, *buf;
  struct stat sbuf;
  int fd, size;
  assert(curia && kbuf && start >= 0);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(!(path = crgetlobpath(curia, kbuf, ksiz))) return NULL;
  if((fd = open(path, O_RDONLY, CR_FILEMODE)) == -1){
    free(path);
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return NULL;
  }
  free(path);
  if(fstat(fd, &sbuf) == -1){
    close(fd);
    dpecodeset(DP_ESTAT, __FILE__, __LINE__);
    return NULL;
  }
  if(start > sbuf.st_size){
    close(fd);
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return NULL;
  }
  if(lseek(fd, start, SEEK_SET) == -1){
    close(fd);
    dpecodeset(DP_ESEEK, __FILE__, __LINE__);
    return NULL;
  }
  if(max < 0) max = sbuf.st_size;
  if(!(buf = malloc(max + 1))){
    close(fd);
    dpecodeset(DP_EALLOC, __FILE__, __LINE__);
    return NULL;
  }
  size = crread(fd, buf, max);
  close(fd);
  if(size == -1){
    free(buf);
    dpecodeset(DP_EREAD, __FILE__, __LINE__);
    return NULL;
  }
  buf[size] = '\0';
  if(sp) *sp = size;
  return buf;
}


/* Get the file descriptor of a large object. */
int crgetlobfd(CURIA *curia, const char *kbuf, int ksiz){
  char *path;
  int fd;
  assert(curia && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(!(path = crgetlobpath(curia, kbuf, ksiz))) return -1;
  if((fd = open(path, curia->wmode ? O_RDWR: O_RDONLY, CR_FILEMODE)) == -1){
    free(path);
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return -1;
  }
  free(path);
  return fd;
}


/* Get the size of the value of a large object. */
int crvsizlob(CURIA *curia, const char *kbuf, int ksiz){
  char *path;
  struct stat sbuf;
  assert(curia && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(!(path = crgetlobpath(curia, kbuf, ksiz))) return -1;
  if(lstat(path, &sbuf) == -1){
    free(path);
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return -1;
  }
  free(path);
  return sbuf.st_size;
}


/* Get the number of the large objects stored in a database. */
int crrnumlob(CURIA *curia){
  assert(curia);
  return curia->lrnum;
}



/*************************************************************************************************
 * features for experts
 *************************************************************************************************/


/* Synchronize updating contents on memory. */
int crmemsync(CURIA *curia){
  int i, err;
  assert(curia);
  if(!curia->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  if(!dpput(curia->attr, CR_KEYLRNUM, -1, (char *)&(curia->lrnum), sizeof(int), DP_DOVER) ||
     !dpmemsync(curia->attr)) err = TRUE;
  for(i = 0; i < curia->dnum; i++){
    if(!dpmemsync(curia->depots[i])){
      err = TRUE;
      break;
    }
  }
  return err ? FALSE : TRUE;
}


/* Synchronize updating contents on memory, not physically. */
int crmemflush(CURIA *curia){
  int i, err;
  assert(curia);
  if(!curia->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  if(!dpput(curia->attr, CR_KEYLRNUM, -1, (char *)&(curia->lrnum), sizeof(int), DP_DOVER) ||
     !dpmemsync(curia->attr)) err = TRUE;
  for(i = 0; i < curia->dnum; i++){
    if(!dpmemflush(curia->depots[i])){
      err = TRUE;
      break;
    }
  }
  return err ? FALSE : TRUE;
}


/* Get flags of a database. */
int crgetflags(CURIA *curia){
  assert(curia);
  return dpgetflags(curia->attr);
}


/* Set flags of a database. */
int crsetflags(CURIA *curia, int flags){
  assert(curia);
  if(!curia->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  return dpsetflags(curia->attr, flags);
}



/*************************************************************************************************
 * private objects
 *************************************************************************************************/


/* Get a copied string.
   `str' specifies an original string.
   The return value is a copied string whose region is allocated by `malloc'. */
static char *crstrdup(const char *str){
  int len;
  char *buf;
  assert(str);
  len = strlen(str);
  if(!(buf = malloc(len + 1))) return NULL;
  memcpy(buf, str, len + 1);
  return buf;
}


/* Get an integer from a database.
   `depot' specifies an inner database handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the key.
   The return value is the integer of the corresponding record. */
static int crdpgetnum(DEPOT *depot, const char *kbuf, int ksiz){
  char *vbuf;
  int vsiz, rv;
  if(!(vbuf = dpget(depot, kbuf, ksiz, 0, -1, &vsiz)) || vsiz != sizeof(int)){
    free(vbuf);
    return INT_MIN;
  }
  rv = *(int *)vbuf;
  free(vbuf);
  return rv;
}


/* Get the path of a large object.
   `curia' specifies a database handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the key.
   The return value is a path string whose region is allocated by `malloc'. */
static char *crgetlobpath(CURIA *curia, const char *kbuf, int ksiz){
  char prefix[CR_PATHBUFSIZ], *wp, *path;
  int i, hash;
  assert(curia && kbuf && ksiz >= 0);
  wp = prefix;
  wp += sprintf(wp, "%s%c%04d%c%s%c",
                curia->name, MYPATHCHR, dpouterhash(kbuf, ksiz) % curia->dnum + 1,
                MYPATHCHR, CR_LOBDIR, MYPATHCHR);
  hash = dpinnerhash(kbuf, ksiz);
  for(i = 0; i < CR_LOBDDEPTH; i++){
    wp += sprintf(wp, "%02X%c", hash % 0x100, MYPATHCHR);
    hash /= 0x100;
  }
  if(!(path = malloc(strlen(prefix) + ksiz * 2 + 1))){
    dpecodeset(DP_EALLOC, __FILE__, __LINE__);
    return NULL;
  }
  wp = path;
  wp += sprintf(path, "%s", prefix);
  for(i = 0; i < ksiz; i++){
    wp += sprintf(wp, "%02X", ((unsigned char *)kbuf)[i]);
  }
  return path;
}


/* Create directories included in a path.
   `path' specifies a path.
   The return value is true if successful, else, it is false. */
static int crmklobdir(const char *path){
  char elem[CR_PATHBUFSIZ], *wp;
  const char *dp;
  int err, len;
  wp = elem;
  err = FALSE;
  while(*path != '\0' && (dp = strchr(path, MYPATHCHR)) != NULL){
    len = dp - path;
    if((wp != elem || dp == path)) wp += sprintf(wp, "%c", MYPATHCHR);
    memcpy(wp, path, len);
    wp[len] = '\0';
    wp += len;
    if(mkdir(elem, CR_DIRMODE) == -1 && errno != EEXIST) err = TRUE;
    path = dp + 1;
  }
  if(err) dpecodeset(DP_EMKDIR, __FILE__, __LINE__);
  return err ? FALSE : TRUE;
}


/* Remove file and directories under a directory.
   `path' specifies a path.
   The return value is true if successful, else, it is false. */
static int crrmlobdir(const char *path){
  char elem[CR_PATHBUFSIZ];
  DIR *DD;
  struct dirent *dp;
  assert(path);
  if(unlink(path) != -1){
    return TRUE;
  } else {
    if(errno == ENOENT) return TRUE;
    if(!(DD = opendir(path))){
      dpecodeset(DP_EMISC, __FILE__, __LINE__);
      return FALSE;
    }
    while((dp = readdir(DD)) != NULL){
      if(!strcmp(dp->d_name, MYCDIRSTR) || !strcmp(dp->d_name, MYPDIRSTR)) continue;
      sprintf(elem, "%s%c%s", path, MYPATHCHR, dp->d_name);
      if(!crrmlobdir(elem)){
        closedir(DD);
        return FALSE;
      }
    }
  }
  if(closedir(DD) == -1){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  if(rmdir(path) == -1){
    dpecodeset(DP_ERMDIR, __FILE__, __LINE__);
    return FALSE;
  }
  return TRUE;
}


/* Copy file and directories under a directory for repairing.
   `path' specifies a path.
   The return value is true if successful, else, it is false. */
static int crcplobdir(CURIA *curia, const char *path){
  char elem[CR_PATHBUFSIZ], numbuf[3], *rp, *kbuf, *vbuf;
  DIR *DD;
  struct dirent *dp;
  struct stat sbuf;
  int i, ksiz, vsiz, fd;
  assert(curia && path);
  if(lstat(path, &sbuf) == -1){
    dpecodeset(DP_ESTAT, __FILE__, __LINE__);
    return FALSE;
  }
  if(S_ISREG(sbuf.st_mode)){
    rp = strrchr(path, MYPATHCHR) + 1;
    for(i = 0; rp[i] != '\0'; i += 2){
      numbuf[0] = rp[i];
      numbuf[1] = rp[i+1];
      numbuf[2] = '\0';
      elem[i/2] = (int)strtol(numbuf, NULL, 16);
    }
    kbuf = elem;
    ksiz = i / 2;
    vsiz = sbuf.st_size;
    if(!(vbuf = malloc(vsiz + 1))){
      dpecodeset(DP_EALLOC, __FILE__, __LINE__);
      return FALSE;
    }
    if((fd = open(path, O_RDONLY, CR_FILEMODE)) == -1){
      free(vbuf);
      dpecodeset(DP_EOPEN, __FILE__, __LINE__);
      return FALSE;
    }
    if(crread(fd, vbuf, vsiz) == -1){
      close(fd);
      free(vbuf);
      dpecodeset(DP_EOPEN, __FILE__, __LINE__);
      return FALSE;
    }
    if(!crputlob(curia, kbuf, ksiz, vbuf, vsiz, DP_DOVER)){
      close(fd);
      free(vbuf);
      return FALSE;
    }
    close(fd);
    free(vbuf);
    return TRUE;
  }
  if(!(DD = opendir(path))){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  while((dp = readdir(DD)) != NULL){
    if(!strcmp(dp->d_name, MYCDIRSTR) || !strcmp(dp->d_name, MYPDIRSTR)) continue;
    sprintf(elem, "%s%c%s", path, MYPATHCHR, dp->d_name);
    if(!crcplobdir(curia, elem)){
      closedir(DD);
      return FALSE;
    }
  }
  if(closedir(DD) == -1){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  return TRUE;
}


/* Write into a file.
   `fd' specifies a file descriptor.
   `buf' specifies a buffer to write.
   `size' specifies the size of the buffer.
   The return value is the size of the written buffer, or, -1 on failure. */
static int crwrite(int fd, const void *buf, int size){
  char *lbuf;
  int rv, wb;
  assert(fd >= 0 && buf && size >= 0);
  lbuf = (char *)buf;
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


/* Read from a file and store the data into a buffer.
   `fd' specifies a file descriptor.
   `buffer' specifies a buffer to store into.
   `size' specifies the size to read with.
   The return value is the size read with, or, -1 on failure. */
static int crread(int fd, void *buf, int size){
  char *lbuf;
  int i, bs;
  assert(fd >= 0 && buf && size >= 0);
  lbuf = buf;
  for(i = 0; i < size && (bs = read(fd, lbuf + i, size - i)) != 0; i += bs){
    if(bs == -1 && errno != EINTR) return -1;
  }
  return i;
}



/* END OF FILE */
