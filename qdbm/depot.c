/*************************************************************************************************
 * Implementation of Depot
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

#include "depot.h"
#include "myconf.h"

#define DP_FILEMODE    00644             /* permission of a creating file */
#define DP_MAGICNUMB   "[DEPOT]\n\f"     /* magic number on environments of big endian */
#define DP_MAGICNUML   "[depot]\n\f"     /* magic number on environments of little endian */
#define DP_HEADSIZ     48                /* size of the reagion of the header */
#define DP_LIBVEROFF   12                /* offset of the region for the library version */
#define DP_FLAGSOFF    16                /* offset of the region for flags */
#define DP_FSIZOFF     24                /* offset of the region for the file size */
#define DP_BNUMOFF     32                /* offset of the region for the bucket number */
#define DP_RNUMOFF     40                /* offset of the region for the record number */
#define DP_DEFBNUM     8191              /* default bucket number */
#define DP_FBPOOLSIZ   16                /* size of free block pool */
#define DP_ENTBUFSIZ   128               /* size of the entity buffer */
#define DP_STKBUFSIZ   256               /* size of the stack key buffer */
#define DP_WRTBUFSIZ   8192              /* size of the writing buffer */
#define DP_FSBLKSIZ    4096              /* size of a block of the file system */
#define DP_TMPFSUF     MYEXTSTR "dptmp"  /* suffix of a temporary file */
#define DP_OPTBLOAD    0.25              /* ratio of bucket loading at optimization */
#define DP_OPTRUNIT    256               /* number of records in a process of optimization */
#define DP_NUMBUFSIZ   32                /* size of a buffer for a number */
#define DP_IOBUFSIZ    8192              /* size of an I/O buffer */

/* get the first hash value */
#define DP_FIRSTHASH(DP_res, DP_kbuf, DP_ksiz) \
  do { \
    const unsigned char *_DP_p; \
    int _DP_ksiz; \
    _DP_p = (const unsigned char *)(DP_kbuf); \
    _DP_ksiz = DP_ksiz; \
    if((_DP_ksiz) == sizeof(int)){ \
      memcpy(&(DP_res), (DP_kbuf), sizeof(int)); \
    } else { \
      (DP_res) = 751; \
    } \
    while(_DP_ksiz--){ \
      (DP_res) = (DP_res) * 31 + *(_DP_p)++; \
    } \
    (DP_res) = ((DP_res) * 87767623) & INT_MAX; \
  } while(FALSE)

/* get the second hash value */
#define DP_SECONDHASH(DP_res, DP_kbuf, DP_ksiz) \
  do { \
    const unsigned char *_DP_p; \
    int _DP_ksiz; \
    _DP_p = (const unsigned char *)(DP_kbuf) + DP_ksiz - 1; \
    _DP_ksiz = DP_ksiz; \
    for((DP_res) = 19780211; _DP_ksiz--;){ \
      (DP_res) = (DP_res) * 37 + *(_DP_p)--; \
    } \
    (DP_res) = ((DP_res) * 43321879) & INT_MAX; \
  } while(FALSE)

/* get the third hash value */
#define DP_THIRDHASH(DP_res, DP_kbuf, DP_ksiz) \
  do { \
    int _DP_i; \
    (DP_res) = 774831917; \
    for(_DP_i = (DP_ksiz) - 1; _DP_i >= 0; _DP_i--){ \
      (DP_res) = (DP_res) * 29 + ((const unsigned char *)(DP_kbuf))[_DP_i]; \
    } \
    (DP_res) = ((DP_res) * 5157883) & INT_MAX; \
  } while(FALSE)

enum {                                   /* enumeration for a record header */
  DP_RHIFLAGS,                           /* offset of flags */
  DP_RHIHASH,                            /* offset of value of the second hash function */
  DP_RHIKSIZ,                            /* offset of the size of the key */
  DP_RHIVSIZ,                            /* offset of the size of the value */
  DP_RHIPSIZ,                            /* offset of the size of the padding bytes */
  DP_RHILEFT,                            /* offset of the offset of the left child */
  DP_RHIRIGHT,                           /* offset of the offset of the right child */
  DP_RHNUM                               /* number of elements of a header */
};

enum {                                   /* enumeration for the flag of a record */
  DP_RECFDEL = 1 << 0,                   /* deleted */
  DP_RECFREUSE = 1 << 1                  /* reusable */
};


/* private function prototypes */
static int dpbigendian(void);
static char *dpstrdup(const char *str);
static int dplock(int fd, int ex, int nb);
static int dpwrite(int fd, const void *buf, int size);
static int dpseekwrite(int fd, int off, const void *buf, int size);
static int dpseekwritenum(int fd, int off, int num);
static int dpread(int fd, void *buf, int size);
static int dpseekread(int fd, int off, void *buf, int size);
static int dpfcopy(int destfd, int destoff, int srcfd, int srcoff);
static int dpgetprime(int num);
static int dppadsize(DEPOT *depot, int ksiz, int vsiz);
static int dprecsize(int *head);
static int dprechead(DEPOT *depot, int off, int *head, char *ebuf, int *eep);
static char *dpreckey(DEPOT *depot, int off, int *head);
static char *dprecval(DEPOT *depot, int off, int *head, int start, int max);
static int dprecvalwb(DEPOT *depot, int off, int *head, int start, int max, char *vbuf);
static int dpkeycmp(const char *abuf, int asiz, const char *bbuf, int bsiz);
static int dprecsearch(DEPOT *depot, const char *kbuf, int ksiz, int hash, int *bip, int *offp,
                       int *entp, int *head, char *ebuf, int *eep, int delhit);
static int dprecrewrite(DEPOT *depot, int off, int rsiz, const char *kbuf, int ksiz,
                        const char *vbuf, int vsiz, int hash, int left, int right);
static int dprecappend(DEPOT *depot, const char *kbuf, int ksiz, const char *vbuf, int vsiz,
                       int hash, int left, int right);
static int dprecover(DEPOT *depot, int off, int *head, const char *vbuf, int vsiz, int cat);
static int dprecdelete(DEPOT *depot, int off, int *head, int reusable);
static void dpfbpoolcoal(DEPOT *depot);
static int dpfbpoolcmp(const void *a, const void *b);



/*************************************************************************************************
 * public objects
 *************************************************************************************************/


/* String containing the version information. */
const char *dpversion = _QDBM_VERSION;


/* Get a message string corresponding to an error code. */
const char *dperrmsg(int ecode){
  switch(ecode){
  case DP_ENOERR: return "no error";
  case DP_EFATAL: return "with fatal error";
  case DP_EMODE: return "invalid mode";
  case DP_EBROKEN: return "broken database file";
  case DP_EKEEP: return "existing record";
  case DP_ENOITEM: return "no item found";
  case DP_EALLOC: return "memory allocation error";
  case DP_EMAP: return "memory mapping error";
  case DP_EOPEN: return "open error";
  case DP_ECLOSE: return "close error";
  case DP_ETRUNC: return "trunc error";
  case DP_ESYNC: return "sync error";
  case DP_ESTAT: return "stat error";
  case DP_ESEEK: return "seek error";
  case DP_EREAD: return "read error";
  case DP_EWRITE: return "write error";
  case DP_ELOCK: return "lock error";
  case DP_EUNLINK: return "unlink error";
  case DP_EMKDIR: return "mkdir error";
  case DP_ERMDIR: return "rmdir error";
  case DP_EMISC: return "miscellaneous error";
  }
  return "(invalid ecode)";
}


