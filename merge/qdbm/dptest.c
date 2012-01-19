/*************************************************************************************************
 * Test cases of Depot
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

#define RECBUFSIZ      32                /* buffer for records */


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
int runrcat(int argc, char **argv);
int runcombo(int argc, char **argv);
int runwicked(int argc, char **argv);
int printfflush(const char *format, ...);
void pdperror(const char *name);
int myrand(void);
int dowrite(const char *name, int rnum, int bnum, int sparse);
int doread(const char *name, int wb);
int dorcat(const char *name, int rnum, int bnum, int pnum, int align, int fbpsiz, int cb);
int docombo(const char *name);
int dowicked(const char *name, int rnum, int cb);


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
  } else if(!strcmp(argv[1], "rcat")){
    rv = runrcat(argc, argv);
  } else if(!strcmp(argv[1], "combo")){
    rv = runcombo(argc, argv);
  } else if(!strcmp(argv[1], "wicked")){
    rv = runwicked(argc, argv);
  } else {
    usage();
  }
  return rv;
}


/* print the usage and exit */
void usage(void){
  fprintf(stderr, "%s: test cases for Depot\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s write [-s] name rnum bnum\n", progname);
  fprintf(stderr, "  %s read [-wb] name\n", progname);
  fprintf(stderr, "  %s rcat [-c] name rnum bnum pnum align fbpsiz\n", progname);
  fprintf(stderr, "  %s combo name\n", progname);
  fprintf(stderr, "  %s wicked [-c] name rnum\n", progname);
  fprintf(stderr, "\n");
  exit(1);
}


/* parse arguments of write command */
int runwrite(int argc, char **argv){
  char *name, *rstr, *bstr;
  int i, rnum, bnum, sb, rv;
  name = NULL;
  rstr = NULL;
  bstr = NULL;
  rnum = 0;
  bnum = 0;
  sb = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-s")){
        sb = TRUE;
      } else {
        usage();
      }
    } else if(!name){
      name = argv[i];
    } else if(!rstr){
      rstr = argv[i];
    } else if(!bstr){
      bstr = argv[i];
    } else {
      usage();
    }
  }
  if(!name || !rstr || !bstr) usage();
  rnum = atoi(rstr);
  bnum = atoi(bstr);
  if(rnum < 1 || bnum < 1) usage();
  rv = dowrite(name, rnum, bnum, sb);
  return rv;
}


/* parse arguments of read command */
int runread(int argc, char **argv){
  char *name;
  int i, wb, rv;
  name = NULL;
  wb = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-wb")){
        wb = TRUE;
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
  rv = doread(name, wb);
  return rv;
}


/* parse arguments of rcat command */
int runrcat(int argc, char **argv){
  char *name, *rstr, *bstr, *pstr, *astr, *fstr;
  int i, rnum, bnum, pnum, align, fbpsiz, cb, rv;
  name = NULL;
  rstr = NULL;
  bstr = NULL;
  pstr = NULL;
  astr = NULL;
  fstr = NULL;
  rnum = 0;
  bnum = 0;
  pnum = 0;
  align = 0;
  fbpsiz = 0;
  cb = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-c")){
        cb = TRUE;
      } else {
        usage();
      }
    } else if(!name){
      name = argv[i];
    } else if(!rstr){
      rstr = argv[i];
    } else if(!bstr){
      bstr = argv[i];
    } else if(!pstr){
      pstr = argv[i];
    } else if(!astr){
      astr = argv[i];
    } else if(!fstr){
      fstr = argv[i];
    } else {
      usage();
    }
  }
  if(!name || !rstr || !bstr || !pstr || !astr || !fstr) usage();
  rnum = atoi(rstr);
  bnum = atoi(bstr);
  pnum = atoi(pstr);
  align = atoi(astr);
  fbpsiz= atoi(fstr);
  if(rnum < 1 || bnum < 1 || pnum < 1 || fbpsiz < 0) usage();
  rv = dorcat(name, rnum, bnum, pnum, align, fbpsiz, cb);
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
  char *name, *rstr;
  int i, rnum, cb, rv;
  name = NULL;
  rstr = NULL;
  rnum = 0;
  cb = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-c")){
        cb = TRUE;
      } else {
        usage();
      }
    } else if(!name){
      name = argv[i];
    } else if(!rstr){
      rstr = argv[i];
    } else {
      usage();
    }
  }
  if(!name || !rstr) usage();
  rnum = atoi(rstr);
  if(rnum < 1) usage();
  rv = dowicked(name, rnum, cb);
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


