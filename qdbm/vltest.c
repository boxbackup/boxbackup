/*************************************************************************************************
 * Test cases of Villa
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
#include <villa.h>
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
int runrdup(int argc, char **argv);
int runcombo(int argc, char **argv);
int runwicked(int argc, char **argv);
int printfflush(const char *format, ...);
void pdperror(const char *name);
int myrand(void);
int dowrite(const char *name, int rnum, int ii, int cmode,
            int lrecmax, int nidxmax, int lcnum, int ncnum, int fbp);
int doread(const char *name, int ii, int vc);
int dordup(const char *name, int rnum, int pnum, int ii, int cmode, int cc,
           int lrecmax, int nidxmax, int lcnum, int ncnum, int fbp);
int docombo(const char *name, int cmode);
int dowicked(const char *name, int rnum, int cb, int cmode);


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
  } else if(!strcmp(argv[1], "rdup")){
    rv = runrdup(argc, argv);
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
  fprintf(stderr, "%s: test cases for Villa\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s write [-int] [-cz|-cy|-cx] [-tune lrecmax nidxmax lcnum ncnum]"
          " [-fbp num] name rnum\n", progname);
  fprintf(stderr, "  %s read [-int] [-vc] name\n", progname);
  fprintf(stderr, "  %s rdup [-int] [-cz|-cy|-cx] [-cc] [-tune lrecmax nidxmax lcnum ncnum]"
          " [-fbp num] name rnum pnum\n", progname);
  fprintf(stderr, "  %s combo [-cz|-cy|-cx] name\n", progname);
  fprintf(stderr, "  %s wicked [-c] [-cz|-cy|-cx] name rnum\n", progname);
  fprintf(stderr, "\n");
  exit(1);
}


/* parse arguments of write command */
int runwrite(int argc, char **argv){
  char *name, *rstr;
  int i, rnum, ii, cmode, lrecmax, nidxmax, lcnum, ncnum, fbp, rv;
  name = NULL;
  rstr = NULL;
  rnum = 0;
  ii = FALSE;
  cmode = 0;
  lrecmax = -1;
  nidxmax = -1;
  lcnum = -1;
  ncnum = -1;
  fbp = -1;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-int")){
        ii = TRUE;
      } else if(!strcmp(argv[i], "-cz")){
        cmode |= VL_OZCOMP;
      } else if(!strcmp(argv[i], "-cy")){
        cmode |= VL_OYCOMP;
      } else if(!strcmp(argv[i], "-cx")){
        cmode |= VL_OXCOMP;
      } else if(!strcmp(argv[i], "-tune")){
        if(++i >= argc) usage();
        lrecmax = atoi(argv[i]);
        if(++i >= argc) usage();
        nidxmax = atoi(argv[i]);
        if(++i >= argc) usage();
        lcnum = atoi(argv[i]);
        if(++i >= argc) usage();
        ncnum = atoi(argv[i]);
      } else if(!strcmp(argv[i], "-fbp")){
        if(++i >= argc) usage();
        fbp = atoi(argv[i]);
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
  rv = dowrite(name, rnum, ii, cmode, lrecmax, nidxmax, lcnum, ncnum, fbp);
  return rv;
}


/* parse arguments of read command */
int runread(int argc, char **argv){
  char *name;
  int i, ii, vc, rv;
  name = NULL;
  ii = FALSE;
  vc = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-int")){
        ii = TRUE;
      } else if(!strcmp(argv[i], "-vc")){
        vc = TRUE;
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
  rv = doread(name, ii, vc);
  return rv;
}


/* parse arguments of rdup command */
int runrdup(int argc, char **argv){
  char *name, *rstr, *pstr;
  int i, rnum, pnum, ii, cmode, cc, lrecmax, nidxmax, lcnum, ncnum, fbp, rv;
  name = NULL;
  rstr = NULL;
  pstr = NULL;
  rnum = 0;
  pnum = 0;
  ii = FALSE;
  cmode = 0;
  cc = FALSE;
  lrecmax = -1;
  nidxmax = -1;
  lcnum = -1;
  ncnum = -1;
  fbp = -1;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-int")){
        ii = TRUE;
      } else if(!strcmp(argv[i], "-cz")){
        cmode |= VL_OZCOMP;
      } else if(!strcmp(argv[i], "-cy")){
        cmode |= VL_OYCOMP;
      } else if(!strcmp(argv[i], "-cx")){
        cmode |= VL_OXCOMP;
      } else if(!strcmp(argv[i], "-cc")){
        cc = TRUE;
      } else if(!strcmp(argv[i], "-tune")){
        if(++i >= argc) usage();
        lrecmax = atoi(argv[i]);
        if(++i >= argc) usage();
        nidxmax = atoi(argv[i]);
        if(++i >= argc) usage();
        lcnum = atoi(argv[i]);
        if(++i >= argc) usage();
        ncnum = atoi(argv[i]);
      } else if(!strcmp(argv[i], "-fbp")){
        if(++i >= argc) usage();
        fbp = atoi(argv[i]);
      } else {
        usage();
      }
    } else if(!name){
      name = argv[i];
    } else if(!rstr){
      rstr = argv[i];
    } else if(!pstr){
      pstr = argv[i];
    } else {
      usage();
    }
  }
  if(!name || !rstr || !pstr) usage();
  rnum = atoi(rstr);
  pnum = atoi(pstr);
  if(rnum < 1 || pnum < 1) usage();
  rv = dordup(name, rnum, pnum, ii, cmode, cc, lrecmax, nidxmax, lcnum, ncnum, fbp);
  return rv;
}


