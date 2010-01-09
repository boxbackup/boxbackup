/*************************************************************************************************
 * Implementation of Odeum
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

#include "odeum.h"
#include "myconf.h"

#define OD_NAMEMAX     256               /* max size of a database name */
#define OD_DIRMODE     00755             /* permission of a creating directory */
#define OD_PATHBUFSIZ  1024              /* size of a path buffer */
#define OD_NUMBUFSIZ   32                /* size of a buffer for a number */
#define OD_MAPPBNUM    127               /* bucket size of a petit map handle */
#define OD_DOCSNAME    "docs"            /* name of the database for documents */
#define OD_INDEXNAME   "index"           /* name of the database for inverted index */
#define OD_RDOCSNAME   "rdocs"           /* name of the database for reverse dictionary */
#define OD_DOCSBNUM    2039              /* initial bucket number of document database */
#define OD_DOCSDNUM    17                /* division number of document database */
#define OD_DOCSALIGN   -4                /* alignment of document database */
#define OD_DOCSFBP     32                /* size of free block pool of document database */
#define OD_INDEXBNUM   32749             /* initial bucket number of inverted index */
#define OD_INDEXDNUM   7                 /* division number of inverted index */
#define OD_INDEXALIGN  -2                /* alignment of inverted index */
#define OD_INDEXFBP    32                /* size of free block pool of inverted index */
#define OD_RDOCSLRM    81                /* records in a leaf node of reverse dictionary */
#define OD_RDOCSNIM    192               /* records in a non-leaf node of reverse dictionary */
#define OD_RDOCSLCN    128               /* number of leaf cache of reverse dictionary */
#define OD_RDOCSNCN    32                /* number of non-leaf cache of reverse dictionary */
#define OD_CACHEBNUM   262139            /* number of buckets for dirty buffers */
#define OD_CACHESIZ    8388608           /* max bytes to use memory for dirty buffers */
#define OD_CFLIVERAT   0.8               /* ratio of usable cache region */
#define OD_CFBEGSIZ    2048              /* beginning size of flushing frequent words */
#define OD_CFENDSIZ    64                /* lower limit of flushing frequent words */
#define OD_CFRFRAT     0.2               /* ratio of flushing rare words a time */
#define OD_OTCBBUFSIZ  1024              /* size of a buffer for call back functions */
#define OD_OTPERWORDS  10000             /* frequency of call back in merging index */
#define OD_OTPERDOCS   1000              /* frequency of call back in merging docs */
#define OD_MDBRATIO    2.5               /* ratio of bucket number and document number */
#define OD_MIBRATIO    1.5               /* ratio of bucket number and word number */
#define OD_MIARATIO    0.75              /* ratio of alignment to the first words */
#define OD_MIWUNIT     32                /* writing unit of merging inverted index */
#define OD_DMAXEXPR    "dmax"            /* key of max number of the document ID */
#define OD_DNUMEXPR    "dnum"            /* key of number of the documents */
#define OD_URIEXPR     "1"               /* map key of URI */
#define OD_ATTRSEXPR   "2"               /* map key of attributes */
#define OD_NWORDSEXPR  "3"               /* map key of normal words */
#define OD_AWORDSEXPR  "4"               /* map key of as-is words */
#define OD_WTOPRATE    0.1               /* ratio of top words */
#define OD_WTOPBONUS   5000              /* bonus points of top words */
#define OD_KEYCRATIO   1.75              /* ratio of number to max of keyword candidates */
#define OD_WOCCRPOINT  10000             /* points per occurence */
#define OD_SPACECHARS  "\t\n\v\f\r "     /* space characters */
#define OD_DELIMCHARS  "!\"#$%&'()*/<=>?[\\]^`{|}~"  /* delimiter characters */
#define OD_GLUECHARS   "+,-.:;@"         /* glueing characters */
#define OD_MAXWORDLEN  48                /* max length of a word */

typedef struct {                         /* type of structure for word counting */
  const char *word;                      /* pointer to the word */
  int num;                               /* frequency of the word */
} ODWORD;

enum {                                   /* enumeration for events binded to each character */
  OD_EVWORD,                             /* word */
  OD_EVSPACE,                            /* space */
  OD_EVDELIM,                            /* delimiter */
  OD_EVGLUE                              /* glue */
};


/* private global variables */
int odindexbnum = OD_INDEXBNUM;
int odindexdnum = OD_INDEXDNUM;
int odcachebnum = OD_CACHEBNUM;
int odcachesiz = OD_CACHESIZ;
void (*odotcb)(const char *, ODEUM *, const char *) = NULL;


/* private function prototypes */
static ODEUM *odopendb(const char *name, int omode, int docsbnum, int indexbnum,
                       const char *fname);
static int odcacheflush(ODEUM *odeum, const char *fname);
static int odcacheflushfreq(ODEUM *odeum, const char *fname, int min);
static int odcacheflushrare(ODEUM *odeum, const char *fname, double ratio);
static int odsortindex(ODEUM *odeum, const char *fname);
static int odsortcompare(const void *a, const void *b);
static int odpurgeindex(ODEUM *odeum, const char *fname);
static CBMAP *odpairsmap(const ODPAIR *pairs, int num);
static int odwordcompare(const void *a, const void *b);
static int odmatchoperator(ODEUM *odeum, CBLIST *tokens);
static ODPAIR *odparsesubexpr(ODEUM *odeum, CBLIST *tokens, CBLIST *nwords, int *np,
                              CBLIST *errors);
static ODPAIR *odparseexpr(ODEUM *odeum, CBLIST *tokens, CBLIST *nwords, int *np,
                           CBLIST *errors);
static void odfixtokens(ODEUM *odeum, CBLIST *tokens);
static void odcleannormalized(ODEUM *odeum, CBLIST *nwords);



/*************************************************************************************************
 * public objects
 *************************************************************************************************/


/* Get a database handle. */
ODEUM *odopen(const char *name, int omode){
  assert(name);
  return odopendb(name, omode, OD_DOCSBNUM, odindexbnum, "odopen");
}


/* Close a database handle. */
int odclose(ODEUM *odeum){
  char numbuf[OD_NUMBUFSIZ];
  int err;
  assert(odeum);
  err = FALSE;
  if(odotcb) odotcb("odclose", odeum, "closing the connection");
  if(odeum->wmode){
    if(odotcb) odotcb("odclose", odeum, "writing meta information");
    sprintf(numbuf, "%d", odeum->dmax);
    if(!vlput(odeum->rdocsdb, OD_DMAXEXPR, sizeof(OD_DMAXEXPR), numbuf, -1, VL_DOVER)) err = TRUE;
    sprintf(numbuf, "%d", odeum->dnum);
    if(!vlput(odeum->rdocsdb, OD_DNUMEXPR, sizeof(OD_DNUMEXPR), numbuf, -1, VL_DOVER)) err = TRUE;
    if(!odcacheflushfreq(odeum, "odclose", OD_CFENDSIZ)) err = TRUE;
    if(!odcacheflushrare(odeum, "odclose", OD_CFRFRAT)) err = TRUE;
    if(!odcacheflush(odeum, "odclose")) err = TRUE;
    if(!odsortindex(odeum, "odclose")) err = TRUE;
    cbmapclose(odeum->cachemap);
    cbmapclose(odeum->sortmap);
  }
  if(!vlclose(odeum->rdocsdb)) err = TRUE;
  if(!crclose(odeum->indexdb)) err = TRUE;
  if(!crclose(odeum->docsdb)) err = TRUE;
  free(odeum->name);
  free(odeum);
  return err ? FALSE : TRUE;
}