/* perform write command */
int dowrite(const char *name, int rnum, int bnum, int sparse){
  DEPOT *depot;
  int i, omode, err, len;
  char buf[RECBUFSIZ];
  printfflush("<Writing Test>\n  name=%s  rnum=%d  bnum=%d  s=%d\n\n", name, rnum, bnum, sparse);
  /* open a database */
  omode = DP_OWRITER | DP_OCREAT | DP_OTRUNC | (sparse ? DP_OSPARSE : 0);
  if(!(depot = dpopen(name, omode, bnum))){
    pdperror(name);
    return 1;
  }
  err = FALSE;
  /* loop for each record */
  for(i = 1; i <= rnum; i++){
    /* store a record */
    len = sprintf(buf, "%08d", i);
    if(!dpput(depot, buf, len, buf, len, DP_DOVER)){
      pdperror(name);
      err = TRUE;
      break;
    }
    /* print progression */
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0){
        printfflush(" (%08d)\n", i);
      }
    }
  }
  /* close the database */
  if(!dpclose(depot)){
    pdperror(name);
    return 1;
  }
  if(!err) printfflush("ok\n\n");
  return err ? 1 : 0;
}


/* perform read command */
int doread(const char *name, int wb){
  DEPOT *depot;
  int i, rnum, err, len;
  char buf[RECBUFSIZ], vbuf[RECBUFSIZ], *val;
  printfflush("<Reading Test>\n  name=%s  wb=%d\n\n", name, wb);
  /* open a database */
  if(!(depot = dpopen(name, DP_OREADER, -1))){
    pdperror(name);
    return 1;
  }
  /* get the number of records */
  rnum = dprnum(depot);
  err = FALSE;
  /* loop for each record */
  for(i = 1; i <= rnum; i++){
    /* retrieve a record */
    len = sprintf(buf, "%08d", i);
    if(wb){
      if(dpgetwb(depot, buf, len, 0, RECBUFSIZ, vbuf) == -1){
        pdperror(name);
        err = TRUE;
        break;
      }
    } else {
      if(!(val = dpget(depot, buf, len, 0, -1, NULL))){
        pdperror(name);
        err = TRUE;
        break;
      }
      free(val);
    }
    /* print progression */
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0){
        printfflush(" (%08d)\n", i);
      }
    }
  }
  /* close the database */
  if(!dpclose(depot)){
    pdperror(name);
    return 1;
  }
  if(!err) printfflush("ok\n\n");
  return err ? 1 : 0;
}


/* perform rcat command */
int dorcat(const char *name, int rnum, int bnum, int pnum, int align, int fbpsiz, int cb){
  DEPOT *depot;
  CBMAP *map;
  int i, err, len, ksiz, vsiz, rsiz;
  const char *kbuf, *vbuf;
  char buf[RECBUFSIZ], *rbuf;
  printfflush("<Random Writing Test>\n  name=%s  rnum=%d  bnum=%d  pnum=%d  align=%d"
              "  fbpsiz=%d  c=%d\n\n", name, rnum, bnum, pnum, align, fbpsiz, cb);
  if(!(depot = dpopen(name, DP_OWRITER | DP_OCREAT | DP_OTRUNC, bnum))){
    pdperror(name);
    return 1;
  }
  if(!dpsetalign(depot, align) || !dpsetfbpsiz(depot, fbpsiz)){
    pdperror(name);
    dpclose(depot);
    return 1;
  }
  map = NULL;
  if(cb) map = cbmapopen();
  err = FALSE;
  for(i = 1; i <= rnum; i++){
    len = sprintf(buf, "%08d", myrand() % pnum + 1);
    if(!dpput(depot, buf, len, buf, len, DP_DCAT)){
      pdperror(name);
      err = TRUE;
      break;
    }
    if(map) cbmapputcat(map, buf, len, buf, len);
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0){
        printfflush(" (%08d: fsiz=%d rnum=%d)\n", i, dpfsiz(depot), dprnum(depot));
      }
    }
  }
  if(map){
    printfflush("Matching records ... ");
    cbmapiterinit(map);
    while((kbuf = cbmapiternext(map, &ksiz)) != NULL){
      vbuf = cbmapget(map, kbuf, ksiz, &vsiz);
      if(!(rbuf = dpget(depot, kbuf, ksiz, 0, -1, &rsiz))){
        pdperror(name);
        err = TRUE;
        break;
      }
      if(rsiz != vsiz || memcmp(rbuf, vbuf, rsiz)){
        fprintf(stderr, "%s: %s: unmatched record\n", progname, name);
        free(rbuf);
        err = TRUE;
        break;
      }
      free(rbuf);
    }
    cbmapclose(map);
    if(!err) printfflush("ok\n");
  }
  if(!dpclose(depot)){
    pdperror(name);
    return 1;
  }
  if(!err) printfflush("ok\n\n");
  return err ? 1 : 0;
}


