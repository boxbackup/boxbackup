/*************************************************************************************************
 * Test cases of Odeum
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
#include <stdarg.h>
#include <limits.h>
#include <time.h>

#undef TRUE
#define TRUE           1                 /* boolean true */
#undef FALSE
#define FALSE          0                 /* boolean false */

#define DOCBUFSIZ      256               /* buffer for documents */


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
int runwrite(int argc, char **argv);
int runread(int argc, char **argv);
int runcombo(int argc, char **argv);
int runwicked(int argc, char **argv);
int printfflush(const char *format, ...);
void pdperror(const char *name);
int myrand(void);
ODDOC *makedoc(int id, int wnum, int pnum);
int dowrite(const char *name, int dnum, int wnum, int pnum,
            int ibnum, int idnum, int cbnum, int csiz);
int doread(const char *name);
int docombo(const char *name);
int dowicked(const char *name, int dnum);


/* main routine */
int main(int argc, char **argv){
  char *env;
  int rv;
  cbstdiobin();
  if((env = getenv("QDBMDBGFD")) != NULL) dpdbgfd = atoi(env);
  progname = argv[0];
  if(argc < 2) usage();
  rv = 0;
  if(!strcmp(argv[1], "write")){
    rv = runwrite(argc, argv);
  } else if(!strcmp(argv[1], "read")){
    rv = runread(argc, argv);
  } else if(!strcmp(argv[1], "combo")){
    rv = runcombo(argc, argv);
  } else if(!strcmp(argv[1], "wicked")){
    rv = runwicked(argc, argv);
  } else {
    usage();
  }
  return 0;
}


/* print the usage and exit */
void usage(void){
  fprintf(stderr, "%s: test cases for Odeum\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s write [-tune ibnum idnum cbnum csiz] name dnum wnum pnum\n", progname);
  fprintf(stderr, "  %s read name\n", progname);
  fprintf(stderr, "  %s combo name\n", progname);
  fprintf(stderr, "  %s wicked name dnum\n", progname);
  fprintf(stderr, "\n");
  exit(1);
}


/* parse arguments of write command */
int runwrite(int argc, char **argv){
  char *name, *dstr, *wstr, *pstr;
  int i, dnum, wnum, pnum, ibnum, idnum, cbnum, csiz, rv;
  name = NULL;
  dstr = NULL;
  wstr = NULL;
  pstr = NULL;
  dnum = 0;
  wnum = 0;
  pnum = 0;
  ibnum = -1;
  idnum = -1;
  cbnum = -1;
  csiz = -1;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-tune")){
        if(++i >= argc) usage();
        ibnum = atoi(argv[i]);
        if(++i >= argc) usage();
        idnum = atoi(argv[i]);
        if(++i >= argc) usage();
        cbnum = atoi(argv[i]);
        if(++i >= argc) usage();
        csiz = atoi(argv[i]);
      } else {
        usage();
      }
    } else if(!name){
      name = argv[i];
    } else if(!dstr){
      dstr = argv[i];
    } else if(!wstr){
      wstr = argv[i];
    } else if(!pstr){
      pstr = argv[i];
    } else {
      usage();
    }
  }
  if(!name || !dstr || !wstr || !pstr) usage();
  dnum = atoi(dstr);
  wnum = atoi(wstr);
  pnum = atoi(pstr);
  if(dnum < 1 || wnum < 1 || pnum < 1) usage();
  rv = dowrite(name, dnum, wnum, pnum, ibnum, idnum, cbnum, csiz);
  return rv;
}


/* parse arguments of read command */
int runread(int argc, char **argv){
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
  rv = doread(name);
  return rv;
}


/* parse arguments of combo command */
int runcombo(int argc, char **argv){
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
  rv = docombo(name);
  return rv;
}