/* Get a database handle. */
DEPOT *dpopen(const char *name, int omode, int bnum){
  char hbuf[DP_HEADSIZ], *map, c, *tname;
  int i, mode, fd, inode, fsiz, rnum, msiz, *fbpool;
  struct stat sbuf;
  time_t mtime;
  DEPOT *depot;
  assert(name);
  mode = O_RDONLY;
  if(omode & DP_OWRITER){
    mode = O_RDWR;
    if(omode & DP_OCREAT) mode |= O_CREAT;
  }
  if((fd = open(name, mode, DP_FILEMODE)) == -1){
    dpecodeset(DP_EOPEN, __FILE__, __LINE__);
    return NULL;
  }
  if(!(omode & DP_ONOLCK)){
    if(!dplock(fd, omode & DP_OWRITER, omode & DP_OLCKNB)){
      close(fd);
      return NULL;
    }
  }
  if((omode & DP_OWRITER) && (omode & DP_OTRUNC)){
    if(ftruncate(fd, 0) == -1){
      close(fd);
      dpecodeset(DP_ETRUNC, __FILE__, __LINE__);
      return NULL;
    }
  }
  if(fstat(fd, &sbuf) == -1 || !S_ISREG(sbuf.st_mode) ||
     (sbuf.st_ino == 0 && lstat(name, &sbuf) == -1)){
    close(fd);
    dpecodeset(DP_ESTAT, __FILE__, __LINE__);
    return NULL;
  }
  inode = sbuf.st_ino;
  mtime = sbuf.st_mtime;
  fsiz = sbuf.st_size;
  if((omode & DP_OWRITER) && fsiz == 0){
    memset(hbuf, 0, DP_HEADSIZ);
    if(dpbigendian()){
      memcpy(hbuf, DP_MAGICNUMB, strlen(DP_MAGICNUMB));
    } else {
      memcpy(hbuf, DP_MAGICNUML, strlen(DP_MAGICNUML));
    }
    sprintf(hbuf + DP_LIBVEROFF, "%d", _QDBM_LIBVER / 100);
    bnum = bnum < 1 ? DP_DEFBNUM : bnum;
    bnum = dpgetprime(bnum);
    memcpy(hbuf + DP_BNUMOFF, &bnum, sizeof(int));
    rnum = 0;
    memcpy(hbuf + DP_RNUMOFF, &rnum, sizeof(int));
    fsiz = DP_HEADSIZ + bnum * sizeof(int);
    memcpy(hbuf + DP_FSIZOFF, &fsiz, sizeof(int));
    if(!dpseekwrite(fd, 0, hbuf, DP_HEADSIZ)){
      close(fd);
      return NULL;
    }
    if(omode & DP_OSPARSE){
      c = 0;
      if(!dpseekwrite(fd, fsiz - 1, &c, 1)){
        close(fd);
        return NULL;
      }
    } else {
      if(!(map = malloc(bnum * sizeof(int)))){
        close(fd);
        dpecodeset(DP_EALLOC, __FILE__, __LINE__);
        return NULL;
      }
      memset(map, 0, bnum * sizeof(int));
      if(!dpseekwrite(fd, DP_HEADSIZ, map, bnum * sizeof(int))){
        free(map);
        close(fd);
        return NULL;
      }
      free(map);
    }
  }
  if(!dpseekread(fd, 0, hbuf, DP_HEADSIZ)){
    close(fd);
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return NULL;
  }
  if(!(omode & DP_ONOLCK) &&
     ((dpbigendian() ? memcmp(hbuf, DP_MAGICNUMB, strlen(DP_MAGICNUMB)) != 0 :
       memcmp(hbuf, DP_MAGICNUML, strlen(DP_MAGICNUML)) != 0) ||
      *((int *)(hbuf + DP_FSIZOFF)) != fsiz)){
    close(fd);
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return NULL;
  }
  bnum = *((int *)(hbuf + DP_BNUMOFF));
  rnum = *((int *)(hbuf + DP_RNUMOFF));
  if(bnum < 1 || rnum < 0 || fsiz < DP_HEADSIZ + bnum * sizeof(int)){
    close(fd);
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return NULL;
  }
  msiz = DP_HEADSIZ + bnum * sizeof(int);
  map = mmap(0, msiz, PROT_READ | ((mode & DP_OWRITER) ? PROT_WRITE : 0), MAP_SHARED, fd, 0);
  if(map == MAP_FAILED){
    close(fd);
    dpecodeset(DP_EMAP, __FILE__, __LINE__);
    return NULL;
  }
  tname = NULL;
  fbpool = NULL;
  if(!(depot = malloc(sizeof(DEPOT))) || !(tname = dpstrdup(name)) ||
     !(fbpool = malloc(DP_FBPOOLSIZ * 2 * sizeof(int)))){
    free(fbpool);
    free(tname);
    free(depot);
    munmap(map, msiz);
    close(fd);
    dpecodeset(DP_EALLOC, __FILE__, __LINE__);
    return NULL;
  }
  depot->name = tname;
  depot->wmode = (mode & DP_OWRITER);
  depot->inode = inode;
  depot->mtime = mtime;
  depot->fd = fd;
  depot->fsiz = fsiz;
  depot->map = map;
  depot->msiz = msiz;
  depot->buckets = (int *)(map + DP_HEADSIZ);
  depot->bnum = bnum;
  depot->rnum = rnum;
  depot->fatal = FALSE;
  depot->ioff = 0;
  depot->fbpool = fbpool;
  for(i = 0; i < DP_FBPOOLSIZ * 2; i += 2){
    depot->fbpool[i] = -1;
    depot->fbpool[i+1] = -1;
  }
  depot->fbpsiz = DP_FBPOOLSIZ * 2;
  depot->fbpinc = 0;
  depot->align = 0;
  return depot;
}


/* Close a database handle. */
int dpclose(DEPOT *depot){
  int fatal, err;
  assert(depot);
  fatal = depot->fatal;
  err = FALSE;
  if(depot->wmode){
    *((int *)(depot->map + DP_FSIZOFF)) = depot->fsiz;
    *((int *)(depot->map + DP_RNUMOFF)) = depot->rnum;
  }
  if(depot->map != MAP_FAILED){
    if(munmap(depot->map, depot->msiz) == -1){
      err = TRUE;
      dpecodeset(DP_EMAP, __FILE__, __LINE__);
    }
  }
  if(close(depot->fd) == -1){
    err = TRUE;
    dpecodeset(DP_ECLOSE, __FILE__, __LINE__);
  }
  free(depot->fbpool);
  free(depot->name);
  free(depot);
  if(fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  return err ? FALSE : TRUE;
}


/* Store a record. */
int dpput(DEPOT *depot, const char *kbuf, int ksiz, const char *vbuf, int vsiz, int dmode){
  int head[DP_RHNUM], next[DP_RHNUM];
  int i, hash, bi, off, entoff, ee, newoff, rsiz, nsiz, fdel, mroff, mrsiz, mi, min;
  char ebuf[DP_ENTBUFSIZ], *tval, *swap;
  assert(depot && kbuf && vbuf);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  if(!depot->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(vsiz < 0) vsiz = strlen(vbuf);
  newoff = -1;
  DP_SECONDHASH(hash, kbuf, ksiz);
  switch(dprecsearch(depot, kbuf, ksiz, hash, &bi, &off, &entoff, head, ebuf, &ee, TRUE)){
  case -1:
    depot->fatal = TRUE;
    return FALSE;
  case 0:
    fdel = head[DP_RHIFLAGS] & DP_RECFDEL;
    if(dmode == DP_DKEEP && !fdel){
      dpecodeset(DP_EKEEP, __FILE__, __LINE__);
      return FALSE;
    }
    if(fdel){
      head[DP_RHIPSIZ] += head[DP_RHIVSIZ];
      head[DP_RHIVSIZ] = 0;
    }
    rsiz = dprecsize(head);
    nsiz = DP_RHNUM * sizeof(int) + ksiz + vsiz;
    if(dmode == DP_DCAT) nsiz += head[DP_RHIVSIZ];
    if(off + rsiz >= depot->fsiz){
      if(rsiz < nsiz){
        head[DP_RHIPSIZ] += nsiz - rsiz;
        rsiz = nsiz;
        depot->fsiz = off + rsiz;
      }
    } else {
      while(nsiz > rsiz && off + rsiz < depot->fsiz){
        if(!dprechead(depot, off + rsiz, next, NULL, NULL)) return FALSE;
        if(!(next[DP_RHIFLAGS] & DP_RECFREUSE)) break;
        head[DP_RHIPSIZ] += dprecsize(next);
        rsiz += dprecsize(next);
      }
      for(i = 0; i < depot->fbpsiz; i += 2){
        if(depot->fbpool[i] >= off && depot->fbpool[i] < off + rsiz){
          depot->fbpool[i] = -1;
          depot->fbpool[i+1] = -1;
        }
      }
    }
    if(nsiz <= rsiz){
      if(!dprecover(depot, off, head, vbuf, vsiz, dmode == DP_DCAT)){
        depot->fatal = TRUE;
        return FALSE;
      }
    } else {
      tval = NULL;
      if(dmode == DP_DCAT){
        if(ee && DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ] + head[DP_RHIVSIZ] <= DP_ENTBUFSIZ){
          if(!(tval = malloc(head[DP_RHIVSIZ] + vsiz + 1))){
            dpecodeset(DP_EALLOC, __FILE__, __LINE__);
            depot->fatal = TRUE;
            return FALSE;
          }
          memcpy(tval, ebuf + (DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ]), head[DP_RHIVSIZ]);
        } else {
          if(!(tval = dprecval(depot, off, head, 0, -1))){
            depot->fatal = TRUE;
            return FALSE;
          }
          if(!(swap = realloc(tval, head[DP_RHIVSIZ] + vsiz + 1))){
            free(tval);
            dpecodeset(DP_EALLOC, __FILE__, __LINE__);
            depot->fatal = TRUE;
            return FALSE;
          }
          tval = swap;
        }
        memcpy(tval + head[DP_RHIVSIZ], vbuf, vsiz);
        vsiz += head[DP_RHIVSIZ];
        vbuf = tval;
      }
      mi = -1;
      min = -1;
      for(i = 0; i < depot->fbpsiz; i += 2){
        if(depot->fbpool[i+1] < nsiz) continue;
        if(mi == -1 || depot->fbpool[i+1] < min){
          mi = i;
          min = depot->fbpool[i+1];
        }
      }
      if(mi >= 0){
        mroff = depot->fbpool[mi];
        mrsiz = depot->fbpool[mi+1];
        depot->fbpool[mi] = -1;
        depot->fbpool[mi+1] = -1;
      } else {
        mroff = -1;
        mrsiz = -1;
      }
      if(!dprecdelete(depot, off, head, TRUE)){
        free(tval);
        depot->fatal = TRUE;
        return FALSE;
      }
      if(mroff > 0 && nsiz <= mrsiz){
        if(!dprecrewrite(depot, mroff, mrsiz, kbuf, ksiz, vbuf, vsiz,
                         hash, head[DP_RHILEFT], head[DP_RHIRIGHT])){
          free(tval);
          depot->fatal = TRUE;
          return FALSE;
        }
        newoff = mroff;
      } else {
        if((newoff = dprecappend(depot, kbuf, ksiz, vbuf, vsiz,
                                 hash, head[DP_RHILEFT], head[DP_RHIRIGHT])) == -1){
          free(tval);
          depot->fatal = TRUE;
          return FALSE;
        }
      }
      free(tval);
    }
    if(fdel) depot->rnum++;
    break;
  default:
    if((newoff = dprecappend(depot, kbuf, ksiz, vbuf, vsiz, hash, 0, 0)) == -1){
      depot->fatal = TRUE;
      return FALSE;
    }
    depot->rnum++;
    break;
  }
  if(newoff > 0){
    if(entoff > 0){
      if(!dpseekwritenum(depot->fd, entoff, newoff)){
        depot->fatal = TRUE;
        return FALSE;
      }
    } else {
      depot->buckets[bi] = newoff;
    }
  }
  return TRUE;
}