/* perform combo command */
int docombo(const char *name){
  DEPOT *depot;
  char buf[RECBUFSIZ], wbuf[RECBUFSIZ], *vbuf;
  int i, len, wlen, vsiz;
  printfflush("<Combination Test>\n  name=%s\n\n", name);
  printfflush("Creating a database with bnum 3 ... ");
  if(!(depot = dpopen(name, DP_OWRITER | DP_OCREAT | DP_OTRUNC, 3))){
    pdperror(name);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Setting alignment as 16 ... ");
  if(!dpsetalign(depot, 16)){
    pdperror(name);
    dpclose(depot);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Adding 20 records ... ");
  for(i = 1; i <= 20; i++){
    len = sprintf(buf, "%08d", i);
    if(!dpput(depot, buf, len, buf, len, DP_DOVER)){
      pdperror(name);
      dpclose(depot);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Checking records ... ");
  for(i = 1; i <= 20; i++){
    len = sprintf(buf, "%08d", i);
    if(!(vbuf = dpget(depot, buf, len, 0, -1, &vsiz))){
      pdperror(name);
      dpclose(depot);
      return 1;
    }
    free(vbuf);
    if(vsiz != dpvsiz(depot, buf, len)){
      fprintf(stderr, "%s: %s: invalid vsiz\n", progname, name);
      dpclose(depot);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Overwriting top 10 records without moving rooms ... ");
  for(i = 1; i <= 10; i++){
    len = sprintf(buf, "%08d", i);
    if(!dpput(depot, buf, len, buf, len, DP_DOVER)){
      pdperror(name);
      dpclose(depot);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Overwriting top 5 records with moving rooms ... ");
  for(i = 1; i <= 5; i++){
    len = sprintf(buf, "%08d", i);
    wlen = sprintf(wbuf, "%024d", i);
    if(!dpput(depot, buf, len, wbuf, wlen, DP_DOVER)){
      pdperror(name);
      dpclose(depot);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Overwriting top 15 records in concatenation with moving rooms ... ");
  for(i = 1; i <= 15; i++){
    len = sprintf(buf, "%08d", i);
    wlen = sprintf(wbuf, "========================");
    if(!dpput(depot, buf, len, wbuf, wlen, DP_DCAT)){
      pdperror(name);
      dpclose(depot);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Checking records ... ");
  for(i = 1; i <= 20; i++){
    len = sprintf(buf, "%08d", i);
    if(!(vbuf = dpget(depot, buf, len, 0, -1, &vsiz))){
      pdperror(name);
      dpclose(depot);
      return 1;
    }
    free(vbuf);
    if(vsiz != dpvsiz(depot, buf, len)){
      fprintf(stderr, "%s: %s: invalid vsiz\n", progname, name);
      dpclose(depot);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Deleting top 10 records ... ");
  for(i = 1; i <= 10; i++){
    len = sprintf(buf, "%08d", i);
    if(!dpout(depot, buf, len)){
      pdperror(name);
      dpclose(depot);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Checking deleted records ... ");
  for(i = 1; i <= 10; i++){
    len = sprintf(buf, "%08d", i);
    vbuf = dpget(depot, buf, len, 0, -1, &vsiz);
    free(vbuf);
    if(vbuf || dpecode != DP_ENOITEM){
      fprintf(stderr, "%s: %s: deleting failed\n", progname, name);
      dpclose(depot);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Overwriting top 15 records in concatenation with moving rooms ... ");
  for(i = 1; i <= 15; i++){
    len = sprintf(buf, "%08d", i);
    wlen = sprintf(wbuf, "========================");
    if(!dpput(depot, buf, len, wbuf, wlen, DP_DCAT)){
      pdperror(name);
      dpclose(depot);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Checking records ... ");
  for(i = 1; i <= 20; i++){
    len = sprintf(buf, "%08d", i);
    if(!(vbuf = dpget(depot, buf, len, 0, -1, &vsiz))){
      pdperror(name);
      dpclose(depot);
      return 1;
    }
    free(vbuf);
    if(vsiz != dpvsiz(depot, buf, len)){
      fprintf(stderr, "%s: %s: invalid vsiz\n", progname, name);
      dpclose(depot);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Optimizing the database ... ");
  if(!dpoptimize(depot, -1)){
    pdperror(name);
    dpclose(depot);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Checking records ... ");
  for(i = 1; i <= 20; i++){
    len = sprintf(buf, "%08d", i);
    if(!(vbuf = dpget(depot, buf, len, 0, -1, &vsiz))){
      pdperror(name);
      dpclose(depot);
      return 1;
    }
    free(vbuf);
    if(vsiz != dpvsiz(depot, buf, len)){
      fprintf(stderr, "%s: %s: invalid vsiz\n", progname, name);
      dpclose(depot);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Closing the database ... ");
  if(!dpclose(depot)){
    pdperror(name);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Creating a database with bnum 1000000 ... ");
  if(!(depot = dpopen(name, DP_OWRITER | DP_OCREAT | DP_OTRUNC, 1000000))){
    pdperror(name);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Adding 1000 records ... ");
  for(i = 1; i <= 1000; i++){
    len = sprintf(buf, "%08d", i);
    if(!dpput(depot, buf, len, buf, len, DP_DOVER)){
      pdperror(name);
      dpclose(depot);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Adding 64 records ... ");
  for(i = 1; i <= 1000; i++){
    len = sprintf(buf, "%o", i);
    if(!dpput(depot, buf, len, buf, len, DP_DOVER)){
      pdperror(name);
      dpclose(depot);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Syncing the database ... ");
  if(!dpsync(depot)){
    pdperror(name);
    dpclose(depot);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Retrieving records directly ... ");
  for(i = 1; i <= 64; i++){
    len = sprintf(buf, "%o", i);
    if(!(vbuf = dpsnaffle(name, buf, len, &vsiz))){
      pdperror(name);
      dpclose(depot);
      return 1;
    }
    if(strcmp(vbuf, buf)){
      fprintf(stderr, "%s: %s: invalid content\n", progname, name);
      free(vbuf);
      dpclose(depot);
      return 1;
    }
    free(vbuf);
    if(vsiz != dpvsiz(depot, buf, len)){
      fprintf(stderr, "%s: %s: invalid vsiz\n", progname, name);
      dpclose(depot);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Optimizing the database ... ");
  if(!dpoptimize(depot, -1)){
    pdperror(name);
    dpclose(depot);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Closing the database ... ");
  if(!dpclose(depot)){
    pdperror(name);
    return 1;
  }
  printfflush("ok\n");
  printfflush("all ok\n\n");
  return 0;
}


/* perform wicked command */
int dowicked(const char *name, int rnum, int cb){
  DEPOT *depot;
  CBMAP *map;
  int i, len, err, align, mksiz, mvsiz, rsiz;
  const char *mkbuf, *mvbuf;
  char buf[RECBUFSIZ], vbuf[RECBUFSIZ], *val;
  printfflush("<Wicked Writing Test>\n  name=%s  rnum=%d\n\n", name, rnum);
  err = FALSE;
  if(!(depot = dpopen(name, DP_OWRITER | DP_OCREAT | DP_OTRUNC, rnum / 10))){
    pdperror(name);
    return 1;
  }
  if(!dpsetalign(depot, 16) || !dpsetfbpsiz(depot, 256)){
    pdperror(name);
    err = TRUE;
  }
  map = NULL;
  if(cb) map = cbmapopen();
  for(i = 1; i <= rnum; i++){
    len = sprintf(buf, "%08d", myrand() % rnum + 1);
    switch(myrand() % 16){
    case 0:
      putchar('O');
      if(!dpput(depot, buf, len, buf, len, DP_DOVER)) err = TRUE;
      if(map) cbmapput(map, buf, len, buf, len, TRUE);
      break;
    case 1:
      putchar('K');
      if(!dpput(depot, buf, len, buf, len, DP_DKEEP) && dpecode != DP_EKEEP) err = TRUE;
      if(map) cbmapput(map, buf, len, buf, len, FALSE);
      break;
    case 2:
      putchar('D');
      if(!dpout(depot, buf, len) && dpecode != DP_ENOITEM) err = TRUE;
      if(map) cbmapout(map, buf, len);
      break;
    case 3:
      putchar('G');
      if(dpgetwb(depot, buf, len, 2, RECBUFSIZ, vbuf) == -1 && dpecode != DP_ENOITEM) err = TRUE;
      break;
    case 4:
      putchar('V');
      if(dpvsiz(depot, buf, len) == -1 && dpecode != DP_ENOITEM) err = TRUE;
      break;
    default:
      putchar('C');
      if(!dpput(depot, buf, len, buf, len, DP_DCAT)) err = TRUE;
      if(map) cbmapputcat(map, buf, len, buf, len);
      break;
    }
    if(i % 50 == 0) printfflush(" (%08d)\n", i);
    if(!err && rnum > 100 && myrand() % (rnum / 100) == 0){
      if(myrand() % 10 == 0){
        align = (myrand() % 4 + 1) * -1;
      } else {
        align = myrand() % 32;
      }
      if(!dpsetalign(depot, align)) err = TRUE;
    }
    if(err){
      pdperror(name);
      break;
    }
  }
  if(!dpoptimize(depot, -1)){
    pdperror(name);
    err = TRUE;
  }
  for(i = 1; i <= rnum; i++){
    len = sprintf(buf, "%08d", i);
    if(!dpput(depot, buf, len, ":", -1, DP_DCAT)){
      pdperror(name);
      err = TRUE;
      break;
    }
    if(map) cbmapputcat(map, buf, len, ":", -1);
    putchar(':');
    if(i % 50 == 0) printfflush(" (%08d)\n", i);
  }
  if(!dpoptimize(depot, -1)){
    pdperror(name);
    err = TRUE;
  }
  for(i = 1; i <= rnum; i++){
    len = sprintf(buf, "%08d", i);
    if(!(val = dpget(depot, buf, len, 0, -1, NULL))){
      pdperror(name);
      err = TRUE;
      break;
    }
    free(val);
    putchar('=');
    if(i % 50 == 0) printfflush(" (%08d)\n", i);
  }
  if(!dpiterinit(depot)){
    pdperror(name);
    err = TRUE;
  }
  for(i = 1; i <= rnum; i++){
    if(!(val = dpiternext(depot, NULL))){
      pdperror(name);
      err = TRUE;
      break;
    }
    free(val);
    putchar('@');
    if(i % 50 == 0) printfflush(" (%08d)\n", i);
  }
  if(map){
    printfflush("Matching records ... ");
    cbmapiterinit(map);
    while((mkbuf = cbmapiternext(map, &mksiz)) != NULL){
      mvbuf = cbmapget(map, mkbuf, mksiz, &mvsiz);
      if(!(val = dpget(depot, mkbuf, mksiz, 0, -1, &rsiz))){
        pdperror(name);
        err = TRUE;
        break;
      }
      if(rsiz != mvsiz || memcmp(val, mvbuf, rsiz)){
        fprintf(stderr, "%s: %s: unmatched record\n", progname, name);
        free(val);
        err = TRUE;
        break;
      }
      free(val);
    }
    cbmapclose(map);
    if(!err) printfflush("ok\n");
  }
  if(!dpclose(depot)){
    pdperror(name);
    return 1;
  }
  if(!err) printfflush("ok\n\n");
  return err ? 1 : 0;
}



/* END OF FILE */
