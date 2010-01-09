/*************************************************************************************************
 * Implementation of Villa
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

#include "villa.h"
#include "myconf.h"

#define VL_LEAFIDMIN   1                 /* minimum number of leaf ID */
#define VL_NODEIDMIN   100000000         /* minimum number of node ID */
#define VL_VNUMBUFSIZ  8                 /* size of a buffer for variable length number */
#define VL_NUMBUFSIZ   32                /* size of a buffer for a number */
#define VL_PAGEBUFSIZ  32768             /* size of a buffer to read each page */
#define VL_MAXLEAFSIZ  49152             /* maximum size of each leaf */
#define VL_DEFLRECMAX  49                /* default number of records in each leaf */
#define VL_DEFNIDXMAX  192               /* default number of indexes in each node */
#define VL_DEFLCNUM    1024              /* default number of leaf cache */
#define VL_DEFNCNUM    512               /* default number of node cache */
#define VL_CACHEOUT    8                 /* number of pages in a process of cacheout */
#define VL_INITBNUM    32749             /* initial bucket number */
#define VL_PAGEALIGN   -3                /* alignment for pages */
#define VL_FBPOOLSIZ   128               /* size of free block pool */
#define VL_PATHBUFSIZ  1024              /* size of a path buffer */
#define VL_TMPFSUF     MYEXTSTR "vltmp"  /* suffix of a temporary file */
#define VL_ROOTKEY     -1                /* key of the root key */
#define VL_LASTKEY     -2                /* key of the last key */
#define VL_LNUMKEY     -3                /* key of the number of leaves */
#define VL_NNUMKEY     -4                /* key of the number of nodes */
#define VL_RNUMKEY     -5                /* key of the number of records */
#define VL_CRDNUM      7                 /* default division number for Vista */

/* set a buffer for a variable length number */
#define VL_SETVNUMBUF(VL_len, VL_buf, VL_num) \
  do { \
    int _VL_num; \
    _VL_num = VL_num; \
    if(_VL_num == 0){ \
      ((signed char *)(VL_buf))[0] = 0; \
      (VL_len) = 1; \
    } else { \
      (VL_len) = 0; \
      while(_VL_num > 0){ \
        int _VL_rem = _VL_num & 0x7f; \
        _VL_num >>= 7; \
        if(_VL_num > 0){ \
          ((signed char *)(VL_buf))[(VL_len)] = -_VL_rem - 1; \
        } else { \
          ((signed char *)(VL_buf))[(VL_len)] = _VL_rem; \
        } \
        (VL_len)++; \
      } \
    } \
  } while(FALSE)

/* read a variable length buffer */
#define VL_READVNUMBUF(VL_buf, VL_size, VL_num, VL_step) \
  do { \
    int _VL_i, _VL_base; \
    (VL_num) = 0; \
    _VL_base = 1; \
    if((VL_size) < 2){ \
      (VL_num) = ((signed char *)(VL_buf))[0]; \
      (VL_step) = 1; \
    } else { \
      for(_VL_i = 0; _VL_i < (VL_size); _VL_i++){ \
        if(((signed char *)(VL_buf))[_VL_i] >= 0){ \
          (VL_num) += ((signed char *)(VL_buf))[_VL_i] * _VL_base; \
          break; \
        } \
        (VL_num) += _VL_base * (((signed char *)(VL_buf))[_VL_i] + 1) * -1; \
        _VL_base *= 128; \
      } \
      (VL_step) = _VL_i + 1; \
    } \
  } while(FALSE)

enum {                                   /* enumeration for flags */
  VL_FLISVILLA = 1 << 0,                 /* whether for Villa */
  VL_FLISZLIB = 1 << 1,                  /* whether with ZLIB */
  VL_FLISLZO = 1 << 2,                   /* whether with LZO */
  VL_FLISBZIP = 1 << 3                   /* whether with BZIP2 */
};


/* private function prototypes */
static int vllexcompare(const char *aptr, int asiz, const char *bptr, int bsiz);
static int vlintcompare(const char *aptr, int asiz, const char *bptr, int bsiz);
static int vlnumcompare(const char *aptr, int asiz, const char *bptr, int bsiz);
static int vldeccompare(const char *aptr, int asiz, const char *bptr, int bsiz);
static int vldpputnum(DEPOT *depot, int knum, int vnum);
static int vldpgetnum(DEPOT *depot, int knum, int *vnp);
static VLLEAF *vlleafnew(VILLA *villa, int prev, int next);
static int vlleafcacheout(VILLA *villa, int id);
static int vlleafsave(VILLA *villa, VLLEAF *leaf);
static VLLEAF *vlleafload(VILLA *villa, int id);
static VLLEAF *vlgethistleaf(VILLA *villa, const char *kbuf, int ksiz);
static int vlleafaddrec(VILLA *villa, VLLEAF *leaf, int dmode,
                        const char *kbuf, int ksiz, const char *vbuf, int vsiz);
static int vlleafdatasize(VLLEAF *leaf);
static VLLEAF *vlleafdivide(VILLA *villa, VLLEAF *leaf);
static VLNODE *vlnodenew(VILLA *villa, int heir);
static int vlnodecacheout(VILLA *villa, int id);
static int vlnodesave(VILLA *villa, VLNODE *node);
static VLNODE *vlnodeload(VILLA *villa, int id);
static void vlnodeaddidx(VILLA *villa, VLNODE *node, int order,
                         int pid, const char *kbuf, int ksiz);
static int vlsearchleaf(VILLA *villa, const char *kbuf, int ksiz);
static int vlcacheadjust(VILLA *villa);
static VLREC *vlrecsearch(VILLA *villa, VLLEAF *leaf, const char *kbuf, int ksiz, int *ip);



/*************************************************************************************************
 * public objects
 *************************************************************************************************/


/* Comparing functions. */
VLCFUNC VL_CMPLEX = vllexcompare;
VLCFUNC VL_CMPINT = vlintcompare;
VLCFUNC VL_CMPNUM = vlnumcompare;
VLCFUNC VL_CMPDEC = vldeccompare;


/* Get a database handle. */
VILLA *vlopen(const char *name, int omode, VLCFUNC cmp){
  DEPOT *depot;
  int dpomode, flags, cmode, root, last, lnum, nnum, rnum;
  VILLA *villa;
  VLLEAF *leaf;
  assert(name && cmp);
  dpomode = DP_OREADER;
  if(omode & VL_OWRITER){
    dpomode = DP_OWRITER;
    if(omode & VL_OCREAT) dpomode |= DP_OCREAT;
    if(omode & VL_OTRUNC) dpomode |= DP_OTRUNC;
  }
  if(omode & VL_ONOLCK) dpomode |= DP_ONOLCK;
  if(omode & VL_OLCKNB) dpomode |= DP_OLCKNB;
  if(!(depot = dpopen(name, dpomode, VL_INITBNUM))) return NULL;
  flags = dpgetflags(depot);
  cmode = 0;
  root = -1;
  last = -1;
  lnum = 0;
  nnum = 0;
  rnum = 0;
  if(dprnum(depot) > 0){
    if(!(flags & VL_FLISVILLA) ||
       !vldpgetnum(depot, VL_ROOTKEY, &root) || !vldpgetnum(depot, VL_LASTKEY, &last) ||
       !vldpgetnum(depot, VL_LNUMKEY, &lnum) || !vldpgetnum(depot, VL_NNUMKEY, &nnum) ||
       !vldpgetnum(depot, VL_RNUMKEY, &rnum) || root < VL_LEAFIDMIN || last < VL_LEAFIDMIN ||
       lnum < 0 || nnum < 0 || rnum < 0){
      dpclose(depot);
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      return NULL;
    }
    if(flags & VL_FLISZLIB){
      cmode = VL_OZCOMP;
    } else if(flags & VL_FLISLZO){
      cmode = VL_OYCOMP;
    } else if(flags & VL_FLISBZIP){
      cmode = VL_OXCOMP;
    }
  } else if(omode & VL_OWRITER){
    if(omode & VL_OZCOMP){
      cmode = VL_OZCOMP;
    } else if(omode & VL_OYCOMP){
      cmode = VL_OYCOMP;
    } else if(omode & VL_OXCOMP){
      cmode = VL_OXCOMP;
    }
  }
  if(omode & VL_OWRITER){
    flags |= VL_FLISVILLA;
    if(_qdbm_deflate && cmode == VL_OZCOMP){
      flags |= VL_FLISZLIB;
    } else if(_qdbm_lzoencode && cmode == VL_OYCOMP){
      flags |= VL_FLISLZO;
    } else if(_qdbm_bzencode && cmode == VL_OXCOMP){
      flags |= VL_FLISBZIP;
    }
    if(!dpsetflags(depot, flags) || !dpsetalign(depot, VL_PAGEALIGN) ||
       !dpsetfbpsiz(depot, VL_FBPOOLSIZ)){
      dpclose(depot);
      return NULL;
    }
  }
  CB_MALLOC(villa, sizeof(VILLA));
  villa->depot = depot;
  villa->cmp = cmp;
  villa->wmode = (omode & VL_OWRITER);
  villa->cmode = cmode;
  villa->root = root;
  villa->last = last;
  villa->lnum = lnum;
  villa->nnum = nnum;
  villa->rnum = rnum;
  villa->leafc = cbmapopen();
  villa->nodec = cbmapopen();
  villa->hnum = 0;
  villa->hleaf = -1;
  villa->lleaf = -1;
  villa->curleaf = -1;
  villa->curknum = -1;
  villa->curvnum = -1;
  villa->leafrecmax = VL_DEFLRECMAX;
  villa->nodeidxmax = VL_DEFNIDXMAX;
  villa->leafcnum = VL_DEFLCNUM;
  villa->nodecnum = VL_DEFNCNUM;
  villa->tran = FALSE;
  villa->rbroot = -1;
  villa->rblast = -1;
  villa->rblnum = -1;
  villa->rbnnum = -1;
  villa->rbrnum = -1;
  if(root == -1){
    leaf = vlleafnew(villa, -1, -1);
    villa->root = leaf->id;
    villa->last = leaf->id;
    if(!vltranbegin(villa) || !vltranabort(villa)){
      vlclose(villa);
      return NULL;
    }
  }
  return villa;
}


/* Close a database handle. */
int vlclose(VILLA *villa){
  int err, pid;
  const char *tmp;
  assert(villa);
  err = FALSE;
  if(villa->tran){
    if(!vltranabort(villa)) err = TRUE;
  }
  cbmapiterinit(villa->leafc);
  while((tmp = cbmapiternext(villa->leafc, NULL)) != NULL){
    pid = *(int *)tmp;
    if(!vlleafcacheout(villa, pid)) err = TRUE;
  }
  cbmapiterinit(villa->nodec);
  while((tmp = cbmapiternext(villa->nodec, NULL)) != NULL){
    pid = *(int *)tmp;
    if(!vlnodecacheout(villa, pid)) err = TRUE;
  }
  if(villa->wmode){
    if(!dpsetalign(villa->depot, 0)) err = TRUE;
    if(!vldpputnum(villa->depot, VL_ROOTKEY, villa->root)) err = TRUE;
    if(!vldpputnum(villa->depot, VL_LASTKEY, villa->last)) err = TRUE;
    if(!vldpputnum(villa->depot, VL_LNUMKEY, villa->lnum)) err = TRUE;
    if(!vldpputnum(villa->depot, VL_NNUMKEY, villa->nnum)) err = TRUE;
    if(!vldpputnum(villa->depot, VL_RNUMKEY, villa->rnum)) err = TRUE;
  }
  cbmapclose(villa->leafc);
  cbmapclose(villa->nodec);
  if(!dpclose(villa->depot)) err = TRUE;
  free(villa);
  return err ? FALSE : TRUE;
}


