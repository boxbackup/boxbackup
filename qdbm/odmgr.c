/*************************************************************************************************
 * Utility for debugging Odeum and its applications
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


#include <depot.h>
#include <cabin.h>
#include <odeum.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#undef TRUE
#define TRUE           1                 /* boolean true */
#undef FALSE
#define FALSE          0                 /* boolean false */

#define MAXSRCHWORDS   256               /* max number of search words */
#define WOCCRPOINT     10000             /* points per occurence */
#define MAXKEYWORDS    8                 /* max number of keywords */
#define SUMMARYWIDTH   16                /* width of each phrase in a summary */
#define MAXSUMMARY     128               /* max number of words in a summary */


/* for RISC OS */
#if defined(__riscos__) || defined(__riscos)
#include <unixlib/local.h>
int __riscosify_control = __RISCOSIFY_NO_PROCESS;
#endif


/* global variables */
const char *progname;                    /* program name */


/* function prototypes */
int main(int argc, char **argv);
void usage(void);
char *readstdin(int *sp);
void otcb(const char *fname, ODEUM *odeum, const char *msg);
int runcreate(int argc, char **argv);
int runput(int argc, char **argv);
int runout(int argc, char **argv);
int runget(int argc, char **argv);
int runsearch(int argc, char **argv);
int runlist(int argc, char **argv);
int runoptimize(int argc, char **argv);
int runinform(int argc, char **argv);
int runmerge(int argc, char **argv);
int runremove(int argc, char **argv);
int runbreak(int argc, char **argv);
void pdperror(const char *name);
void printdoc(const ODDOC *doc, int tb, int hb, int score, ODEUM *odeum, const CBLIST *skeys);
char *docsummary(const ODDOC *doc, const CBLIST *kwords, int num, int hilight);
CBMAP *listtomap(const CBLIST *list);
int docreate(const char *name);
int doput(const char *name, const char *text, const char *uri, const char *title,
          const char *author, const char *date, int wmax, int keep);
int doout(const char *name, const char *uri, int id);
int doget(const char *name, const char *uri, int id, int tb, int hb);
int dosearch(const char *name, const char *text, int max, int or, int idf, int ql,
             int tb, int hb, int nb);
int dolist(const char *name, int tb, int hb);
int dooptimize(const char *name);
int doinform(const char *name);
int domerge(const char *name, const CBLIST *elems);
int doremove(const char *name);
int dobreak(const char *text, int hb, int kb, int sb);


/* main routine */
int main(int argc, char **argv){
  char *env;
  int rv;
  cbstdiobin();
  progname = argv[0];
  if((env = getenv("QDBMDBGFD")) != NULL) dpdbgfd = atoi(env);
  if(argc < 2) usage();
  rv = 0;
  if(!strcmp(argv[1], "create")){
    odsetotcb(otcb);
    rv = runcreate(argc, argv);
  } else if(!strcmp(argv[1], "put")){
    odsetotcb(otcb);
    rv = runput(argc, argv);
  } else if(!strcmp(argv[1], "out")){
    odsetotcb(otcb);
    rv = runout(argc, argv);
  } else if(!strcmp(argv[1], "get")){
    rv = runget(argc, argv);
  } else if(!strcmp(argv[1], "search")){
    rv = runsearch(argc, argv);
  } else if(!strcmp(argv[1], "list")){
    rv = runlist(argc, argv);
  } else if(!strcmp(argv[1], "optimize")){
    odsetotcb(otcb);
    rv = runoptimize(argc, argv);
  } else if(!strcmp(argv[1], "inform")){
    rv = runinform(argc, argv);
  } else if(!strcmp(argv[1], "merge")){
    odsetotcb(otcb);
    rv = runmerge(argc, argv);
  } else if(!strcmp(argv[1], "remove")){
    rv = runremove(argc, argv);
  } else if(!strcmp(argv[1], "break")){
    rv = runbreak(argc, argv);
  } else if(!strcmp(argv[1], "version") || !strcmp(argv[1], "--version")){
    printf("Powered by QDBM version %s\n", dpversion);
    printf("Copyright (c) 2000-2007 Mikio Hirabayashi\n");
    rv = 0;
  } else {
    usage();
  }
  return rv;
}