/* Store a document. */
int odput(ODEUM *odeum, ODDOC *doc, int wmax, int over){
  char *tmp, *zbuf;
  const char *word, *ctmp;
  int i, docid, tsiz, wsiz, wnum, tmax, num, zsiz;
  double ival;
  ODPAIR pair;
  CBMAP *map;
  CBLIST *tlist;
  assert(odeum);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  if(!odeum->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if((tmp = vlget(odeum->rdocsdb, doc->uri, -1, &tsiz)) != NULL){
    if(!over){
      free(tmp);
      dpecodeset(DP_EKEEP, __FILE__, __LINE__);
      return FALSE;
    }
    if(tsiz != sizeof(int) || !odoutbyid(odeum, *(int *)tmp)){
      free(tmp);
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      odeum->fatal = TRUE;
      return FALSE;
    }
    free(tmp);
  }
  odeum->dmax++;
  odeum->dnum++;
  docid = odeum->dmax;
  map = cbmapopen();
  cbmapput(map, OD_URIEXPR, sizeof(OD_URIEXPR), doc->uri, -1, TRUE);
  tmp = cbmapdump(doc->attrs, &tsiz);
  cbmapput(map, OD_ATTRSEXPR, sizeof(OD_ATTRSEXPR), tmp, tsiz, TRUE);
  free(tmp);
  if(wmax < 0 || wmax > cblistnum(doc->nwords)) wmax = cblistnum(doc->nwords);
  tlist = cblistopen();
  for(i = 0; i < wmax; i++){
    ctmp = cblistval(doc->nwords, i, &wsiz);
    cblistpush(tlist, ctmp, wsiz);
  }
  tmp = cblistdump(tlist, &tsiz);
  cbmapput(map, OD_NWORDSEXPR, sizeof(OD_NWORDSEXPR), tmp, tsiz, TRUE);
  free(tmp);
  cblistclose(tlist);
  tlist = cblistopen();
  for(i = 0; i < wmax; i++){
    ctmp = cblistval(doc->awords, i, &wsiz);
    if(strcmp(ctmp, cblistval(doc->nwords, i, NULL))){
      cblistpush(tlist, ctmp, wsiz);
    } else {
      cblistpush(tlist, "\0", 1);
    }
  }
  tmp = cblistdump(tlist, &tsiz);
  cbmapput(map, OD_AWORDSEXPR, sizeof(OD_AWORDSEXPR), tmp, tsiz, TRUE);
  free(tmp);
  cblistclose(tlist);
  tmp = cbmapdump(map, &tsiz);
  cbmapclose(map);
  if(_qdbm_deflate){
    if(!(zbuf = _qdbm_deflate(tmp, tsiz, &zsiz, _QDBM_ZMRAW))){
      free(tmp);
      dpecodeset(DP_EMISC, __FILE__, __LINE__);
      odeum->fatal = TRUE;
      return FALSE;
    }
    free(tmp);
    tmp = zbuf;
    tsiz = zsiz;
  }
  if(!crput(odeum->docsdb, (char *)&docid, sizeof(int), tmp, tsiz, CR_DKEEP)){
    free(tmp);
    if(dpecode == DP_EKEEP) dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    odeum->fatal = TRUE;
    return FALSE;
  }
  free(tmp);
  if(!vlput(odeum->rdocsdb, doc->uri, -1, (char *)&docid, sizeof(int), VL_DOVER)){
    odeum->fatal = TRUE;
    return FALSE;
  }
  map = cbmapopen();
  wnum = cblistnum(doc->nwords);
  tmax = (int)(wnum * OD_WTOPRATE);
  for(i = 0; i < wnum; i++){
    word = cblistval(doc->nwords, i, &wsiz);
    if(wsiz < 1) continue;
    if((ctmp = cbmapget(map, word, wsiz, NULL)) != NULL){
      num = *(int *)ctmp + OD_WOCCRPOINT;
    } else {
      num = i <= tmax ? OD_WTOPBONUS + OD_WOCCRPOINT : OD_WOCCRPOINT;
    }
    cbmapput(map, word, wsiz, (char *)&num, sizeof(int), TRUE);
  }
  ival = odlogarithm(wnum);
  ival = (ival * ival * ival) / 8.0;
  if(ival < 8.0) ival = 8.0;
  cbmapiterinit(map);
  while((word = cbmapiternext(map, &wsiz)) != NULL){
    pair.id = docid;
    pair.score = (int)(*(int *)cbmapget(map, word, wsiz, NULL) / ival);
    cbmapputcat(odeum->cachemap, word, wsiz, (char *)&pair, sizeof(pair));
    cbmapmove(odeum->cachemap, word, wsiz, FALSE);
    odeum->cacheasiz += sizeof(pair);
    cbmapput(odeum->sortmap, word, wsiz, "", 0, FALSE);
  }
  cbmapclose(map);
  if(odeum->cacheasiz > odcachesiz){
    for(i = OD_CFBEGSIZ; odeum->cacheasiz > odcachesiz * OD_CFLIVERAT && i >= OD_CFENDSIZ;
        i /= 2){
      if(!odcacheflushfreq(odeum, "odput", i)) return FALSE;
    }
    while(odeum->cacheasiz > odcachesiz * OD_CFLIVERAT){
      if(!odcacheflushrare(odeum, "odput", OD_CFRFRAT)) return FALSE;
    }
  }
  doc->id = docid;
  odeum->ldid = docid;
  return TRUE;
}


/* Delete a document by a URL. */
int odout(ODEUM *odeum, const char *uri){
  char *tmp;
  int tsiz, docid;
  assert(odeum && uri);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  if(!odeum->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(!(tmp = vlget(odeum->rdocsdb, uri, -1, &tsiz))){
    if(dpecode != DP_ENOITEM) odeum->fatal = TRUE;
    return FALSE;
  }
  if(tsiz != sizeof(int)){
    free(tmp);
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    odeum->fatal = TRUE;
    return FALSE;
  }
  docid = *(int *)tmp;
  free(tmp);
  return odoutbyid(odeum, docid);
}


/* Delete a document specified by an ID number. */
int odoutbyid(ODEUM *odeum, int id){
  char *tmp, *zbuf;
  const char *uritmp;
  int tsiz, uritsiz, zsiz;
  CBMAP *map;
  assert(odeum && id > 0);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  if(!odeum->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(!(tmp = crget(odeum->docsdb, (char *)&id, sizeof(int), 0, -1, &tsiz))){
    if(dpecode != DP_ENOITEM) odeum->fatal = TRUE;
    return FALSE;
  }
  if(_qdbm_inflate){
    if(!(zbuf = _qdbm_inflate(tmp, tsiz, &zsiz, _QDBM_ZMRAW))){
      free(tmp);
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      odeum->fatal = TRUE;
      return FALSE;
    }
    free(tmp);
    tmp = zbuf;
    tsiz = zsiz;
  }
  map = cbmapload(tmp, tsiz);
  free(tmp);
  uritmp = cbmapget(map, OD_URIEXPR, sizeof(OD_URIEXPR), &uritsiz);
  if(!uritmp || !vlout(odeum->rdocsdb, uritmp, uritsiz)){
    cbmapclose(map);
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    odeum->fatal = TRUE;
    return FALSE;
  }
  cbmapclose(map);
  if(!crout(odeum->docsdb, (char *)&id, sizeof(int))){
    odeum->fatal = TRUE;
    return FALSE;
  }
  odeum->dnum--;
  return TRUE;
}


/* Retrieve a document by a URI. */
ODDOC *odget(ODEUM *odeum, const char *uri){
  char *tmp;
  int tsiz, docid;
  assert(odeum && uri);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return NULL;
  }
  if(!(tmp = vlget(odeum->rdocsdb, uri, -1, &tsiz))){
    if(dpecode != DP_ENOITEM) odeum->fatal = TRUE;
    return NULL;
  }
  if(tsiz != sizeof(int)){
    free(tmp);
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    odeum->fatal = TRUE;
    return NULL;
  }
  docid = *(int *)tmp;
  free(tmp);
  return odgetbyid(odeum, docid);
}


/* Retrieve a document by an ID number. */
ODDOC *odgetbyid(ODEUM *odeum, int id){
  char *tmp, *zbuf;
  const char *uritmp, *attrstmp, *nwordstmp, *awordstmp, *asis, *normal;
  int i, tsiz, uritsiz, attrstsiz, nwordstsiz, awordstsiz, zsiz, asiz, nsiz;
  ODDOC *doc;
  CBMAP *map;
  assert(odeum);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return NULL;
  }
  if(id < 1){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return NULL;
  }
  if(!(tmp = crget(odeum->docsdb, (char *)&id, sizeof(int), 0, -1, &tsiz))){
    if(dpecode != DP_ENOITEM) odeum->fatal = TRUE;
    return NULL;
  }
  if(_qdbm_inflate){
    if(!(zbuf = _qdbm_inflate(tmp, tsiz, &zsiz, _QDBM_ZMRAW))){
      free(tmp);
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      odeum->fatal = TRUE;
      return NULL;
    }
    free(tmp);
    tmp = zbuf;
    tsiz = zsiz;
  }
  map = cbmapload(tmp, tsiz);
  free(tmp);
  uritmp = cbmapget(map, OD_URIEXPR, sizeof(OD_URIEXPR), &uritsiz);
  attrstmp = cbmapget(map, OD_ATTRSEXPR, sizeof(OD_ATTRSEXPR), &attrstsiz);
  nwordstmp = cbmapget(map, OD_NWORDSEXPR, sizeof(OD_NWORDSEXPR), &nwordstsiz);
  awordstmp = cbmapget(map, OD_AWORDSEXPR, sizeof(OD_AWORDSEXPR), &awordstsiz);
  if(!uritmp || !attrstmp || !nwordstmp || !awordstmp){
    cbmapclose(map);
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    odeum->fatal = TRUE;
    return NULL;
  }
  doc = cbmalloc(sizeof(ODDOC));
  doc->id = id;
  doc->uri = cbmemdup(uritmp, uritsiz);
  doc->attrs = cbmapload(attrstmp, attrstsiz);
  doc->nwords = cblistload(nwordstmp, nwordstsiz);
  doc->awords = cblistload(awordstmp, awordstsiz);
  cbmapclose(map);
  for(i = 0; i < cblistnum(doc->awords); i++){
    asis = cblistval(doc->awords, i, &asiz);
    if(asiz == 1 && asis[0] == '\0'){
      normal = cblistval(doc->nwords, i, &nsiz);
      cblistover(doc->awords, i, normal, nsiz);
    }
  }
  return doc;
}


/* Retrieve the ID of the document specified by a URI. */
int odgetidbyuri(ODEUM *odeum, const char *uri){
  char *tmp;
  int tsiz, docid;
  assert(odeum && uri);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return -1;
  }
  if(!(tmp = vlget(odeum->rdocsdb, uri, -1, &tsiz))){
    if(dpecode != DP_ENOITEM) odeum->fatal = TRUE;
    return -1;
  }
  if(tsiz != sizeof(int)){
    free(tmp);
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    odeum->fatal = TRUE;
    return -1;
  }
  docid = *(int *)tmp;
  free(tmp);
  return docid;
}