/* Store a record. */
int vlput(VILLA *villa, const char *kbuf, int ksiz, const char *vbuf, int vsiz, int dmode){
  VLLEAF *leaf, *newleaf;
  VLNODE *node, *newnode;
  VLIDX *idxp;
  CBDATUM *key;
  int i, pid, todiv, heir, parent, mid;
  assert(villa && kbuf && vbuf);
  villa->curleaf = -1;
  villa->curknum = -1;
  villa->curvnum = -1;
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(vsiz < 0) vsiz = strlen(vbuf);
  if(villa->hleaf < VL_LEAFIDMIN || !(leaf = vlgethistleaf(villa, kbuf, ksiz))){
    if((pid = vlsearchleaf(villa, kbuf, ksiz)) == -1) return FALSE;
    if(!(leaf = vlleafload(villa, pid))) return FALSE;
  }
  if(!vlleafaddrec(villa, leaf, dmode, kbuf, ksiz, vbuf, vsiz)){
    dpecodeset(DP_EKEEP, __FILE__, __LINE__);
    return FALSE;
  }
  todiv = FALSE;
  switch(CB_LISTNUM(leaf->recs) % 4){
  case 0:
    if(CB_LISTNUM(leaf->recs) >= 4 &&
       vlleafdatasize(leaf) > VL_MAXLEAFSIZ * (villa->cmode > 0 ? 2 : 1)){
      todiv = TRUE;
      break;
    }
  case 2:
    if(CB_LISTNUM(leaf->recs) > villa->leafrecmax) todiv = TRUE;
    break;
  }
  if(todiv){
    if(!(newleaf = vlleafdivide(villa, leaf))) return FALSE;
    if(leaf->id == villa->last) villa->last = newleaf->id;
    heir = leaf->id;
    pid = newleaf->id;
    key = ((VLREC *)CB_LISTVAL(newleaf->recs, 0))->key;
    key = cbdatumdup(key);
    while(TRUE){
      if(villa->hnum < 1){
        node = vlnodenew(villa, heir);
        vlnodeaddidx(villa, node, TRUE, pid, CB_DATUMPTR(key), CB_DATUMSIZE(key));
        villa->root = node->id;
        CB_DATUMCLOSE(key);
        break;
      }
      parent = villa->hist[--villa->hnum];
      if(!(node = vlnodeload(villa, parent))){
        CB_DATUMCLOSE(key);
        return FALSE;
      }
      vlnodeaddidx(villa, node, FALSE, pid, CB_DATUMPTR(key), CB_DATUMSIZE(key));
      CB_DATUMCLOSE(key);
      if(CB_LISTNUM(node->idxs) <= villa->nodeidxmax) break;
      mid = CB_LISTNUM(node->idxs) / 2;
      idxp = (VLIDX *)CB_LISTVAL(node->idxs, mid);
      newnode = vlnodenew(villa, idxp->pid);
      heir = node->id;
      pid = newnode->id;
      CB_DATUMOPEN2(key, CB_DATUMPTR(idxp->key), CB_DATUMSIZE(idxp->key));
      for(i = mid + 1; i < CB_LISTNUM(node->idxs); i++){
        idxp = (VLIDX *)CB_LISTVAL(node->idxs, i);
        vlnodeaddidx(villa, newnode, TRUE, idxp->pid,
                     CB_DATUMPTR(idxp->key), CB_DATUMSIZE(idxp->key));
      }
      for(i = 0; i < CB_LISTNUM(newnode->idxs); i++){
        idxp = (VLIDX *)cblistpop(node->idxs, NULL);
        CB_DATUMCLOSE(idxp->key);
        free(idxp);
      }
      node->dirty = TRUE;
    }
  }
  if(!villa->tran && !vlcacheadjust(villa)) return FALSE;
  return TRUE;
}