/* print the usage and exit */
void usage(void){
  fprintf(stderr, "%s: administration utility for Odeum\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s create name\n", progname);
  fprintf(stderr, "  %s put [-uri str] [-title str] [-author str] [-date str]"
          " [-wmax num] [-keep] name [file]\n", progname);
  fprintf(stderr, "  %s out [-id] name expr\n", progname);
  fprintf(stderr, "  %s get [-id] [-t|-h] name expr\n", progname);
  fprintf(stderr, "  %s search [-max num] [-or] [-idf] [-t|-h|-n] name words...\n", progname);
  fprintf(stderr, "  %s list [-t|-h] name\n", progname);
  fprintf(stderr, "  %s optimize name\n", progname);
  fprintf(stderr, "  %s inform name\n", progname);
  fprintf(stderr, "  %s merge name elems...\n", progname);
  fprintf(stderr, "  %s remove name\n", progname);
  fprintf(stderr, "  %s break [-h|-k|-s] [file]\n", progname);
  fprintf(stderr, "  %s version\n", progname);
  fprintf(stderr, "\n");
  exit(1);
}


/* read the standard input */
char *readstdin(int *sp){
  char *buf;
  int i, blen, c;
  blen = 256;
  buf = cbmalloc(blen);
  for(i = 0; (c = getchar()) != EOF; i++){
    if(i >= blen - 1) buf = cbrealloc(buf, blen *= 2);
    buf[i] = c;
  }
  buf[i] = '\0';
  *sp = i;
  return buf;
}


/* report the outturn */
void otcb(const char *fname, ODEUM *odeum, const char *msg){
  char *name;
  name = odname(odeum);
  printf("%s: %s: %s: %s\n", progname, fname, name, msg);
  free(name);
}


/* parse arguments of create command */
int runcreate(int argc, char **argv){
  char *name;
  int i, rv;
  name = NULL;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      usage();
    } else if(!name){
      name = argv[i];
    } else {
      usage();
    }
  }
  if(!name) usage();
  rv = docreate(name);
  return rv;
}


/* parse arguments of put command */
int runput(int argc, char **argv){
  char *name, *file, *uri, *title, *author, *date, *text;
  int i, wmax, keep, size, rv;
  name = NULL;
  file = NULL;
  uri = NULL;
  title = NULL;
  author = NULL;
  date = NULL;
  wmax = -1;
  keep = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-uri")){
        if(++i >= argc) usage();
        uri = argv[i];
      } else if(!strcmp(argv[i], "-uri")){
        if(++i >= argc) usage();
        uri = argv[i];
      } else if(!strcmp(argv[i], "-title")){
        if(++i >= argc) usage();
        title = argv[i];
      } else if(!strcmp(argv[i], "-author")){
        if(++i >= argc) usage();
        author = argv[i];
      } else if(!strcmp(argv[i], "-date")){
        if(++i >= argc) usage();
        date = argv[i];
      } else if(!strcmp(argv[i], "-wmax")){
        if(++i >= argc) usage();
        wmax = atoi(argv[i]);
      } else if(!strcmp(argv[i], "-keep")){
        keep = TRUE;
      } else {
        usage();
      }
    } else if(!name){
      name = argv[i];
    } else if(!file){
      file = argv[i];
    } else {
      usage();
    }
  }
  if(!name) usage();
  if(!uri) uri = file;
  if(!uri) usage();
  if(file){
    if(!(text = cbreadfile(file, &size))){
      fprintf(stderr, "%s: %s: cannot open\n", progname, file);
      return 1;
    }
  } else {
    text = readstdin(&size);
  }
  rv = doput(name, text, uri, title, author, date, wmax, keep);
  free(text);
  return rv;
}


/* parse arguments of out command */
int runout(int argc, char **argv){
  char *name, *expr;
  int i, ib, id, rv;
  name = NULL;
  expr = NULL;
  ib = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-id")){
        ib = TRUE;
      } else {
        usage();
      }
    } else if(!name){
      name = argv[i];
    } else if(!expr){
      expr = argv[i];
    } else {
      usage();
    }
  }
  if(!name || !expr) usage();
  id = -1;
  if(ib){
    id = atoi(expr);
    if(id < 1) usage();
  }
  rv = doout(name, expr, id);
  return rv;
}