/* Check whether the document specified by an ID number exists. */
int odcheck(ODEUM *odeum, int id){
  assert(odeum);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  if(id < 1){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  return crvsiz(odeum->docsdb, (char *)&id, sizeof(int)) != -1;
}


/* Search the inverted index for documents including a word. */
ODPAIR *odsearch(ODEUM *odeum, const char *word, int max, int *np){
  char *tmp;
  int tsiz;
  assert(odeum && word && np);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return NULL;
  }
  if(odeum->wmode && cbmaprnum(odeum->sortmap) > 0 &&
     (!odcacheflush(odeum, "odsearch") || !odsortindex(odeum, "odsearch"))){
    odeum->fatal = TRUE;
    return NULL;
  }
  max = max < 0 ? -1 : max * sizeof(ODPAIR);
  if(!(tmp = crget(odeum->indexdb, word, -1, 0, max, &tsiz))){
    if(dpecode != DP_ENOITEM){
      odeum->fatal = TRUE;
      return NULL;
    }
    *np = 0;
    return cbmalloc(1);
  }
  *np = tsiz / sizeof(ODPAIR);
  return (ODPAIR *)tmp;
}


/* Get the number of documents including a word. */
int odsearchdnum(ODEUM *odeum, const char *word){
  int rv;
  assert(odeum && word);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return -1;
  }
  rv = crvsiz(odeum->indexdb, word, -1);
  return rv < 0 ? -1 : rv / sizeof(ODPAIR);
}


/* Initialize the iterator of a database handle. */
int oditerinit(ODEUM *odeum){
  assert(odeum);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  return criterinit(odeum->docsdb);
}


/* Get the next key of the iterator. */
ODDOC *oditernext(ODEUM *odeum){
  char *tmp;
  int tsiz, docsid;
  ODDOC *doc;
  assert(odeum);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return NULL;
  }
  doc = NULL;
  while(TRUE){
    if(!(tmp = criternext(odeum->docsdb, &tsiz))){
      if(dpecode != DP_ENOITEM) odeum->fatal = TRUE;
      return NULL;
    }
    if(tsiz != sizeof(int)){
      free(tmp);
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      odeum->fatal = TRUE;
      return NULL;
    }
    docsid = *(int *)tmp;
    free(tmp);
    if((doc = odgetbyid(odeum, docsid)) != NULL) break;
    if(dpecode != DP_ENOITEM){
      odeum->fatal = TRUE;
      return NULL;
    }
  }
  return doc;
}


/* Synchronize updating contents with the files and the devices. */
int odsync(ODEUM *odeum){
  char numbuf[OD_NUMBUFSIZ];
  assert(odeum);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  if(!odeum->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(odotcb) odotcb("odsync", odeum, "writing meta information");
  sprintf(numbuf, "%d", odeum->dmax);
  if(!vlput(odeum->rdocsdb, OD_DMAXEXPR, sizeof(OD_DMAXEXPR), numbuf, -1, VL_DOVER)){
    odeum->fatal = TRUE;
    return FALSE;
  }
  sprintf(numbuf, "%d", odeum->dnum);
  if(!vlput(odeum->rdocsdb, OD_DNUMEXPR, sizeof(OD_DNUMEXPR), numbuf, -1, VL_DOVER)){
    odeum->fatal = TRUE;
    return FALSE;
  }
  if(!odcacheflush(odeum, "odsync")){
    odeum->fatal = TRUE;
    return FALSE;
  }
  if(!odsortindex(odeum, "odsync")){
    odeum->fatal = TRUE;
    return FALSE;
  }
  if(odotcb) odotcb("odsync", odeum, "synchronizing the document database");
  if(!crsync(odeum->docsdb)){
    odeum->fatal = TRUE;
    return FALSE;
  }
  if(odotcb) odotcb("odsync", odeum, "synchronizing the inverted index");
  if(!crsync(odeum->indexdb)){
    odeum->fatal = TRUE;
    return FALSE;
  }
  if(odotcb) odotcb("odsync", odeum, "synchronizing the reverse dictionary");
  if(!vlsync(odeum->rdocsdb)){
    odeum->fatal = TRUE;
    return FALSE;
  }
  return TRUE;
}


/* Optimize a database. */
int odoptimize(ODEUM *odeum){
  assert(odeum);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return FALSE;
  }
  if(!odeum->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(!odcacheflush(odeum, "odoptimize")){
    odeum->fatal = TRUE;
    return FALSE;
  }
  if(odeum->ldid < 1 || odeum->ldid != odeum->dnum){
    if(!odpurgeindex(odeum, "odoptimize")){
      odeum->fatal = TRUE;
      return FALSE;
    }
  }
  if(odeum->ldid > 0){
    if(!odsortindex(odeum, "odoptimize")){
      odeum->fatal = TRUE;
      return FALSE;
    }
  }
  if(odotcb) odotcb("odoptimize", odeum, "optimizing the document database");
  if(!croptimize(odeum->docsdb, -1)){
    odeum->fatal = TRUE;
    return FALSE;
  }
  if(odotcb) odotcb("odoptimize", odeum, "optimizing the inverted index");
  if(!croptimize(odeum->indexdb, -1)){
    odeum->fatal = TRUE;
    return FALSE;
  }
  if(odotcb) odotcb("odoptimize", odeum, "optimizing the reverse dictionary");
  if(!vloptimize(odeum->rdocsdb)){
    odeum->fatal = TRUE;
    return FALSE;
  }
  return TRUE;
}


/* Get the name of a database. */
char *odname(ODEUM *odeum){
  assert(odeum);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return NULL;
  }
  return cbmemdup(odeum->name, -1);
}


/* Get the total size of database files. */
double odfsiz(ODEUM *odeum){
  double fsiz, rv;
  assert(odeum);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return -1;
  }
  fsiz = 0;
  if((rv = crfsizd(odeum->docsdb)) < 0) return -1.0;
  fsiz += rv;
  if((rv = crfsizd(odeum->indexdb)) < 0) return -1.0;
  fsiz += rv;
  if((rv = vlfsiz(odeum->rdocsdb)) == -1) return -1.0;
  fsiz += rv;
  return fsiz;
}


/* Get the total number of the elements of the bucket arrays for the inverted index. */
int odbnum(ODEUM *odeum){
  assert(odeum);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return -1;
  }
  return crbnum(odeum->indexdb);
}


/* Get the total number of the used elements of the bucket arrays in the inverted index. */
int odbusenum(ODEUM *odeum){
  assert(odeum);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return -1;
  }
  return crbusenum(odeum->indexdb);
}


/* Get the number of the documents stored in a database. */
int oddnum(ODEUM *odeum){
  assert(odeum);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return -1;
  }
  return odeum->dnum;
}


/* Get the number of the words stored in a database. */
int odwnum(ODEUM *odeum){
  assert(odeum);
  if(odeum->fatal){
    dpecodeset(DP_EFATAL, __FILE__, __LINE__);
    return -1;
  }
  return crrnum(odeum->indexdb);
}


/* Check whether a database handle is a writer or not. */
int odwritable(ODEUM *odeum){
  assert(odeum);
  return odeum->wmode;
}


/* Check whether a database has a fatal error or not. */
int odfatalerror(ODEUM *odeum){
  assert(odeum);
  return odeum->fatal;
}


/* Get the inode number of a database directory. */
int odinode(ODEUM *odeum){
  assert(odeum);
  return odeum->inode;
}


/* Get the last modified time of a database. */
time_t odmtime(ODEUM *odeum){
  assert(odeum);
  return crmtime(odeum->indexdb);
}