/* Delete a record. */
int vlout(VILLA *villa, const char *kbuf, int ksiz){
  VLLEAF *leaf;
  VLREC *recp;
  int pid, ri, vsiz;
  char *vbuf;
  assert(villa && kbuf);
  villa->curleaf = -1;
  villa->curknum = -1;
  villa->curvnum = -1;
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(villa->hleaf < VL_LEAFIDMIN || !(leaf = vlgethistleaf(villa, kbuf, ksiz))){
    if((pid = vlsearchleaf(villa, kbuf, ksiz)) == -1) return FALSE;
    if(!(leaf = vlleafload(villa, pid))) return FALSE;
  }
  if(!(recp = vlrecsearch(villa, leaf, kbuf, ksiz, &ri))){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  if(recp->rest){
    CB_DATUMCLOSE(recp->first);
    vbuf = cblistshift(recp->rest, &vsiz);
    CB_DATUMOPEN2(recp->first, vbuf, vsiz);
    free(vbuf);
    if(CB_LISTNUM(recp->rest) < 1){
      CB_LISTCLOSE(recp->rest);
      recp->rest = NULL;
    }
  } else {
    CB_DATUMCLOSE(recp->key);
    CB_DATUMCLOSE(recp->first);
    free(cblistremove(leaf->recs, ri, NULL));
  }
  leaf->dirty = TRUE;
  villa->rnum--;
  if(!villa->tran && !vlcacheadjust(villa)) return FALSE;
  return TRUE;
}


/* Retrieve a record. */
char *vlget(VILLA *villa, const char *kbuf, int ksiz, int *sp){
  VLLEAF *leaf;
  VLREC *recp;
  char *rv;
  int pid;
  assert(villa && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(villa->hleaf < VL_LEAFIDMIN || !(leaf = vlgethistleaf(villa, kbuf, ksiz))){
    if((pid = vlsearchleaf(villa, kbuf, ksiz)) == -1) return NULL;
    if(!(leaf = vlleafload(villa, pid))) return NULL;
  }
  if(!(recp = vlrecsearch(villa, leaf, kbuf, ksiz, NULL))){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return NULL;
  }
  if(!villa->tran && !vlcacheadjust(villa)) return NULL;
  if(sp) *sp = CB_DATUMSIZE(recp->first);
  CB_MEMDUP(rv, CB_DATUMPTR(recp->first), CB_DATUMSIZE(recp->first));
  return rv;
}


/* Get the size of the value of a record. */
int vlvsiz(VILLA *villa, const char *kbuf, int ksiz){
  VLLEAF *leaf;
  VLREC *recp;
  int pid;
  assert(villa && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(villa->hleaf < VL_LEAFIDMIN || !(leaf = vlgethistleaf(villa, kbuf, ksiz))){
    if((pid = vlsearchleaf(villa, kbuf, ksiz)) == -1) return -1;
    if(!(leaf = vlleafload(villa, pid))) return -1;
  }
  if(!(recp = vlrecsearch(villa, leaf, kbuf, ksiz, NULL))){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return -1;
  }
  if(!villa->tran && !vlcacheadjust(villa)) return -1;
  return CB_DATUMSIZE(recp->first);
}


/* Get the number of records corresponding a key. */
int vlvnum(VILLA *villa, const char *kbuf, int ksiz){
  VLLEAF *leaf;
  VLREC *recp;
  int pid;
  assert(villa && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(villa->hleaf < VL_LEAFIDMIN || !(leaf = vlgethistleaf(villa, kbuf, ksiz))){
    if((pid = vlsearchleaf(villa, kbuf, ksiz)) == -1) return 0;
    if(!(leaf = vlleafload(villa, pid))) return 0;
  }
  if(!(recp = vlrecsearch(villa, leaf, kbuf, ksiz, NULL))){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return 0;
  }
  if(!villa->tran && !vlcacheadjust(villa)) return 0;
  return 1 + (recp->rest ? CB_LISTNUM(recp->rest) : 0);
}


/* Store plural records corresponding a key. */
int vlputlist(VILLA *villa, const char *kbuf, int ksiz, const CBLIST *vals){
  int i, vsiz;
  const char *vbuf;
  assert(villa && kbuf && vals);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(CB_LISTNUM(vals) < 1){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  for(i = 0; i < CB_LISTNUM(vals); i++){
    vbuf = CB_LISTVAL2(vals, i, vsiz);
    if(!vlput(villa, kbuf, ksiz, vbuf, vsiz, VL_DDUP)) return FALSE;
  }
  return TRUE;
}


/* Delete all records corresponding a key. */
int vloutlist(VILLA *villa, const char *kbuf, int ksiz){
  int i, vnum;
  assert(villa && kbuf);
  if(!villa->wmode){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  if((vnum = vlvnum(villa, kbuf, ksiz)) < 1) return FALSE;
  for(i = 0; i < vnum; i++){
    if(!vlout(villa, kbuf, ksiz)) return FALSE;
  }
  return TRUE;
}


/* Retrieve values of all records corresponding a key. */
CBLIST *vlgetlist(VILLA *villa, const char *kbuf, int ksiz){
  VLLEAF *leaf;
  VLREC *recp;
  int pid, i, vsiz;
  CBLIST *vals;
  const char *vbuf;
  assert(villa && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(villa->hleaf < VL_LEAFIDMIN || !(leaf = vlgethistleaf(villa, kbuf, ksiz))){
    if((pid = vlsearchleaf(villa, kbuf, ksiz)) == -1) return NULL;
    if(!(leaf = vlleafload(villa, pid))) return NULL;
  }
  if(!(recp = vlrecsearch(villa, leaf, kbuf, ksiz, NULL))){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return NULL;
  }
  CB_LISTOPEN(vals);
  CB_LISTPUSH(vals, CB_DATUMPTR(recp->first), CB_DATUMSIZE(recp->first));
  if(recp->rest){
    for(i = 0; i < CB_LISTNUM(recp->rest); i++){
      vbuf = CB_LISTVAL2(recp->rest, i, vsiz);
      CB_LISTPUSH(vals, vbuf, vsiz);
    }
  }
  if(!villa->tran && !vlcacheadjust(villa)){
    CB_LISTCLOSE(vals);
    return NULL;
  }
  return vals;
}


/* Retrieve concatenated values of all records corresponding a key. */
char *vlgetcat(VILLA *villa, const char *kbuf, int ksiz, int *sp){
  VLLEAF *leaf;
  VLREC *recp;
  int pid, i, vsiz, rsiz;
  char *rbuf;
  const char *vbuf;
  assert(villa && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(villa->hleaf < VL_LEAFIDMIN || !(leaf = vlgethistleaf(villa, kbuf, ksiz))){
    if((pid = vlsearchleaf(villa, kbuf, ksiz)) == -1) return NULL;
    if(!(leaf = vlleafload(villa, pid))) return NULL;
  }
  if(!(recp = vlrecsearch(villa, leaf, kbuf, ksiz, NULL))){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return NULL;
  }
  rsiz = CB_DATUMSIZE(recp->first);
  CB_MALLOC(rbuf, rsiz + 1);
  memcpy(rbuf, CB_DATUMPTR(recp->first), rsiz);
  if(recp->rest){
    for(i = 0; i < CB_LISTNUM(recp->rest); i++){
      vbuf = CB_LISTVAL2(recp->rest, i, vsiz);
      CB_REALLOC(rbuf, rsiz + vsiz + 1);
      memcpy(rbuf + rsiz, vbuf, vsiz);
      rsiz += vsiz;
    }
  }
  rbuf[rsiz] = '\0';
  if(!villa->tran && !vlcacheadjust(villa)){
    free(rbuf);
    return NULL;
  }
  if(sp) *sp = rsiz;
  return rbuf;
}


/* Move the cursor to the first record. */
int vlcurfirst(VILLA *villa){
  VLLEAF *leaf;
  assert(villa);
  villa->curleaf = VL_LEAFIDMIN;
  villa->curknum = 0;
  villa->curvnum = 0;
  if(!(leaf = vlleafload(villa, villa->curleaf))){
    villa->curleaf = -1;
    return FALSE;
  }
  while(CB_LISTNUM(leaf->recs) < 1){
    villa->curleaf = leaf->next;
    villa->curknum = 0;
    villa->curvnum = 0;
    if(villa->curleaf == -1){
      dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
      return FALSE;
    }
    if(!(leaf = vlleafload(villa, villa->curleaf))){
      villa->curleaf = -1;
      return FALSE;
    }
  }
  return TRUE;
}


/* Move the cursor to the last record. */
int vlcurlast(VILLA *villa){
  VLLEAF *leaf;
  VLREC *recp;
  assert(villa);
  villa->curleaf = villa->last;
  if(!(leaf = vlleafload(villa, villa->curleaf))){
    villa->curleaf = -1;
    return FALSE;
  }
  while(CB_LISTNUM(leaf->recs) < 1){
    villa->curleaf = leaf->prev;
    if(villa->curleaf == -1){
      villa->curleaf = -1;
      dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
      return FALSE;
    }
    if(!(leaf = vlleafload(villa, villa->curleaf))){
      villa->curleaf = -1;
      return FALSE;
    }
  }
  villa->curknum = CB_LISTNUM(leaf->recs) - 1;
  recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum);
  villa->curvnum = recp->rest ? CB_LISTNUM(recp->rest) : 0;
  return TRUE;
}


/* Move the cursor to the previous record. */
int vlcurprev(VILLA *villa){
  VLLEAF *leaf;
  VLREC *recp;
  assert(villa);
  if(villa->curleaf == -1){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  if(!(leaf = vlleafload(villa, villa->curleaf)) || CB_LISTNUM(leaf->recs) < 1){
    villa->curleaf = -1;
    return FALSE;
  }
  recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum);
  villa->curvnum--;
  if(villa->curvnum < 0){
    villa->curknum--;
    if(villa->curknum < 0){
      villa->curleaf = leaf->prev;
      if(villa->curleaf == -1){
        villa->curleaf = -1;
        dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
        return FALSE;
      }
      if(!(leaf = vlleafload(villa, villa->curleaf))){
        villa->curleaf = -1;
        return FALSE;
      }
      while(CB_LISTNUM(leaf->recs) < 1){
        villa->curleaf = leaf->prev;
        if(villa->curleaf == -1){
          dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
          return FALSE;
        }
        if(!(leaf = vlleafload(villa, villa->curleaf))){
          villa->curleaf = -1;
          return FALSE;
        }
      }
      villa->curknum = CB_LISTNUM(leaf->recs) - 1;
      recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum);
      villa->curvnum = recp->rest ? CB_LISTNUM(recp->rest) : 0;
    }
    recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum);
    villa->curvnum = recp->rest ? CB_LISTNUM(recp->rest) : 0;
  }
  if(!villa->tran && !vlcacheadjust(villa)) return FALSE;
  return TRUE;
}


/* Move the cursor to the next record. */
int vlcurnext(VILLA *villa){
  VLLEAF *leaf;
  VLREC *recp;
  assert(villa);
  if(villa->curleaf == -1){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  if(!(leaf = vlleafload(villa, villa->curleaf)) || CB_LISTNUM(leaf->recs) < 1){
    villa->curleaf = -1;
    return FALSE;
  }
  recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum);
  villa->curvnum++;
  if(villa->curvnum > (recp->rest ? CB_LISTNUM(recp->rest) : 0)){
    villa->curknum++;
    villa->curvnum = 0;
  }
  if(villa->curknum >= CB_LISTNUM(leaf->recs)){
    villa->curleaf = leaf->next;
    villa->curknum = 0;
    villa->curvnum = 0;
    if(villa->curleaf == -1){
      dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
      return FALSE;
    }
    if(!(leaf = vlleafload(villa, villa->curleaf))){
      villa->curleaf = -1;
      return FALSE;
    }
    while(CB_LISTNUM(leaf->recs) < 1){
      villa->curleaf = leaf->next;
      villa->curknum = 0;
      villa->curvnum = 0;
      if(villa->curleaf == -1){
        dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
        return FALSE;
      }
      if(!(leaf = vlleafload(villa, villa->curleaf))){
        villa->curleaf = -1;
        return FALSE;
      }
    }
  }
  if(!villa->tran && !vlcacheadjust(villa)) return FALSE;
  return TRUE;
}


/* Move the cursor to a position around a record. */
int vlcurjump(VILLA *villa, const char *kbuf, int ksiz, int jmode){
  VLLEAF *leaf;
  VLREC *recp;
  int pid, index;
  assert(villa && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if((pid = vlsearchleaf(villa, kbuf, ksiz)) == -1){
    villa->curleaf = -1;
    return FALSE;
  }
  if(!(leaf = vlleafload(villa, pid))){
    villa->curleaf = -1;
    return FALSE;
  }
  while(CB_LISTNUM(leaf->recs) < 1){
    villa->curleaf = (jmode == VL_JFORWARD) ? leaf->next : leaf->prev;
    if(villa->curleaf == -1){
      dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
      return FALSE;
    }
    if(!(leaf = vlleafload(villa, villa->curleaf))){
      villa->curleaf = -1;
      return FALSE;
    }
  }
  if(!(recp = vlrecsearch(villa, leaf, kbuf, ksiz, &index))){
    if(jmode == VL_JFORWARD){
      villa->curleaf = leaf->id;
      if(index >= CB_LISTNUM(leaf->recs)) index--;
      villa->curknum = index;
      villa->curvnum = 0;
      recp = (VLREC *)CB_LISTVAL(leaf->recs, index);
      if(villa->cmp(kbuf, ksiz, CB_DATUMPTR(recp->key), CB_DATUMSIZE(recp->key)) < 0) return TRUE;
      villa->curvnum = (recp->rest ? CB_LISTNUM(recp->rest) : 0);
      return vlcurnext(villa);
    } else {
      villa->curleaf = leaf->id;
      if(index >= CB_LISTNUM(leaf->recs)) index--;
      villa->curknum = index;
      recp = (VLREC *)CB_LISTVAL(leaf->recs, index);
      villa->curvnum = (recp->rest ? CB_LISTNUM(recp->rest) : 0);
      if(villa->cmp(kbuf, ksiz, CB_DATUMPTR(recp->key), CB_DATUMSIZE(recp->key)) > 0) return TRUE;
      villa->curvnum = 0;
      return vlcurprev(villa);
    }
  }
  if(jmode == VL_JFORWARD){
    villa->curleaf = pid;
    villa->curknum = index;
    villa->curvnum = 0;
  } else {
    villa->curleaf = pid;
    villa->curknum = index;
    villa->curvnum = (recp->rest ? CB_LISTNUM(recp->rest) : 0);
  }
  return TRUE;
}


/* Get the key of the record where the cursor is. */
char *vlcurkey(VILLA *villa, int *sp){
  VLLEAF *leaf;
  VLREC *recp;
  const char *kbuf;
  char *rv;
  int ksiz;
  assert(villa);
  if(villa->curleaf == -1){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  if(!(leaf = vlleafload(villa, villa->curleaf))){
    villa->curleaf = -1;
    return FALSE;
  }
  recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum);
  kbuf = CB_DATUMPTR(recp->key);
  ksiz = CB_DATUMSIZE(recp->key);
  if(sp) *sp = ksiz;
  CB_MEMDUP(rv, kbuf, ksiz);
  return rv;
}


/* Get the value of the record where the cursor is. */
char *vlcurval(VILLA *villa, int *sp){
  VLLEAF *leaf;
  VLREC *recp;
  const char *vbuf;
  char *rv;
  int vsiz;
  assert(villa);
  if(villa->curleaf == -1){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  if(!(leaf = vlleafload(villa, villa->curleaf))){
    villa->curleaf = -1;
    return FALSE;
  }
  recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum);
  if(villa->curvnum < 1){
    vbuf = CB_DATUMPTR(recp->first);
    vsiz = CB_DATUMSIZE(recp->first);
  } else {
    vbuf = CB_LISTVAL2(recp->rest, villa->curvnum - 1, vsiz);
  }
  if(sp) *sp = vsiz;
  CB_MEMDUP(rv, vbuf, vsiz);
  return rv;
}


/* Insert a record around the cursor. */
int vlcurput(VILLA *villa, const char *vbuf, int vsiz, int cpmode){
  VLLEAF *leaf;
  VLREC *recp;
  char *tbuf;
  int tsiz;
  assert(villa && vbuf);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(vsiz < 0) vsiz = strlen(vbuf);
  if(villa->curleaf == -1){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  if(!(leaf = vlleafload(villa, villa->curleaf))){
    villa->curleaf = -1;
    return FALSE;
  }
  recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum);
  switch(cpmode){
  case VL_CPBEFORE:
    if(villa->curvnum < 1){
      if(!recp->rest){
        CB_DATUMTOMALLOC(recp->first, tbuf, tsiz);
        CB_DATUMOPEN2(recp->first, vbuf, vsiz);
        CB_LISTOPEN(recp->rest);
        CB_LISTPUSHBUF(recp->rest, tbuf, tsiz);
      } else {
        cblistunshift(recp->rest, CB_DATUMPTR(recp->first), CB_DATUMSIZE(recp->first));
        CB_DATUMSETSIZE(recp->first, 0);
        CB_DATUMCAT(recp->first, vbuf, vsiz);
      }
    } else {
      CB_LISTINSERT(recp->rest, villa->curvnum - 1, vbuf, vsiz);
    }
    villa->rnum++;
    break;
  case VL_CPAFTER:
    if(!recp->rest) CB_LISTOPEN(recp->rest);
    CB_LISTINSERT(recp->rest, villa->curvnum, vbuf, vsiz);
    villa->curvnum++;
    villa->rnum++;
    break;
  default:
    if(villa->curvnum < 1){
      CB_DATUMSETSIZE(recp->first, 0);
      CB_DATUMCAT(recp->first, vbuf, vsiz);
    } else {
      cblistover(recp->rest, villa->curvnum - 1, vbuf, vsiz);
    }
    break;
  }
  leaf->dirty = TRUE;
  return TRUE;
}


/* Delete the record where the cursor is. */
int vlcurout(VILLA *villa){
  VLLEAF *leaf;
  VLREC *recp;
  char *vbuf;
  int vsiz;
  assert(villa);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(villa->curleaf == -1){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  if(!(leaf = vlleafload(villa, villa->curleaf))){
    villa->curleaf = -1;
    return FALSE;
  }
  recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum);
  if(villa->curvnum < 1){
    if(recp->rest){
      vbuf = cblistshift(recp->rest, &vsiz);
      CB_DATUMSETSIZE(recp->first, 0);
      CB_DATUMCAT(recp->first, vbuf, vsiz);
      free(vbuf);
      if(CB_LISTNUM(recp->rest) < 1){
        CB_LISTCLOSE(recp->rest);
        recp->rest = NULL;
      }
    } else {
      CB_DATUMCLOSE(recp->first);
      CB_DATUMCLOSE(recp->key);
      free(cblistremove(leaf->recs, villa->curknum, NULL));
    }
  } else {
    free(cblistremove(recp->rest, villa->curvnum - 1, NULL));
    if(villa->curvnum - 1 >= CB_LISTNUM(recp->rest)){
      villa->curknum++;
      villa->curvnum = 0;
    }
    if(CB_LISTNUM(recp->rest) < 1){
      CB_LISTCLOSE(recp->rest);
      recp->rest = NULL;
    }
  }
  villa->rnum--;
  leaf->dirty = TRUE;
  if(villa->curknum >= CB_LISTNUM(leaf->recs)){
    villa->curleaf = leaf->next;
    villa->curknum = 0;
    villa->curvnum = 0;
    while(villa->curleaf != -1 && (leaf = vlleafload(villa, villa->curleaf)) != NULL &&
          CB_LISTNUM(leaf->recs) < 1){
      villa->curleaf = leaf->next;
    }
  }
  return TRUE;
}


/* Set the tuning parameters for performance. */
void vlsettuning(VILLA *villa, int lrecmax, int nidxmax, int lcnum, int ncnum){
  assert(villa);
  if(lrecmax < 1) lrecmax = VL_DEFLRECMAX;
  if(lrecmax < 3) lrecmax = 3;
  if(nidxmax < 1) nidxmax = VL_DEFNIDXMAX;
  if(nidxmax < 4) nidxmax = 4;
  if(lcnum < 1) lcnum = VL_DEFLCNUM;
  if(lcnum < VL_CACHEOUT * 2) lcnum = VL_CACHEOUT * 2;
  if(ncnum < 1) ncnum = VL_DEFNCNUM;
  if(ncnum < VL_CACHEOUT * 2) ncnum = VL_CACHEOUT * 2;
  villa->leafrecmax = lrecmax;
  villa->nodeidxmax = nidxmax;
  villa->leafcnum = lcnum;
  villa->nodecnum = ncnum;
}


/* Set the size of the free block pool of a database handle. */
int vlsetfbpsiz(VILLA *villa, int size){
  assert(villa && size >= 0);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  return dpsetfbpsiz(villa->depot, size);
}


/* Synchronize updating contents with the file and the device. */
int vlsync(VILLA *villa){
  int err;
  err = FALSE;
  if(!vlmemsync(villa)) err = TRUE;
  if(!dpsync(villa->depot)) err = TRUE;
  return err ? FALSE : TRUE;
}


/* Optimize a database. */
int vloptimize(VILLA *villa){
  int err;
  assert(villa);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(villa->tran){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  if(!vlsync(villa)) return FALSE;
  if(!dpoptimize(villa->depot, -1)) err = TRUE;
  return err ? FALSE : TRUE;
}


/* Get the name of a database. */
char *vlname(VILLA *villa){
  assert(villa);
  return dpname(villa->depot);
}


/* Get the size of a database file. */
int vlfsiz(VILLA *villa){
  return dpfsiz(villa->depot);
}


/* Get the number of the leaf nodes of B+ tree. */
int vllnum(VILLA *villa){
  assert(villa);
  return villa->lnum;
}


/* Get the number of the non-leaf nodes of B+ tree. */
int vlnnum(VILLA *villa){
  assert(villa);
  return villa->nnum;
}


/* Get the number of the records stored in a database. */
int vlrnum(VILLA *villa){
  assert(villa);
  return villa->rnum;
}


/* Check whether a database handle is a writer or not. */
int vlwritable(VILLA *villa){
  assert(villa);
  return villa->wmode;
}


/* Check whether a database has a fatal error or not. */
int vlfatalerror(VILLA *villa){
  assert(villa);
  return dpfatalerror(villa->depot);
}


/* Get the inode number of a database file. */
int vlinode(VILLA *villa){
  assert(villa);
  return dpinode(villa->depot);
}


/* Get the last modified time of a database. */
time_t vlmtime(VILLA *villa){
  assert(villa);
  return dpmtime(villa->depot);
}


/* Begin the transaction. */
int vltranbegin(VILLA *villa){
  int err, pid;
  const char *tmp;
  VLLEAF *leaf;
  VLNODE *node;
  assert(villa);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(villa->tran){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  cbmapiterinit(villa->leafc);
  while((tmp = cbmapiternext(villa->leafc, NULL)) != NULL){
    pid = *(int *)tmp;
    leaf = (VLLEAF *)cbmapget(villa->leafc, (char *)&pid, sizeof(int), NULL);
    if(leaf->dirty && !vlleafsave(villa, leaf)) err = TRUE;
  }
  cbmapiterinit(villa->nodec);
  while((tmp = cbmapiternext(villa->nodec, NULL)) != NULL){
    pid = *(int *)tmp;
    node = (VLNODE *)cbmapget(villa->nodec, (char *)&pid, sizeof(int), NULL);
    if(node->dirty && !vlnodesave(villa, node)) err = TRUE;
  }
  if(!dpsetalign(villa->depot, 0)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_ROOTKEY, villa->root)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_LASTKEY, villa->last)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_LNUMKEY, villa->lnum)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_NNUMKEY, villa->nnum)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_RNUMKEY, villa->rnum)) err = TRUE;
  if(!dpmemsync(villa->depot)) err = TRUE;
  if(!dpsetalign(villa->depot, VL_PAGEALIGN)) err = TRUE;
  villa->tran = TRUE;
  villa->rbroot = villa->root;
  villa->rblast = villa->last;
  villa->rblnum = villa->lnum;
  villa->rbnnum = villa->nnum;
  villa->rbrnum = villa->rnum;
  return err ? FALSE : TRUE;
}


/* Commit the transaction. */
int vltrancommit(VILLA *villa){
  int err, pid;
  const char *tmp;
  VLLEAF *leaf;
  VLNODE *node;
  assert(villa);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(!villa->tran){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  cbmapiterinit(villa->leafc);
  while((tmp = cbmapiternext(villa->leafc, NULL)) != NULL){
    pid = *(int *)tmp;
    leaf = (VLLEAF *)cbmapget(villa->leafc, (char *)&pid, sizeof(int), NULL);
    if(leaf->dirty && !vlleafsave(villa, leaf)) err = TRUE;
  }
  cbmapiterinit(villa->nodec);
  while((tmp = cbmapiternext(villa->nodec, NULL)) != NULL){
    pid = *(int *)tmp;
    node = (VLNODE *)cbmapget(villa->nodec, (char *)&pid, sizeof(int), NULL);
    if(node->dirty && !vlnodesave(villa, node)) err = TRUE;
  }
  if(!dpsetalign(villa->depot, 0)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_ROOTKEY, villa->root)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_LASTKEY, villa->last)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_LNUMKEY, villa->lnum)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_NNUMKEY, villa->nnum)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_RNUMKEY, villa->rnum)) err = TRUE;
  if(!dpmemsync(villa->depot)) err = TRUE;
  if(!dpsetalign(villa->depot, VL_PAGEALIGN)) err = TRUE;
  villa->tran = FALSE;
  villa->rbroot = -1;
  villa->rblast = -1;
  villa->rblnum = -1;
  villa->rbnnum = -1;
  villa->rbrnum = -1;
  while(cbmaprnum(villa->leafc) > villa->leafcnum || cbmaprnum(villa->nodec) > villa->nodecnum){
    if(!vlcacheadjust(villa)){
      err = TRUE;
      break;
    }
  }
  return err ? FALSE : TRUE;
}