/* parse arguments of get command */
int runget(int argc, char **argv){
  char *name, *expr;
  int i, ib, tb, hb, id, rv;
  name = NULL;
  expr = NULL;
  ib = FALSE;
  tb = FALSE;
  hb = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-id")){
        ib = TRUE;
      } else if(!strcmp(argv[i], "-t")){
        tb = TRUE;
      } else if(!strcmp(argv[i], "-h")){
        hb = TRUE;
      } else {
        usage();
      }
    } else if(!name){
      name = argv[i];
    } else if(!expr){
      expr = argv[i];
    } else {
      usage();
    }
  }
  if(!name || !expr) usage();
  id = -1;
  if(ib){
    id = atoi(expr);
    if(id < 1) usage();
  }
  rv = doget(name, expr, id, tb, hb);
  return rv;
}


/* parse arguments of search command */
int runsearch(int argc, char **argv){
  char *name, *srchwords[MAXSRCHWORDS];
  int i, wnum, max, or, idf, ql, tb, hb, nb, rv;
  CBDATUM *text;
  name = NULL;
  wnum = 0;
  max = -1;
  or = FALSE;
  idf = FALSE;
  ql = FALSE;
  tb = FALSE;
  hb = FALSE;
  nb = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-max")){
        if(++i >= argc) usage();
        max = atoi(argv[i]);
      } else if(!strcmp(argv[i], "-or")){
        or = TRUE;
      } else if(!strcmp(argv[i], "-idf")){
        idf = TRUE;
      } else if(!strcmp(argv[i], "-ql")){
        ql = TRUE;
      } else if(!strcmp(argv[i], "-t")){
        tb = TRUE;
      } else if(!strcmp(argv[i], "-h")){
        hb = TRUE;
      } else if(!strcmp(argv[i], "-n")){
        nb = TRUE;
      } else {
        usage();
      }
    } else if(!name){
      name = argv[i];
    } else if(wnum < MAXSRCHWORDS){
      srchwords[wnum++] = argv[i];
    }
  }
  if(!name) usage();
  text = cbdatumopen(NULL, -1);
  for(i = 0; i < wnum; i++){
    if(i > 0) cbdatumcat(text, " ", 1);
    cbdatumcat(text, srchwords[i], -1);
  }
  rv = dosearch(name, cbdatumptr(text), max, or, idf, ql, tb, hb, nb);
  cbdatumclose(text);
  return rv;
}


/* parse arguments of list command */
int runlist(int argc, char **argv){
  char *name;
  int i, tb, hb, rv;
  name = NULL;
  tb = FALSE;
  hb = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-t")){
        tb = TRUE;
      } else if(!strcmp(argv[i], "-h")){
        hb = TRUE;
      } else {
        usage();
      }
    } else if(!name){
      name = argv[i];
    } else {
      usage();
    }
  }
  if(!name) usage();
  rv = dolist(name, tb, hb);
  return rv;
}


/* parse arguments of optimize command */
int runoptimize(int argc, char **argv){
  char *name;
  int i, rv;
  name = NULL;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      usage();
    } else if(!name){
      name = argv[i];
    } else {
      usage();
    }
  }
  if(!name) usage();
  rv = dooptimize(name);
  return rv;
}


/* parse arguments of inform command */
int runinform(int argc, char **argv){
  char *name;
  int i, rv;
  name = NULL;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      usage();
    } else if(!name){
      name = argv[i];
    } else {
      usage();
    }
  }
  if(!name) usage();
  rv = doinform(name);
  return rv;
}


/* parse arguments of merge command */
int runmerge(int argc, char **argv){
  char *name;
  CBLIST *elems;
  int i, rv;
  name = NULL;
  elems = cblistopen();
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      usage();
    } else if(!name){
      name = argv[i];
    } else {
      cblistpush(elems, argv[i], -1);
    }
  }
  if(!name) usage();
  if(cblistnum(elems) < 1){
    cblistclose(elems);
    usage();
  }
  rv = domerge(name, elems);
  cblistclose(elems);
  return rv;
}


/* parse arguments of remove command */
int runremove(int argc, char **argv){
  char *name;
  int i, rv;
  name = NULL;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      usage();
    } else if(!name){
      name = argv[i];
    } else {
      usage();
    }
  }
  if(!name) usage();
  rv = doremove(name);
  return rv;
}