/* Merge plural database directories. */
int odmerge(const char *name, const CBLIST *elemnames){
  ODEUM *odeum, **elems;
  CURIA *curia, *ecuria;
  VILLA *villa, *evilla;
  ODPAIR *pairs;
  char *word, *kbuf, *vbuf, *dbuf, otmsg[OD_OTCBBUFSIZ];
  char *wpunit[OD_MIWUNIT], *vpunit[OD_MIWUNIT];
  int i, j, k, num, dnum, wnum, dbnum, ibnum, tnum, wsunit[OD_MIWUNIT], vsunit[OD_MIWUNIT];
  int err, *bases, sum, max, wsiz, ksiz, vsiz, uend, unum, pnum, align, id, nid, dsiz;
  assert(name && elemnames);
  num = cblistnum(elemnames);
  elems = cbmalloc(num * sizeof(ODEUM *) + 1);
  dnum = 0;
  wnum = 0;
  for(i = 0; i < num; i++){
    if(!(elems[i] = odopen(cblistval(elemnames, i, NULL), OD_OREADER))){
      for(i -= 1; i >= 0; i--){
        odclose(elems[i]);
      }
      free(elems);
      return FALSE;
    }
    dnum += oddnum(elems[i]);
    wnum += odwnum(elems[i]);
  }
  dbnum = (int)(dnum * OD_MDBRATIO / OD_DOCSDNUM);
  ibnum = (int)(wnum * OD_MIBRATIO / odindexdnum);
  if(!(odeum = odopendb(name, OD_OWRITER | OD_OCREAT | OD_OTRUNC, dbnum, ibnum, "odmerge"))){
    for(i = 0; i < num; i++){
      odclose(elems[i]);
    }
    free(elems);
    return FALSE;
  }
  err = FALSE;
  if(odotcb) odotcb("odmerge", odeum, "calculating the base ID numbers");
  bases = cbmalloc(num * sizeof(int) + 1);
  sum = 0;
  for(i = 0; i < num; i++){
    ecuria = elems[i]->docsdb;
    max = 0;
    if(!criterinit(ecuria) && dpecode != DP_ENOITEM) err = TRUE;
    while((kbuf = criternext(ecuria, &ksiz)) != NULL){
      if(ksiz == sizeof(int)){
        if(*(int *)kbuf > max) max = *(int *)kbuf;
      }
      free(kbuf);
    }
    bases[i] = sum;
    sum += max;
  }
  curia = odeum->indexdb;
  for(i = 0; i < num; i++){
    if(odotcb){
      sprintf(otmsg, "merging the inverted index (%d/%d)", i + 1, num);
      odotcb("odmerge", odeum, otmsg);
    }
    ecuria = elems[i]->indexdb;
    tnum = 0;
    uend = FALSE;
    if(!criterinit(ecuria) && dpecode != DP_ENOITEM) err = TRUE;
    while(!uend){
      for(unum = 0; unum < OD_MIWUNIT; unum++){
        if(!(word = criternext(ecuria, &wsiz))){
          uend = TRUE;
          break;
        }
        if(!(vbuf = crget(ecuria, word, wsiz, 0, -1, &vsiz))){
          err = TRUE;
          free(word);
          break;
        }
        wpunit[unum] = word;
        wsunit[unum] = wsiz;
        vpunit[unum] = vbuf;
        vsunit[unum] = vsiz;
      }
      for(j = 0; j < unum; j++){
        word = wpunit[j];
        wsiz = wsunit[j];
        vbuf = vpunit[j];
        vsiz = vsunit[j];
        pairs = (ODPAIR *)vbuf;
        pnum = vsiz / sizeof(ODPAIR);
        for(k = 0; k < pnum; k++){
          pairs[k].id += bases[i];
        }
        align = (int)(i < num - 1 ? vsiz * (num - i) * OD_MIARATIO : OD_INDEXALIGN);
        if(!crsetalign(curia, align)) err = TRUE;
        if(!crput(curia, word, wsiz, vbuf, vsiz, CR_DCAT)) err = TRUE;
        free(vbuf);
        free(word);
        if(odotcb && (tnum + 1) % OD_OTPERWORDS == 0){
          sprintf(otmsg, "... (%d/%d)", tnum + 1, crrnum(ecuria));
          odotcb("odmerge", odeum, otmsg);
        }
        tnum++;
      }
    }
  }
  if(odotcb) odotcb("odmerge", odeum, "sorting the inverted index");
  tnum = 0;
  if(!criterinit(curia) && dpecode != DP_ENOITEM) err = TRUE;
  while((word = criternext(curia, &wsiz)) != NULL){
    if((vbuf = crget(curia, word, wsiz, 0, -1, &vsiz)) != NULL){
      if(vsiz > sizeof(ODPAIR)){
        pairs = (ODPAIR *)vbuf;
        pnum = vsiz / sizeof(ODPAIR);
        qsort(pairs, pnum, sizeof(ODPAIR), odsortcompare);
        if(!crput(curia, word, wsiz, vbuf, vsiz, CR_DOVER)) err = TRUE;
      }
      free(vbuf);
    }
    free(word);
    if(odotcb && (tnum + 1) % OD_OTPERWORDS == 0){
      sprintf(otmsg, "... (%d/%d)", tnum + 1, crrnum(curia));
      odotcb("odmerge", odeum, otmsg);
    }
    tnum++;
  }
  if(odotcb) odotcb("odmerge", odeum, "synchronizing the inverted index");
  if(!crsync(curia)) err = TRUE;
  dnum = 0;
  curia = odeum->docsdb;
  villa = odeum->rdocsdb;
  for(i = 0; i < num; i++){
    if(odotcb){
      sprintf(otmsg, "merging the document database (%d/%d)", i + 1, num);
      odotcb("odmerge", odeum, otmsg);
    }
    evilla = elems[i]->rdocsdb;
    ecuria = elems[i]->docsdb;
    tnum = 0;
    if(!vlcurfirst(evilla) && dpecode != DP_ENOITEM) err = TRUE;
    while(TRUE){
      if(!(kbuf = vlcurkey(evilla, &ksiz))) break;
      if((ksiz == sizeof(OD_DMAXEXPR) && !memcmp(kbuf, OD_DMAXEXPR, ksiz)) ||
         (ksiz == sizeof(OD_DNUMEXPR) && !memcmp(kbuf, OD_DNUMEXPR, ksiz))){
        free(kbuf);
        if(!vlcurnext(evilla)) break;
        continue;
      }
      if(!(vbuf = vlcurval(evilla, &vsiz))){
        free(kbuf);
        if(!vlcurnext(evilla)) break;
        continue;
      }
      if(vsiz != sizeof(int)){
        free(vbuf);
        free(kbuf);
        if(!vlcurnext(evilla)) break;
        continue;
      }
      id =  *(int *)vbuf;
      nid = id + bases[i];
      if(vlput(villa, kbuf, ksiz, (char *)&nid, sizeof(int), VL_DKEEP)){
        if((dbuf = crget(ecuria, (char *)&id, sizeof(int), 0, -1, &dsiz)) != NULL){
          if(crput(curia, (char *)&nid, sizeof(int), dbuf, dsiz, CR_DKEEP)){
            dnum++;
          } else {
            err = TRUE;
          }
          free(dbuf);
        } else {
          err = TRUE;
        }
      } else if(dpecode != DP_EKEEP){
        err = TRUE;
      }
      free(vbuf);
      free(kbuf);
      odeum->dnum++;
      if(odotcb && (tnum + 1) % OD_OTPERDOCS == 0){
        sprintf(otmsg, "... (%d/%d)", tnum + 1, crrnum(ecuria));
        odotcb("odmerge", odeum, otmsg);
      }
      tnum++;
      if(!vlcurnext(evilla)) break;
    }
  }
  odeum->dnum = dnum;
  odeum->dmax = dnum;
  free(bases);
  if(odotcb) odotcb("odmerge", odeum, "synchronizing the document index");
  if(!crsync(curia)) err = TRUE;
  if(!odclose(odeum)) err = TRUE;
  for(i = 0; i < num; i++){
    if(!odclose(elems[i])) err = TRUE;
  }
  free(elems);
  return err ? FALSE : TRUE;
}


/* Remove a database directory. */
int odremove(const char *name){
  char docsname[OD_PATHBUFSIZ], indexname[OD_PATHBUFSIZ], rdocsname[OD_PATHBUFSIZ];
  char path[OD_PATHBUFSIZ];
  const char *file;
  struct stat sbuf;
  CBLIST *list;
  int i;
  assert(name);
  sprintf(docsname, "%s%c%s", name, MYPATHCHR, OD_DOCSNAME);
  sprintf(indexname, "%s%c%s", name, MYPATHCHR, OD_INDEXNAME);
  sprintf(rdocsname, "%s%c%s", name, MYPATHCHR, OD_RDOCSNAME);
  if(lstat(name, &sbuf) == -1){
    dpecodeset(DP_ESTAT, __FILE__, __LINE__);
    return FALSE;
  }
  if(lstat(docsname, &sbuf) != -1 && !crremove(docsname)) return FALSE;
  if(lstat(indexname, &sbuf) != -1 && !crremove(indexname)) return FALSE;
  if(lstat(rdocsname, &sbuf) != -1 && !vlremove(rdocsname)) return FALSE;
  if((list = cbdirlist(name)) != NULL){
    for(i = 0; i < cblistnum(list); i++){
      file = cblistval(list, i, NULL);
      if(!strcmp(file, MYCDIRSTR) || !strcmp(file, MYPDIRSTR)) continue;
      sprintf(path, "%s%c%s", name, MYPATHCHR, file);
      if(lstat(path, &sbuf) == -1) continue;
      if(S_ISDIR(sbuf.st_mode)){
        if(!crremove(path)) return FALSE;
      } else {
        if(!dpremove(path)) return FALSE;
      }
    }
    cblistclose(list);
  }
  if(rmdir(name) == -1){
    dpecodeset(DP_ERMDIR, __FILE__, __LINE__);
    return FALSE;
  }
  return TRUE;
}


/* Get a document handle. */
ODDOC *oddocopen(const char *uri){
  ODDOC *doc;
  assert(uri);
  doc = cbmalloc(sizeof(ODDOC));
  doc->id = -1;
  doc->uri = cbmemdup(uri, -1);
  doc->attrs = cbmapopenex(OD_MAPPBNUM);
  doc->nwords = cblistopen();
  doc->awords = cblistopen();
  return doc;
}


/* Close a document handle. */
void oddocclose(ODDOC *doc){
  assert(doc);
  cblistclose(doc->awords);
  cblistclose(doc->nwords);
  cbmapclose(doc->attrs);
  free(doc->uri);
  free(doc);
}


/* Add an attribute to a document. */
void oddocaddattr(ODDOC *doc, const char *name, const char *value){
  assert(doc && name && value);
  cbmapput(doc->attrs, name, -1, value, -1, TRUE);
}


/* Add a word to a document. */
void oddocaddword(ODDOC *doc, const char *normal, const char *asis){
  assert(doc && normal && asis);
  cblistpush(doc->nwords, normal, -1);
  cblistpush(doc->awords, asis, -1);
}


/* Get the ID number of a document. */
int oddocid(const ODDOC *doc){
  assert(doc);
  return doc->id;
}


/* Get the URI of a document. */
const char *oddocuri(const ODDOC *doc){
  assert(doc);
  return doc->uri;
}