/* Abort the transaction. */
int vltranabort(VILLA *villa){
  int err, pid;
  const char *tmp;
  VLLEAF *leaf;
  VLNODE *node;
  assert(villa);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(!villa->tran){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  cbmapiterinit(villa->leafc);
  while((tmp = cbmapiternext(villa->leafc, NULL)) != NULL){
    pid = *(int *)tmp;
    if(!(leaf = (VLLEAF *)cbmapget(villa->leafc, (char *)&pid, sizeof(int), NULL))){
      err = TRUE;
      continue;
    }
    if(leaf->dirty){
      leaf->dirty = FALSE;
      if(!vlleafcacheout(villa, pid)) err = TRUE;
    }
  }
  cbmapiterinit(villa->nodec);
  while((tmp = cbmapiternext(villa->nodec, NULL)) != NULL){
    pid = *(int *)tmp;
    if(!(node = (VLNODE *)cbmapget(villa->nodec, (char *)&pid, sizeof(int), NULL))){
      err = TRUE;
      continue;
    }
    if(node->dirty){
      node->dirty = FALSE;
      if(!vlnodecacheout(villa, pid)) err = TRUE;
    }
  }
  villa->tran = FALSE;
  villa->root = villa->rbroot;
  villa->last = villa->rblast;
  villa->lnum = villa->rblnum;
  villa->nnum = villa->rbnnum;
  villa->rnum = villa->rbrnum;
  while(cbmaprnum(villa->leafc) > villa->leafcnum || cbmaprnum(villa->nodec) > villa->nodecnum){
    if(!vlcacheadjust(villa)){
      err = TRUE;
      break;
    }
  }
  return err ? FALSE : TRUE;
}


/* Remove a database file. */
int vlremove(const char *name){
  assert(name);
  return dpremove(name);
}


/* Repair a broken database file. */
int vlrepair(const char *name, VLCFUNC cmp){
  DEPOT *depot;
  VILLA *tvilla;
  char path[VL_PATHBUFSIZ], *kbuf, *vbuf, *zbuf, *rp, *tkbuf, *tvbuf;
  int i, err, flags, omode, ksiz, vsiz, zsiz, size, step, tksiz, tvsiz, vnum;
  assert(name && cmp);
  err = FALSE;
  if(!dprepair(name)) err = TRUE;
  if(!(depot = dpopen(name, DP_OREADER, -1))) return FALSE;
  flags = dpgetflags(depot);
  if(!(flags & VL_FLISVILLA)){
    dpclose(depot);
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return FALSE;
  }
  sprintf(path, "%s%s", name, VL_TMPFSUF);
  omode = VL_OWRITER | VL_OCREAT | VL_OTRUNC;
  if(flags & VL_FLISZLIB){
    omode |= VL_OZCOMP;
  } else if(flags & VL_FLISLZO){
    omode |= VL_OXCOMP;
  } else if(flags & VL_FLISBZIP){
    omode |= VL_OYCOMP;
  }
  if(!(tvilla = vlopen(path, omode, cmp))){
    dpclose(depot);
    return FALSE;
  }
  if(!dpiterinit(depot)) err = TRUE;
  while((kbuf =  dpiternext(depot, &ksiz)) != NULL){
    if(ksiz == sizeof(int) && *(int *)kbuf < VL_NODEIDMIN && *(int *)kbuf > 0){
      if((vbuf = dpget(depot, (char *)kbuf, sizeof(int), 0, -1, &vsiz)) != NULL){
        if(_qdbm_inflate && (flags & VL_FLISZLIB) &&
           (zbuf = _qdbm_inflate(vbuf, vsiz, &zsiz, _QDBM_ZMRAW)) != NULL){
          free(vbuf);
          vbuf = zbuf;
          vsiz = zsiz;
        } else if(_qdbm_lzodecode && (flags & VL_FLISLZO) &&
                  (zbuf = _qdbm_lzodecode(vbuf, vsiz, &zsiz)) != NULL){
          free(vbuf);
          vbuf = zbuf;
          vsiz = zsiz;
        } else if(_qdbm_bzdecode && (flags & VL_FLISBZIP) &&
                  (zbuf = _qdbm_bzdecode(vbuf, vsiz, &zsiz)) != NULL){
          free(vbuf);
          vbuf = zbuf;
          vsiz = zsiz;
        }
        rp = vbuf;
        size = vsiz;
        if(size >= 1){
          VL_READVNUMBUF(rp, size, vnum, step);
          rp += step;
          size -= step;
        }
        if(size >= 1){
          VL_READVNUMBUF(rp, size, vnum, step);
          rp += step;
          size -= step;
        }
        while(size >= 1){
          VL_READVNUMBUF(rp, size, tksiz, step);
          rp += step;
          size -= step;
          if(size < tksiz) break;
          tkbuf = rp;
          rp += tksiz;
          size -= tksiz;
          if(size < 1) break;
          VL_READVNUMBUF(rp, size, vnum, step);
          rp += step;
          size -= step;
          if(vnum < 1 || size < 1) break;
          for(i = 0; i < vnum && size >= 1; i++){
            VL_READVNUMBUF(rp, size, tvsiz, step);
            rp += step;
            size -= step;
            if(size < tvsiz) break;
            tvbuf = rp;
            rp += tvsiz;
            size -= tvsiz;
            if(!vlput(tvilla, tkbuf, tksiz, tvbuf, tvsiz, VL_DDUP)) err = TRUE;
          }
        }
        free(vbuf);
      }
    }
    free(kbuf);
  }
  if(!vlclose(tvilla)) err = TRUE;
  if(!dpclose(depot)) err = TRUE;
  if(!dpremove(name)) err = TRUE;
  if(rename(path, name) == -1){
    if(!err) dpecodeset(DP_EMISC, __FILE__, __LINE__);
    err = TRUE;
  }
  return err ? FALSE : TRUE;
}


/* Dump all records as endian independent data. */
int vlexportdb(VILLA *villa, const char *name){
  DEPOT *depot;
  char path[VL_PATHBUFSIZ], *kbuf, *vbuf, *nkey;
  int i, err, ksiz, vsiz, ki;
  assert(villa && name);
  sprintf(path, "%s%s", name, VL_TMPFSUF);
  if(!(depot = dpopen(path, DP_OWRITER | DP_OCREAT | DP_OTRUNC, -1))) return FALSE;
  err = FALSE;
  vlcurfirst(villa);
  for(i = 0; !err && (kbuf = vlcurkey(villa, &ksiz)) != NULL; i++){
    if((vbuf = vlcurval(villa, &vsiz)) != NULL){
      CB_MALLOC(nkey, ksiz + VL_NUMBUFSIZ);
      ki = sprintf(nkey, "%X\t", i);
      memcpy(nkey + ki, kbuf, ksiz);
      if(!dpput(depot, nkey, ki + ksiz, vbuf, vsiz, DP_DKEEP)) err = TRUE;
      free(nkey);
      free(vbuf);
    } else {
      err = TRUE;
    }
    free(kbuf);
    vlcurnext(villa);
  }
  if(!dpexportdb(depot, name)) err = TRUE;
  if(!dpclose(depot)) err = TRUE;
  if(!dpremove(path)) err = TRUE;
  return !err && !vlfatalerror(villa);
}


/* Load all records from endian independent data. */
int vlimportdb(VILLA *villa, const char *name){
  DEPOT *depot;
  char path[VL_PATHBUFSIZ], *kbuf, *vbuf, *rp;
  int err, ksiz, vsiz;
  assert(villa && name);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(vlrnum(villa) > 0){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  kbuf = dpname(villa->depot);
  sprintf(path, "%s%s", kbuf, VL_TMPFSUF);
  free(kbuf);
  if(!(depot = dpopen(path, DP_OWRITER | DP_OCREAT | DP_OTRUNC, -1))) return FALSE;
  err = FALSE;
  if(!dpimportdb(depot, name)) err = TRUE;
  dpiterinit(depot);
  while(!err && (kbuf = dpiternext(depot, &ksiz)) != NULL){
    if((vbuf = dpget(depot, kbuf, ksiz, 0, -1, &vsiz)) != NULL){
      if((rp = strchr(kbuf, '\t')) != NULL){
        rp++;
        if(!vlput(villa, rp, ksiz - (rp - kbuf), vbuf, vsiz, VL_DDUP)) err = TRUE;
      } else {
        dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
        err = TRUE;
      }
      free(vbuf);
    } else {
      err = TRUE;
    }
    free(kbuf);
  }
  if(!dpclose(depot)) err = TRUE;
  if(!dpremove(path)) err = TRUE;
  return !err && !vlfatalerror(villa);
}



/*************************************************************************************************
 * features for experts
 *************************************************************************************************/


/* Number of division of the database for Vista. */
int *vlcrdnumptr(void){
  static int defvlcrdnum = VL_CRDNUM;
  void *ptr;
  if(_qdbm_ptsafe){
    if(!(ptr = _qdbm_settsd(&defvlcrdnum, sizeof(int), &defvlcrdnum))){
      defvlcrdnum = DP_EMISC;
      return &defvlcrdnum;
    }
    return (int *)ptr;
  }
  return &defvlcrdnum;
}


/* Synchronize updating contents on memory. */
int vlmemsync(VILLA *villa){
  int err, pid;
  const char *tmp;
  assert(villa);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(villa->tran){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  cbmapiterinit(villa->leafc);
  while((tmp = cbmapiternext(villa->leafc, NULL)) != NULL){
    pid = *(int *)tmp;
    if(!vlleafcacheout(villa, pid)) err = TRUE;
  }
  cbmapiterinit(villa->nodec);
  while((tmp = cbmapiternext(villa->nodec, NULL)) != NULL){
    pid = *(int *)tmp;
    if(!vlnodecacheout(villa, pid)) err = TRUE;
  }
  if(!dpsetalign(villa->depot, 0)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_ROOTKEY, villa->root)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_LASTKEY, villa->last)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_LNUMKEY, villa->lnum)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_NNUMKEY, villa->nnum)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_RNUMKEY, villa->rnum)) err = TRUE;
  if(!dpsetalign(villa->depot, VL_PAGEALIGN)) err = TRUE;
  if(!dpmemsync(villa->depot)) err = TRUE;
  return err ? FALSE : TRUE;
}