/* Delete a record. */
int dpout(DEPOT *depot, const char *kbuf, int ksiz){
  int head[DP_RHNUM], hash, bi, off, entoff, ee;
  char ebuf[DP_ENTBUFSIZ];
  assert(depot && kbuf);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  if(!depot->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  DP_SECONDHASH(hash, kbuf, ksiz);
  switch(dprecsearch(depot, kbuf, ksiz, hash, &bi, &off, &entoff, head, ebuf, &ee, FALSE)){
  case -1:
    depot->fatal = TRUE;
    return FALSE;
  case 0:
    break;
  default:
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  if(!dprecdelete(depot, off, head, FALSE)){
    depot->fatal = TRUE;
    return FALSE;
  }
  depot->rnum--;
  return TRUE;
}


/* Retrieve a record. */
char *dpget(DEPOT *depot, const char *kbuf, int ksiz, int start, int max, int *sp){
  int head[DP_RHNUM], hash, bi, off, entoff, ee, vsiz;
  char ebuf[DP_ENTBUFSIZ], *vbuf;
  assert(depot && kbuf && start >= 0);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return NULL;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  DP_SECONDHASH(hash, kbuf, ksiz);
  switch(dprecsearch(depot, kbuf, ksiz, hash, &bi, &off, &entoff, head, ebuf, &ee, FALSE)){
  case -1:
    depot->fatal = TRUE;
    return NULL;
  case 0:
    break;
  default:
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return NULL;
  }
  if(start > head[DP_RHIVSIZ]){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return NULL;
  }
  if(ee && DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ] + head[DP_RHIVSIZ] <= DP_ENTBUFSIZ){
    head[DP_RHIVSIZ] -= start;
    if(max < 0){
      vsiz = head[DP_RHIVSIZ];
    } else {
      vsiz = max < head[DP_RHIVSIZ] ? max : head[DP_RHIVSIZ];
    }
    if(!(vbuf = malloc(vsiz + 1))){
      dpecodeset(DP_EALLOC, __FILE__, __LINE__);
      depot->fatal = TRUE;
      return NULL;
    }
    memcpy(vbuf, ebuf + (DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ] + start), vsiz);
    vbuf[vsiz] = '\0';
  } else {
    if(!(vbuf = dprecval(depot, off, head, start, max))){
      depot->fatal = TRUE;
      return NULL;
    }
  }
  if(sp){
    if(max < 0){
      *sp = head[DP_RHIVSIZ];
    } else {
      *sp = max < head[DP_RHIVSIZ] ? max : head[DP_RHIVSIZ];
    }
  }
  return vbuf;
}


/* Retrieve a record and write the value into a buffer. */
int dpgetwb(DEPOT *depot, const char *kbuf, int ksiz, int start, int max, char *vbuf){
  int head[DP_RHNUM], hash, bi, off, entoff, ee, vsiz;
  char ebuf[DP_ENTBUFSIZ];
  assert(depot && kbuf && start >= 0 && max >= 0 && vbuf);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return -1;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  DP_SECONDHASH(hash, kbuf, ksiz);
  switch(dprecsearch(depot, kbuf, ksiz, hash, &bi, &off, &entoff, head, ebuf, &ee, FALSE)){
  case -1:
    depot->fatal = TRUE;
    return -1;
  case 0:
    break;
  default:
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return -1;
  }
  if(start > head[DP_RHIVSIZ]){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return -1;
  }
  if(ee && DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ] + head[DP_RHIVSIZ] <= DP_ENTBUFSIZ){
    head[DP_RHIVSIZ] -= start;
    vsiz = max < head[DP_RHIVSIZ] ? max : head[DP_RHIVSIZ];
    memcpy(vbuf, ebuf + (DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ] + start), vsiz);
  } else {
    if((vsiz = dprecvalwb(depot, off, head, start, max, vbuf)) == -1){
      depot->fatal = TRUE;
      return -1;
    }
  }
  return vsiz;
}


/* Get the size of the value of a record. */
int dpvsiz(DEPOT *depot, const char *kbuf, int ksiz){
  int head[DP_RHNUM], hash, bi, off, entoff, ee;
  char ebuf[DP_ENTBUFSIZ];
  assert(depot && kbuf);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return -1;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  DP_SECONDHASH(hash, kbuf, ksiz);
  switch(dprecsearch(depot, kbuf, ksiz, hash, &bi, &off, &entoff, head, ebuf, &ee, FALSE)){
  case -1:
    depot->fatal = TRUE;
    return -1;
  case 0:
    break;
  default:
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return -1;
  }
  return head[DP_RHIVSIZ];
}


/* Initialize the iterator of a database handle. */
int dpiterinit(DEPOT *depot){
  assert(depot);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  depot->ioff = 0;
  return TRUE;
}


/* Get the next key of the iterator. */
char *dpiternext(DEPOT *depot, int *sp){
  int off, head[DP_RHNUM], ee;
  char ebuf[DP_ENTBUFSIZ], *kbuf;
  assert(depot);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return NULL;
  }
  off = DP_HEADSIZ + depot->bnum * sizeof(int);
  off = off > depot->ioff ? off : depot->ioff;
  while(off < depot->fsiz){
    if(!dprechead(depot, off, head, ebuf, &ee)){
      depot->fatal = TRUE;
      return NULL;
    }
    if(head[DP_RHIFLAGS] & DP_RECFDEL){
      off += dprecsize(head);
    } else {
      if(ee && DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ] <= DP_ENTBUFSIZ){
        if(!(kbuf = malloc(head[DP_RHIKSIZ] + 1))){
          dpecodeset(DP_EALLOC, __FILE__, __LINE__);
          depot->fatal = TRUE;
          return NULL;
        }
        memcpy(kbuf, ebuf + (DP_RHNUM * sizeof(int)), head[DP_RHIKSIZ]);
        kbuf[head[DP_RHIKSIZ]] = '\0';
      } else {
        if(!(kbuf = dpreckey(depot, off, head))){
          depot->fatal = TRUE;
          return NULL;
        }
      }
      depot->ioff = off + dprecsize(head);
      if(sp) *sp = head[DP_RHIKSIZ];
      return kbuf;
    }
  }
  dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
  return NULL;
}


/* Set alignment of a database handle. */
int dpsetalign(DEPOT *depot, int align){
  assert(depot);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  if(!depot->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  depot->align = align;
  return TRUE;
}


/* Set the size of the free block pool of a database handle. */
int dpsetfbpsiz(DEPOT *depot, int size){
  int *fbpool;
  int i;
  assert(depot && size >= 0);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  if(!depot->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  size *= 2;
  if(!(fbpool = realloc(depot->fbpool, size * sizeof(int) + 1))){
    dpecodeset(DP_EALLOC, __FILE__, __LINE__);
    return FALSE;
  }
  for(i = 0; i < size; i += 2){
    fbpool[i] = -1;
    fbpool[i+1] = -1;
  }
  depot->fbpool = fbpool;
  depot->fbpsiz = size;
  return TRUE;
}



/* Synchronize contents of updating a database with the file and the device. */
int dpsync(DEPOT *depot){
  assert(depot);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  if(!depot->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  *((int *)(depot->map + DP_FSIZOFF)) = depot->fsiz;
  *((int *)(depot->map + DP_RNUMOFF)) = depot->rnum;
  if(msync(depot->map, depot->msiz, MS_SYNC) == -1){
    dpecodeset(DP_EMAP, __FILE__, __LINE__);
    depot->fatal = TRUE;
    return FALSE;
  }
  if(fsync(depot->fd) == -1){
    dpecodeset(DP_ESYNC, __FILE__, __LINE__);
    depot->fatal = TRUE;
    return FALSE;
  }
  return TRUE;
}


/* Optimize a database. */
int dpoptimize(DEPOT *depot, int bnum){
  DEPOT *tdepot;
  char *name;
  int i, err, off, head[DP_RHNUM], ee, ksizs[DP_OPTRUNIT], vsizs[DP_OPTRUNIT], unum;
  char ebuf[DP_ENTBUFSIZ], *kbufs[DP_OPTRUNIT], *vbufs[DP_OPTRUNIT];
  assert(depot);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  if(!depot->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(!(name = malloc(strlen(depot->name) + strlen(DP_TMPFSUF) + 1))){
    dpecodeset(DP_EALLOC, __FILE__, __LINE__);
    depot->fatal = FALSE;
    return FALSE;
  }
  sprintf(name, "%s%s", depot->name, DP_TMPFSUF);
  if(bnum < 0){
    bnum = (int)(depot->rnum * (1.0 / DP_OPTBLOAD)) + 1;
    if(bnum < DP_DEFBNUM / 2) bnum = DP_DEFBNUM / 2;
  }
  if(!(tdepot = dpopen(name, DP_OWRITER | DP_OCREAT | DP_OTRUNC, bnum))){
    free(name);
    depot->fatal = TRUE;
    return FALSE;
  }
  free(name);
  if(!dpsetflags(tdepot, dpgetflags(depot))){
    dpclose(tdepot);
    depot->fatal = TRUE;
    return FALSE;
  }
  tdepot->align = depot->align;
  err = FALSE;
  off = DP_HEADSIZ + depot->bnum * sizeof(int);
  unum = 0;
  while(off < depot->fsiz){
    if(!dprechead(depot, off, head, ebuf, &ee)){
      err = TRUE;
      break;
    }
    if(!(head[DP_RHIFLAGS] & DP_RECFDEL)){
      if(ee && DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ] <= DP_ENTBUFSIZ){
        if(!(kbufs[unum] = malloc(head[DP_RHIKSIZ] + 1))){
          dpecodeset(DP_EALLOC, __FILE__, __LINE__);
          err = TRUE;
          break;
        }
        memcpy(kbufs[unum], ebuf + (DP_RHNUM * sizeof(int)), head[DP_RHIKSIZ]);
        if(DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ] + head[DP_RHIVSIZ] <= DP_ENTBUFSIZ){
          if(!(vbufs[unum] = malloc(head[DP_RHIVSIZ] + 1))){
            dpecodeset(DP_EALLOC, __FILE__, __LINE__);
            err = TRUE;
            break;
          }
          memcpy(vbufs[unum], ebuf + (DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ]),
                 head[DP_RHIVSIZ]);
        } else {
          vbufs[unum] = dprecval(depot, off, head, 0, -1);
        }
      } else {
        kbufs[unum] = dpreckey(depot, off, head);
        vbufs[unum] = dprecval(depot, off, head, 0, -1);
      }
      ksizs[unum] = head[DP_RHIKSIZ];
      vsizs[unum] = head[DP_RHIVSIZ];
      unum++;
      if(unum >= DP_OPTRUNIT){
        for(i = 0; i < unum; i++){
          if(kbufs[i] && vbufs[i]){
            if(!dpput(tdepot, kbufs[i], ksizs[i], vbufs[i], vsizs[i], DP_DKEEP)) err = TRUE;
          } else {
            err = TRUE;
          }
          free(kbufs[i]);
          free(vbufs[i]);
          if(err) break;
        }
        unum = 0;
      }
    }
    off += dprecsize(head);
    if(err) break;
  }
  for(i = 0; i < unum; i++){
    if(kbufs[i] && vbufs[i]){
      if(!dpput(tdepot, kbufs[i], ksizs[i], vbufs[i], vsizs[i], DP_DKEEP)) err = TRUE;
    } else {
      err = TRUE;
    }
    free(kbufs[i]);
    free(vbufs[i]);
    if(err) break;
  }
  if(!dpsync(tdepot)) err = TRUE;
  if(err){
    unlink(tdepot->name);
    dpclose(tdepot);
    depot->fatal = TRUE;
    return FALSE;
  }
  if(munmap(depot->map, depot->msiz) == -1){
    dpclose(tdepot);
    dpecodeset(DP_EMAP, __FILE__, __LINE__);
    depot->fatal = TRUE;
    return FALSE;
  }
  depot->map = MAP_FAILED;
  if(ftruncate(depot->fd, 0) == -1){
    dpclose(tdepot);
    unlink(tdepot->name);
    dpecodeset(DP_ETRUNC, __FILE__, __LINE__);
    depot->fatal = TRUE;
    return FALSE;
  }
  if(dpfcopy(depot->fd, 0, tdepot->fd, 0) == -1){
    dpclose(tdepot);
    unlink(tdepot->name);
    depot->fatal = TRUE;
    return FALSE;
  }
  depot->fsiz = tdepot->fsiz;
  depot->bnum = tdepot->bnum;
  depot->ioff = 0;
  for(i = 0; i < depot->fbpsiz; i += 2){
    depot->fbpool[i] = -1;
    depot->fbpool[i+1] = -1;
  }
  depot->msiz = tdepot->msiz;
  depot->map = mmap(0, depot->msiz, PROT_READ | PROT_WRITE, MAP_SHARED, depot->fd, 0);
  if(depot->map == MAP_FAILED){
    dpecodeset(DP_EMAP, __FILE__, __LINE__);
    depot->fatal = TRUE;
    return FALSE;
  }
  depot->buckets = (int *)(depot->map + DP_HEADSIZ);
  if(!(name = dpname(tdepot))){
    dpclose(tdepot);
    unlink(tdepot->name);
    depot->fatal = TRUE;
    return FALSE;
  }
  if(!dpclose(tdepot)){
    free(name);
    unlink(tdepot->name);
    depot->fatal = TRUE;
    return FALSE;
  }
  if(unlink(name) == -1){
    free(name);
    dpecodeset(DP_EUNLINK, __FILE__, __LINE__);
    depot->fatal = TRUE;
    return FALSE;
  }
  free(name);
  return TRUE;
}


/* Get the name of a database. */
char *dpname(DEPOT *depot){
  char *name;
  assert(depot);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return NULL;
  }
  if(!(name = dpstrdup(depot->name))){
    dpecodeset(DP_EALLOC, __FILE__, __LINE__);
    depot->fatal = TRUE;
    return NULL;
  }
  return name;
}