/* Get the value of an attribute of a document. */
const char *oddocgetattr(const ODDOC *doc, const char *name){
  assert(doc && name);
  return cbmapget(doc->attrs, name, -1, NULL);
}


/* Get the list handle contains words in normalized form of a document. */
const CBLIST *oddocnwords(const ODDOC *doc){
  assert(doc);
  return doc->nwords;
}


/* Get the list handle contains words in appearance form of a document. */
const CBLIST *oddocawords(const ODDOC *doc){
  assert(doc);
  return doc->awords;
}


/* Get the map handle contains keywords in normalized form and their scores. */
CBMAP *oddocscores(const ODDOC *doc, int max, ODEUM *odeum){
  const CBLIST *nwords;
  CBMAP *map, *kwmap;
  const char *word, *ctmp;
  char numbuf[OD_NUMBUFSIZ];
  ODWORD *owords;
  int i, wsiz, wnum, hnum, mnum, nbsiz;
  double ival;
  assert(doc && max >= 0);
  map = cbmapopen();
  nwords = oddocnwords(doc);
  for(i = 0; i < cblistnum(nwords); i++){
    word = cblistval(nwords, i, &wsiz);
    if(wsiz < 1) continue;
    if((ctmp = cbmapget(map, word, wsiz, NULL)) != NULL){
      wnum = *(int *)ctmp + OD_WOCCRPOINT;
    } else {
      wnum = OD_WOCCRPOINT;
    }
    cbmapput(map, word, wsiz, (char *)&wnum, sizeof(int), TRUE);
  }
  mnum = cbmaprnum(map);
  owords = cbmalloc(mnum * sizeof(ODWORD) + 1);
  cbmapiterinit(map);
  for(i = 0; (word = cbmapiternext(map, &wsiz)) != NULL; i++){
    owords[i].word = word;
    owords[i].num = *(int *)cbmapget(map, word, wsiz, NULL);
  }
  qsort(owords, mnum, sizeof(ODWORD), odwordcompare);
  if(odeum){
    if(mnum > max * OD_KEYCRATIO) mnum = (int)(max * OD_KEYCRATIO);
    for(i = 0; i < mnum; i++){
      if((hnum = odsearchdnum(odeum, owords[i].word)) < 0) hnum = 0;
      ival = odlogarithm(hnum);
      ival = (ival * ival * ival) / 8.0;
      if(ival < 8.0) ival = 8.0;
      owords[i].num = (int)(owords[i].num / ival);
    }
    qsort(owords, mnum, sizeof(ODWORD), odwordcompare);
  }
  if(mnum > max) mnum = max;
  kwmap = cbmapopenex(OD_MAPPBNUM);
  for(i = 0; i < mnum; i++){
    nbsiz = sprintf(numbuf, "%d", owords[i].num);
    cbmapput(kwmap, owords[i].word, -1, numbuf, nbsiz, TRUE);
  }
  free(owords);
  cbmapclose(map);
  return kwmap;
}


/* Break a text into words in appearance form. */
CBLIST *odbreaktext(const char *text){
  const char *word;
  CBLIST *elems, *words;
  int i, j, dif, wsiz, pv, delim;
  assert(text);
  words = cblistopen();
  elems = cbsplit(text, -1, OD_SPACECHARS);
  for(i = 0; i < cblistnum(elems); i++){
    word = cblistval(elems, i, &wsiz);
    delim = FALSE;
    j = 0;
    pv = 0;
    while(TRUE){
      dif = j - pv;
      if(j >= wsiz){
        if(dif > 0 && dif <= OD_MAXWORDLEN) cblistpush(words, word + pv, j - pv);
        break;
      }
      if(delim){
        if(!strchr(OD_DELIMCHARS, word[j])){
          if(dif > 0 && dif <= OD_MAXWORDLEN) cblistpush(words, word + pv, j - pv);
          pv = j;
          delim = FALSE;
        }
      } else {
        if(strchr(OD_DELIMCHARS, word[j])){
          if(dif > 0 && dif <= OD_MAXWORDLEN) cblistpush(words, word + pv, j - pv);
          pv = j;
          delim = TRUE;
        }
      }
      j++;
    }
  }
  cblistclose(elems);
  return words;
}


/* Make the normalized form of a word. */
char *odnormalizeword(const char *asis){
  char *nword;
  int i;
  assert(asis);
  for(i = 0; asis[i] != '\0'; i++){
    if(!strchr(OD_DELIMCHARS, asis[i])) break;
  }
  if(asis[i] == '\0') return cbmemdup("", 0);
  nword = cbmemdup(asis, -1);
  for(i = 0; nword[i] != '\0'; i++){
    if(nword[i] >= 'A' && nword[i] <= 'Z') nword[i] += 'a' - 'A';
  }
  while(i >= 0){
    if(strchr(OD_GLUECHARS, nword[i])){
      nword[i] = '\0';
    } else {
      break;
    }
    i--;
  }
  return nword;
}


/* Get the common elements of two sets of documents. */
ODPAIR *odpairsand(ODPAIR *apairs, int anum, ODPAIR *bpairs, int bnum, int *np){
  CBMAP *map;
  ODPAIR *result;
  const char *tmp;
  int i, rnum;
  assert(apairs && anum >= 0 && bpairs && bnum >= 0);
  map = odpairsmap(bpairs, bnum);
  result = cbmalloc(sizeof(ODPAIR) * anum + 1);
  rnum = 0;
  for(i = 0; i < anum; i++){
    if(!(tmp = cbmapget(map, (char *)&(apairs[i].id), sizeof(int), NULL))) continue;
    result[rnum].id = apairs[i].id;
    result[rnum].score = apairs[i].score + *(int *)tmp;
    rnum++;
  }
  cbmapclose(map);
  qsort(result, rnum, sizeof(ODPAIR), odsortcompare);
  *np = rnum;
  return result;
}


/* Get the sum of elements of two sets of documents. */
ODPAIR *odpairsor(ODPAIR *apairs, int anum, ODPAIR *bpairs, int bnum, int *np){
  CBMAP *map;
  ODPAIR *result;
  const char *tmp;
  int i, score, rnum;
  assert(apairs && anum >= 0 && bpairs && bnum >= 0);
  map = odpairsmap(bpairs, bnum);
  for(i = 0; i < anum; i++){
    score = 0;
    if((tmp = cbmapget(map, (char *)&(apairs[i].id), sizeof(int), NULL)) != NULL)
      score = *(int *)tmp;
    score += apairs[i].score;
    cbmapput(map, (char *)&(apairs[i].id), sizeof(int),
             (char *)&score, sizeof(int), TRUE);
  }
  rnum = cbmaprnum(map);
  result = cbmalloc(rnum * sizeof(ODPAIR) + 1);
  cbmapiterinit(map);
  for(i = 0; (tmp = cbmapiternext(map, NULL)) != NULL; i++){
    result[i].id = *(int *)tmp;
    result[i].score = *(int *)cbmapget(map, tmp, sizeof(int), NULL);
  }
  cbmapclose(map);
  qsort(result, rnum, sizeof(ODPAIR), odsortcompare);
  *np = rnum;
  return result;
}


/* Get the difference set of documents. */
ODPAIR *odpairsnotand(ODPAIR *apairs, int anum, ODPAIR *bpairs, int bnum, int *np){
  CBMAP *map;
  ODPAIR *result;
  const char *tmp;
  int i, rnum;
  assert(apairs && anum >= 0 && bpairs && bnum >= 0);
  map = odpairsmap(bpairs, bnum);
  result = cbmalloc(sizeof(ODPAIR) * anum + 1);
  rnum = 0;
  for(i = 0; i < anum; i++){
    if((tmp = cbmapget(map, (char *)&(apairs[i].id), sizeof(int), NULL)) != NULL) continue;
    result[rnum].id = apairs[i].id;
    result[rnum].score = apairs[i].score;
    rnum++;
  }
  cbmapclose(map);
  qsort(result, rnum, sizeof(ODPAIR), odsortcompare);
  *np = rnum;
  return result;
}


/* Sort a set of documents in descending order of scores. */
void odpairssort(ODPAIR *pairs, int pnum){
  assert(pairs && pnum >= 0);
  qsort(pairs, pnum, sizeof(ODPAIR), odsortcompare);
}


/* Get the natural logarithm of a number. */
double odlogarithm(double x){
  int i;
  if(x <= 1.0) return 0.0;
  x = x * x * x * x * x * x * x * x * x * x;
  for(i = 0; x > 1.0; i++){
    x /= 2.718281828459;
  }
  return (double)i / 10.0;
}


/* Get the cosine of the angle of two vectors. */
double odvectorcosine(const int *avec, const int *bvec, int vnum){
  double rv;
  assert(avec && bvec && vnum >= 0);
  rv = odvecinnerproduct(avec, bvec, vnum) /
    ((odvecabsolute(avec, vnum) * odvecabsolute(bvec, vnum)));
  return rv > 0.0 ? rv : 0.0;
}


/* Set the global tuning parameters. */
void odsettuning(int ibnum, int idnum, int cbnum, int csiz){
  if(ibnum > 0) odindexbnum = ibnum;
  if(idnum > 0) odindexdnum = idnum;
  if(cbnum > 0) odcachebnum = dpprimenum(cbnum);
  if(csiz > 0) odcachesiz = csiz;
}