/* parse arguments of break command */
int runbreak(int argc, char **argv){
  char *file, *text;
  int i, hb, kb, sb, size, rv;
  file = NULL;
  hb = FALSE;
  kb = FALSE;
  sb = FALSE;
  for(i = 2; i < argc; i++){
    if(!file && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-h")){
        hb = TRUE;
      } else if(!strcmp(argv[i], "-k")){
        kb = TRUE;
      } else if(!strcmp(argv[i], "-s")){
        sb = TRUE;
      } else {
        usage();
      }
    } else if(!file){
      file = argv[i];
    } else {
      usage();
    }
  }
  if(file){
    if(!(text = cbreadfile(file, &size))){
      fprintf(stderr, "%s: %s: cannot open\n", progname, file);
      return 1;
    }
  } else {
    text = readstdin(&size);
  }
  rv = dobreak(text, hb, kb, sb);
  free(text);
  return rv;
}


/* print an error message */
void pdperror(const char *name){
  fprintf(stderr, "%s: %s: %s\n", progname, name, dperrmsg(dpecode));
}


/* print the contents of a document */
void printdoc(const ODDOC *doc, int tb, int hb, int score, ODEUM *odeum, const CBLIST *skeys){
  const CBLIST *words;
  CBMAP *scores;
  CBLIST *kwords;
  const char *title, *author, *word, *date;
  char *summary;
  int i, wsiz;
  title = oddocgetattr(doc, "title");
  author = oddocgetattr(doc, "author");
  date = oddocgetattr(doc, "date");
  if(hb){
    printf("ID: %d\n", oddocid(doc));
    printf("URI: %s\n", oddocuri(doc));
    if(title) printf("TITLE: %s\n", title);
    if(author) printf("AUTHOR: %s\n", author);
    if(date) printf("DATE: %s\n", date);
    if(score >= 0) printf("SCORE: %d\n", score);
    scores = oddocscores(doc, MAXKEYWORDS, odeum);
    kwords = cblistopen();
    printf("KEYWORDS: ");
    cbmapiterinit(scores);
    while((word = cbmapiternext(scores, &wsiz)) != NULL){
      if(cblistnum(kwords) > 0) printf(", ");
      printf("%s (%s)", word, cbmapget(scores, word, wsiz, NULL));
      cblistpush(kwords, word, wsiz);
    }
    putchar('\n');
    summary = docsummary(doc, skeys ? skeys : kwords, MAXSUMMARY, skeys != NULL);
    printf("SUMMARY: %s\n", summary);
    free(summary);
    cblistclose(kwords);
    cbmapclose(scores);
    printf("\n\n");
  } else if(tb){
    printf("%d\t%s\t%s\t%s\t%s\t%d\n", oddocid(doc), oddocuri(doc),
           title ? title : "", author ? author : "", date ? date : "", score);
    words = oddocnwords(doc);
    for(i = 0; i < cblistnum(words); i++){
      word = cblistval(words, i, &wsiz);
      if(i > 0) putchar('\t');
      printf("%s", word);
    }
    putchar('\n');
    words = oddocawords(doc);
    for(i = 0; i < cblistnum(words); i++){
      word = cblistval(words, i, &wsiz);
      if(i > 0) putchar('\t');
      printf("%s", word);
    }
    putchar('\n');
  } else {
    printf("%d\t%s\t%d\n", oddocid(doc), oddocuri(doc), score);
  }
}