/* Get the size of a database file. */
int dpfsiz(DEPOT *depot){
  assert(depot);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return -1;
  }
  return depot->fsiz;
}


/* Get the number of the elements of the bucket array. */
int dpbnum(DEPOT *depot){
  assert(depot);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return -1;
  }
  return depot->bnum;
}


/* Get the number of the used elements of the bucket array. */
int dpbusenum(DEPOT *depot){
  int i, hits;
  assert(depot);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return -1;
  }
  hits = 0;
  for(i = 0; i < depot->bnum; i++){
    if(depot->buckets[i]) hits++;
  }
  return hits;
}


/* Get the number of the records stored in a database. */
int dprnum(DEPOT *depot){
  assert(depot);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return -1;
  }
  return depot->rnum;
}


/* Check whether a database handle is a writer or not. */
int dpwritable(DEPOT *depot){
  assert(depot);
  return depot->wmode;
}


/* Check whether a database has a fatal error or not. */
int dpfatalerror(DEPOT *depot){
  assert(depot);
  return depot->fatal;
}


/* Get the inode number of a database file. */
int dpinode(DEPOT *depot){
  assert(depot);
  return depot->inode;
}


/* Get the last modified time of a database. */
time_t dpmtime(DEPOT *depot){
  assert(depot);
  return depot->mtime;
}


/* Get the file descriptor of a database file. */
int dpfdesc(DEPOT *depot){
  assert(depot);
  return depot->fd;
}


/* Remove a database file. */
int dpremove(const char *name){
  struct stat sbuf;
  DEPOT *depot;
  assert(name);
  if(lstat(name, &sbuf) == -1){
    dpecodeset(DP_ESTAT, __FILE__, __LINE__);
    return FALSE;
  }
  if((depot = dpopen(name, DP_OWRITER | DP_OTRUNC, -1)) != NULL) dpclose(depot);
  if(unlink(name) == -1){
    dpecodeset(DP_EUNLINK, __FILE__, __LINE__);
    return FALSE;
  }
  return TRUE;
}


/* Repair a broken database file. */
int dprepair(const char *name){
  DEPOT *tdepot;
  char dbhead[DP_HEADSIZ], *tname, *kbuf, *vbuf;
  int fd, fsiz, flags, bnum, tbnum, err, head[DP_RHNUM], off, rsiz, ksiz, vsiz;
  struct stat sbuf;
  assert(name);
  if(lstat(name, &sbuf) == -1){
    dpecodeset(DP_ESTAT, __FILE__, __LINE__);
    return FALSE;
  }
  fsiz = sbuf.st_size;
  if((fd = open(name, O_RDWR, DP_FILEMODE)) == -1){
    dpecodeset(DP_EOPEN, __FILE__, __LINE__);
    return FALSE;
  }
  if(!dpseekread(fd, 0, dbhead, DP_HEADSIZ)){
    close(fd);
    return FALSE;
  }
  flags = *(int *)(dbhead + DP_FLAGSOFF);
  bnum = *(int *)(dbhead + DP_BNUMOFF);
  tbnum = *(int *)(dbhead + DP_RNUMOFF) * 2;
  if(tbnum < DP_DEFBNUM) tbnum = DP_DEFBNUM;
  if(!(tname = malloc(strlen(name) + strlen(DP_TMPFSUF) + 1))){
    dpecodeset(DP_EALLOC, __FILE__, __LINE__);
    return FALSE;
  }
  sprintf(tname, "%s%s", name, DP_TMPFSUF);
  if(!(tdepot = dpopen(tname, DP_OWRITER | DP_OCREAT | DP_OTRUNC, tbnum))){
    free(tname);
    close(fd);
    return FALSE;
  }
  err = FALSE;
  off = DP_HEADSIZ + bnum * sizeof(int);
  while(off < fsiz){
    if(!dpseekread(fd, off, head, DP_RHNUM * sizeof(int))) break;
    if(head[DP_RHIFLAGS] & DP_RECFDEL){
      if((rsiz = dprecsize(head)) < 0) break;
      off += rsiz;
      continue;
    }
    ksiz = head[DP_RHIKSIZ];
    vsiz = head[DP_RHIVSIZ];
    if(ksiz >= 0 && vsiz >= 0){
      kbuf = malloc(ksiz + 1);
      vbuf = malloc(vsiz + 1);
      if(kbuf && vbuf){
        if(dpseekread(fd, off + DP_RHNUM * sizeof(int), kbuf, ksiz) &&
           dpseekread(fd, off + DP_RHNUM * sizeof(int) + ksiz, vbuf, vsiz)){
          if(!dpput(tdepot, kbuf, ksiz, vbuf, vsiz, DP_DKEEP)) err = TRUE;
        } else {
          err = TRUE;
        }
      } else {
        if(!err) dpecodeset(DP_EALLOC, __FILE__, __LINE__);
        err = TRUE;
      }
      free(vbuf);
      free(kbuf);
    } else {
      if(!err) dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      err = TRUE;
    }
    if((rsiz = dprecsize(head)) < 0) break;
    off += rsiz;
  }
  if(!dpsetflags(tdepot, flags)) err = TRUE;
  if(!dpsync(tdepot)) err = TRUE;
  if(ftruncate(fd, 0) == -1){
    if(!err) dpecodeset(DP_ETRUNC, __FILE__, __LINE__);
    err = TRUE;
  }
  if(dpfcopy(fd, 0, tdepot->fd, 0) == -1) err = TRUE;
  if(!dpclose(tdepot)) err = TRUE;
  if(close(fd) == -1){
    if(!err) dpecodeset(DP_ECLOSE, __FILE__, __LINE__);
    err = TRUE;
  }
  if(unlink(tname) == -1){
    if(!err) dpecodeset(DP_EUNLINK, __FILE__, __LINE__);
    err = TRUE;
  }
  free(tname);
  return err ? FALSE : TRUE;
}