/* Break a text into words and store appearance forms and normalized form into lists. */
void odanalyzetext(ODEUM *odeum, const char *text, CBLIST *awords, CBLIST *nwords){
  char aword[OD_MAXWORDLEN+1], *wp;
  int lev, wsiz;
  assert(odeum && text && awords);
  lev = OD_EVSPACE;
  wsiz = 0;
  for(; *text != '\0'; text++){
    switch(odeum->statechars[*(unsigned char *)text]){
    case OD_EVWORD:
      if(wsiz > 0 && lev == OD_EVDELIM){
        cblistpush(awords, aword, wsiz);
        if(nwords) cblistpush(nwords, "", 0);
        wsiz = 0;
      }
      if(wsiz <= OD_MAXWORDLEN){
        aword[wsiz++] = *text;
      }
      lev = OD_EVWORD;
      break;
    case OD_EVGLUE:
      if(wsiz > 0 && lev == OD_EVDELIM){
        cblistpush(awords, aword, wsiz);
        if(nwords) cblistpush(nwords, "", 0);
        wsiz = 0;
      }
      if(wsiz <= OD_MAXWORDLEN){
        aword[wsiz++] = *text;
      }
      lev = OD_EVGLUE;
      break;
    case OD_EVDELIM:
      if(wsiz > 0 && lev != OD_EVDELIM){
        cblistpush(awords, aword, wsiz);
        if(nwords){
          wp = aword;
          aword[wsiz] = '\0';
          while(*wp != '\0'){
            if(*wp >= 'A' && *wp <= 'Z') *wp += 'a' - 'A';
            wp++;
          }
          wp--;
          while(wp >= aword && odeum->statechars[*(unsigned char *)wp] == OD_EVGLUE){
            wsiz--;
            wp--;
          }
          cblistpush(nwords, aword, wsiz);
        }
        wsiz = 0;
      }
      if(wsiz <= OD_MAXWORDLEN){
        aword[wsiz++] = *text;
      }
      lev = OD_EVDELIM;
      break;
    default:
      if(wsiz > 0){
        cblistpush(awords, aword, wsiz);
        if(nwords){
          if(lev == OD_EVDELIM){
            cblistpush(nwords, "", 0);
          } else {
            wp = aword;
            aword[wsiz] = '\0';
            while(*wp != '\0'){
              if(*wp >= 'A' && *wp <= 'Z') *wp += 'a' - 'A';
              wp++;
            }
            wp--;
            while(wp >= aword && odeum->statechars[*(unsigned char *)wp] == OD_EVGLUE){
              wsiz--;
              wp--;
            }
            cblistpush(nwords, aword, wsiz);
          }
        }
        wsiz = 0;
      }
      lev = OD_EVSPACE;
      break;
    }
  }
  if(wsiz > 0){
    cblistpush(awords, aword, wsiz);
    if(nwords){
      if(lev == OD_EVDELIM){
        cblistpush(nwords, "", 0);
      } else {
        wp = aword;
        aword[wsiz] = '\0';
        while(*wp != '\0'){
          if(*wp >= 'A' && *wp <= 'Z') *wp += 'a' - 'A';
          wp++;
        }
        wp--;
        while(wp >= aword && odeum->statechars[*(unsigned char *)wp] == OD_EVGLUE){
          wsiz--;
          wp--;
        }
        cblistpush(nwords, aword, wsiz);
      }
    }
    wsiz = 0;
  }
}


/* Set the classes of characters used by `odanalyzetext'. */
void odsetcharclass(ODEUM *odeum, const char *spacechars, const char *delimchars,
                    const char *gluechars){
  assert(odeum && spacechars && delimchars && gluechars);
  memset(odeum->statechars, OD_EVWORD, sizeof(odeum->statechars));
  for(; *spacechars != '\0'; spacechars++){
    odeum->statechars[*(unsigned char *)spacechars] = OD_EVSPACE;
  }
  for(; *delimchars != '\0'; delimchars++){
    odeum->statechars[*(unsigned char *)delimchars] = OD_EVDELIM;
  }
  for(; *gluechars != '\0'; gluechars++){
    odeum->statechars[*(unsigned char *)gluechars] = OD_EVGLUE;
  }
}


/* Query a database using a small boolean query language. */
ODPAIR *odquery(ODEUM *odeum, const char *query, int *np, CBLIST *errors){
  CBLIST *tokens = cblistopen();
  CBLIST *nwords = cblistopen();
  ODPAIR *results = NULL;
  assert(odeum && query && np);
  odanalyzetext(odeum, query, tokens, nwords);
  odcleannormalized(odeum, nwords);
  odfixtokens(odeum, tokens);
  results = odparseexpr(odeum, tokens, nwords, np, errors);
  cblistclose(tokens);
  cblistclose(nwords);
  return results;
}



/*************************************************************************************************
 * features for experts
 *************************************************************************************************/


/* Get the internal database handle for documents. */
CURIA *odidbdocs(ODEUM *odeum){
  assert(odeum);
  return odeum->docsdb;
}


/* Get the internal database handle for the inverted index. */
CURIA *odidbindex(ODEUM *odeum){
  assert(odeum);
  return odeum->indexdb;
}


/* Get the internal database handle for the reverse dictionary. */
VILLA *odidbrdocs(ODEUM *odeum){
  assert(odeum);
  return odeum->rdocsdb;
}


/* Set the call back function called in merging. */
void odsetotcb(void (*otcb)(const char *, ODEUM *, const char *)){
  odotcb = otcb;
}


/* Get the positive one of square roots of a number. */
double odsquareroot(double x){
  double c, rv;
  if(x <= 0.0) return 0.0;
  c = x > 1.0 ? x : 1;
  do {
    rv = c;
    c = (x / c + c) * 0.5;
  } while(c < rv);
  return rv;
}


/* Get the absolute of a vector. */
double odvecabsolute(const int *vec, int vnum){
  double rv;
  int i;
  assert(vec && vnum >= 0);
  rv = 0;
  for(i = 0; i < vnum; i++){
    rv += (double)vec[i] * (double)vec[i];
  }
  return odsquareroot(rv);
}


/* Get the inner product of two vectors. */
double odvecinnerproduct(const int *avec, const int *bvec, int vnum){
  double rv;
  int i;
  assert(avec && bvec && vnum >= 0);
  rv = 0;
  for(i = 0; i < vnum; i++){
    rv += (double)avec[i] * (double)bvec[i];
  }
  return rv;
}



/*************************************************************************************************
 * private objects
 *************************************************************************************************/


/* Get a database handle.
   `name' specifies the name of a database directory.
   `omode' specifies the connection mode.
   `docsbnum` specifies the number of buckets of the document database.
   `indexbnum` specifies the number of buckets of the index database.
   `fname' specifies the name of caller function.
   The return value is the database handle or `NULL' if it is not successful. */
static ODEUM *odopendb(const char *name, int omode, int docsbnum, int indexbnum,
                       const char *fname){
  int cromode, vlomode, inode, dmax, dnum;
  char docsname[OD_PATHBUFSIZ], indexname[OD_PATHBUFSIZ], rdocsname[OD_PATHBUFSIZ], *tmp;
  struct stat sbuf;
  CURIA *docsdb, *indexdb;
  VILLA *rdocsdb;
  CBMAP *cachemap;
  CBMAP *sortmap;
  ODEUM *odeum;
  assert(name);
  if(strlen(name) > OD_NAMEMAX){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return NULL;
  }
  cromode = CR_OREADER;
  vlomode = VL_OREADER;
  if(omode & OD_OWRITER){
    cromode = CR_OWRITER;
    vlomode = VL_OWRITER | VL_OZCOMP | VL_OYCOMP;
    if(omode & OD_OCREAT){
      cromode |= CR_OCREAT;
      vlomode |= VL_OCREAT;
    }
    if(omode & OD_OTRUNC){
      cromode |= CR_OTRUNC;
      vlomode |= VL_OTRUNC;
    }
  }
  if(omode & OD_ONOLCK){
    cromode |= CR_ONOLCK;
    vlomode |= VL_ONOLCK;
  }
  if(omode & OD_OLCKNB){
    cromode |= CR_OLCKNB;
    vlomode |= VL_OLCKNB;
  }
  sprintf(docsname, "%s%c%s", name, MYPATHCHR, OD_DOCSNAME);
  sprintf(indexname, "%s%c%s", name, MYPATHCHR, OD_INDEXNAME);
  sprintf(rdocsname, "%s%c%s", name, MYPATHCHR, OD_RDOCSNAME);
  docsdb = NULL;
  indexdb = NULL;
  rdocsdb = NULL;
  if((omode & OD_OWRITER) && (omode & OD_OCREAT)){
    if(mkdir(name, OD_DIRMODE) == -1 && errno != EEXIST){
      dpecodeset(DP_EMKDIR, __FILE__, __LINE__);
      return NULL;
    }
  }
  if(lstat(name, &sbuf) == -1){
    dpecodeset(DP_ESTAT, __FILE__, __LINE__);
    return NULL;
  }
  inode = sbuf.st_ino;
  if(!(docsdb = cropen(docsname, cromode, docsbnum, OD_DOCSDNUM))) return NULL;
  if(!(indexdb = cropen(indexname, cromode, indexbnum, odindexdnum))){
    crclose(docsdb);
    return NULL;
  }
  if(omode & OD_OWRITER){
    if(!crsetalign(docsdb, OD_DOCSALIGN) || !crsetfbpsiz(docsdb, OD_DOCSFBP) ||
       !crsetalign(indexdb, OD_INDEXALIGN) || !crsetfbpsiz(indexdb, OD_INDEXFBP)){
      crclose(indexdb);
      crclose(docsdb);
      return NULL;
    }
  }
  if(!(rdocsdb = vlopen(rdocsname, vlomode, VL_CMPLEX))){
    crclose(indexdb);
    crclose(docsdb);
    return NULL;
  }
  vlsettuning(rdocsdb, OD_RDOCSLRM, OD_RDOCSNIM, OD_RDOCSLCN, OD_RDOCSNCN);
  if(omode & OD_OWRITER){
    cachemap = cbmapopenex(odcachebnum);
    sortmap = cbmapopenex(odcachebnum);
  } else {
    cachemap = NULL;
    sortmap = NULL;
  }
  if(vlrnum(rdocsdb) > 0){
    dmax = -1;
    dnum = -1;
    if((tmp = vlget(rdocsdb, OD_DMAXEXPR, sizeof(OD_DMAXEXPR), NULL)) != NULL){
      dmax = atoi(tmp);
      free(tmp);
    }
    if((tmp = vlget(rdocsdb, OD_DNUMEXPR, sizeof(OD_DNUMEXPR), NULL)) != NULL){
      dnum = atoi(tmp);
      free(tmp);
    }
    if(dmax < 0 || dnum < 0){
      if(sortmap) cbmapclose(sortmap);
      if(cachemap) cbmapclose(cachemap);
      vlclose(rdocsdb);
      crclose(indexdb);
      crclose(docsdb);
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      return NULL;
    }
  } else {
    dmax = 0;
    dnum = 0;
  }
  odeum = cbmalloc(sizeof(ODEUM));
  odeum->name = cbmemdup(name, -1);
  odeum->wmode = omode & OD_OWRITER;
  odeum->fatal = FALSE;
  odeum->inode = inode;
  odeum->docsdb = docsdb;
  odeum->indexdb = indexdb;
  odeum->rdocsdb = rdocsdb;
  odeum->cachemap = cachemap;
  odeum->cacheasiz = 0;
  odeum->sortmap = sortmap;
  odeum->dmax = dmax;
  odeum->dnum = dnum;
  odeum->ldid = -1;
  odsetcharclass(odeum, OD_SPACECHARS, OD_DELIMCHARS, OD_GLUECHARS);
  if(odotcb) odotcb(fname, odeum, "the connection was established");
  return odeum;
}