/* get a list handle contains summary of a document */
char *docsummary(const ODDOC *doc, const CBLIST *kwords, int num, int hilight){
  const CBLIST *nwords, *awords;
  CBMAP *kmap, *map;
  const char *normal, *asis;
  char *sbuf;
  int i, j, bsiz, ssiz, lnum, nwsiz, awsiz, pv, bi, first;
  bsiz = 256;
  sbuf = cbmalloc(bsiz);
  ssiz = 0;
  nwords = oddocnwords(doc);
  awords = oddocawords(doc);
  kmap = listtomap(kwords);
  map = listtomap(kwords);
  lnum = cblistnum(nwords);
  first = TRUE;
  for(i = 0; i < lnum && i < SUMMARYWIDTH; i++){
    normal = cblistval(nwords, i, &nwsiz);
    asis = cblistval(awords, i, &awsiz);
    if(awsiz < 1) continue;
    cbmapout(map, normal, nwsiz);
    if(ssiz + awsiz + 16 >= bsiz){
      bsiz = bsiz * 2 + awsiz;
      sbuf = cbrealloc(sbuf, bsiz);
    }
    if(!first) ssiz += sprintf(sbuf + ssiz, " ");
    if(hilight && normal[0] != '\0' && cbmapget(kmap, normal, nwsiz, NULL)){
      ssiz += sprintf(sbuf + ssiz, "<<%s>>", asis);
    } else {
      ssiz += sprintf(sbuf + ssiz, "%s", asis);
    }
    first = FALSE;
    num--;
  }
  ssiz += sprintf(sbuf + ssiz, " ...");
  pv = i;
  while(i < lnum){
    if(cbmaprnum(map) < 1){
      cbmapclose(map);
      map = listtomap(kwords);
    }
    normal = cblistval(nwords, i, &nwsiz);
    if(cbmapget(map, normal, nwsiz, NULL)){
      bi = i - SUMMARYWIDTH / 2;
      bi = bi > pv ? bi : pv;
      for(j = bi; j < lnum && j <= bi + SUMMARYWIDTH; j++){
        normal = cblistval(nwords, j, &nwsiz);
        asis = cblistval(awords, j, &awsiz);
        if(awsiz < 1) continue;
        cbmapout(map, normal, nwsiz);
        if(ssiz + awsiz + 16 >= bsiz){
          bsiz = bsiz * 2 + awsiz;
          sbuf = cbrealloc(sbuf, bsiz);
        }
        ssiz += sprintf(sbuf + ssiz, " ");
        if(hilight && normal[0] != '\0' && cbmapget(kmap, normal, nwsiz, NULL)){
          ssiz += sprintf(sbuf + ssiz, "<<%s>>", asis);
        } else {
          ssiz += sprintf(sbuf + ssiz, "%s", asis);
        }
        num--;
      }
      ssiz += sprintf(sbuf + ssiz, " ...");
      i = j;
      pv = i;
    } else {
      i++;
    }
    if(num <= 0) break;
  }
  cbmapclose(map);
  cbmapclose(kmap);
  return sbuf;
}


/* get a map made from a list */
CBMAP *listtomap(const CBLIST *list){
  CBMAP *map;
  const char *tmp;
  int i, tsiz;
  map = cbmapopen();
  for(i = 0; i < cblistnum(list); i++){
    tmp = cblistval(list, i, &tsiz);
    cbmapput(map, tmp, tsiz, "", 0, FALSE);
  }
  return map;
}