/* Dump all records as endian independent data. */
int dpexportdb(DEPOT *depot, const char *name){
  char *kbuf, *vbuf, *pbuf;
  int fd, err, ksiz, vsiz, psiz;
  assert(depot && name);
  if(!(dpiterinit(depot))) return FALSE;
  if((fd = open(name, O_RDWR | O_CREAT | O_TRUNC, DP_FILEMODE)) == -1){
    dpecodeset(DP_EOPEN, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  while(!err && (kbuf = dpiternext(depot, &ksiz)) != NULL){
    if((vbuf = dpget(depot, kbuf, ksiz, 0, -1, &vsiz)) != NULL){
      if((pbuf = malloc(ksiz + vsiz + DP_NUMBUFSIZ * 2)) != NULL){
        psiz = 0;
        psiz += sprintf(pbuf + psiz, "%X\n%X\n", ksiz, vsiz);
        memcpy(pbuf + psiz, kbuf, ksiz);
        psiz += ksiz;
        pbuf[psiz++] = '\n';
        memcpy(pbuf + psiz, vbuf, vsiz);
        psiz += vsiz;
        pbuf[psiz++] = '\n';
        if(!dpwrite(fd, pbuf, psiz)){
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
  if(close(fd) == -1){
    if(!err) dpecodeset(DP_ECLOSE, __FILE__, __LINE__);
    return FALSE;
  }
  return !err && !dpfatalerror(depot);
}


/* Load all records from endian independent data. */
int dpimportdb(DEPOT *depot, const char *name){
  char mbuf[DP_IOBUFSIZ], *rbuf;
  int i, j, fd, err, fsiz, off, msiz, ksiz, vsiz, hlen;
  struct stat sbuf;
  assert(depot && name);
  if(!depot->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(dprnum(depot) > 0){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  if((fd = open(name, O_RDONLY, DP_FILEMODE)) == -1){
    dpecodeset(DP_EOPEN, __FILE__, __LINE__);
    return FALSE;
  }
  if(fstat(fd, &sbuf) == -1 || !S_ISREG(sbuf.st_mode)){
    dpecodeset(DP_ESTAT, __FILE__, __LINE__);
    close(fd);
    return FALSE;
  }
  err = FALSE;
  fsiz = sbuf.st_size;
  off = 0;
  while(!err && off < fsiz){
    msiz = fsiz - off;
    if(msiz > DP_IOBUFSIZ) msiz = DP_IOBUFSIZ;
    if(!dpseekread(fd, off, mbuf, msiz)){
      err = TRUE;
      break;
    }
    hlen = 0;
    ksiz = -1;
    vsiz = -1;
    for(i = 0; i < msiz; i++){
      if(mbuf[i] == '\n'){
        mbuf[i] = '\0';
        ksiz = strtol(mbuf, NULL, 16);
        for(j = i + 1; j < msiz; j++){
          if(mbuf[j] == '\n'){
            mbuf[j] = '\0';
            vsiz = strtol(mbuf + i + 1, NULL, 16);
            hlen = j + 1;
            break;
          }
        }
        break;
      }
    }
    if(ksiz < 0 || vsiz < 0 || hlen < 4){
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      err = TRUE;
      break;
    }
    if(hlen + ksiz + vsiz + 2 < DP_IOBUFSIZ){
      if(!dpput(depot, mbuf + hlen, ksiz, mbuf + hlen + ksiz + 1, vsiz, DP_DKEEP)) err = TRUE;
    } else {
      if((rbuf = malloc(ksiz + vsiz + 2)) != NULL){
        if(dpseekread(fd, off + hlen, rbuf, ksiz + vsiz + 2)){
          if(!dpput(depot, rbuf, ksiz, rbuf + ksiz + 1, vsiz, DP_DKEEP)) err = TRUE;
        } else {
          err = TRUE;
        }
        free(rbuf);
      } else {
        dpecodeset(DP_EALLOC, __FILE__, __LINE__);
        err = TRUE;
      }
    }
    off += hlen + ksiz + vsiz + 2;
  }
  if(close(fd) == -1){
    if(!err) dpecodeset(DP_ECLOSE, __FILE__, __LINE__);
    return FALSE;
  }
  return !err && !dpfatalerror(depot);
}


/* Retrieve a record directly from a database file. */
char *dpsnaffle(const char *name, const char* kbuf, int ksiz, int *sp){
  char hbuf[DP_HEADSIZ], *map, *vbuf, *tkbuf;
  int fd, fsiz, bnum, msiz, *buckets, hash, thash, head[DP_RHNUM], err, off, vsiz, tksiz, kcmp;
  struct stat sbuf;
  assert(name && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if((fd = open(name, O_RDONLY, DP_FILEMODE)) == -1){
    dpecodeset(DP_EOPEN, __FILE__, __LINE__);
    return NULL;
  }
  if(fstat(fd, &sbuf) == -1 || !S_ISREG(sbuf.st_mode)){
    close(fd);
    dpecodeset(DP_ESTAT, __FILE__, __LINE__);
    return NULL;
  }
  fsiz = sbuf.st_size;
  if(!dpseekread(fd, 0, hbuf, DP_HEADSIZ)){
    close(fd);
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return NULL;
  }
  if(dpbigendian() ? memcmp(hbuf, DP_MAGICNUMB, strlen(DP_MAGICNUMB)) != 0 :
     memcmp(hbuf, DP_MAGICNUML, strlen(DP_MAGICNUML)) != 0){
    close(fd);
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return NULL;
  }
  bnum = *((int *)(hbuf + DP_BNUMOFF));
  if(bnum < 1 || fsiz < DP_HEADSIZ + bnum * sizeof(int)){
    close(fd);
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return NULL;
  }
  msiz = DP_HEADSIZ + bnum * sizeof(int);
  map = mmap(0, msiz, PROT_READ, MAP_SHARED, fd, 0);
  if(map == MAP_FAILED){
    close(fd);
    dpecodeset(DP_EMAP, __FILE__, __LINE__);
    return NULL;
  }
  buckets = (int *)(map + DP_HEADSIZ);
  err = FALSE;
  vbuf = NULL;
  vsiz = 0;
  DP_SECONDHASH(hash, kbuf, ksiz);
  DP_FIRSTHASH(thash, kbuf, ksiz);
  off = buckets[thash%bnum];
  while(off != 0){
    if(!dpseekread(fd, off, head, DP_RHNUM * sizeof(int))){
      err = TRUE;
      break;
    }
    if(head[DP_RHIKSIZ] < 0 || head[DP_RHIVSIZ] < 0 || head[DP_RHIPSIZ] < 0 ||
       head[DP_RHILEFT] < 0 || head[DP_RHIRIGHT] < 0){
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      err = TRUE;
      break;
    }
    thash = head[DP_RHIHASH];
    if(hash > thash){
      off = head[DP_RHILEFT];
    } else if(hash < thash){
      off = head[DP_RHIRIGHT];
    } else {
      tksiz = head[DP_RHIKSIZ];
      if(!(tkbuf = malloc(tksiz + 1))){
        dpecodeset(DP_EALLOC, __FILE__, __LINE__);
        err = TRUE;
        break;
      }
      if(!dpseekread(fd, off + DP_RHNUM * sizeof(int), tkbuf, tksiz)){
        free(tkbuf);
        err = TRUE;
        break;
      }
      tkbuf[tksiz] = '\0';
      kcmp = dpkeycmp(kbuf, ksiz, tkbuf, tksiz);
      free(tkbuf);
      if(kcmp > 0){
        off = head[DP_RHILEFT];
      } else if(kcmp < 0){
        off = head[DP_RHIRIGHT];
      } else if(head[DP_RHIFLAGS] & DP_RECFDEL){
        break;
      } else {
        vsiz = head[DP_RHIVSIZ];
        if(!(vbuf = malloc(vsiz + 1))){
          dpecodeset(DP_EALLOC, __FILE__, __LINE__);
          err = TRUE;
          break;
        }
        if(!dpseekread(fd, off + DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ], vbuf, vsiz)){
          free(vbuf);
          vbuf = NULL;
          err = TRUE;
          break;
        }
        vbuf[vsiz] = '\0';
        break;
      }
    }
  }
  if(vbuf){
    if(sp) *sp = vsiz;
  } else if(!err){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
  }
  munmap(map, msiz);
  close(fd);
  return vbuf;
}


/* Hash function used inside Depot. */
int dpinnerhash(const char *kbuf, int ksiz){
  int res;
  assert(kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  DP_FIRSTHASH(res, kbuf, ksiz);
  return res;
}


/* Hash function which is independent from the hash functions used inside Depot. */
int dpouterhash(const char *kbuf, int ksiz){
  int res;
  assert(kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  DP_THIRDHASH(res, kbuf, ksiz);
  return res;
}


/* Get a natural prime number not less than a number. */
int dpprimenum(int num){
  assert(num > 0);
  return dpgetprime(num);
}



/*************************************************************************************************
 * features for experts
 *************************************************************************************************/


/* Name of the operating system. */
const char *dpsysname = _QDBM_SYSNAME;


/* File descriptor for debugging output. */
int dpdbgfd = -1;


/* Whether this build is reentrant. */
const int dpisreentrant = _qdbm_ptsafe;


/* Set the last happened error code. */
void dpecodeset(int ecode, const char *file, int line){
  char iobuf[DP_IOBUFSIZ];
  assert(ecode >= DP_ENOERR && file && line >= 0);
  dpecode = ecode;
  if(dpdbgfd >= 0){
    fflush(stdout);
    fflush(stderr);
    sprintf(iobuf, "* dpecodeset: %s:%d: [%d] %s\n", file, line, ecode, dperrmsg(ecode));
    dpwrite(dpdbgfd, iobuf, strlen(iobuf));
  }
}


/* Get the pointer of the variable of the last happened error code. */
int *dpecodeptr(void){
  static int defdpecode = DP_ENOERR;
  void *ptr;
  if(_qdbm_ptsafe){
    if(!(ptr = _qdbm_settsd(&defdpecode, sizeof(int), &defdpecode))){
      defdpecode = DP_EMISC;
      return &defdpecode;
    }
    return (int *)ptr;
  }
  return &defdpecode;
}


/* Synchronize updating contents on memory. */
int dpmemsync(DEPOT *depot){
  assert(depot);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  if(!depot->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  *((int *)(depot->map + DP_FSIZOFF)) = depot->fsiz;
  *((int *)(depot->map + DP_RNUMOFF)) = depot->rnum;
  if(msync(depot->map, depot->msiz, MS_SYNC) == -1){
    dpecodeset(DP_EMAP, __FILE__, __LINE__);
    depot->fatal = TRUE;
    return FALSE;
  }
  return TRUE;
}


/* Synchronize updating contents on memory, not physically. */
int dpmemflush(DEPOT *depot){
  assert(depot);
  if(depot->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  if(!depot->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  *((int *)(depot->map + DP_FSIZOFF)) = depot->fsiz;
  *((int *)(depot->map + DP_RNUMOFF)) = depot->rnum;
  if(mflush(depot->map, depot->msiz, MS_SYNC) == -1){
    dpecodeset(DP_EMAP, __FILE__, __LINE__);
    depot->fatal = TRUE;
    return FALSE;
  }
  return TRUE;
}


/* Get flags of a database. */
int dpgetflags(DEPOT *depot){
  int flags;
  assert(depot);
  memcpy(&flags, depot->map + DP_FLAGSOFF, sizeof(int));
  return flags;
}


/* Set flags of a database. */
int dpsetflags(DEPOT *depot, int flags){
  assert(depot);
  if(!depot->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  memcpy(depot->map + DP_FLAGSOFF, &flags, sizeof(int));
  return TRUE;
}



/*************************************************************************************************
 * private objects
 *************************************************************************************************/


/* Check whether the byte order of the platform is big endian or not.
   The return value is true if bigendian, else, it is false. */
static int dpbigendian(void){
  char buf[sizeof(int)];
  *(int *)buf = 1;
  return !buf[0];
}


/* Get a copied string.
   `str' specifies an original string.
   The return value is a copied string whose region is allocated by `malloc'. */
static char *dpstrdup(const char *str){
  int len;
  char *buf;
  assert(str);
  len = strlen(str);
  if(!(buf = malloc(len + 1))) return NULL;
  memcpy(buf, str, len + 1);
  return buf;
}


/* Lock a file descriptor.
   `fd' specifies a file descriptor.
   `ex' specifies whether an exclusive lock or a shared lock is performed.
   `nb' specifies whether to request with non-blocking.
   The return value is true if successful, else, it is false. */
static int dplock(int fd, int ex, int nb){
  struct flock lock;
  assert(fd >= 0);
  memset(&lock, 0, sizeof(struct flock));
  lock.l_type = ex ? F_WRLCK : F_RDLCK;
  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;
  lock.l_pid = 0;
  while(fcntl(fd, nb ? F_SETLK : F_SETLKW, &lock) == -1){
    if(errno != EINTR){
      dpecodeset(DP_ELOCK, __FILE__, __LINE__);
      return FALSE;
    }
  }
  return TRUE;
}


/* Write into a file.
   `fd' specifies a file descriptor.
   `buf' specifies a buffer to write.
   `size' specifies the size of the buffer.
   The return value is the size of the written buffer, or, -1 on failure. */
static int dpwrite(int fd, const void *buf, int size){
  const char *lbuf;
  int rv, wb;
  assert(fd >= 0 && buf && size >= 0);
  lbuf = buf;
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


/* Write into a file at an offset.
   `fd' specifies a file descriptor.
   `off' specifies an offset of the file.
   `buf' specifies a buffer to write.
   `size' specifies the size of the buffer.
   The return value is true if successful, else, it is false. */
static int dpseekwrite(int fd, int off, const void *buf, int size){
  assert(fd >= 0 && buf && size >= 0);
  if(size < 1) return TRUE;
  if(off < 0){
    if(lseek(fd, 0, SEEK_END) == -1){
      dpecodeset(DP_ESEEK, __FILE__, __LINE__);
      return FALSE;
    }
  } else {
    if(lseek(fd, off, SEEK_SET) != off){
      dpecodeset(DP_ESEEK, __FILE__, __LINE__);
      return FALSE;
    }
  }
  if(dpwrite(fd, buf, size) != size){
    dpecodeset(DP_EWRITE, __FILE__, __LINE__);
    return FALSE;
  }
  return TRUE;
}


/* Write an integer into a file at an offset.
   `fd' specifies a file descriptor.
   `off' specifies an offset of the file.
   `num' specifies an integer.
   The return value is true if successful, else, it is false. */
static int dpseekwritenum(int fd, int off, int num){
  assert(fd >= 0);
  return dpseekwrite(fd, off, &num, sizeof(int));
}


/* Read from a file and store the data into a buffer.
   `fd' specifies a file descriptor.
   `buffer' specifies a buffer to store into.
   `size' specifies the size to read with.
   The return value is the size read with, or, -1 on failure. */
static int dpread(int fd, void *buf, int size){
  char *lbuf;
  int i, bs;
  assert(fd >= 0 && buf && size >= 0);
  lbuf = buf;
  for(i = 0; i < size && (bs = read(fd, lbuf + i, size - i)) != 0; i += bs){
    if(bs == -1 && errno != EINTR) return -1;
  }
  return i;
}


/* Read from a file at an offset and store the data into a buffer.
   `fd' specifies a file descriptor.
   `off' specifies an offset of the file.
   `buffer' specifies a buffer to store into.
   `size' specifies the size to read with.
   The return value is true if successful, else, it is false. */
static int dpseekread(int fd, int off, void *buf, int size){
  char *lbuf;
  assert(fd >= 0 && off >= 0 && buf && size >= 0);
  lbuf = (char *)buf;
  if(lseek(fd, off, SEEK_SET) != off){
    dpecodeset(DP_ESEEK, __FILE__, __LINE__);
    return FALSE;
  }
  if(dpread(fd, lbuf, size) != size){
    dpecodeset(DP_EREAD, __FILE__, __LINE__);
    return FALSE;
  }
  return TRUE;
}


/* Copy data between files.
   `destfd' specifies a file descriptor of a destination file.
   `destoff' specifies an offset of the destination file.
   `srcfd' specifies a file descriptor of a source file.
   `srcoff' specifies an offset of the source file.
   The return value is the size copied with, or, -1 on failure. */
static int dpfcopy(int destfd, int destoff, int srcfd, int srcoff){
  char iobuf[DP_IOBUFSIZ];
  int sum, iosiz;
  if(lseek(srcfd, srcoff, SEEK_SET) == -1 || lseek(destfd, destoff, SEEK_SET) == -1){
    dpecodeset(DP_ESEEK, __FILE__, __LINE__);
    return -1;
  }
  sum = 0;
  while((iosiz = dpread(srcfd, iobuf, DP_IOBUFSIZ)) > 0){
    if(dpwrite(destfd, iobuf, iosiz) == -1){
      dpecodeset(DP_EWRITE, __FILE__, __LINE__);
      return -1;
    }
    sum += iosiz;
  }
  if(iosiz < 0){
    dpecodeset(DP_EREAD, __FILE__, __LINE__);
    return -1;
  }
  return sum;
}


/* Get a natural prime number not less than a number.
   `num' specified a natural number.
   The return value is a prime number not less than the specified number. */
static int dpgetprime(int num){
  int primes[] = {
    1, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 43, 47, 53, 59, 61, 71, 79, 83,
    89, 103, 109, 113, 127, 139, 157, 173, 191, 199, 223, 239, 251, 283, 317, 349,
    383, 409, 443, 479, 509, 571, 631, 701, 761, 829, 887, 953, 1021, 1151, 1279,
    1399, 1531, 1663, 1789, 1913, 2039, 2297, 2557, 2803, 3067, 3323, 3583, 3833,
    4093, 4603, 5119, 5623, 6143, 6653, 7159, 7673, 8191, 9209, 10223, 11261,
    12281, 13309, 14327, 15359, 16381, 18427, 20479, 22511, 24571, 26597, 28669,
    30713, 32749, 36857, 40949, 45053, 49139, 53239, 57331, 61417, 65521, 73727,
    81919, 90107, 98299, 106487, 114679, 122869, 131071, 147451, 163819, 180221,
    196597, 212987, 229373, 245759, 262139, 294911, 327673, 360439, 393209, 425977,
    458747, 491503, 524287, 589811, 655357, 720887, 786431, 851957, 917503, 982981,
    1048573, 1179641, 1310719, 1441771, 1572853, 1703903, 1835003, 1966079,
    2097143, 2359267, 2621431, 2883577, 3145721, 3407857, 3670013, 3932153,
    4194301, 4718579, 5242877, 5767129, 6291449, 6815741, 7340009, 7864301,
    8388593, 9437179, 10485751, 11534329, 12582893, 13631477, 14680063, 15728611,
    16777213, 18874367, 20971507, 23068667, 25165813, 27262931, 29360087, 31457269,
    33554393, 37748717, 41943023, 46137319, 50331599, 54525917, 58720253, 62914549,
    67108859, 75497467, 83886053, 92274671, 100663291, 109051903, 117440509,
    125829103, 134217689, 150994939, 167772107, 184549373, 201326557, 218103799,
    234881011, 251658227, 268435399, 301989881, 335544301, 369098707, 402653171,
    436207613, 469762043, 503316469, 536870909, 603979769, 671088637, 738197503,
    805306357, 872415211, 939524087, 1006632947, 1073741789, 1207959503,
    1342177237, 1476394991, 1610612711, 1744830457, 1879048183, 2013265907, -1
  };
  int i;
  assert(num > 0);
  for(i = 0; primes[i] > 0; i++){
    if(num <= primes[i]) return primes[i];
  }
  return primes[i-1];
}


/* Get the padding size of a record.
   `vsiz' specifies the size of the value of a record.
   The return value is the padding size of a record. */
static int dppadsize(DEPOT *depot, int ksiz, int vsiz){
  int pad;
  assert(depot && vsiz >= 0);
  if(depot->align > 0){
    return depot->align - (depot->fsiz + DP_RHNUM * sizeof(int) + ksiz + vsiz) % depot->align;
  } else if(depot->align < 0){
    pad = (int)(vsiz * (2.0 / (1 << -(depot->align))));
    if(vsiz + pad >= DP_FSBLKSIZ){
      if(vsiz <= DP_FSBLKSIZ) pad = 0;
      if(depot->fsiz % DP_FSBLKSIZ == 0){
        return (pad / DP_FSBLKSIZ) * DP_FSBLKSIZ + DP_FSBLKSIZ -
          (depot->fsiz + DP_RHNUM * sizeof(int) + ksiz + vsiz) % DP_FSBLKSIZ;
      } else {
        return (pad / (DP_FSBLKSIZ / 2)) * (DP_FSBLKSIZ / 2) + (DP_FSBLKSIZ / 2) -
          (depot->fsiz + DP_RHNUM * sizeof(int) + ksiz + vsiz) % (DP_FSBLKSIZ / 2);
      }
    } else {
      return pad >= DP_RHNUM * sizeof(int) ? pad : DP_RHNUM * sizeof(int);
    }
  }
  return 0;
}


/* Get the size of a record in a database file.
   `head' specifies the header of  a record.
   The return value is the size of a record in a database file. */
static int dprecsize(int *head){
  assert(head);
  return DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ] + head[DP_RHIVSIZ] + head[DP_RHIPSIZ];
}


/* Read the header of a record.
   `depot' specifies a database handle.
   `off' specifies an offset of the database file.
   `head' specifies a buffer for the header.
   `ebuf' specifies the pointer to the entity buffer.
   `eep' specifies the pointer to a variable to which whether ebuf was used is assigned.
   The return value is true if successful, else, it is false. */
static int dprechead(DEPOT *depot, int off, int *head, char *ebuf, int *eep){
  assert(depot && off >= 0 && head);
  if(off > depot->fsiz){
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return FALSE;
  }
  if(ebuf){
    *eep = FALSE;
    if(off < depot->fsiz - DP_ENTBUFSIZ){
      *eep = TRUE;
      if(!dpseekread(depot->fd, off, ebuf, DP_ENTBUFSIZ)) return FALSE;
      memcpy(head, ebuf, DP_RHNUM * sizeof(int));
      if(head[DP_RHIKSIZ] < 0 || head[DP_RHIVSIZ] < 0 || head[DP_RHIPSIZ] < 0 ||
         head[DP_RHILEFT] < 0 || head[DP_RHIRIGHT] < 0){
        dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
        return FALSE;
      }
      return TRUE;
    }
  }
  if(!dpseekread(depot->fd, off, head, DP_RHNUM * sizeof(int))) return FALSE;
  if(head[DP_RHIKSIZ] < 0 || head[DP_RHIVSIZ] < 0 || head[DP_RHIPSIZ] < 0 ||
     head[DP_RHILEFT] < 0 || head[DP_RHIRIGHT] < 0){
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return FALSE;
  }
  return TRUE;
}


/* Read the entitiy of the key of a record.
   `depot' specifies a database handle.
   `off' specifies an offset of the database file.
   `head' specifies the header of a record.
   The return value is a key data whose region is allocated by `malloc', or NULL on failure. */
static char *dpreckey(DEPOT *depot, int off, int *head){
  char *kbuf;
  int ksiz;
  assert(depot && off >= 0);
  ksiz = head[DP_RHIKSIZ];
  if(!(kbuf = malloc(ksiz + 1))){
    dpecodeset(DP_EALLOC, __FILE__, __LINE__);
    return NULL;
  }
  if(!dpseekread(depot->fd, off + DP_RHNUM * sizeof(int), kbuf, ksiz)){
    free(kbuf);
    return NULL;
  }
  kbuf[ksiz] = '\0';
  return kbuf;
}


/* Read the entitiy of the value of a record.
   `depot' specifies a database handle.
   `off' specifies an offset of the database file.
   `head' specifies the header of a record.
   `start' specifies the offset address of the beginning of the region of the value to be read.
   `max' specifies the max size to be read.  If it is negative, the size to read is unlimited.
   The return value is a value data whose region is allocated by `malloc', or NULL on failure. */
static char *dprecval(DEPOT *depot, int off, int *head, int start, int max){
  char *vbuf;
  int vsiz;
  assert(depot && off >= 0 && start >= 0);
  head[DP_RHIVSIZ] -= start;
  if(max < 0){
    vsiz = head[DP_RHIVSIZ];
  } else {
    vsiz = max < head[DP_RHIVSIZ] ? max : head[DP_RHIVSIZ];
  }
  if(!(vbuf = malloc(vsiz + 1))){
    dpecodeset(DP_EALLOC, __FILE__, __LINE__);
    return NULL;
  }
  if(!dpseekread(depot->fd, off + DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ] + start, vbuf, vsiz)){
    free(vbuf);
    return NULL;
  }
  vbuf[vsiz] = '\0';
  return vbuf;
}


/* Read the entitiy of the value of a record and write it into a given buffer.
   `depot' specifies a database handle.
   `off' specifies an offset of the database file.
   `head' specifies the header of a record.
   `start' specifies the offset address of the beginning of the region of the value to be read.
   `max' specifies the max size to be read.  It shuld be less than the size of the writing buffer.
   If successful, the return value is the size of the written data, else, it is -1. */
static int dprecvalwb(DEPOT *depot, int off, int *head, int start, int max, char *vbuf){
  int vsiz;
  assert(depot && off >= 0 && start >= 0 && max >= 0 && vbuf);
  head[DP_RHIVSIZ] -= start;
  vsiz = max < head[DP_RHIVSIZ] ? max : head[DP_RHIVSIZ];
  if(!dpseekread(depot->fd, off + DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ] + start, vbuf, vsiz))
    return -1;
  return vsiz;
}


/* Compare two keys.
   `abuf' specifies the pointer to the region of the former.
   `asiz' specifies the size of the region.
   `bbuf' specifies the pointer to the region of the latter.
   `bsiz' specifies the size of the region.
   The return value is 0 if two equals, positive if the formar is big, else, negative. */
static int dpkeycmp(const char *abuf, int asiz, const char *bbuf, int bsiz){
  assert(abuf && asiz >= 0 && bbuf && bsiz >= 0);
  if(asiz > bsiz) return 1;
  if(asiz < bsiz) return -1;
  return memcmp(abuf, bbuf, asiz);
}


/* Search for a record.
   `depot' specifies a database handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region.
   `hash' specifies the second hash value of the key.
   `bip' specifies the pointer to the region to assign the index of the corresponding record.
   `offp' specifies the pointer to the region to assign the last visited node in the hash chain,
   or, -1 if the hash chain is empty.
   `entp' specifies the offset of the last used joint, or, -1 if the hash chain is empty.
   `head' specifies the pointer to the region to store the header of the last visited record in.
   `ebuf' specifies the pointer to the entity buffer.
   `eep' specifies the pointer to a variable to which whether ebuf was used is assigned.
   `delhit' specifies whether a deleted record corresponds or not.
   The return value is 0 if successful, 1 if there is no corresponding record, -1 on error. */
static int dprecsearch(DEPOT *depot, const char *kbuf, int ksiz, int hash, int *bip, int *offp,
                       int *entp, int *head, char *ebuf, int *eep, int delhit){
  int off, entoff, thash, kcmp;
  char stkey[DP_STKBUFSIZ], *tkey;
  assert(depot && kbuf && ksiz >= 0 && hash >= 0 && bip && offp && entp && head && ebuf && eep);
  DP_FIRSTHASH(thash, kbuf, ksiz);
  *bip = thash % depot->bnum;
  off = depot->buckets[*bip];
  *offp = -1;
  *entp = -1;
  entoff = -1;
  *eep = FALSE;
  while(off != 0){
    if(!dprechead(depot, off, head, ebuf, eep)) return -1;
    thash = head[DP_RHIHASH];
    if(hash > thash){
      entoff = off + DP_RHILEFT * sizeof(int);
      off = head[DP_RHILEFT];
    } else if(hash < thash){
      entoff = off + DP_RHIRIGHT * sizeof(int);
      off = head[DP_RHIRIGHT];
    } else {
      if(*eep && DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ] <= DP_ENTBUFSIZ){
        kcmp = dpkeycmp(kbuf, ksiz, ebuf + (DP_RHNUM * sizeof(int)), head[DP_RHIKSIZ]);
      } else if(head[DP_RHIKSIZ] > DP_STKBUFSIZ){
        if(!(tkey = dpreckey(depot, off, head))) return -1;
        kcmp = dpkeycmp(kbuf, ksiz, tkey, head[DP_RHIKSIZ]);
        free(tkey);
      } else {
        if(!dpseekread(depot->fd, off + DP_RHNUM * sizeof(int), stkey, head[DP_RHIKSIZ]))
          return -1;
        kcmp = dpkeycmp(kbuf, ksiz, stkey, head[DP_RHIKSIZ]);
      }
      if(kcmp > 0){
        entoff = off + DP_RHILEFT * sizeof(int);
        off = head[DP_RHILEFT];
      } else if(kcmp < 0){
        entoff = off + DP_RHIRIGHT * sizeof(int);
        off = head[DP_RHIRIGHT];
      } else {
        if(!delhit && (head[DP_RHIFLAGS] & DP_RECFDEL)){
          entoff = off + DP_RHILEFT * sizeof(int);
          off = head[DP_RHILEFT];
        } else {
          *offp = off;
          *entp = entoff;
          return 0;
        }
      }
    }
  }
  *offp = off;
  *entp = entoff;
  return 1;
}


/* Overwrite a record.
   `depot' specifies a database handle.
   `off' specifies the offset of the database file.
   `rsiz' specifies the size of the existing record.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region.
   `vbuf' specifies the pointer to the region of a value.
   `vsiz' specifies the size of the region.
   `hash' specifies the second hash value of the key.
   `left' specifies the offset of the left child.
   `right' specifies the offset of the right child.
   The return value is true if successful, or, false on failure. */
static int dprecrewrite(DEPOT *depot, int off, int rsiz, const char *kbuf, int ksiz,
                        const char *vbuf, int vsiz, int hash, int left, int right){
  char ebuf[DP_WRTBUFSIZ];
  int i, head[DP_RHNUM], asiz, hoff, koff, voff, mi, min, size;
  assert(depot && off >= 1 && rsiz > 0 && kbuf && ksiz >= 0 && vbuf && vsiz >= 0);
  head[DP_RHIFLAGS] = 0;
  head[DP_RHIHASH] = hash;
  head[DP_RHIKSIZ] = ksiz;
  head[DP_RHIVSIZ] = vsiz;
  head[DP_RHIPSIZ] = rsiz - sizeof(head) - ksiz - vsiz;
  head[DP_RHILEFT] = left;
  head[DP_RHIRIGHT] = right;
  asiz = sizeof(head) + ksiz + vsiz;
  if(depot->fbpsiz > DP_FBPOOLSIZ * 4 && head[DP_RHIPSIZ] > asiz){
    rsiz = (head[DP_RHIPSIZ] - asiz) / 2 + asiz;
    head[DP_RHIPSIZ] -= rsiz;
  } else {
    rsiz = 0;
  }
  if(asiz <= DP_WRTBUFSIZ){
    memcpy(ebuf, head, sizeof(head));
    memcpy(ebuf + sizeof(head), kbuf, ksiz);
    memcpy(ebuf + sizeof(head) + ksiz, vbuf, vsiz);
    if(!dpseekwrite(depot->fd, off, ebuf, asiz)) return FALSE;
  } else {
    hoff = off;
    koff = hoff + sizeof(head);
    voff = koff + ksiz;
    if(!dpseekwrite(depot->fd, hoff, head, sizeof(head)) ||
       !dpseekwrite(depot->fd, koff, kbuf, ksiz) || !dpseekwrite(depot->fd, voff, vbuf, vsiz))
      return FALSE;
  }
  if(rsiz > 0){
    off += sizeof(head) + ksiz + vsiz + head[DP_RHIPSIZ];
    head[DP_RHIFLAGS] = DP_RECFDEL | DP_RECFREUSE;
    head[DP_RHIHASH] = hash;
    head[DP_RHIKSIZ] = ksiz;
    head[DP_RHIVSIZ] = vsiz;
    head[DP_RHIPSIZ] = rsiz - sizeof(head) - ksiz - vsiz;
    head[DP_RHILEFT] = 0;
    head[DP_RHIRIGHT] = 0;
    if(!dpseekwrite(depot->fd, off, head, sizeof(head))) return FALSE;
    size = dprecsize(head);
    mi = -1;
    min = -1;
    for(i = 0; i < depot->fbpsiz; i += 2){
      if(depot->fbpool[i] == -1){
        depot->fbpool[i] = off;
        depot->fbpool[i+1] = size;
        dpfbpoolcoal(depot);
        mi = -1;
        break;
      }
      if(mi == -1 || depot->fbpool[i+1] < min){
        mi = i;
        min = depot->fbpool[i+1];
      }
    }
    if(mi >= 0 && size > min){
      depot->fbpool[mi] = off;
      depot->fbpool[mi+1] = size;
      dpfbpoolcoal(depot);
    }
  }
  return TRUE;
}


/* Write a record at the end of a database file.
   `depot' specifies a database handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region.
   `vbuf' specifies the pointer to the region of a value.
   `vsiz' specifies the size of the region.
   `hash' specifies the second hash value of the key.
   `left' specifies the offset of the left child.
   `right' specifies the offset of the right child.
   The return value is the offset of the record, or, -1 on failure. */
static int dprecappend(DEPOT *depot, const char *kbuf, int ksiz, const char *vbuf, int vsiz,
                       int hash, int left, int right){
  char ebuf[DP_WRTBUFSIZ], *hbuf;
  int head[DP_RHNUM], asiz, psiz, off;
  assert(depot && kbuf && ksiz >= 0 && vbuf && vsiz >= 0);
  psiz = dppadsize(depot, ksiz, vsiz);
  head[DP_RHIFLAGS] = 0;
  head[DP_RHIHASH] = hash;
  head[DP_RHIKSIZ] = ksiz;
  head[DP_RHIVSIZ] = vsiz;
  head[DP_RHIPSIZ] = psiz;
  head[DP_RHILEFT] = left;
  head[DP_RHIRIGHT] = right;
  asiz = sizeof(head) + ksiz + vsiz + psiz;
  off = depot->fsiz;
  if(asiz <= DP_WRTBUFSIZ){
    memcpy(ebuf, head, sizeof(head));
    memcpy(ebuf + sizeof(head), kbuf, ksiz);
    memcpy(ebuf + sizeof(head) + ksiz, vbuf, vsiz);
    memset(ebuf + sizeof(head) + ksiz + vsiz, 0, psiz);
    if(!dpseekwrite(depot->fd, off, ebuf, asiz)) return -1;
  } else {
    if(!(hbuf = malloc(asiz))){
      dpecodeset(DP_EALLOC, __FILE__, __LINE__);
      return -1;
    }
    memcpy(hbuf, head, sizeof(head));
    memcpy(hbuf + sizeof(head), kbuf, ksiz);
    memcpy(hbuf + sizeof(head) + ksiz, vbuf, vsiz);
    memset(hbuf + sizeof(head) + ksiz + vsiz, 0, psiz);
    if(!dpseekwrite(depot->fd, off, hbuf, asiz)){
      free(hbuf);
      return -1;
    }
    free(hbuf);
  }
  depot->fsiz += asiz;
  return off;
}


/* Overwrite the value of a record.
   `depot' specifies a database handle.
   `off' specifies the offset of the database file.
   `head' specifies the header of the record.
   `vbuf' specifies the pointer to the region of a value.
   `vsiz' specifies the size of the region.
   `cat' specifies whether it is concatenate mode or not.
   The return value is true if successful, or, false on failure. */
static int dprecover(DEPOT *depot, int off, int *head, const char *vbuf, int vsiz, int cat){
  int i, hsiz, hoff, voff;
  assert(depot && off >= 0 && head && vbuf && vsiz >= 0);
  for(i = 0; i < depot->fbpsiz; i += 2){
    if(depot->fbpool[i] == off){
      depot->fbpool[i] = -1;
      depot->fbpool[i+1] = -1;
      break;
    }
  }
  if(cat){
    head[DP_RHIFLAGS] = 0;
    head[DP_RHIPSIZ] -= vsiz;
    head[DP_RHIVSIZ] += vsiz;
    hsiz = DP_RHNUM * sizeof(int);
    hoff = off;
    voff = hoff + DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ] + head[DP_RHIVSIZ] - vsiz;
  } else {
    head[DP_RHIFLAGS] = 0;
    head[DP_RHIPSIZ] += head[DP_RHIVSIZ] - vsiz;
    head[DP_RHIVSIZ] = vsiz;
    hsiz = DP_RHNUM * sizeof(int);
    hoff = off;
    voff = hoff + DP_RHNUM * sizeof(int) + head[DP_RHIKSIZ];
  }
  if(!dpseekwrite(depot->fd, hoff, head, hsiz) ||
     !dpseekwrite(depot->fd, voff, vbuf, vsiz)) return FALSE;
  return TRUE;
}


/* Delete a record.
   `depot' specifies a database handle.
   `off' specifies the offset of the database file.
   `head' specifies the header of the record.
   `reusable' specifies whether the region is reusable or not.
   The return value is true if successful, or, false on failure. */
static int dprecdelete(DEPOT *depot, int off, int *head, int reusable){
  int i, mi, min, size;
  assert(depot && off >= 0 && head);
  if(reusable){
    size = dprecsize(head);
    mi = -1;
    min = -1;
    for(i = 0; i < depot->fbpsiz; i += 2){
      if(depot->fbpool[i] == -1){
        depot->fbpool[i] = off;
        depot->fbpool[i+1] = size;
        dpfbpoolcoal(depot);
        mi = -1;
        break;
      }
      if(mi == -1 || depot->fbpool[i+1] < min){
        mi = i;
        min = depot->fbpool[i+1];
      }
    }
    if(mi >= 0 && size > min){
      depot->fbpool[mi] = off;
      depot->fbpool[mi+1] = size;
      dpfbpoolcoal(depot);
    }
  }
  return dpseekwritenum(depot->fd, off + DP_RHIFLAGS * sizeof(int),
                        DP_RECFDEL | (reusable ? DP_RECFREUSE : 0));
}


/* Make contiguous records of the free block pool coalesce.
   `depot' specifies a database handle. */
static void dpfbpoolcoal(DEPOT *depot){
  int i;
  assert(depot);
  if(depot->fbpinc++ <= depot->fbpsiz / 4) return;
  depot->fbpinc = 0;
  qsort(depot->fbpool, depot->fbpsiz / 2, sizeof(int) * 2, dpfbpoolcmp);
  for(i = 2; i < depot->fbpsiz; i += 2){
    if(depot->fbpool[i-2] > 0 &&
       depot->fbpool[i-2] + depot->fbpool[i-1] - depot->fbpool[i] == 0){
      depot->fbpool[i] = depot->fbpool[i-2];
      depot->fbpool[i+1] += depot->fbpool[i-1];
      depot->fbpool[i-2] = -1;
      depot->fbpool[i-1] = -1;
    }
  }
}


/* Compare two records of the free block pool.
   `a' specifies the pointer to one record.
   `b' specifies the pointer to the other record.
   The return value is 0 if two equals, positive if the formar is big, else, negative. */
static int dpfbpoolcmp(const void *a, const void *b){
  assert(a && b);
  return *(int *)a - *(int *)b;
}



/* END OF FILE */