/* parse arguments of combo command */
int runcombo(int argc, char **argv){
  char *name;
  int i, cmode, rv;
  name = NULL;
  cmode = 0;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-cz")){
        cmode |= VL_OZCOMP;
      } else if(!strcmp(argv[i], "-cy")){
        cmode |= VL_OYCOMP;
      } else if(!strcmp(argv[i], "-cx")){
        cmode |= VL_OXCOMP;
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
  rv = docombo(name, cmode);
  return rv;
}


/* parse arguments of wicked command */
int runwicked(int argc, char **argv){
  char *name, *rstr;
  int i, cb, cmode, rnum, rv;
  name = NULL;
  rstr = NULL;
  cb = FALSE;
  cmode = 0;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-c")){
        cb = TRUE;
      } else if(!strcmp(argv[i], "-cz")){
        cmode |= VL_OZCOMP;
      } else if(!strcmp(argv[i], "-cy")){
        cmode |= VL_OYCOMP;
      } else if(!strcmp(argv[i], "-cx")){
        cmode |= VL_OXCOMP;
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
  rv = dowicked(name, rnum, cb, cmode);
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
int dowrite(const char *name, int rnum, int ii, int cmode,
            int lrecmax, int nidxmax, int lcnum, int ncnum, int fbp){
  VILLA *villa;
  int i, omode, err, len;
  char buf[RECBUFSIZ];
  printfflush("<Writing Test>\n  name=%s  rnum=%d  int=%d  cmode=%d  "
              "lrecmax=%d  nidxmax=%d  lcnum=%d  ncnum=%d  fbp=%d\n\n",
              name, rnum, ii, cmode, lrecmax, nidxmax, lcnum, ncnum, fbp);
  /* open a database */
  omode = VL_OWRITER | VL_OCREAT | VL_OTRUNC | cmode;
  if(!(villa = vlopen(name, omode, ii ? VL_CMPINT : VL_CMPLEX))){
    pdperror(name);
    return 1;
  }
  err = FALSE;
  /* set tuning parameters */
  if(lrecmax > 0) vlsettuning(villa, lrecmax, nidxmax, lcnum, ncnum);
  if(fbp >= 0) vlsetfbpsiz(villa, fbp);
  /* loop for each record */
  for(i = 1; i <= rnum; i++){
    /* store a record */
    if(ii){
      if(!vlput(villa, (char *)&i, sizeof(int), (char *)&i, sizeof(int), VL_DOVER)){
        pdperror(name);
        err = TRUE;
        break;
      }
    } else {
      len = sprintf(buf, "%08d", i);
      if(!vlput(villa, buf, len, buf, len, VL_DOVER)){
        pdperror(name);
        err = TRUE;
        break;
      }
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
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  if(!err) printfflush("ok\n\n");
  return 0;
}


/* perform read command */
int doread(const char *name, int ii, int vc){
  VILLA *villa;
  int i, rnum, err, len;
  const char *cval;
  char buf[RECBUFSIZ], *val;
  printfflush("<Reading Test>\n  name=%s  int=%d\n\n", name, ii);
  /* open a database */
  if(!(villa = vlopen(name, VL_OREADER, ii ? VL_CMPINT : VL_CMPLEX))){
    pdperror(name);
    return 1;
  }
  /* get the number of records */
  rnum = vlrnum(villa);
  err = FALSE;
  /* loop for each record */
  for(i = 1; i <= rnum; i++){
    /* retrieve a record */
    if(ii){
      if(vc){
        if(!(cval = vlgetcache(villa, (char *)&i, sizeof(int), NULL))){
          pdperror(name);
          err = TRUE;
          break;
        }
      } else {
        if(!(val = vlget(villa, (char *)&i, sizeof(int), NULL))){
          pdperror(name);
          err = TRUE;
          break;
        }
        free(val);
      }
    } else {
      len = sprintf(buf, "%08d", i);
      if(vc){
        if(!(cval = vlgetcache(villa, buf, len, NULL))){
          pdperror(name);
          err = TRUE;
          break;
        }
      } else {
        if(!(val = vlget(villa, buf, len, NULL))){
          pdperror(name);
          err = TRUE;
          break;
        }
        free(val);
      }
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
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  if(!err) printfflush("ok\n\n");
  return 0;
}


/* perform rdup command */
int dordup(const char *name, int rnum, int pnum, int ii, int cmode, int cc,
           int lrecmax, int nidxmax, int lcnum, int ncnum, int fbp){
  VILLA *villa;
  int i, omode, err, dmode, vi, len;
  char buf[RECBUFSIZ];
  printfflush("<Random Writing Test>\n  name=%s  rnum=%d  int=%d  cmode=%d  "
              "lrecmax=%d  nidxmax=%d  lcnum=%d  ncnum=%d  fbp=%d\n\n",
              name, rnum, ii, cmode, lrecmax, nidxmax, lcnum, ncnum, fbp);
  omode = VL_OWRITER | VL_OCREAT | VL_OTRUNC | cmode;
  if(!(villa = vlopen(name, omode, ii ? VL_CMPINT : VL_CMPLEX))){
    pdperror(name);
    return 1;
  }
  err = FALSE;
  if(lrecmax > 0) vlsettuning(villa, lrecmax, nidxmax, lcnum, ncnum);
  if(fbp >= 0) vlsetfbpsiz(villa, fbp);
  for(i = 1; i <= rnum; i++){
    dmode = i % 3 == 0 ? VL_DDUPR : VL_DDUP;
    if(cc && myrand() % 2 == 0) dmode = VL_DCAT;
    vi = myrand() % pnum + 1;
    if(ii){
      if(!vlput(villa, (char *)&vi, sizeof(int), (char *)&vi, sizeof(int), dmode)){
        pdperror(name);
        err = TRUE;
        break;
      }
    } else {
      len = sprintf(buf, "%08d", vi);
      if(!vlput(villa, buf, len, buf, len, dmode)){
        pdperror(name);
        err = TRUE;
        break;
      }
    }
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0){
        printfflush(" (%08d: fsiz=%d lnum=%d nnum=%d)\n",
                    i, vlfsiz(villa), vllnum(villa), vlnnum(villa));
      }
    }
  }
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  if(!err) printfflush("ok\n\n");
  return 0;
}


/* perform combo command */
int docombo(const char *name, int cmode){
  VILLA *villa;
  VLMULCUR **mulcurs;
  char buf[RECBUFSIZ], *vbuf, *kbuf;
  int i, j, omode, len, vsiz, ksiz, fsiz, lnum, nnum, rnum;
  CBLIST *alist, *dlist;
  const char *ap, *dp;
  printfflush("<Combination Test>\n  name=%s  cmode=%d\n\n", name, cmode);
  printfflush("Creating a database with VL_CMPLEX ... ");
  omode = VL_OWRITER | VL_OCREAT | VL_OTRUNC | cmode;
  if(!(villa = vlopen(name, omode, VL_CMPLEX))){
    pdperror(name);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Setting tuning parameters with 3, 4, 16, 16 ... ");
  vlsettuning(villa, 3, 4, 16, 16);
  printfflush("ok\n");
  printfflush("Adding 100 records with VL_DOVER ... ");
  for(i = 1; i <= 100; i++){
    len = sprintf(buf, "%08d", i);
    if(!vlput(villa, buf, len, buf, len, VL_DOVER)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Checking records ... ");
  for(i = 1; i <= 100; i++){
    len = sprintf(buf, "%08d", i);
    if(!(vbuf = vlget(villa, buf, len, &vsiz))){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
    free(vbuf);
    if(vsiz != 8 || vlvsiz(villa, buf, len) != 8){
      fprintf(stderr, "%s: %s: invalid vsiz\n", progname, name);
      vlclose(villa);
      return 1;
    }
    if(vlvnum(villa, buf, len) != 1){
      fprintf(stderr, "%s: %s: invalid vnum\n", progname, name);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Deleting x1 - x5 records ... ");
  for(i = 1; i <= 100; i++){
    if(i % 10 < 1 || i % 10 > 5) continue;
    len = sprintf(buf, "%08d", i);
    if(!vlout(villa, buf, len)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Adding 100 records with VL_DOVER ... ");
  for(i = 1; i <= 100; i++){
    len = sprintf(buf, "%08d", i);
    if(!vlput(villa, buf, len, buf, len, VL_DOVER)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Deleting x1 - x5 records ... ");
  for(i = 1; i <= 100; i++){
    if(i % 10 < 1 || i % 10 > 5) continue;
    len = sprintf(buf, "%08d", i);
    if(!vlout(villa, buf, len)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Checking number of records ... ");
  if(vlrnum(villa) != 50){
    fprintf(stderr, "%s: %s: invalid rnum\n", progname, name);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Adding 100 records with VL_DDUP ... ");
  for(i = 1; i <= 100; i++){
    len = sprintf(buf, "%08d", i);
    if(!vlput(villa, buf, len, buf, len, VL_DDUP)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Deleting x6 - x0 records ... ");
  for(i = 1; i <= 100; i++){
    if(i % 10 >= 1 && i % 10 <= 5) continue;
    len = sprintf(buf, "%08d", i);
    if(!vlout(villa, buf, len)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Optimizing the database ... ");
  if(!vloptimize(villa)){
    pdperror(name);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Checking number of records ... ");
  if(vlrnum(villa) != 100){
    fprintf(stderr, "%s: %s: invalid rnum\n", progname, name);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Checking records ... ");
  for(i = 1; i <= 100; i++){
    len = sprintf(buf, "%08d", i);
    if(!(vbuf = vlget(villa, buf, len, &vsiz))){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
    free(vbuf);
    if(vsiz != 8){
      fprintf(stderr, "%s: %s: invalid vsiz\n", progname, name);
      vlclose(villa);
      return 1;
    }
    if(vlvnum(villa, buf, len) != 1){
      fprintf(stderr, "%s: %s: invalid vnum\n", progname, name);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Deleting x6 - x0 records ... ");
  for(i = 1; i <= 100; i++){
    if(i % 10 >= 1 && i % 10 <= 5) continue;
    len = sprintf(buf, "%08d", i);
    if(!vlout(villa, buf, len)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Scanning with the cursor in ascending order ... ");
  if(!vlcurfirst(villa)){
    pdperror(name);
    vlclose(villa);
    return 1;
  }
  i = 0;
  do {
    kbuf = NULL;
    vbuf = NULL;
    if(!(kbuf = vlcurkey(villa, &ksiz)) || !(vbuf = vlcurval(villa, &vsiz))){
      pdperror(name);
      free(kbuf);
      free(vbuf);
      vlclose(villa);
      return 1;
    }
    free(kbuf);
    free(vbuf);
    i++;
  } while(vlcurnext(villa));
  if(i != 50){
    fprintf(stderr, "%s: %s: invalid cursor\n", progname, name);
    vlclose(villa);
    return 1;
  }
  if(dpecode != DP_ENOITEM){
    pdperror(name);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Scanning with the cursor in decending order ... ");
  if(!vlcurlast(villa)){
    pdperror(name);
    vlclose(villa);
    return 1;
  }
  i = 0;
  do {
    kbuf = NULL;
    vbuf = NULL;
    if(!(kbuf = vlcurkey(villa, &ksiz)) || !(vbuf = vlcurval(villa, &vsiz))){
      pdperror(name);
      free(kbuf);
      free(vbuf);
      vlclose(villa);
      return 1;
    }
    free(kbuf);
    free(vbuf);
    i++;
  } while(vlcurprev(villa));
  if(i != 50){
    fprintf(stderr, "%s: %s: invalid cursor\n", progname, name);
    vlclose(villa);
    return 1;
  }
  if(dpecode != DP_ENOITEM){
    pdperror(name);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Adding 50 random records with VL_DDUPR ... ");
  for(i = 0; i < 50; i++){
    len = sprintf(buf, "%08d", myrand() % 100 + 1);
    if(!vlput(villa, buf, len, buf, len, VL_DDUPR)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Deleting 80 random records ... ");
  i = 0;
  while(i < 80){
    len = sprintf(buf, "%08d", myrand() % 100 + 1);
    if(!vlout(villa, buf, len)){
      if(dpecode == DP_ENOITEM) continue;
      pdperror(name);
      vlclose(villa);
      return 1;
    }
    i++;
  }
  printfflush("ok\n");
  alist = cblistopen();
  dlist = cblistopen();
  printfflush("Scanning with the cursor in ascending order ... ");
  if(!vlcurfirst(villa)){
    pdperror(name);
    vlclose(villa);
    return 1;
  }
  i = 0;
  do {
    kbuf = NULL;
    vbuf = NULL;
    if(!(kbuf = vlcurkey(villa, &ksiz)) || !(vbuf = vlcurval(villa, &vsiz))){
      pdperror(name);
      cblistclose(alist);
      cblistclose(dlist);
      free(kbuf);
      free(vbuf);
      vlclose(villa);
      return 1;
    }
    cblistpush(alist, kbuf, ksiz);
    free(kbuf);
    free(vbuf);
    i++;
  } while(vlcurnext(villa));
  if(i != 20){
    fprintf(stderr, "%s: %s: invalid cursor\n", progname, name);
    cblistclose(alist);
    cblistclose(dlist);
    vlclose(villa);
    return 1;
  }
  if(dpecode != DP_ENOITEM){
    pdperror(name);
    cblistclose(alist);
    cblistclose(dlist);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Scanning with the cursor in decending order ... ");
  if(!vlcurlast(villa)){
    pdperror(name);
    cblistclose(alist);
    cblistclose(dlist);
    vlclose(villa);
    return 1;
  }
  i = 0;
  do {
    kbuf = NULL;
    vbuf = NULL;
    if(!(kbuf = vlcurkey(villa, &ksiz)) || !(vbuf = vlcurval(villa, &vsiz))){
      pdperror(name);
      free(kbuf);
      free(vbuf);
      cblistclose(alist);
      cblistclose(dlist);
      vlclose(villa);
      return 1;
    }
    cblistunshift(dlist, kbuf, ksiz);
    free(kbuf);
    free(vbuf);
    i++;
  } while(vlcurprev(villa));
  if(i != 20){
    fprintf(stderr, "%s: %s: invalid cursor\n", progname, name);
    cblistclose(alist);
    cblistclose(dlist);
    vlclose(villa);
    return 1;
  }
  if(dpecode != DP_ENOITEM){
    pdperror(name);
    cblistclose(alist);
    cblistclose(dlist);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Matching result of ascending scan and desending scan  ... ");
  for(i = 0; i < cblistnum(alist); i++){
    ap = cblistval(alist, i, NULL);
    dp = cblistval(dlist, i, NULL);
    if(strcmp(ap, dp)){
      fprintf(stderr, "%s: %s: not match\n", progname, name);
      cblistclose(alist);
      cblistclose(dlist);
      vlclose(villa);
      return 1;
    }
  }
  cblistsort(alist);
  for(i = 0; i < cblistnum(alist); i++){
    ap = cblistval(alist, i, NULL);
    dp = cblistval(dlist, i, NULL);
    if(strcmp(ap, dp)){
      fprintf(stderr, "%s: %s: not match\n", progname, name);
      cblistclose(alist);
      cblistclose(dlist);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  cblistclose(alist);
  cblistclose(dlist);
  printfflush("Resetting tuning parameters with 41, 80, 32, 32 ... ");
  vlsettuning(villa, 41, 80, 32, 32);
  printfflush("ok\n");
  printfflush("Adding 1000 random records with VL_DDUP ... ");
  for(i = 0; i < 1000; i++){
    len = sprintf(buf, "%08d", myrand() % 1000 + 1);
    if(!vlput(villa, buf, len, buf, len, VL_DDUP)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Resetting tuning parameters with 8, 5, 16, 16 ... ");
  vlsettuning(villa, 8, 5, 16, 16);
  printfflush("ok\n");
  printfflush("Adding 1000 random records with VL_DDUP ... ");
  for(i = 0; i < 1000; i++){
    len = sprintf(buf, "%08d", myrand() % 1000 + 1);
    if(!vlput(villa, buf, len, buf, len, VL_DDUP)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Beginning the transaction ... ");
  if(!vltranbegin(villa)){
    pdperror(name);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Adding 100 random records with VL_DDUP ... ");
  for(i = 0; i < 100; i++){
    len = sprintf(buf, "%08d", myrand() % 1000 + 1);
    if(!vlput(villa, buf, len, buf, len, VL_DDUP)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Scanning and checking ... ");
  i = 0;
  for(vlcurlast(villa); (kbuf = vlcurkey(villa, &ksiz)) != NULL; vlcurprev(villa)){
    if(vlvnum(villa, kbuf, ksiz) < 1 || !(vbuf = vlcurval(villa, NULL))){
      pdperror(name);
      free(kbuf);
      vlclose(villa);
      return 1;
    }
    free(vbuf);
    free(kbuf);
    i++;
  }
  if(i != vlrnum(villa)){
    fprintf(stderr, "%s: %s: invalid\n", progname, name);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Committing the transaction ... ");
  if(!vltrancommit(villa)){
    pdperror(name);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Scanning and checking ... ");
  i = 0;
  for(vlcurlast(villa); (kbuf = vlcurkey(villa, &ksiz)) != NULL; vlcurprev(villa)){
    if(vlvnum(villa, kbuf, ksiz) < 1 || !(vbuf = vlcurval(villa, NULL))){
      pdperror(name);
      free(kbuf);
      vlclose(villa);
      return 1;
    }
    free(vbuf);
    free(kbuf);
    i++;
  }
  if(i != vlrnum(villa)){
    fprintf(stderr, "%s: %s: invalid\n", progname, name);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  lnum = vllnum(villa);
  nnum = vlnnum(villa);
  rnum = vlrnum(villa);
  fsiz = vlfsiz(villa);
  printfflush("Beginning the transaction ... ");
  if(!vltranbegin(villa)){
    pdperror(name);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Adding 100 random records with VL_DDUP ... ");
  for(i = 0; i < 100; i++){
    len = sprintf(buf, "%08d", myrand() % 1000 + 1);
    if(!vlput(villa, buf, len, buf, len, VL_DDUP)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Aborting the transaction ... ");
  if(!vltranabort(villa)){
    pdperror(name);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Checking rollback ... ");
  if(vlfsiz(villa) != fsiz || vllnum(villa) != lnum ||
     vlnnum(villa) != nnum || vlrnum(villa) != rnum){
    fprintf(stderr, "%s: %s: invalid\n", progname, name);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Scanning and checking ... ");
  i = 0;
  for(vlcurlast(villa); (kbuf = vlcurkey(villa, &ksiz)) != NULL; vlcurprev(villa)){
    if(vlvnum(villa, kbuf, ksiz) < 1 || !(vbuf = vlcurval(villa, NULL))){
      pdperror(name);
      free(kbuf);
      vlclose(villa);
      return 1;
    }
    free(vbuf);
    free(kbuf);
    i++;
  }
  if(i != vlrnum(villa)){
    fprintf(stderr, "%s: %s: invalid\n", progname, name);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Closing the database ... ");
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Creating a database with VL_CMPLEX ... ");
  omode = VL_OWRITER | VL_OCREAT | VL_OTRUNC | cmode;
  if(!(villa = vlopen(name, omode, VL_CMPLEX))){
    pdperror(name);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Setting tuning parameters with 5, 6, 16, 16 ... ");
  vlsettuning(villa, 5, 6, 16, 16);
  printfflush("ok\n");
  printfflush("Adding 3 * 3 records with VL_DDUP ... ");
  for(i = 0; i < 3; i++){
    for(j = 0; j < 3; j++){
      len = sprintf(buf, "%08d", j);
      if(!vlput(villa, buf, len, buf, -1, VL_DDUP)){
        pdperror(name);
        vlclose(villa);
        return 1;
      }
    }
  }
  printfflush("ok\n");
  printfflush("Inserting records with the cursor ... ");
  if(!vlcurjump(villa, "00000001", -1, VL_JFORWARD) ||
     !vlcurput(villa, "first", -1, VL_CPAFTER) || !vlcurput(villa, "second", -1, VL_CPAFTER) ||
     !vlcurnext(villa) ||
     !vlcurput(villa, "third", -1, VL_CPAFTER) ||
     strcmp(vlcurvalcache(villa, NULL), "third") ||
     !vlcurput(villa, "fourth", -1, VL_CPCURRENT) ||
     strcmp(vlcurvalcache(villa, NULL), "fourth") ||
     !vlcurjump(villa, "00000001", -1, VL_JFORWARD) ||
     strcmp(vlcurvalcache(villa, NULL), "00000001") ||
     !vlcurput(villa, "one", -1, VL_CPBEFORE) || !vlcurput(villa, "two", -1, VL_CPBEFORE) ||
     !vlcurput(villa, "three", -1, VL_CPBEFORE) || !vlcurput(villa, "five", -1, VL_CPBEFORE) ||
     !vlcurnext(villa) ||
     !vlcurput(villa, "four", -1, VL_CPBEFORE) ||
     strcmp(vlcurvalcache(villa, NULL), "four") ||
     !vlcurjump(villa, "00000001*", -1, VL_JBACKWARD) ||
     strcmp(vlcurvalcache(villa, NULL), "00000001") ||
     !vlcurput(villa, "omega", -1, VL_CPAFTER) ||
     strcmp(vlcurkeycache(villa, NULL), "00000001") ||
     strcmp(vlcurvalcache(villa, NULL), "omega") ||
     !vlcurjump(villa, "00000000*", -1, VL_JFORWARD) ||
     !vlcurput(villa, "alpha", -1, VL_CPBEFORE) ||
     strcmp(vlcurvalcache(villa, NULL), "alpha") ||
     !vlcurprev(villa) ||
     strcmp(vlcurkeycache(villa, NULL), "00000000") ||
     strcmp(vlcurvalcache(villa, NULL), "00000000") ||
     !vlcurput(villa, "before", -1, VL_CPAFTER) ||
     strcmp(vlcurvalcache(villa, NULL), "before") ||
     !vlcurjump(villa, "00000001*", -1, VL_JFORWARD) ||
     !vlcurput(villa, "after", -1, VL_CPBEFORE) ||
     strcmp(vlcurvalcache(villa, NULL), "after") ||
     !vlcurfirst(villa) ||
     strcmp(vlcurvalcache(villa, NULL), "00000000") ||
     !vlcurput(villa, "top", -1, VL_CPBEFORE) ||
     strcmp(vlcurvalcache(villa, NULL), "top") ||
     !vlcurlast(villa) ||
     !vlcurput(villa, "bottom", -1, VL_CPAFTER) ||
     strcmp(vlcurvalcache(villa, NULL), "bottom")){
    fprintf(stderr, "%s: %s: invalid\n", progname, name);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Deleting records with the cursor ... ");
  if(!vlcurjump(villa, "00000000*", -1, VL_JBACKWARD) ||
     strcmp(vlcurvalcache(villa, NULL), "before") ||
     !vlcurout(villa) ||
     strcmp(vlcurvalcache(villa, NULL), "alpha") ||
     !vlcurout(villa) ||
     strcmp(vlcurvalcache(villa, NULL), "five") ||
     !vlcurfirst(villa) || !vlcurnext(villa) ||
     !vlcurout(villa) || !vlcurout(villa) || !vlcurout(villa) ||
     strcmp(vlcurvalcache(villa, NULL), "five") ||
     !vlcurprev(villa) ||
     strcmp(vlcurvalcache(villa, NULL), "top") ||
     !vlcurout(villa) ||
     strcmp(vlcurvalcache(villa, NULL), "five") ||
     !vlcurjump(villa, "00000002", -1, VL_JBACKWARD) ||
     strcmp(vlcurvalcache(villa, NULL), "bottom") ||
     !vlcurout(villa) ||
     !vlcurjump(villa, "00000001", -1, VL_JBACKWARD) ||
     !vlcurout(villa) ||
     !vlcurout(villa) || !vlcurout(villa) || !vlcurout(villa) ||
     strcmp(vlcurkeycache(villa, NULL), "00000002") ||
     strcmp(vlcurvalcache(villa, NULL), "00000002") ||
     !vlcurout(villa) || vlcurout(villa) ||
     !vlcurfirst(villa) ||
     strcmp(vlcurvalcache(villa, NULL), "five")){
    fprintf(stderr, "%s: %s: invalid\n", progname, name);
    vlclose(villa);
    return 1;
  }
  vlcurfirst(villa);
  while(vlcurout(villa)){
    free(vlcurval(villa, NULL));
  }
  if(vlrnum(villa) != 0){
    printf("%d\n", vlrnum(villa));
    fprintf(stderr, "%s: %s: invalid\n", progname, name);
    vlclose(villa);
    return 1;
  }
  for(i = 0; i < 1000; i++){
    len = sprintf(buf, "%08d", i);
    if(!vlput(villa, buf, len, buf, -1, VL_DKEEP)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  for(i = 200; i < 800; i++){
    len = sprintf(buf, "%08d", i);
    if(!vlout(villa, buf, len)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  vlcurfirst(villa);
  while(vlcurout(villa)){
    free(vlcurval(villa, NULL));
  }
  if(vlrnum(villa) != 0){
    printf("%d\n", vlrnum(villa));
    fprintf(stderr, "%s: %s: invalid\n", progname, name);
    vlclose(villa);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Adding 3 * 100 records with VL_DDUP ... ");
  for(i = 1; i <= 100; i++){
    len = sprintf(buf, "%08d", i);
    for(j = 0; j < 3; j++){
      if(!vlput(villa, buf, len, buf, len, VL_DDUP)){
        pdperror(name);
        vlclose(villa);
        return 1;
      }
    }
  }
  printfflush("ok\n");
  printfflush("Closing the database ... ");
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Opening the database as a reader ... ");
  if(!(villa = vlopen(name, VL_OREADER, VL_CMPLEX))){
    pdperror(name);
    return 1;
  }
  printfflush("ok\n");
  printfflush("Opening multiple cursors ... ");
  mulcurs = cbmalloc(sizeof(VLMULCUR *) * 8);
  for(i = 0; i < 8; i++){
    if(!(mulcurs[i] = vlmulcuropen(villa))){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  printfflush("ok\n");
  printfflush("Scanning multiple cursors ... ");
  for(i = 0; i < 8; i++){
    if(i % 2 == 0){
      vlmulcurfirst(mulcurs[i]);
    } else {
      vlmulcurlast(mulcurs[i]);
    }
  }
  for(i = 0; i < 300; i++){
    for(j = 0; j < 8; j++){
      if(j % 2 == 0){
        if(!(vbuf = vlmulcurkey(mulcurs[j], &vsiz))){
          pdperror(name);
          vlclose(villa);
          return 1;
        }
        free(vbuf);
        vlmulcurnext(mulcurs[j]);
      } else {
        if(!(vbuf = vlmulcurval(mulcurs[j], &vsiz))){
          pdperror(name);
          vlclose(villa);
          return 1;
        }
        free(vbuf);
        vlmulcurprev(mulcurs[j]);
      }
    }
  }
  printfflush("ok\n");
  printfflush("Closing multiple cursors ... ");
  for(i = 0; i < 8; i++){
    vlmulcurclose(mulcurs[i]);
  }
  free(mulcurs);
  printfflush("ok\n");
  printfflush("Closing the database ... ");
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  printfflush("ok\n");
  printfflush("all ok\n\n");
  return 0;
}


/* perform wicked command */
int dowicked(const char *name, int rnum, int cb, int cmode){
  VILLA *villa;
  CBMAP *map;
  int i, j, omode, len, err, ksiz, vsiz, tran, mksiz, mvsiz, rsiz;
  const char *mkbuf, *mvbuf;
  char buf[32], *kbuf, *vbuf;
  CBLIST *list;
  printfflush("<Wicked Writing Test>\n  name=%s  rnum=%d\n\n", name, rnum);
  omode = VL_OWRITER | VL_OCREAT | VL_OTRUNC | cmode;
  if(!(villa = vlopen(name, omode, VL_CMPLEX))){
    pdperror(name);
    return 1;
  }
  err = FALSE;
  tran = FALSE;
  vlsettuning(villa, 5, 10, 64, 64);
  map = NULL;
  if(cb) map = cbmapopen();
  for(i = 1; i <= rnum; i++){
    len = sprintf(buf, "%08d", myrand() % rnum + 1);
    switch(cb ? (myrand() % 5) : myrand() % 16){
    case 0:
      putchar('O');
      if(!vlput(villa, buf, len, buf, len, VL_DOVER)) err = TRUE;
      if(map) cbmapput(map, buf, len, buf, len, TRUE);
      break;
    case 1:
      putchar('K');
      if(!vlput(villa, buf, len, buf, len, VL_DKEEP) && dpecode != DP_EKEEP) err = TRUE;
      if(map) cbmapput(map, buf, len, buf, len, FALSE);
      break;
    case 2:
      putchar('C');
      if(!vlput(villa, buf, len, buf, len, VL_DCAT)) err = TRUE;
      if(map) cbmapputcat(map, buf, len, buf, len);
      break;
    case 3:
      putchar('D');
      if(!vlout(villa, buf, len) && dpecode != DP_ENOITEM) err = TRUE;
      if(map) cbmapout(map, buf, len);
      break;
    case 4:
      putchar('G');
      if((vbuf = vlget(villa, buf, len, NULL)) != NULL){
        free(vbuf);
      } else if(dpecode != DP_ENOITEM){
        err = TRUE;
      }
      break;
    case 5:
      putchar('V');
      if(vlvsiz(villa, buf, len) < 0 && dpecode != DP_ENOITEM) err = TRUE;
      if(!vlvnum(villa, buf, len) && dpecode != DP_ENOITEM) err = TRUE;
      break;
    case 6:
      putchar('X');
      list = cblistopen();
      cblistpush(list, buf, len);
      cblistpush(list, buf, len);
      if(!vlputlist(villa, buf, len, list)) err = TRUE;
      cblistclose(list);
      break;
    case 7:
      putchar('Y');
      if(!vloutlist(villa, buf, len) && dpecode != DP_ENOITEM) err = TRUE;
      break;
    case 8:
      putchar('Z');
      if((list = vlgetlist(villa, buf, len)) != NULL){
        cblistclose(list);
      } else if(dpecode != DP_ENOITEM){
        err = TRUE;
      }
      if((vbuf = vlgetcat(villa, buf, len, NULL)) != NULL){
        free(vbuf);
      } else if(dpecode != DP_ENOITEM){
        err = TRUE;
      }
      break;
    case 9:
      putchar('Q');
      if(vlcurjump(villa, buf, len, VL_JFORWARD)){
        for(j = 0; j < 3 && (kbuf = vlcurkey(villa, &ksiz)); j++){
          if(VL_CMPLEX(buf, len, kbuf, ksiz) > 0) err = TRUE;
          if(strcmp(vlcurkeycache(villa, NULL), kbuf)) err = TRUE;
          if((vbuf = vlcurval(villa, &vsiz)) != NULL){
            if(strcmp(vlcurvalcache(villa, NULL), vbuf)) err = TRUE;
            free(vbuf);
          } else {
            err = TRUE;
          }
          free(kbuf);
          if(!vlcurnext(villa) && dpecode != DP_ENOITEM) err = TRUE;
        }
      } else {
        if(dpecode != DP_ENOITEM) err = TRUE;
      }
      break;
    case 10:
      putchar('W');
      if(vlcurjump(villa, buf, len, VL_JBACKWARD)){
        for(j = 0; j < 3 && (kbuf = vlcurkey(villa, &ksiz)); j++){
          if(VL_CMPLEX(buf, len, kbuf, ksiz) < 0) err = TRUE;
          if(strcmp(vlcurkeycache(villa, NULL), kbuf)) err = TRUE;
          if((vbuf = vlcurval(villa, &vsiz)) != NULL){
            if(strcmp(vlcurvalcache(villa, NULL), vbuf)) err = TRUE;
            free(vbuf);
          } else {
            err = TRUE;
          }
          free(kbuf);
          if(!vlcurprev(villa) && dpecode != DP_ENOITEM) err = TRUE;
        }
      } else {
        if(dpecode != DP_ENOITEM) err = TRUE;
      }
      break;
    case 11:
      putchar('L');
      if(myrand() % 3 == 0 &&
         !vlcurjump(villa, buf, len, i % 3 == 0 ? VL_JFORWARD : VL_JBACKWARD) &&
         dpecode != DP_ENOITEM) err = TRUE;
      for(j = myrand() % 5; j >= 0; j--){
        switch(myrand() % 6){
        case 0:
          if(!vlcurput(villa, buf, len, VL_CPAFTER) && dpecode != DP_ENOITEM) err = TRUE;
          break;
        case 1:
          if(!vlcurput(villa, buf, len, VL_CPBEFORE) && dpecode != DP_ENOITEM) err = TRUE;
          break;
        case 2:
          if(!vlcurput(villa, buf, len, VL_CPCURRENT) && dpecode != DP_ENOITEM) err = TRUE;
          break;
        default:
          if(!vlcurout(villa)){
            if(dpecode != DP_ENOITEM) err = TRUE;
            break;
          }
          break;
        }
      }
      break;
    case 12:
      if(tran ? myrand() % 32 != 0 : myrand() % 1024 != 0){
        putchar('N');
        break;
      }
      putchar('T');
      if(tran){
        if(myrand() % 5 == 0){
          if(!vltranabort(villa)) err = TRUE;
        } else {
          if(!vltrancommit(villa)) err = TRUE;
        }
        tran = FALSE;
      } else {
        if(!vltranbegin(villa)) err = TRUE;
        tran = TRUE;
      }
      break;
    default:
      putchar('P');
      if(!vlput(villa, buf, len, buf, len, myrand() % 3 == 0 ? VL_DDUPR : VL_DDUP)) err = TRUE;
      break;
    }
    if(i % 50 == 0) printfflush(" (%08d)\n", i);
    if(err){
      pdperror(name);
      break;
    }
  }
  if(tran){
    if(!vltranabort(villa)) err = TRUE;
  }
  if(!vloptimize(villa)){
    pdperror(name);
    err = TRUE;
  }
  if((rnum = vlrnum(villa)) == -1){
    pdperror(name);
    err = TRUE;
  }
  if(!vlcurfirst(villa)){
    pdperror(name);
    err = TRUE;
  }
  i = 0;
  do {
    kbuf = NULL;
    vbuf = NULL;
    if(!(kbuf = vlcurkey(villa, &ksiz)) || !(vbuf = vlcurval(villa, &vsiz)) ||
       ksiz != 8 || vsiz % 8 != 0 || vlvnum(villa, kbuf, ksiz) < 1){
      pdperror(name);
      free(kbuf);
      free(vbuf);
      err = TRUE;
      break;
    }
    free(kbuf);
    free(vbuf);
    i++;
  } while(vlcurnext(villa));
  if(i != rnum){
    fprintf(stderr, "%s: %s: invalid cursor\n", progname, name);
    err = TRUE;
  }
  if(dpecode != DP_ENOITEM){
    pdperror(name);
    err = TRUE;
  }
  if(!vlcurlast(villa)){
    pdperror(name);
    err = TRUE;
  }
  i = 0;
  do {
    kbuf = NULL;
    vbuf = NULL;
    if(!(kbuf = vlcurkey(villa, &ksiz)) || !(vbuf = vlcurval(villa, &vsiz)) ||
       ksiz != 8 || vsiz % 8 != 0 || vlvnum(villa, kbuf, ksiz) < 1){
      pdperror(name);
      free(kbuf);
      free(vbuf);
      err = TRUE;
      break;
    }
    free(kbuf);
    free(vbuf);
    i++;
  } while(vlcurprev(villa));
  if(i != rnum){
    fprintf(stderr, "%s: %s: invalid cursor\n", progname, name);
    err = TRUE;
  }
  if(dpecode != DP_ENOITEM){
    pdperror(name);
    err = TRUE;
  }
  if(map){
    printfflush("Matching records ... ");
    cbmapiterinit(map);
    while((mkbuf = cbmapiternext(map, &mksiz)) != NULL){
      mvbuf = cbmapget(map, mkbuf, mksiz, &mvsiz);
      if(!(vbuf = vlget(villa, mkbuf, mksiz, &rsiz))){
        pdperror(name);
        err = TRUE;
        break;
      }
      if(rsiz != mvsiz || memcmp(vbuf, mvbuf, rsiz)){
        fprintf(stderr, "%s: %s: unmatched record\n", progname, name);
        free(vbuf);
        err = TRUE;
        break;
      }
      free(vbuf);
    }
    cbmapclose(map);
    if(!err) printfflush("ok\n");
  }
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  if(!err) printfflush("ok\n\n");
  return err ? 1 : 0;
}



/* END OF FILE */