/* perform create command */
int docreate(const char *name){
  ODEUM *odeum;
  if(!(odeum = odopen(name, OD_OWRITER | OD_OCREAT | OD_OTRUNC))){
    pdperror(name);
    return 1;
  }
  if(!odclose(odeum)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform put command */
int doput(const char *name, const char *text, const char *uri, const char *title,
          const char *author, const char *date, int wmax, int keep){
  ODEUM *odeum;
  ODDOC *doc;
  CBLIST *awords;
  const char *asis;
  char *normal;
  int i;
  if(!(odeum = odopen(name, OD_OWRITER))){
    pdperror(name);
    return 1;
  }
  doc = oddocopen(uri);
  if(title) oddocaddattr(doc, "title", title);
  if(author) oddocaddattr(doc, "author", author);
  if(date) oddocaddattr(doc, "date", date);
  awords = odbreaktext(text);
  for(i = 0; i < cblistnum(awords); i++){
    asis = cblistval(awords, i, NULL);
    normal = odnormalizeword(asis);
    oddocaddword(doc, normal, asis);
    free(normal);
  }
  cblistclose(awords);
  if(!odput(odeum, doc, wmax, keep ? FALSE : TRUE)){
    pdperror(name);
    oddocclose(doc);
    odclose(odeum);
    return 1;
  }
  oddocclose(doc);
  if(!odclose(odeum)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform out command */
int doout(const char *name, const char *uri, int id){
  ODEUM *odeum;
  if(!(odeum = odopen(name, OD_OWRITER))){
    pdperror(name);
    return 1;
  }
  if(id > 0){
    if(!odoutbyid(odeum, id)){
      pdperror(name);
      odclose(odeum);
      return 1;
    }
  } else {
    if(!odout(odeum, uri)){
      pdperror(name);
      odclose(odeum);
      return 1;
    }
  }
  if(!odclose(odeum)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform get command */
int doget(const char *name, const char *uri, int id, int tb, int hb){
  ODEUM *odeum;
  ODDOC *doc;
  if(!(odeum = odopen(name, OD_OREADER))){
    pdperror(name);
    return 1;
  }
  if(id > 0){
    if(!(doc = odgetbyid(odeum, id))){
      pdperror(name);
      odclose(odeum);
      return 1;
    }
  } else {
    if(!(doc = odget(odeum, uri))){
      pdperror(name);
      odclose(odeum);
      return 1;
    }
  }
  printdoc(doc, tb, hb, -1, odeum, NULL);
  oddocclose(doc);
  if(!odclose(odeum)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform search command */
int dosearch(const char *name, const char *text, int max, int or, int idf, int ql,
             int tb, int hb, int nb){
  ODEUM *odeum;
  CBLIST *awords, *nwords, *uris, *hits;
  ODPAIR *pairs, *last, *tmp;
  ODDOC *doc;
  const char *asis;
  char *normal, numbuf[32];
  int i, j, pnum, lnum, hnum, tnum, shows;
  double ival;
  if(!(odeum = odopen(name, OD_OREADER))){
    pdperror(name);
    return 1;
  }
  awords = odbreaktext(text);
  nwords = cblistopen();
  uris = cblistopen();
  hits = cblistopen();
  last = NULL;
  lnum = 0;
  if(ql){
    last= odquery(odeum, text, &lnum, NULL);
  } else {
    for(i = 0; i < cblistnum(awords); i++){
      asis = cblistval(awords, i, NULL);
      normal = odnormalizeword(asis);
      cblistpush(nwords, normal, -1);
      if(strlen(normal) < 1){
        free(normal);
        continue;
      }
      if(!(pairs = odsearch(odeum, normal, or ? max : -1, &pnum))){
        pdperror(name);
        free(normal);
        continue;
      }
      if((hnum = odsearchdnum(odeum, normal)) < 0) hnum = 0;
      if(idf){
        ival = odlogarithm(hnum);
        ival = (ival * ival) / 4.0;
        if(ival < 4.0) ival = 4.0;
        for(j = 0; j < pnum; j++){
          pairs[j].score = (int)(pairs[j].score / ival);
        }
      }
      cblistpush(uris, normal, -1);
      sprintf(numbuf, "%d", hnum);
      cblistpush(hits, numbuf, -1);
      if(last){
        if(or){
          tmp = odpairsor(last, lnum, pairs, pnum, &tnum);
        } else {
          tmp = odpairsand(last, lnum, pairs, pnum, &tnum);
        }
        free(last);
        free(pairs);
        last = tmp;
        lnum = tnum;
      } else {
        last = pairs;
        lnum = pnum;
      }
      free(normal);
    }
  }
  if(hb){
    printf("TOTAL: %d\n", lnum);
    printf("EACHWORD: ");
  } else {
    printf("%d", lnum);
  }
  for(i = 0; i < cblistnum(uris); i++){
    if(hb){
      if(i > 0) printf(", ");
      printf("%s(%s)", cblistval(uris, i, NULL), cblistval(hits, i, NULL));
    } else {
      printf("\t%s\t%s", cblistval(uris, i, NULL), cblistval(hits, i, NULL));
    }
  }
  putchar('\n');
  if(hb) putchar('\n');
  if(last){
    if(max < 0) max = lnum;
    shows = 0;
    for(i = 0; i < lnum && shows < max; i++){
      if(nb){
        printf("%d\t%d\n", last[i].id, last[i].score);
        shows++;
      } else {
        if(!(doc = odgetbyid(odeum, last[i].id))) continue;
        printdoc(doc, tb, hb, last[i].score, odeum, nwords);
        oddocclose(doc);
        shows++;
      }
    }
    free(last);
  }
  cblistclose(uris);
  cblistclose(hits);
  cblistclose(nwords);
  cblistclose(awords);
  if(!odclose(odeum)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform list command */
int dolist(const char *name, int tb, int hb){
  ODEUM *odeum;
  ODDOC *doc;
  if(!(odeum = odopen(name, OD_OREADER))){
    pdperror(name);
    return 1;
  }
  if(!oditerinit(odeum)){
    odclose(odeum);
    pdperror(name);
    return 1;
  }
  while(TRUE){
    if(!(doc = oditernext(odeum))){
      if(dpecode == DP_ENOITEM) break;
      odclose(odeum);
      pdperror(name);
      return 1;
    }
    printdoc(doc, tb, hb, -1, odeum, NULL);
    oddocclose(doc);
  }
  if(!odclose(odeum)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform optimize command */
int dooptimize(const char *name){
  ODEUM *odeum;
  if(!(odeum = odopen(name, OD_OWRITER))){
    pdperror(name);
    return 1;
  }
  if(!odoptimize(odeum)){
    pdperror(name);
    odclose(odeum);
    return 1;
  }
  if(!odclose(odeum)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform inform command */
int doinform(const char *name){
  ODEUM *odeum;
  char *tmp;
  if(!(odeum = odopen(name, OD_OREADER))){
    pdperror(name);
    return 1;
  }
  tmp = odname(odeum);
  printf("name: %s\n", tmp ? tmp : "(null)");
  free(tmp);
  printf("file size: %.0f\n", odfsiz(odeum));
  printf("index buckets: %d\n", odbnum(odeum));
  printf("used buckets: %d\n", odbusenum(odeum));
  printf("all documents: %d\n", oddnum(odeum));
  printf("all words: %d\n", odwnum(odeum));
  printf("inode number: %d\n", odinode(odeum));
  printf("modified time: %.0f\n", (double)odmtime(odeum));
  if(!odclose(odeum)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform merge command */
int domerge(const char *name, const CBLIST *elems){
  if(!odmerge(name, elems)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform remove command */
int doremove(const char *name){
  if(!odremove(name)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform break command */
int dobreak(const char *text, int hb, int kb, int sb){
  CBLIST *awords, *kwords;
  CBMAP *scores;
  ODDOC *doc;
  const char *asis;
  char *normal, *summary;
  int i, first;
  awords = odbreaktext(text);
  if(kb || sb){
    doc = oddocopen("");
    for(i = 0; i < cblistnum(awords); i++){
      asis = cblistval(awords, i, NULL);
      normal = odnormalizeword(asis);
      oddocaddword(doc, normal, asis);
      free(normal);
    }
    scores = oddocscores(doc, MAXKEYWORDS, NULL);
    cbmapiterinit(scores);
    kwords = cbmapkeys(scores);
    if(kb){
      for(i = 0; i < cblistnum(kwords); i++){
        if(i > 0) putchar('\t');
        printf("%s", cblistval(kwords, i, NULL));
      }
      putchar('\n');
    } else {
      summary = docsummary(doc, kwords, MAXSUMMARY, FALSE);
      printf("%s\n", summary);
      free(summary);
    }
    cblistclose(kwords);
    cbmapclose(scores);
    oddocclose(doc);
  } else if(hb){
    printf("NWORDS: ");
    first = TRUE;
    for(i = 0; i < cblistnum(awords); i++){
      asis = cblistval(awords, i, NULL);
      normal = odnormalizeword(asis);
      if(normal[0] == '\0'){
        free(normal);
        continue;
      }
      if(!first) putchar(' ');
      first = FALSE;
      printf("%s", normal);
      free(normal);
    }
    putchar('\n');
    printf("AWORDS: ");
    first = TRUE;
    for(i = 0; i < cblistnum(awords); i++){
      asis = cblistval(awords, i, NULL);
      if(asis[0] == '\0') continue;
      if(!first) putchar(' ');
      first = FALSE;
      printf("%s", asis);
    }
    putchar('\n');
  } else {
    for(i = 0; i < cblistnum(awords); i++){
      asis = cblistval(awords, i, NULL);
      normal = odnormalizeword(asis);
      printf("%s\t%s\n", normal, asis);
      free(normal);
    }
  }
  cblistclose(awords);
  return 0;
}



/* END OF FILE */