/* Synchronize updating contents on memory, not physically. */
int vlmemflush(VILLA *villa){
  int err, pid;
  const char *tmp;
  assert(villa);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(villa->tran){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  cbmapiterinit(villa->leafc);
  while((tmp = cbmapiternext(villa->leafc, NULL)) != NULL){
    pid = *(int *)tmp;
    if(!vlleafcacheout(villa, pid)) err = TRUE;
  }
  cbmapiterinit(villa->nodec);
  while((tmp = cbmapiternext(villa->nodec, NULL)) != NULL){
    pid = *(int *)tmp;
    if(!vlnodecacheout(villa, pid)) err = TRUE;
  }
  if(!dpsetalign(villa->depot, 0)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_ROOTKEY, villa->root)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_LASTKEY, villa->last)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_LNUMKEY, villa->lnum)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_NNUMKEY, villa->nnum)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_RNUMKEY, villa->rnum)) err = TRUE;
  if(!dpsetalign(villa->depot, VL_PAGEALIGN)) err = TRUE;
  if(!dpmemflush(villa->depot)) err = TRUE;
  return err ? FALSE : TRUE;
}


/* Refer to a volatile cache of a value of a record. */
const char *vlgetcache(VILLA *villa, const char *kbuf, int ksiz, int *sp){
  VLLEAF *leaf;
  VLREC *recp;
  int pid;
  assert(villa && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(villa->hleaf < VL_LEAFIDMIN || !(leaf = vlgethistleaf(villa, kbuf, ksiz))){
    if((pid = vlsearchleaf(villa, kbuf, ksiz)) == -1) return NULL;
    if(!(leaf = vlleafload(villa, pid))) return NULL;
  }
  if(!(recp = vlrecsearch(villa, leaf, kbuf, ksiz, NULL))){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return NULL;
  }
  if(!villa->tran && !vlcacheadjust(villa)) return NULL;
  if(sp) *sp = CB_DATUMSIZE(recp->first);
  return CB_DATUMPTR(recp->first);
}


/* Refer to volatile cache of the key of the record where the cursor is. */
const char *vlcurkeycache(VILLA *villa, int *sp){
  VLLEAF *leaf;
  VLREC *recp;
  const char *kbuf;
  int ksiz;
  assert(villa);
  if(villa->curleaf == -1){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  if(!(leaf = vlleafload(villa, villa->curleaf))){
    villa->curleaf = -1;
    return FALSE;
  }
  recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum);
  kbuf = CB_DATUMPTR(recp->key);
  ksiz = CB_DATUMSIZE(recp->key);
  if(sp) *sp = ksiz;
  return kbuf;
}


/* Refer to volatile cache of the value of the record where the cursor is. */
const char *vlcurvalcache(VILLA *villa, int *sp){
  VLLEAF *leaf;
  VLREC *recp;
  const char *vbuf;
  int vsiz;
  assert(villa);
  if(villa->curleaf == -1){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  if(!(leaf = vlleafload(villa, villa->curleaf))){
    villa->curleaf = -1;
    return FALSE;
  }
  recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum);
  if(villa->curvnum < 1){
    vbuf = CB_DATUMPTR(recp->first);
    vsiz = CB_DATUMSIZE(recp->first);
  } else {
    vbuf = CB_LISTVAL2(recp->rest, villa->curvnum - 1, vsiz);
  }
  if(sp) *sp = vsiz;
  return vbuf;
}


/* Get a multiple cursor handle. */
VLMULCUR *vlmulcuropen(VILLA *villa){
  VLMULCUR *mulcur;
  assert(villa);
  if(villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return NULL;
  }
  CB_MALLOC(mulcur, sizeof(VLMULCUR));
  mulcur->villa = villa;
  mulcur->curleaf = -1;
  mulcur->curknum = -1;
  mulcur->curvnum = -1;
  return mulcur;
}


/* Close a multiple cursor handle. */
void vlmulcurclose(VLMULCUR *mulcur){
  assert(mulcur);
  free(mulcur);
}


/* Move a multiple cursor to the first record. */
int vlmulcurfirst(VLMULCUR *mulcur){
  VLMULCUR swap;
  int rv;
  assert(mulcur);
  swap.curleaf = mulcur->villa->curleaf;
  swap.curknum = mulcur->villa->curknum;
  swap.curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = mulcur->curleaf;
  mulcur->villa->curknum = mulcur->curknum;
  mulcur->villa->curvnum = mulcur->curvnum;
  rv = vlcurfirst(mulcur->villa);
  mulcur->curleaf = mulcur->villa->curleaf;
  mulcur->curknum = mulcur->villa->curknum;
  mulcur->curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = swap.curleaf;
  mulcur->villa->curknum = swap.curknum;
  mulcur->villa->curvnum = swap.curvnum;
  return rv;
}


/* Move a multiple cursor to the last record. */
int vlmulcurlast(VLMULCUR *mulcur){
  VLMULCUR swap;
  int rv;
  assert(mulcur);
  swap.curleaf = mulcur->villa->curleaf;
  swap.curknum = mulcur->villa->curknum;
  swap.curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = mulcur->curleaf;
  mulcur->villa->curknum = mulcur->curknum;
  mulcur->villa->curvnum = mulcur->curvnum;
  rv = vlcurlast(mulcur->villa);
  mulcur->curleaf = mulcur->villa->curleaf;
  mulcur->curknum = mulcur->villa->curknum;
  mulcur->curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = swap.curleaf;
  mulcur->villa->curknum = swap.curknum;
  mulcur->villa->curvnum = swap.curvnum;
  return rv;
}


/* Move a multiple cursor to the previous record. */
int vlmulcurprev(VLMULCUR *mulcur){
  VLMULCUR swap;
  int rv;
  assert(mulcur);
  swap.curleaf = mulcur->villa->curleaf;
  swap.curknum = mulcur->villa->curknum;
  swap.curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = mulcur->curleaf;
  mulcur->villa->curknum = mulcur->curknum;
  mulcur->villa->curvnum = mulcur->curvnum;
  rv = vlcurprev(mulcur->villa);
  mulcur->curleaf = mulcur->villa->curleaf;
  mulcur->curknum = mulcur->villa->curknum;
  mulcur->curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = swap.curleaf;
  mulcur->villa->curknum = swap.curknum;
  mulcur->villa->curvnum = swap.curvnum;
  return rv;
}


/* Move a multiple cursor to the next record. */
int vlmulcurnext(VLMULCUR *mulcur){
  VLMULCUR swap;
  int rv;
  assert(mulcur);
  swap.curleaf = mulcur->villa->curleaf;
  swap.curknum = mulcur->villa->curknum;
  swap.curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = mulcur->curleaf;
  mulcur->villa->curknum = mulcur->curknum;
  mulcur->villa->curvnum = mulcur->curvnum;
  rv = vlcurnext(mulcur->villa);
  mulcur->curleaf = mulcur->villa->curleaf;
  mulcur->curknum = mulcur->villa->curknum;
  mulcur->curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = swap.curleaf;
  mulcur->villa->curknum = swap.curknum;
  mulcur->villa->curvnum = swap.curvnum;
  return rv;
}


/* Move a multiple cursor to a position around a record. */
int vlmulcurjump(VLMULCUR *mulcur, const char *kbuf, int ksiz, int jmode){
  VLMULCUR swap;
  int rv;
  assert(mulcur);
  swap.curleaf = mulcur->villa->curleaf;
  swap.curknum = mulcur->villa->curknum;
  swap.curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = mulcur->curleaf;
  mulcur->villa->curknum = mulcur->curknum;
  mulcur->villa->curvnum = mulcur->curvnum;
  rv = vlcurjump(mulcur->villa, kbuf, ksiz, jmode);
  mulcur->curleaf = mulcur->villa->curleaf;
  mulcur->curknum = mulcur->villa->curknum;
  mulcur->curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = swap.curleaf;
  mulcur->villa->curknum = swap.curknum;
  mulcur->villa->curvnum = swap.curvnum;
  return rv;
}


/* Get the key of the record where a multiple cursor is. */
char *vlmulcurkey(VLMULCUR *mulcur, int *sp){
  VLMULCUR swap;
  char *rv;
  assert(mulcur);
  swap.curleaf = mulcur->villa->curleaf;
  swap.curknum = mulcur->villa->curknum;
  swap.curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = mulcur->curleaf;
  mulcur->villa->curknum = mulcur->curknum;
  mulcur->villa->curvnum = mulcur->curvnum;
  rv = vlcurkey(mulcur->villa, sp);
  mulcur->curleaf = mulcur->villa->curleaf;
  mulcur->curknum = mulcur->villa->curknum;
  mulcur->curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = swap.curleaf;
  mulcur->villa->curknum = swap.curknum;
  mulcur->villa->curvnum = swap.curvnum;
  return rv;
}


/* Get the value of the record where a multiple cursor is. */
char *vlmulcurval(VLMULCUR *mulcur, int *sp){
  VLMULCUR swap;
  char *rv;
  assert(mulcur);
  swap.curleaf = mulcur->villa->curleaf;
  swap.curknum = mulcur->villa->curknum;
  swap.curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = mulcur->curleaf;
  mulcur->villa->curknum = mulcur->curknum;
  mulcur->villa->curvnum = mulcur->curvnum;
  rv = vlcurval(mulcur->villa, sp);
  mulcur->curleaf = mulcur->villa->curleaf;
  mulcur->curknum = mulcur->villa->curknum;
  mulcur->curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = swap.curleaf;
  mulcur->villa->curknum = swap.curknum;
  mulcur->villa->curvnum = swap.curvnum;
  return rv;
}


/* Refer to volatile cache of the key of the record where a multiple cursor is. */
const char *vlmulcurkeycache(VLMULCUR *mulcur, int *sp){
  VLMULCUR swap;
  const char *rv;
  assert(mulcur);
  swap.curleaf = mulcur->villa->curleaf;
  swap.curknum = mulcur->villa->curknum;
  swap.curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = mulcur->curleaf;
  mulcur->villa->curknum = mulcur->curknum;
  mulcur->villa->curvnum = mulcur->curvnum;
  rv = vlcurkeycache(mulcur->villa, sp);
  mulcur->curleaf = mulcur->villa->curleaf;
  mulcur->curknum = mulcur->villa->curknum;
  mulcur->curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = swap.curleaf;
  mulcur->villa->curknum = swap.curknum;
  mulcur->villa->curvnum = swap.curvnum;
  return rv;
}


/* Refer to volatile cache of the value of the record where a multiple cursor is. */
const char *vlmulcurvalcache(VLMULCUR *mulcur, int *sp){
  VLMULCUR swap;
  const char *rv;
  assert(mulcur);
  swap.curleaf = mulcur->villa->curleaf;
  swap.curknum = mulcur->villa->curknum;
  swap.curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = mulcur->curleaf;
  mulcur->villa->curknum = mulcur->curknum;
  mulcur->villa->curvnum = mulcur->curvnum;
  rv = vlcurvalcache(mulcur->villa, sp);
  mulcur->curleaf = mulcur->villa->curleaf;
  mulcur->curknum = mulcur->villa->curknum;
  mulcur->curvnum = mulcur->villa->curvnum;
  mulcur->villa->curleaf = swap.curleaf;
  mulcur->villa->curknum = swap.curknum;
  mulcur->villa->curvnum = swap.curvnum;
  return rv;
}



/*************************************************************************************************
 * private objects
 *************************************************************************************************/


/* Compare keys of two records by lexical order.
   `aptr' specifies the pointer to the region of one key.
   `asiz' specifies the size of the region of one key.
   `bptr' specifies the pointer to the region of the other key.
   `bsiz' specifies the size of the region of the other key.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
static int vllexcompare(const char *aptr, int asiz, const char *bptr, int bsiz){
  int i, min;
  assert(aptr && asiz >= 0 && bptr && bsiz >= 0);
  min = asiz < bsiz ? asiz : bsiz;
  for(i = 0; i < min; i++){
    if(((unsigned char *)aptr)[i] != ((unsigned char *)bptr)[i])
      return ((unsigned char *)aptr)[i] - ((unsigned char *)bptr)[i];
  }
  if(asiz == bsiz) return 0;
  return asiz - bsiz;
}


/* Compare keys of two records as native integers.
   `aptr' specifies the pointer to the region of one key.
   `asiz' specifies the size of the region of one key.
   `bptr' specifies the pointer to the region of the other key.
   `bsiz' specifies the size of the region of the other key.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
static int vlintcompare(const char *aptr, int asiz, const char *bptr, int bsiz){
  int anum, bnum;
  assert(aptr && asiz >= 0 && bptr && bsiz >= 0);
  if(asiz != bsiz) return asiz - bsiz;
  anum = (asiz == sizeof(int) ? *(int *)aptr : INT_MIN);
  bnum = (bsiz == sizeof(int) ? *(int *)bptr : INT_MIN);
  return anum - bnum;
}


/* Compare keys of two records as numbers of big endian.
   `aptr' specifies the pointer to the region of one key.
   `asiz' specifies the size of the region of one key.
   `bptr' specifies the pointer to the region of the other key.
   `bsiz' specifies the size of the region of the other key.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
static int vlnumcompare(const char *aptr, int asiz, const char *bptr, int bsiz){
  int i;
  assert(aptr && asiz >= 0 && bptr && bsiz >= 0);
  if(asiz != bsiz) return asiz - bsiz;
  for(i = 0; i < asiz; i++){
    if(aptr[i] != bptr[i]) return aptr[i] - bptr[i];
  }
  return 0;
}


/* Compare keys of two records as numeric strings of octal, decimal or hexadecimal.
   `aptr' specifies the pointer to the region of one key.
   `asiz' specifies the size of the region of one key.
   `bptr' specifies the pointer to the region of the other key.
   `bsiz' specifies the size of the region of the other key.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
static int vldeccompare(const char *aptr, int asiz, const char *bptr, int bsiz){
  assert(aptr && asiz >= 0 && bptr && bsiz >= 0);
  return (int)(strtod(aptr, NULL) - strtod(bptr, NULL));
}


/* Store a record composed of a pair of integers.
   `depot' specifies an internal database handle.
   `knum' specifies an integer of the key.
   `vnum' specifies an integer of the value.
   The return value is true if successful, else, it is false. */
static int vldpputnum(DEPOT *depot, int knum, int vnum){
  assert(depot);
  return dpput(depot, (char *)&knum, sizeof(int), (char *)&vnum, sizeof(int), DP_DOVER);
}


/* Retrieve a record composed of a pair of integers.
   `depot' specifies an internal database handle.
   `knum' specifies an integer of the key.
   `vip' specifies the pointer to a variable to assign the result to.
   The return value is true if successful, else, it is false. */
static int vldpgetnum(DEPOT *depot, int knum, int *vnp){
  char *vbuf;
  int vsiz;
  assert(depot && vnp);
  vbuf = dpget(depot, (char *)&knum, sizeof(int), 0, -1, &vsiz);
  if(!vbuf || vsiz != sizeof(int)){
    free(vbuf);
    return FALSE;
  }
  *vnp = *(int *)vbuf;
  free(vbuf);
  return TRUE;
}


/* Create a new leaf.
   `villa' specifies a database handle.
   `prev' specifies the ID number of the previous leaf.
   `next' specifies the ID number of the previous leaf.
   The return value is a handle of the leaf. */
static VLLEAF *vlleafnew(VILLA *villa, int prev, int next){
  VLLEAF lent;
  assert(villa);
  lent.id = villa->lnum + VL_LEAFIDMIN;
  lent.dirty = TRUE;
  CB_LISTOPEN(lent.recs);
  lent.prev = prev;
  lent.next = next;
  villa->lnum++;
  cbmapput(villa->leafc, (char *)&(lent.id), sizeof(int), (char *)&lent, sizeof(VLLEAF), TRUE);
  return (VLLEAF *)cbmapget(villa->leafc, (char *)&(lent.id), sizeof(int), NULL);
}


/* Remove a leaf from the cache.
   `villa' specifies a database handle.
   `id' specifies the ID number of the leaf.
   The return value is true if successful, else, it is false. */
static int vlleafcacheout(VILLA *villa, int id){
  VLLEAF *leaf;
  VLREC *recp;
  CBLIST *recs;
  int i, err, ln;
  assert(villa && id >= VL_LEAFIDMIN);
  if(!(leaf = (VLLEAF *)cbmapget(villa->leafc, (char *)&id, sizeof(int), NULL))) return FALSE;
  err = FALSE;
  if(leaf->dirty && !vlleafsave(villa, leaf)) err = TRUE;
  recs = leaf->recs;
  ln = CB_LISTNUM(recs);
  for(i = 0; i < ln; i++){
    recp = (VLREC *)CB_LISTVAL(recs, i);
    CB_DATUMCLOSE(recp->key);
    CB_DATUMCLOSE(recp->first);
    if(recp->rest) CB_LISTCLOSE(recp->rest);
  }
  CB_LISTCLOSE(recs);
  cbmapout(villa->leafc, (char *)&id, sizeof(int));
  return err ? FALSE : TRUE;
}


/* Save a leaf into the database.
   `villa' specifies a database handle.
   `leaf' specifies a leaf handle.
   The return value is true if successful, else, it is false. */
static int vlleafsave(VILLA *villa, VLLEAF *leaf){
  VLREC *recp;
  CBLIST *recs;
  CBDATUM *buf;
  char vnumbuf[VL_VNUMBUFSIZ], *zbuf;
  const char *vbuf;
  int i, j, ksiz, vnum, vsiz, prev, next, vnumsiz, ln, zsiz;
  assert(villa && leaf);
  CB_DATUMOPEN(buf);
  prev = leaf->prev;
  if(prev == -1) prev = VL_NODEIDMIN - 1;
  VL_SETVNUMBUF(vnumsiz, vnumbuf, prev);
  CB_DATUMCAT(buf, vnumbuf, vnumsiz);
  next = leaf->next;
  if(next == -1) next = VL_NODEIDMIN - 1;
  VL_SETVNUMBUF(vnumsiz, vnumbuf, next);
  CB_DATUMCAT(buf, vnumbuf, vnumsiz);
  recs = leaf->recs;
  ln = CB_LISTNUM(recs);
  for(i = 0; i < ln; i++){
    recp = (VLREC *)CB_LISTVAL(recs, i);
    ksiz = CB_DATUMSIZE(recp->key);
    VL_SETVNUMBUF(vnumsiz, vnumbuf, ksiz);
    CB_DATUMCAT(buf, vnumbuf, vnumsiz);
    CB_DATUMCAT(buf, CB_DATUMPTR(recp->key), ksiz);
    vnum = 1 + (recp->rest ? CB_LISTNUM(recp->rest) : 0);
    VL_SETVNUMBUF(vnumsiz, vnumbuf, vnum);
    CB_DATUMCAT(buf, vnumbuf, vnumsiz);
    vsiz = CB_DATUMSIZE(recp->first);
    VL_SETVNUMBUF(vnumsiz, vnumbuf, vsiz);
    CB_DATUMCAT(buf, vnumbuf, vnumsiz);
    CB_DATUMCAT(buf, CB_DATUMPTR(recp->first), vsiz);
    if(recp->rest){
      for(j = 0; j < CB_LISTNUM(recp->rest); j++){
        vbuf = CB_LISTVAL2(recp->rest, j, vsiz);
        VL_SETVNUMBUF(vnumsiz, vnumbuf, vsiz);
        CB_DATUMCAT(buf, vnumbuf, vnumsiz);
        CB_DATUMCAT(buf, vbuf, vsiz);
      }
    }
  }
  if(_qdbm_deflate && villa->cmode == VL_OZCOMP){
    if(!(zbuf = _qdbm_deflate(CB_DATUMPTR(buf), CB_DATUMSIZE(buf), &zsiz, _QDBM_ZMRAW))){
      CB_DATUMCLOSE(buf);
      dpecodeset(DP_EMISC, __FILE__, __LINE__);
      return FALSE;
    }
    if(!dpput(villa->depot, (char *)&(leaf->id), sizeof(int), zbuf, zsiz, DP_DOVER)){
      CB_DATUMCLOSE(buf);
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      return FALSE;
    }
    free(zbuf);
  } else if(_qdbm_lzoencode && villa->cmode == VL_OYCOMP){
    if(!(zbuf = _qdbm_lzoencode(CB_DATUMPTR(buf), CB_DATUMSIZE(buf), &zsiz))){
      CB_DATUMCLOSE(buf);
      dpecodeset(DP_EMISC, __FILE__, __LINE__);
      return FALSE;
    }
    if(!dpput(villa->depot, (char *)&(leaf->id), sizeof(int), zbuf, zsiz, DP_DOVER)){
      CB_DATUMCLOSE(buf);
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      return FALSE;
    }
    free(zbuf);
  } else if(_qdbm_bzencode && villa->cmode == VL_OXCOMP){
    if(!(zbuf = _qdbm_bzencode(CB_DATUMPTR(buf), CB_DATUMSIZE(buf), &zsiz))){
      CB_DATUMCLOSE(buf);
      dpecodeset(DP_EMISC, __FILE__, __LINE__);
      return FALSE;
    }
    if(!dpput(villa->depot, (char *)&(leaf->id), sizeof(int), zbuf, zsiz, DP_DOVER)){
      CB_DATUMCLOSE(buf);
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      return FALSE;
    }
    free(zbuf);
  } else {
    if(!dpput(villa->depot, (char *)&(leaf->id), sizeof(int),
              CB_DATUMPTR(buf), CB_DATUMSIZE(buf), DP_DOVER)){
      CB_DATUMCLOSE(buf);
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      return FALSE;
    }
  }
  CB_DATUMCLOSE(buf);
  leaf->dirty = FALSE;
  return TRUE;
}


/* Load a leaf from the database.
   `villa' specifies a database handle.
   `id' specifies the ID number of the leaf.
   If successful, the return value is the pointer to the leaf, else, it is `NULL'. */
static VLLEAF *vlleafload(VILLA *villa, int id){
  char wbuf[VL_PAGEBUFSIZ], *buf, *rp, *kbuf, *vbuf, *zbuf;
  int i, size, step, ksiz, vnum, vsiz, prev, next, zsiz;
  VLLEAF *leaf, lent;
  VLREC rec;
  assert(villa && id >= VL_LEAFIDMIN);
  if((leaf = (VLLEAF *)cbmapget(villa->leafc, (char *)&id, sizeof(int), NULL)) != NULL){
    cbmapmove(villa->leafc, (char *)&id, sizeof(int), FALSE);
    return leaf;
  }
  ksiz = -1;
  prev = -1;
  next = -1;
  if((size = dpgetwb(villa->depot, (char *)&id, sizeof(int), 0, VL_PAGEBUFSIZ, wbuf)) > 0 &&
     size < VL_PAGEBUFSIZ){
    buf = NULL;
  } else if(!(buf = dpget(villa->depot, (char *)&id, sizeof(int), 0, -1, &size))){
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return NULL;
  }
  if(_qdbm_inflate && villa->cmode == VL_OZCOMP){
    if(!(zbuf = _qdbm_inflate(buf ? buf : wbuf, size, &zsiz, _QDBM_ZMRAW))){
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      free(buf);
      return NULL;
    }
    free(buf);
    buf = zbuf;
    size = zsiz;
  } else if(_qdbm_lzodecode && villa->cmode == VL_OYCOMP){
    if(!(zbuf = _qdbm_lzodecode(buf ? buf : wbuf, size, &zsiz))){
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      free(buf);
      return NULL;
    }
    free(buf);
    buf = zbuf;
    size = zsiz;
  } else if(_qdbm_bzdecode && villa->cmode == VL_OXCOMP){
    if(!(zbuf = _qdbm_bzdecode(buf ? buf : wbuf, size, &zsiz))){
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      free(buf);
      return NULL;
    }
    free(buf);
    buf = zbuf;
    size = zsiz;
  }
  rp = buf ? buf : wbuf;
  if(size >= 1){
    VL_READVNUMBUF(rp, size, prev, step);
    rp += step;
    size -= step;
    if(prev >= VL_NODEIDMIN - 1) prev = -1;
  }
  if(size >= 1){
    VL_READVNUMBUF(rp, size, next, step);
    rp += step;
    size -= step;
    if(next >= VL_NODEIDMIN - 1) next = -1;
  }
  lent.id = id;
  lent.dirty = FALSE;
  CB_LISTOPEN(lent.recs);
  lent.prev = prev;
  lent.next = next;
  while(size >= 1){
    VL_READVNUMBUF(rp, size, ksiz, step);
    rp += step;
    size -= step;
    if(size < ksiz) break;
    kbuf = rp;
    rp += ksiz;
    size -= ksiz;
    VL_READVNUMBUF(rp, size, vnum, step);
    rp += step;
    size -= step;
    if(vnum < 1 || size < 1) break;
    for(i = 0; i < vnum && size >= 1; i++){
      VL_READVNUMBUF(rp, size, vsiz, step);
      rp += step;
      size -= step;
      if(size < vsiz) break;
      vbuf = rp;
      rp += vsiz;
      size -= vsiz;
      if(i < 1){
        CB_DATUMOPEN2(rec.key, kbuf, ksiz);
        CB_DATUMOPEN2(rec.first, vbuf, vsiz);
        rec.rest = NULL;
      } else {
        if(!rec.rest) CB_LISTOPEN(rec.rest);
        CB_LISTPUSH(rec.rest, vbuf, vsiz);
      }
    }
    if(i > 0) CB_LISTPUSH(lent.recs, (char *)&rec, sizeof(VLREC));
  }
  free(buf);
  cbmapput(villa->leafc, (char *)&(lent.id), sizeof(int), (char *)&lent, sizeof(VLLEAF), TRUE);
  return (VLLEAF *)cbmapget(villa->leafc, (char *)&(lent.id), sizeof(int), NULL);
}


/* Load the historical leaf from the database.
   `villa' specifies a database handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.
   If successful, the return value is the pointer to the leaf, else, it is `NULL'. */
static VLLEAF *vlgethistleaf(VILLA *villa, const char *kbuf, int ksiz){
  VLLEAF *leaf;
  VLREC *recp;
  int ln, rv;
  assert(villa && kbuf && ksiz >= 0);
  if(!(leaf = vlleafload(villa, villa->hleaf))) return NULL;
  if((ln = CB_LISTNUM(leaf->recs)) < 2) return NULL;
  recp = (VLREC *)CB_LISTVAL(leaf->recs, 0);
  rv = villa->cmp(kbuf, ksiz, CB_DATUMPTR(recp->key), CB_DATUMSIZE(recp->key));
  if(rv == 0) return leaf;
  if(rv < 0) return NULL;
  recp = (VLREC *)CB_LISTVAL(leaf->recs, ln - 1);
  rv = villa->cmp(kbuf, ksiz, CB_DATUMPTR(recp->key), CB_DATUMSIZE(recp->key));
  if(rv <= 0 || leaf->next < VL_LEAFIDMIN) return leaf;
  return NULL;
}


/* Add a record to a leaf.
   `villa' specifies a database handle.
   `leaf' specifies a leaf handle.
   `dmode' specifies behavior when the key overlaps.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.
   `vbuf' specifies the pointer to the region of a value.
   `vsiz' specifies the size of the region of the value.
   The return value is true if successful, else, it is false. */
static int vlleafaddrec(VILLA *villa, VLLEAF *leaf, int dmode,
                        const char *kbuf, int ksiz, const char *vbuf, int vsiz){
  VLREC *recp, rec;
  CBLIST *recs;
  int i, rv, left, right, ln, tsiz;
  char *tbuf;
  assert(villa && leaf && kbuf && ksiz >= 0 && vbuf && vsiz >= 0);
  left = 0;
  recs = leaf->recs;
  ln = CB_LISTNUM(recs);
  right = ln;
  i = (left + right) / 2;
  while(right >= left && i < ln){
    recp = (VLREC *)CB_LISTVAL(recs, i);
    rv = villa->cmp(kbuf, ksiz, CB_DATUMPTR(recp->key), CB_DATUMSIZE(recp->key));
    if(rv == 0){
      break;
    } else if(rv <= 0){
      right = i - 1;
    } else {
      left = i + 1;
    }
    i = (left + right) / 2;
  }
  while(i < ln){
    recp = (VLREC *)CB_LISTVAL(recs, i);
    rv = villa->cmp(kbuf, ksiz, CB_DATUMPTR(recp->key), CB_DATUMSIZE(recp->key));
    if(rv == 0){
      switch(dmode){
      case VL_DKEEP:
        return FALSE;
      case VL_DCAT:
        CB_DATUMCAT(recp->first, vbuf, vsiz);
        break;
      case VL_DDUP:
        if(!recp->rest) CB_LISTOPEN(recp->rest);
        CB_LISTPUSH(recp->rest, vbuf, vsiz);
        villa->rnum++;
        break;
      case VL_DDUPR:
        if(!recp->rest){
          CB_DATUMTOMALLOC(recp->first, tbuf, tsiz);
          CB_DATUMOPEN2(recp->first, vbuf, vsiz);
          CB_LISTOPEN(recp->rest);
          CB_LISTPUSHBUF(recp->rest, tbuf, tsiz);
        } else {
          cblistunshift(recp->rest, CB_DATUMPTR(recp->first), CB_DATUMSIZE(recp->first));
          CB_DATUMSETSIZE(recp->first, 0);
          CB_DATUMCAT(recp->first, vbuf, vsiz);
        }
        villa->rnum++;
        break;
      default:
        CB_DATUMSETSIZE(recp->first, 0);
        CB_DATUMCAT(recp->first, vbuf, vsiz);
        break;
      }
      break;
    } else if(rv < 0){
      CB_DATUMOPEN2(rec.key, kbuf, ksiz);
      CB_DATUMOPEN2(rec.first, vbuf, vsiz);
      rec.rest = NULL;
      CB_LISTINSERT(recs, i, (char *)&rec, sizeof(VLREC));
      villa->rnum++;
      break;
    }
    i++;
  }
  if(i >= ln){
    CB_DATUMOPEN2(rec.key, kbuf, ksiz);
    CB_DATUMOPEN2(rec.first, vbuf, vsiz);
    rec.rest = NULL;
    CB_LISTPUSH(recs, (char *)&rec, sizeof(VLREC));
    villa->rnum++;
  }
  leaf->dirty = TRUE;
  return TRUE;
}


/* Calculate the size of data of a leaf.
   `leaf' specifies a leaf handle.
   The return value is size of data of the leaf. */
static int vlleafdatasize(VLLEAF *leaf){
  VLREC *recp;
  CBLIST *recs, *rest;
  const char *vbuf;
  int i, j, sum, rnum, restnum, vsiz;
  assert(leaf);
  sum = 0;
  recs = leaf->recs;
  rnum = CB_LISTNUM(recs);
  for(i = 0; i < rnum; i++){
    recp = (VLREC *)CB_LISTVAL(recs, i);
    sum += CB_DATUMSIZE(recp->key);
    sum += CB_DATUMSIZE(recp->first);
    if(recp->rest){
      rest = recp->rest;
      restnum = CB_LISTNUM(rest);
      for(j = 0; j < restnum; j++){
        vbuf = CB_LISTVAL2(rest, j, vsiz);
        sum += vsiz;
      }
    }
  }
  return sum;
}


/* Divide a leaf into two.
   `villa' specifies a database handle.
   `leaf' specifies a leaf handle.
   The return value is the handle of a new leaf, or `NULL' on failure. */
static VLLEAF *vlleafdivide(VILLA *villa, VLLEAF *leaf){
  VLLEAF *newleaf, *nextleaf;
  VLREC *recp;
  CBLIST *recs, *newrecs;
  int i, mid, ln;
  assert(villa && leaf);
  villa->hleaf = -1;
  recs = leaf->recs;
  mid = CB_LISTNUM(recs) / 2;
  recp = (VLREC *)CB_LISTVAL(recs, mid);
  newleaf = vlleafnew(villa, leaf->id, leaf->next);
  if(newleaf->next != -1){
    if(!(nextleaf = vlleafload(villa, newleaf->next))) return NULL;
    nextleaf->prev = newleaf->id;
    nextleaf->dirty = TRUE;
  }
  leaf->next = newleaf->id;
  leaf->dirty = TRUE;
  ln = CB_LISTNUM(recs);
  newrecs = newleaf->recs;
  for(i = mid; i < ln; i++){
    recp = (VLREC *)CB_LISTVAL(recs, i);
    CB_LISTPUSH(newrecs, (char *)recp, sizeof(VLREC));
  }
  ln = CB_LISTNUM(newrecs);
  for(i = 0; i < ln; i++){
    CB_LISTDROP(recs);
  }
  return newleaf;
}


/* Create a new node.
   `villa' specifies a database handle.
   `heir' specifies the ID of the child before the first index.
   The return value is a handle of the node. */
static VLNODE *vlnodenew(VILLA *villa, int heir){
  VLNODE nent;
  assert(villa && heir >= VL_LEAFIDMIN);
  nent.id = villa->nnum + VL_NODEIDMIN;
  nent.dirty = TRUE;
  nent.heir = heir;
  CB_LISTOPEN(nent.idxs);
  villa->nnum++;
  cbmapput(villa->nodec, (char *)&(nent.id), sizeof(int), (char *)&nent, sizeof(VLNODE), TRUE);
  return (VLNODE *)cbmapget(villa->nodec, (char *)&(nent.id), sizeof(int), NULL);
}


/* Remove a node from the cache.
   `villa' specifies a database handle.
   `id' specifies the ID number of the node.
   The return value is true if successful, else, it is false. */
static int vlnodecacheout(VILLA *villa, int id){
  VLNODE *node;
  VLIDX *idxp;
  int i, err, ln;
  assert(villa && id >= VL_NODEIDMIN);
  if(!(node = (VLNODE *)cbmapget(villa->nodec, (char *)&id, sizeof(int), NULL))) return FALSE;
  err = FALSE;
  if(node->dirty && !vlnodesave(villa, node)) err = TRUE;
  ln = CB_LISTNUM(node->idxs);
  for(i = 0; i < ln; i++){
    idxp = (VLIDX *)CB_LISTVAL(node->idxs, i);
    CB_DATUMCLOSE(idxp->key);
  }
  CB_LISTCLOSE(node->idxs);
  cbmapout(villa->nodec, (char *)&id, sizeof(int));
  return err ? FALSE : TRUE;
}


/* Save a node into the database.
   `villa' specifies a database handle.
   `node' specifies a node handle.
   The return value is true if successful, else, it is false. */
static int vlnodesave(VILLA *villa, VLNODE *node){
  CBDATUM *buf;
  char vnumbuf[VL_VNUMBUFSIZ];
  VLIDX *idxp;
  int i, heir, pid, ksiz, vnumsiz, ln;
  assert(villa && node);
  CB_DATUMOPEN(buf);
  heir = node->heir;
  VL_SETVNUMBUF(vnumsiz, vnumbuf, heir);
  CB_DATUMCAT(buf, vnumbuf, vnumsiz);
  ln = CB_LISTNUM(node->idxs);
  for(i = 0; i < ln; i++){
    idxp = (VLIDX *)CB_LISTVAL(node->idxs, i);
    pid = idxp->pid;
    VL_SETVNUMBUF(vnumsiz, vnumbuf, pid);
    CB_DATUMCAT(buf, vnumbuf, vnumsiz);
    ksiz = CB_DATUMSIZE(idxp->key);
    VL_SETVNUMBUF(vnumsiz, vnumbuf, ksiz);
    CB_DATUMCAT(buf, vnumbuf, vnumsiz);
    CB_DATUMCAT(buf, CB_DATUMPTR(idxp->key), ksiz);
  }
  if(!dpput(villa->depot, (char *)&(node->id), sizeof(int),
            CB_DATUMPTR(buf), CB_DATUMSIZE(buf), DP_DOVER)){
    CB_DATUMCLOSE(buf);
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return FALSE;
  }
  CB_DATUMCLOSE(buf);
  node->dirty = FALSE;
  return TRUE;
}


/* Load a node from the database.
   `villa' specifies a database handle.
   `id' specifies the ID number of the node.
   If successful, the return value is the pointer to the node, else, it is `NULL'. */
static VLNODE *vlnodeload(VILLA *villa, int id){
  char wbuf[VL_PAGEBUFSIZ], *buf, *rp, *kbuf;
  int size, step, heir, pid, ksiz;
  VLNODE *node, nent;
  VLIDX idx;
  assert(villa && id >= VL_NODEIDMIN);
  if((node = (VLNODE *)cbmapget(villa->nodec, (char *)&id, sizeof(int), NULL)) != NULL){
    cbmapmove(villa->nodec, (char *)&id, sizeof(int), FALSE);
    return node;
  }
  heir = -1;
  if((size = dpgetwb(villa->depot, (char *)&id, sizeof(int), 0, VL_PAGEBUFSIZ, wbuf)) > 0 &&
     size < VL_PAGEBUFSIZ){
    buf = NULL;
  } else if(!(buf = dpget(villa->depot, (char *)&id, sizeof(int), 0, -1, &size))){
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return NULL;
  }
  rp = buf ? buf : wbuf;
  if(size >= 1){
    VL_READVNUMBUF(rp, size, heir, step);
    rp += step;
    size -= step;
  }
  if(heir < 0){
    free(buf);
    return NULL;
  }
  nent.id = id;
  nent.dirty = FALSE;
  nent.heir = heir;
  CB_LISTOPEN(nent.idxs);
  while(size >= 1){
    VL_READVNUMBUF(rp, size, pid, step);
    rp += step;
    size -= step;
    if(size < 1) break;
    VL_READVNUMBUF(rp, size, ksiz, step);
    rp += step;
    size -= step;
    if(size < ksiz) break;
    kbuf = rp;
    rp += ksiz;
    size -= ksiz;
    idx.pid = pid;
    CB_DATUMOPEN2(idx.key, kbuf, ksiz);
    CB_LISTPUSH(nent.idxs, (char *)&idx, sizeof(VLIDX));
  }
  free(buf);
  cbmapput(villa->nodec, (char *)&(nent.id), sizeof(int), (char *)&nent, sizeof(VLNODE), TRUE);
  return (VLNODE *)cbmapget(villa->nodec, (char *)&(nent.id), sizeof(int), NULL);
}


/* Add an index to a node.
   `villa' specifies a database handle.
   `node' specifies a node handle.
   `order' specifies whether the calling sequence is orderd or not.
   `pid' specifies the ID number of referred page.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key. */
static void vlnodeaddidx(VILLA *villa, VLNODE *node, int order,
                         int pid, const char *kbuf, int ksiz){
  VLIDX idx, *idxp;
  int i, rv, left, right, ln;
  assert(villa && node && pid >= VL_LEAFIDMIN && kbuf && ksiz >= 0);
  idx.pid = pid;
  CB_DATUMOPEN2(idx.key, kbuf, ksiz);
  if(order){
    CB_LISTPUSH(node->idxs, (char *)&idx, sizeof(VLIDX));
  } else {
    left = 0;
    right = CB_LISTNUM(node->idxs);
    i = (left + right) / 2;
    ln = CB_LISTNUM(node->idxs);
    while(right >= left && i < ln){
      idxp = (VLIDX *)CB_LISTVAL(node->idxs, i);
      rv = villa->cmp(kbuf, ksiz, CB_DATUMPTR(idxp->key), CB_DATUMSIZE(idxp->key));
      if(rv == 0){
        break;
      } else if(rv <= 0){
        right = i - 1;
      } else {
        left = i + 1;
      }
      i = (left + right) / 2;
    }
    ln = CB_LISTNUM(node->idxs);
    while(i < ln){
      idxp = (VLIDX *)CB_LISTVAL(node->idxs, i);
      if(villa->cmp(kbuf, ksiz, CB_DATUMPTR(idxp->key), CB_DATUMSIZE(idxp->key)) < 0){
        CB_LISTINSERT(node->idxs, i, (char *)&idx, sizeof(VLIDX));
        break;
      }
      i++;
    }
    if(i >= CB_LISTNUM(node->idxs)) CB_LISTPUSH(node->idxs, (char *)&idx, sizeof(VLIDX));
  }
  node->dirty = TRUE;
}


/* Search the leaf corresponding to a key.
   `villa' specifies a database handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.
   The return value is the ID number of the leaf, or -1 on failure. */
static int vlsearchleaf(VILLA *villa, const char *kbuf, int ksiz){
  VLNODE *node;
  VLIDX *idxp;
  int i, pid, rv, left, right, ln;
  assert(villa && kbuf && ksiz >= 0);
  pid = villa->root;
  idxp = NULL;
  villa->hnum = 0;
  villa->hleaf = -1;
  while(pid >= VL_NODEIDMIN){
    if(!(node = vlnodeload(villa, pid)) || (ln = CB_LISTNUM(node->idxs)) < 1){
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      return -1;
    }
    villa->hist[villa->hnum++] = node->id;
    left = 1;
    right = ln;
    i = (left + right) / 2;
    while(right >= left && i < ln){
      idxp = (VLIDX *)CB_LISTVAL(node->idxs, i);
      rv = villa->cmp(kbuf, ksiz, CB_DATUMPTR(idxp->key), CB_DATUMSIZE(idxp->key));
      if(rv == 0){
        break;
      } else if(rv <= 0){
        right = i - 1;
      } else {
        left = i + 1;
      }
      i = (left + right) / 2;
    }
    if(i > 0) i--;
    while(i < ln){
      idxp = (VLIDX *)CB_LISTVAL(node->idxs, i);
      if(villa->cmp(kbuf, ksiz, CB_DATUMPTR(idxp->key), CB_DATUMSIZE(idxp->key)) < 0){
        if(i == 0){
          pid = node->heir;
          break;
        }
        idxp = (VLIDX *)CB_LISTVAL(node->idxs, i - 1);
        pid = idxp->pid;
        break;
      }
      i++;
    }
    if(i >= ln) pid = idxp->pid;
  }
  if(villa->lleaf == pid) villa->hleaf = pid;
  villa->lleaf = pid;
  return pid;
}


/* Adjust the caches for leaves and nodes.
   `villa' specifies a database handle.
   The return value is true if successful, else, it is false. */
static int vlcacheadjust(VILLA *villa){
  const char *tmp;
  int i, pid, err;
  err = FALSE;
  if(cbmaprnum(villa->leafc) > villa->leafcnum){
    cbmapiterinit(villa->leafc);
    for(i = 0; i < VL_CACHEOUT; i++){
      tmp = cbmapiternext(villa->leafc, NULL);
      pid = *(int *)tmp;
      if(!vlleafcacheout(villa, pid)) err = TRUE;
    }
  }
  if(cbmaprnum(villa->nodec) > villa->nodecnum){
    cbmapiterinit(villa->nodec);
    for(i = 0; i < VL_CACHEOUT; i++){
      tmp = cbmapiternext(villa->nodec, NULL);
      pid = *(int *)tmp;
      if(!vlnodecacheout(villa, pid)) err = TRUE;
    }
  }
  return err ? FALSE : TRUE;
}


/* Search a record of a leaf.
   `villa' specifies a database handle.
   `leaf' specifies a leaf handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.
   `ip' specifies the pointer to a variable to fetch the index of the correspnding record.
   The return value is the pointer to a corresponding record, or `NULL' on failure. */
static VLREC *vlrecsearch(VILLA *villa, VLLEAF *leaf, const char *kbuf, int ksiz, int *ip){
  VLREC *recp;
  CBLIST *recs;
  int i, rv, left, right, ln;
  assert(villa && leaf && kbuf && ksiz >= 0);
  recs = leaf->recs;
  ln = CB_LISTNUM(recs);
  left = 0;
  right = ln;
  i = (left + right) / 2;
  while(right >= left && i < ln){
    recp = (VLREC *)CB_LISTVAL(recs, i);
    rv = villa->cmp(kbuf, ksiz, CB_DATUMPTR(recp->key), CB_DATUMSIZE(recp->key));
    if(rv == 0){
      if(ip) *ip = i;
      return recp;
    } else if(rv <= 0){
      right = i - 1;
    } else {
      left = i + 1;
    }
    i = (left + right) / 2;
  }
  if(ip) *ip = i;
  return NULL;
}


/* Get flags of a database. */
int vlgetflags(VILLA *villa){
  assert(villa);
  return dpgetflags(villa->depot);
}


/* Set flags of a database.
   `villa' specifies a database handle connected as a writer.
   `flags' specifies flags to set.  Lesser ten bits are reserved for internal use.
   If successful, the return value is true, else, it is false. */
int vlsetflags(VILLA *villa, int flags){
  assert(villa);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  return dpsetflags(villa->depot, flags);
}



/* END OF FILE */