/* Flush the cache for dirty buffer of words.
   `odeum' specifies a database handle.
   `fname' specifies the name of caller function.
   If successful, the return value is true, else, it is false. */
static int odcacheflush(ODEUM *odeum, const char *fname){
  const char *kbuf, *vbuf;
  char otmsg[OD_OTCBBUFSIZ];
  int i, rnum, ksiz, vsiz;
  assert(odeum);
  if((rnum = cbmaprnum(odeum->cachemap)) < 1) return TRUE;
  if(odotcb) odotcb(fname, odeum, "flushing caches");
  cbmapiterinit(odeum->cachemap);
  for(i = 0; (kbuf = cbmapiternext(odeum->cachemap, &ksiz)) != NULL; i++){
    vbuf = cbmapget(odeum->cachemap, kbuf, ksiz, &vsiz);
    if(!crput(odeum->indexdb, kbuf, ksiz, vbuf, vsiz, CR_DCAT)){
      odeum->fatal = TRUE;
      return FALSE;
    }
    if(odotcb && (i + 1) % OD_OTPERWORDS == 0){
      sprintf(otmsg, "... (%d/%d)", i + 1, rnum);
      odotcb(fname, odeum, otmsg);
    }
  }
  cbmapclose(odeum->cachemap);
  odeum->cachemap = cbmapopenex(odcachebnum);
  odeum->cacheasiz = 0;
  return TRUE;
}


/* Flush all frequent words in the cache for dirty buffer of words.
   `odeum' specifies a database handle.
   `fname' specifies the name of caller function.
   `min' specifies the minimum size of frequent words.
   If successful, the return value is true, else, it is false. */
static int odcacheflushfreq(ODEUM *odeum, const char *fname, int min){
  const char *kbuf, *vbuf;
  char otmsg[OD_OTCBBUFSIZ];
  int rnum, ksiz, vsiz;
  assert(odeum);
  if((rnum = cbmaprnum(odeum->cachemap)) < 1) return TRUE;
  if(odotcb){
    sprintf(otmsg, "flushing frequent words: min=%d asiz=%d rnum=%d)",
            min, odeum->cacheasiz, rnum);
    odotcb(fname, odeum, otmsg);
  }
  cbmapiterinit(odeum->cachemap);
  while((kbuf = cbmapiternext(odeum->cachemap, &ksiz)) != NULL){
    vbuf = cbmapget(odeum->cachemap, kbuf, ksiz, &vsiz);
    if(vsiz >= sizeof(ODPAIR) * min){
      if(!crput(odeum->indexdb, kbuf, ksiz, vbuf, vsiz, CR_DCAT)){
        odeum->fatal = TRUE;
        return FALSE;
      }
      cbmapout(odeum->cachemap, kbuf, ksiz);
      odeum->cacheasiz -= vsiz;
    }
  }
  if(odotcb){
    sprintf(otmsg, "... (done): min=%d asiz=%d rnum=%d)",
            min, odeum->cacheasiz, cbmaprnum(odeum->cachemap));
    odotcb(fname, odeum, otmsg);
  }
  return TRUE;
}


/* Flush the half of rare words in the cache for dirty buffer of words.
   `odeum' specifies a database handle.
   `fname' specifies the name of caller function.
   `ratio' specifies the ratio of rare words.
   If successful, the return value is true, else, it is false. */
static int odcacheflushrare(ODEUM *odeum, const char *fname, double ratio){
  const char *kbuf, *vbuf;
  char otmsg[OD_OTCBBUFSIZ];
  int i, rnum, limit, ksiz, vsiz;
  assert(odeum);
  if((rnum = cbmaprnum(odeum->cachemap)) < 1) return TRUE;
  if(odotcb){
    sprintf(otmsg, "flushing rare words: ratio=%.2f asiz=%d rnum=%d)",
            ratio, odeum->cacheasiz, rnum);
    odotcb(fname, odeum, otmsg);
  }
  cbmapiterinit(odeum->cachemap);
  limit = (int)(rnum * ratio);
  for(i = 0; i < limit && (kbuf = cbmapiternext(odeum->cachemap, &ksiz)) != NULL; i++){
    vbuf = cbmapget(odeum->cachemap, kbuf, ksiz, &vsiz);
    if(!crput(odeum->indexdb, kbuf, ksiz, vbuf, vsiz, CR_DCAT)){
      odeum->fatal = TRUE;
      return FALSE;
    }
    cbmapout(odeum->cachemap, kbuf, ksiz);
    odeum->cacheasiz -= vsiz;
  }
  if(odotcb){
    sprintf(otmsg, "... (done): ratio=%.2f asiz=%d rnum=%d)",
            ratio, odeum->cacheasiz, cbmaprnum(odeum->cachemap));
    odotcb(fname, odeum, otmsg);
  }
  return TRUE;
}


/* Sort the records of inverted index.
   `odeum' specifies a database handle.
   `fname' specifies the name of caller function.
   If successful, the return value is true, else, it is false. */
static int odsortindex(ODEUM *odeum, const char *fname){
  const char *word;
  char *tmp, otmsg[OD_OTCBBUFSIZ];
  int i, rnum, wsiz, tsiz;
  ODPAIR *pairs;
  assert(odeum);
  if((rnum = cbmaprnum(odeum->sortmap)) < 1) return TRUE;
  if(odotcb) odotcb(fname, odeum, "sorting the inverted index");
  cbmapiterinit(odeum->sortmap);
  for(i = 0; (word = cbmapiternext(odeum->sortmap, &wsiz)) != NULL; i++){
    if((tmp = crget(odeum->indexdb, word, wsiz, 0, -1, &tsiz)) != NULL){
      if(tsiz > sizeof(ODPAIR)){
        pairs = (ODPAIR *)tmp;
        qsort(pairs, tsiz / sizeof(ODPAIR), sizeof(ODPAIR), odsortcompare);
        if(!crput(odeum->indexdb, word, wsiz, tmp, tsiz, CR_DOVER)){
          free(tmp);
          return FALSE;
        }
      }
      free(tmp);
    } else if(dpecode != DP_ENOITEM){
      return FALSE;
    }
    if(odotcb && (i + 1) % OD_OTPERWORDS == 0){
      sprintf(otmsg, "... (%d/%d)", i + 1, rnum);
      odotcb(fname, odeum, otmsg);
    }
  }
  cbmapclose(odeum->sortmap);
  odeum->sortmap = cbmapopenex(odcachebnum);
  return TRUE;
}


/* Compare two pairs of structures of a search result.
   `a' specifies the pointer to the region of one pair.
   `b' specifies the pointer to the region of the other pair.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
static int odsortcompare(const void *a, const void *b){
  ODPAIR *ap, *bp;
  int rv;
  assert(a && b);
  ap = (ODPAIR *)a;
  bp = (ODPAIR *)b;
  rv = bp->score - ap->score;
  if(rv != 0) return rv;
  return ap->id - bp->id;
}


/* Purge the elements of the deleted documents from the inverted index.
   `odeum' specifies a database handle.
   `fname' specifies the name of caller function.
   If successful, the return value is true, else, it is false. */