/* parse arguments of wicked command */
int runwicked(int argc, char **argv){
  char *name, *dstr;
  int i, dnum, rv;
  name = NULL;
  dstr = NULL;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      usage();
    } else if(!name){
      name = argv[i];
    } else if(!dstr){
      dstr = argv[i];
    } else {
      usage();
    }
  }
  if(!name || !dstr) usage();
  dnum = atoi(dstr);
  if(dnum < 1) usage();
  rv = dowicked(name, dnum);
  return rv;
}


/* print formatted string and flush the buffer */
int printfflush(const char *format, ...){
  va_list ap;
  int rv;
  va_start(ap, format);
  rv = vprintf(format, ap);
  if(fflush(stdout) == EOF) rv = -1;
  va_end(ap);
  return rv;
}


/* print an error message */
void pdperror(const char *name){
  fprintf(stderr, "%s: %s: %s\n", progname, name, dperrmsg(dpecode));
}


/* pseudo random number generator */
int myrand(void){
  static int cnt = 0;
  if(cnt == 0) srand(time(NULL));
  return (rand() * rand() + (rand() >> (sizeof(int) * 4)) + (cnt++)) & INT_MAX;
}


/* create a document */
ODDOC *makedoc(int id, int wnum, int pnum){
  ODDOC *doc;
  char buf[DOCBUFSIZ];
  int i;
  sprintf(buf, "%08d", id);
  doc = oddocopen(buf);
  oddocaddattr(doc, "title", buf);
  oddocaddattr(doc, "author", buf);
  oddocaddattr(doc, "date", buf);
  for(i = 0; i < wnum; i++){
    sprintf(buf, "%08d", myrand() % pnum);
    oddocaddword(doc, buf, buf);
  }
  return doc;
}


/* perform write command */
int dowrite(const char *name, int dnum, int wnum, int pnum,
            int ibnum, int idnum, int cbnum, int csiz){
  ODEUM *odeum;
  ODDOC *doc;
  int i, err;
  printfflush("<Writing Test>\n  name=%s  dnum=%d  wnum=%d  pnum=%d"
              "  ibnum=%d  idnum=%d  cbnum=%d  csiz=%d\n\n",
              name, dnum, wnum, pnum, ibnum, idnum, cbnum, csiz);
  /* open a database */
  if(ibnum > 0) odsettuning(ibnum, idnum, cbnum, csiz);
  if(!(odeum = odopen(name, OD_OWRITER | OD_OCREAT | OD_OTRUNC))){
    pdperror(name);
    return 1;
  }
  err = FALSE;
  /* loop for each document */
  for(i = 1; i <= dnum; i++){
    /* store a document */
    doc = makedoc(i, wnum, pnum);
    if(!odput(odeum, doc, -1, FALSE)){
      pdperror(name);
      oddocclose(doc);
      err = TRUE;
      break;
    }
    oddocclose(doc);
    /* print progression */
    if(dnum > 250 && i % (dnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == dnum || i % (dnum / 10) == 0){
        printfflush(" (%08d)\n", i);
      }
    }
  }
  /* close the database */
  if(!odclose(odeum)){
    pdperror(name);
    return 1;
  }
  if(!err) printfflush("ok\n\n");
  return err ? 1 : 0;
}


/* perform read command */
int doread(const char *name){
  ODEUM *odeum;
  ODDOC *doc;
  char buf[DOCBUFSIZ];
  int i, dnum, err;
  printfflush("<Reading Test>\n  name=%s\n\n", name);
  /* open a database */
  if(!(odeum = odopen(name, OD_OREADER))){
    pdperror(name);
    return 1;
  }
  /* get the number of documents */
  dnum = oddnum(odeum);
  err = FALSE;
  /* loop for each document */
  for(i = 1; i <= dnum; i++){
    /* retrieve a document */
    sprintf(buf, "%08d", i);
    if(!(doc = odget(odeum, buf))){
      pdperror(name);
      err = TRUE;
      break;
    }
    oddocclose(doc);
    /* print progression */
    if(dnum > 250 && i % (dnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == dnum || i % (dnum / 10) == 0){
        printfflush(" (%08d)\n", i);
      }
    }
  }
  /* close the database */
  if(!odclose(odeum)){
    pdperror(name);
    return 1;
  }
  if(!err) printfflush("ok\n\n");
  return err ? 1 : 0;
}