static int odpurgeindex(ODEUM *odeum, const char *fname){
  ODPAIR *pairs;
  char *kbuf, *vbuf, otmsg[OD_OTCBBUFSIZ];
  int i, rnum, tnum, ksiz, vsiz, pnum, wi;
  assert(odeum);
  if((rnum = crrnum(odeum->indexdb)) < 1) return TRUE;
  if(odotcb) odotcb(fname, odeum, "purging dispensable regions");
  if(!criterinit(odeum->indexdb)) return FALSE;
  tnum = 0;
  while(TRUE){
    if(!(kbuf = criternext(odeum->indexdb, &ksiz))){
      if(dpecode != DP_ENOITEM) return FALSE;
      break;
    }
    if(!(vbuf = crget(odeum->indexdb, kbuf, ksiz, 0, -1, &vsiz))){
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      free(kbuf);
      return FALSE;
    }
    pairs = (ODPAIR *)vbuf;
    pnum = vsiz / sizeof(ODPAIR);
    wi = 0;
    for(i = 0; i < pnum; i++){
      if(crvsiz(odeum->docsdb, (char *)&(pairs[i].id), sizeof(int)) != -1){
        pairs[wi++] = pairs[i];
      }
    }
    if(wi > 0){
      if(!crput(odeum->indexdb, kbuf, ksiz, vbuf, wi * sizeof(ODPAIR), CR_DOVER)){
        free(vbuf);
        free(kbuf);
        return FALSE;
      }
    } else {
      if(!crout(odeum->indexdb, kbuf, ksiz)){
        free(vbuf);
        free(kbuf);
        return FALSE;
      }
    }
    free(vbuf);
    free(kbuf);
    if(odotcb && (tnum + 1) % OD_OTPERWORDS == 0){
      sprintf(otmsg, "... (%d/%d)", tnum + 1, rnum);
      odotcb(fname, odeum, otmsg);
    }
    tnum++;
  }
  return TRUE;
}


/* Create a map of a document array.
   `pairs' specifies the pointer to a document array.
   `num' specifies the number of elements of the array.
   The return value is a map of the document array. */
static CBMAP *odpairsmap(const ODPAIR *pairs, int num){
  CBMAP *map;
  int i;
  assert(pairs && num >= 0);
  map = cbmapopen();
  for(i = 0; i < num; i++){
    cbmapput(map, (char *)&(pairs[i].id), sizeof(int),
             (char *)&(pairs[i].score), sizeof(int), TRUE);
  }
  return map;
}


/* compare two pairs of structures of words in a document.
   `a' specifies the pointer to the region of one word.
   `b' specifies the pointer to the region of the other word.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
static int odwordcompare(const void *a, const void *b){
  ODWORD *ap, *bp;
  int rv;
  assert(a && b);
  ap = (ODWORD *)a;
  bp = (ODWORD *)b;
  if((rv = bp->num - ap->num) != 0) return rv;
  if((rv = strlen(bp->word) - strlen(ap->word)) != 0) return rv;
  return strcmp(ap->word, bp->word);
}


/* Match an operator without taking it off the token list.
   `odeum' specifies a database handle.
   `tokens' specifies a list handle of tokens.
   The return value is whether the next token is an operator. */
static int odmatchoperator(ODEUM *odeum, CBLIST *tokens){
  const char *tk = NULL;
  int tk_len = 0;
  tk = cblistval(tokens, 0, &tk_len);
  if(tk && (tk[0] == '&' || tk[0] == '|' || tk[0] == '!')) return 1;
  return 0;
}


/* Implements the subexpr part of the grammar.
   `odeum' specifies a database handle.
   `tokens' specifies a list handle of tokens.
   `nwords' specifies a list handle of normalized words.
   `np' specifies the pointer to a variable to which the number of the elements of the return
   value is assigned.
   `errors' specifies a list handle into which error messages are stored.
   The return value is the pointer to an array of document IDs. */
static ODPAIR *odparsesubexpr(ODEUM *odeum, CBLIST *tokens, CBLIST *nwords, int *np,
                              CBLIST *errors){
  char *tk = NULL;
  int tk_len = 0;
  char *nword = NULL;  /* used to do the actual search, should match with tokens */
  ODPAIR *result = NULL;
  int result_num = 0;
  int i;
  double ival;
  if((tk = cblistshift(tokens, &tk_len)) != NULL){
    assert(tk != NULL);
    if(tk[0] == '('){
      free(tk);
      /* recurse into expr */
      result = odparseexpr(odeum, tokens, nwords, &result_num, errors);
      /* match right token RPAREN */
      tk = cblistshift(tokens, &tk_len);
      /* print an error if either we didn't get anything or we didn't get a ) */
      if(tk == NULL){
        if(errors) cblistpush(errors, "Expression ended without closing ')'", -1);
      } else if(tk[0] != ')'){
        if(errors) cblistpush(errors, "Un-balanced parenthesis.", -1);
      }
    } else if(odeum->statechars[*(unsigned char *)tk] == 0){
      /* Perform odsearch with the next norm word that isn't an operator. */
      nword = cblistshift(nwords, NULL);
      assert(nword != NULL);
      if((result = odsearch(odeum, nword, -1, &result_num)) != NULL){
        /* TF-IDF tuning */
        ival = odlogarithm(result_num);
        ival = (ival * ival) / 4.0;
        if(ival < 4.0) ival = 4.0;
        for(i = 0; i < result_num; i++){
          result[i].score = (int)(result[i].score / ival);
        }
      }
      free(nword);
    } else {
      if(errors) cblistpush(errors, "Invalid sub-expression.  Expected '(' or WORD.", -1);
      result = cbmalloc(1);
      result_num = 0;
    }
    /* done with the token */
    free(tk);
  }
  *np = result_num;
  return result;
}


/* Implements the actual recursive decent parser for the mini query language.
   `odeum' specifies a database handle.
   `tokens' specifies a list handle of tokens.
   `nwords' specifies a list handle of normalized words.
   `np' specifies the pointer to a variable to which the number of the elements of the return
   value is assigned.
   `errors' specifies a list handle into which error messages are stored.
   The return value is the pointer to an array of document IDs.
   It simply parses an initial subexpr, and then loops over as many (operator subexpr)
   sequences as it can find.  The odmatchoperator function handles injecting a default &
   between consecutive words. */
static ODPAIR *odparseexpr(ODEUM *odeum, CBLIST *tokens, CBLIST *nwords, int *np,
                           CBLIST *errors){
  ODPAIR *left = NULL;
  ODPAIR *right = NULL;
  ODPAIR *temp = NULL;
  int left_num = 0;
  int right_num = 0;
  int temp_num = 0;
  char *op = NULL;
  int op_len = 0;
  if(!(left = odparsesubexpr(odeum, tokens, nwords, &left_num, errors))) return NULL;
  /* expr ::= subexpr ( op subexpr )* */
  while(odmatchoperator(odeum, tokens)){
    op = cblistshift(tokens, &op_len);
    if(!(right = odparsesubexpr(odeum, tokens, nwords, &right_num, errors))){
      free(op);
      free(left);
      return NULL;
    }
    switch(op[0]){
    case '&':
      temp = odpairsand(left, left_num, right, right_num, &temp_num);
      break;
    case '|':
      temp = odpairsor(left, left_num, right, right_num, &temp_num);
      break;
    case '!':
      temp = odpairsnotand(left, left_num, right, right_num, &temp_num);
      break;
    default:
      if(errors) cblistpush(errors, "Invalid operator.  Expected '&', '|', or '!'.", -1);
      break;
    }
    if(temp){
      /* an operator was done so we must swap it with the left */
      free(left); left = NULL;
      left = temp;
      left_num = temp_num;
    }
    free(op);
    if(right) free(right);
  }
  *np = left_num;
  return left;
}


/* Processes the tokens in order to break them up further.
   `odeum' specifies a database handle.
   `tokens' specifies a list handle of tokens. */
static void odfixtokens(ODEUM *odeum, CBLIST *tokens){
  const char *tk = NULL;
  int tk_len = 0;
  int i = 0;
  int lastword = 0;
  for(i = 0; i < cblistnum(tokens); i++){
    tk = cblistval(tokens, i, &tk_len);
    assert(tk);
    if(tk[0] == '&' || tk[0] == '|' || tk[0] == '!' || tk[0] == '(' || tk[0] == ')'){
      lastword = 0;
      if(tk_len > 1){
        /* need to break it up for the next loop around */
        tk = cblistremove(tokens, i, &tk_len);
        cblistinsert(tokens, i, tk, 1);
        cblistinsert(tokens, i+1, tk+1, tk_len-1);
        free((char *)tk);
      }
    } else if(odeum->statechars[*(unsigned char *)tk] == 0){
      /* if the last one was a word and this is a word then we need a default & between them */
      if(lastword){
        cblistinsert(tokens, i, "&", 1);
        i++;
      }
      lastword = 1;
    }
  }
}


/* Cleans out the parts of the normalized word list that are not considered words.
   `odeum' specifies a database handle.
   `tokens' specifies a list handle of tokens. */
static void odcleannormalized(ODEUM *odeum, CBLIST *nwords){
  char *tk = NULL;
  int tk_len = 0;
  int i = 0;
  for(i = 0; i < cblistnum(nwords); i++){
    tk = (char *)cblistval(nwords, i, &tk_len);
    if(tk_len == 0 || (!odeum->statechars[*(unsigned char *)tk] == 0)){
      /* not a word so delete it */
      tk = cblistremove(nwords, i, &tk_len);
      free(tk);
      i--;
    }
  }
}



/* END OF FILE */