/* perform combo command */
int docombo(const char *name){
  ODEUM *odeum;
  ODDOC *doc;
  const CBLIST *nwords, *awords;
  CBLIST *tawords, *tnwords, *oawords;
  ODPAIR *pairs;
  const char *asis;
  char buf[DOCBUFSIZ], *normal;
  int i, j, pnum;
  printfflush("<Combination Test>\n  name=%s\n\n", name);
  printfflush("Creating a database with ... ");
  if(!(odeum = odopen(name, OD_OWRITER | OD_OCREAT | OD_OTRUNC))){
    pdperror(name);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Adding 20 documents including about 200 words ... ");
  for(i = 1; i <= 20; i++){
    sprintf(buf, "%08d", i);
    doc = makedoc(i, 120 + myrand() % 160, myrand() % 500 + 500);
    if(!odput(odeum, doc, 180 + myrand() % 40, FALSE)){
      pdperror(name);
      oddocclose(doc);
      odclose(odeum);
      return 1;
    }
    oddocclose(doc);
  }
  printfflush("ok\n");
  printfflush("Checking documents ... ");
  for(i = 1; i <= 20; i++){
    sprintf(buf, "%08d", i);
    if(!(doc = odget(odeum, buf))){
      pdperror(name);
      return 1;
    }
    nwords = oddocnwords(doc);
    awords = oddocawords(doc);
    if(!oddocuri(doc) || !oddocgetattr(doc, "title") || cblistnum(nwords) != cblistnum(awords)){
      fprintf(stderr, "%s: %s: invalid document\n", progname, name);
      oddocclose(doc);
      odclose(odeum);
      return 1;
    }
    for(j = 0; j < cblistnum(nwords); j++){
      if(strcmp(cblistval(nwords, j, NULL), cblistval(nwords, j, NULL))){
        fprintf(stderr, "%s: %s: invalid words\n", progname, name);
        oddocclose(doc);
        odclose(odeum);
        return 1;
      }
    }
    oddocclose(doc);
  }
  printfflush("ok\n");
  printfflush("Syncing the database ... ");
  if(!odsync(odeum)){
    pdperror(name);
    odclose(odeum);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Overwriting 1 - 10 documents ... ");
  for(i = 1; i <= 10; i++){
    sprintf(buf, "%08d", i);
    doc = makedoc(i, 120 + myrand() % 160, myrand() % 500 + 500);
    if(!odput(odeum, doc, 180 + myrand() % 40, TRUE)){
      pdperror(name);
      oddocclose(doc);
      odclose(odeum);
      return 1;
    }
    oddocclose(doc);
  }
  printfflush("ok\n");
  printfflush("Deleting 11 - 20 documents ... ");
  for(i = 11; i <= 20; i++){
    sprintf(buf, "%08d", i);
    if(!odout(odeum, buf)){
      pdperror(name);
      odclose(odeum);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Checking documents ... ");
  for(i = 1; i <= 10; i++){
    sprintf(buf, "%08d", i);
    if(!(doc = odget(odeum, buf))){
      pdperror(name);
      return 1;
    }
    nwords = oddocnwords(doc);
    awords = oddocawords(doc);
    if(!oddocuri(doc) || !oddocgetattr(doc, "title") || cblistnum(nwords) != cblistnum(awords)){
      fprintf(stderr, "%s: %s: invalid document\n", progname, name);
      oddocclose(doc);
      odclose(odeum);
      return 1;
    }
    for(j = 0; j < cblistnum(nwords); j++){
      if(strcmp(cblistval(nwords, j, NULL), cblistval(nwords, j, NULL))){
        fprintf(stderr, "%s: %s: invalid words\n", progname, name);
        oddocclose(doc);
        odclose(odeum);
        return 1;
      }
    }
    oddocclose(doc);
  }
  if(oddnum(odeum) != 10){
    fprintf(stderr, "%s: %s: invalid document number\n", progname, name);
    odclose(odeum);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Optimizing the database ... ");
  if(!odoptimize(odeum)){
    pdperror(name);
    odclose(odeum);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Adding 10 documents including about 200 words ... ");
  for(i = 11; i <= 20; i++){
    sprintf(buf, "%08d", i);
    doc = makedoc(i, 120 + myrand() % 160, myrand() % 500 + 500);
    if(!odput(odeum, doc, 180 + myrand() % 40, FALSE)){
      pdperror(name);
      oddocclose(doc);
      odclose(odeum);
      return 1;
    }
    oddocclose(doc);
  }
  printfflush("ok\n");
  printfflush("Deleting 6 - 15 documents ... ");
  for(i = 6; i <= 15; i++){
    sprintf(buf, "%08d", i);
    if(!odout(odeum, buf)){
      pdperror(name);
      odclose(odeum);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Retrieving documents 100 times ... ");
  for(i = 1; i <= 100; i++){
    sprintf(buf, "%08d", myrand() % 1000 + 1);
    if((pairs = odsearch(odeum, buf, -1, &pnum)) != NULL){
      for(j = 0; j < pnum; j++){
        if((doc = odgetbyid(odeum, pairs[j].id)) != NULL){
          oddocclose(doc);
        } else if(dpecode != DP_ENOITEM){
          pdperror(name);
          odclose(odeum);
          return 1;
        }
      }
      free(pairs);
    } else if(dpecode != DP_ENOITEM){
      pdperror(name);
      odclose(odeum);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Analyzing text ... ");
  tawords = cblistopen();
  tnwords = cblistopen();
  odanalyzetext(odeum, "I'd like to ++see++ Mr. X-men tomorrow.", tawords, tnwords);
  odanalyzetext(odeum, "=== :-) SMILE . @ . SAD :-< ===", tawords, tnwords);
  for(i = 0; i < DOCBUFSIZ - 1; i++){
    buf[i] = myrand() % 255 + 1;
  }
  buf[DOCBUFSIZ-1] = '\0';
  cblistclose(tnwords);
  cblistclose(tawords);
  for(i = 0; i < 1000; i++){
    for(j = 0; j < DOCBUFSIZ - 1; j++){
      if((j + 1) % 32 == 0){
        buf[j] = ' ';
      } else {
        buf[j] = myrand() % 255 + 1;
      }
    }
    buf[DOCBUFSIZ-1] = '\0';
    tawords = cblistopen();
    tnwords = cblistopen();
    odanalyzetext(odeum, buf, tawords, tnwords);
    oawords = odbreaktext(buf);
    if(cblistnum(tawords) != cblistnum(oawords) || cblistnum(tnwords) != cblistnum(oawords)){
      fprintf(stderr, "%s: %s: invalid analyzing\n", progname, name);
      cblistclose(oawords);
      cblistclose(tnwords);
      cblistclose(tawords);
      odclose(odeum);
      return 1;
    }
    for(j = 0; j < cblistnum(oawords); j++){
      asis = cblistval(oawords, j, NULL);
      normal = odnormalizeword(asis);
      if(strcmp(asis, cblistval(oawords, j, NULL)) || strcmp(normal, cblistval(tnwords, j, NULL))){
        fprintf(stderr, "%s: %s: invalid analyzing\n", progname, name);
        free(normal);
        cblistclose(oawords);
        cblistclose(tnwords);
        cblistclose(tawords);
        odclose(odeum);
        return 1;
      }
      free(normal);
    }
    cblistclose(oawords);
    cblistclose(tnwords);
    cblistclose(tawords);
  }
  printfflush("ok\n");
  printfflush("Closing the database ... ");
  if(!odclose(odeum)){
    pdperror(name);
    return 1;
  }
  printfflush("ok\n");
  printfflush("all ok\n\n");
  return 0;
}


/* perform wicked command */
int dowicked(const char *name, int dnum){
  ODEUM *odeum;
  ODDOC *doc;
  ODPAIR *pairs;
  char buf[DOCBUFSIZ];
  int i, j, pnum, err;
  printfflush("<Wicked Writing Test>\n  name=%s  dnum=%d\n\n", name, dnum);
  err = FALSE;
  if(!(odeum = odopen(name, OD_OWRITER | OD_OCREAT | OD_OTRUNC))){
    pdperror(name);
    return 1;
  }
  for(i = 1; i <= dnum; i++){
    switch(myrand() % 8){
    case 1:
      putchar('K');
      doc = makedoc(myrand() % dnum + 1, myrand() % 10 + 10, myrand() % dnum + 500);
      if(!odput(odeum, doc, 5, FALSE) && dpecode != DP_EKEEP) err = TRUE;
      oddocclose(doc);
      break;
    case 3:
      putchar('D');
      if(!odoutbyid(odeum, myrand() % dnum + 1) && dpecode != DP_ENOITEM) err = TRUE;
      break;
    case 4:
      putchar('R');
      sprintf(buf, "%08d", myrand() % (dnum + 500) + 1);
      if((pairs = odsearch(odeum, buf, 5, &pnum)) != NULL){
        if(myrand() % 5 == 0){
          for(j = 0; j < pnum; j++){
            if((doc = odgetbyid(odeum, pairs[j].id)) != NULL){
              oddocclose(doc);
            } else if(dpecode != DP_ENOITEM){
              err = TRUE;
              break;
            }
          }
        }
        free(pairs);
      } else if(dpecode != DP_ENOITEM){
        err = TRUE;
      }
      break;
    default:
      putchar('O');
      doc = makedoc(myrand() % dnum + 1, myrand() % 10 + 10, myrand() % dnum + 500);
      if(!odput(odeum, doc, 5, TRUE)) err = TRUE;
      oddocclose(doc);
      break;
    }
    if(i % 50 == 0) printfflush(" (%08d)\n", i);
    if(err){
      pdperror(name);
      break;
    }
  }
  if(!odoptimize(odeum)){
    pdperror(name);
    err = TRUE;
  }
  for(i = 1; i <= dnum; i++){
    doc = makedoc(i, 5, 5);
    if(!odput(odeum, doc, 5, FALSE) && dpecode != DP_EKEEP){
      pdperror(name);
      oddocclose(doc);
      err = TRUE;
      break;
    }
    oddocclose(doc);
    putchar(':');
    if(i % 50 == 0) printfflush(" (%08d)\n", i);
  }
  if(!odoptimize(odeum)){
    pdperror(name);
    err = TRUE;
  }
  for(i = 1; i <= dnum; i++){
    sprintf(buf, "%08d", i);
    if(!(doc = odget(odeum, buf))){
      pdperror(name);
      err = TRUE;
      break;
    }
    oddocclose(doc);
    putchar('=');
    if(i % 50 == 0) printfflush(" (%08d)\n", i);
  }
  if(!oditerinit(odeum)){
    pdperror(name);
    err = TRUE;
  }
  for(i = 1; i <= dnum; i++){
    if(!(doc = oditernext(odeum))){
      pdperror(name);
      err = TRUE;
      break;
    }
    oddocclose(doc);
    putchar('@');
    if(i % 50 == 0) printfflush(" (%08d)\n", i);
  }
  if(!odclose(odeum)){
    pdperror(name);
    return 1;
  }
  if(!err) printfflush("ok\n\n");
  return 0;
}



/* END OF FILE */
