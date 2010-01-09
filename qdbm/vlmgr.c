/*************************************************************************************************
 * Utility for debugging Villa and its applications
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

#undef TRUE
#define TRUE           1                 /* boolean true */
#undef FALSE
#define FALSE          0                 /* boolean false */


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
char *hextoobj(const char *str, int *sp);
char *dectoiobj(const char *str, int *sp);
int runcreate(int argc, char **argv);
int runput(int argc, char **argv);
int runout(int argc, char **argv);
int runget(int argc, char **argv);
int runlist(int argc, char **argv);
int runoptimize(int argc, char **argv);
int runinform(int argc, char **argv);
int runremove(int argc, char **argv);
int runrepair(int argc, char **argv);
int runexportdb(int argc, char **argv);
int runimportdb(int argc, char **argv);
void pdperror(const char *name);
void printobj(const char *obj, int size);
void printobjhex(const char *obj, int size);
int docreate(const char *name, int cmode);
int doput(const char *name, const char *kbuf, int ksiz, const char *vbuf, int vsiz,
          int dmode, VLCFUNC cmp);
int doout(const char *name, const char *kbuf, int ksiz, VLCFUNC cmp, int lb);
int doget(const char *name, int opts, const char *kbuf, int ksiz, VLCFUNC cmp,
          int lb, int ox, int nb);
int dolist(const char *name, int opts, const char *tbuf, int tsiz, const char *bbuf, int bsiz,
           VLCFUNC cmp, int ki, int kb, int vb, int ox, int gt, int lt, int max, int desc);
int dooptimize(const char *name);
int doinform(const char *name, int opts);
int doremove(const char *name);
int dorepair(const char *name, VLCFUNC cmp);
int doexportdb(const char *name, const char *file, VLCFUNC cmp);
int doimportdb(const char *name, const char *file, VLCFUNC cmp);


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
    rv = runcreate(argc, argv);
  } else if(!strcmp(argv[1], "put")){
    rv = runput(argc, argv);
  } else if(!strcmp(argv[1], "out")){
    rv = runout(argc, argv);
  } else if(!strcmp(argv[1], "get")){
    rv = runget(argc, argv);
  } else if(!strcmp(argv[1], "list")){
    rv = runlist(argc, argv);
  } else if(!strcmp(argv[1], "optimize")){
    rv = runoptimize(argc, argv);
  } else if(!strcmp(argv[1], "inform")){
    rv = runinform(argc, argv);
  } else if(!strcmp(argv[1], "remove")){
    rv = runremove(argc, argv);
  } else if(!strcmp(argv[1], "repair")){
    rv = runrepair(argc, argv);
  } else if(!strcmp(argv[1], "exportdb")){
    rv = runexportdb(argc, argv);
  } else if(!strcmp(argv[1], "importdb")){
    rv = runimportdb(argc, argv);
  } else if(!strcmp(argv[1], "version") || !strcmp(argv[1], "--version")){
    printf("Powered by QDBM version %s on %s%s\n",
           dpversion, dpsysname, dpisreentrant ? " (reentrant)" : "");
    printf("Copyright (c) 2000-2007 Mikio Hirabayashi\n");
    rv = 0;
  } else {
    usage();
  }
  return rv;
}


/* print the usage and exit */
void usage(void){
  fprintf(stderr, "%s: administration utility for Villa\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s create [-cz|-cy|-cx] name\n", progname);
  fprintf(stderr, "  %s put [-kx|-ki] [-vx|-vi|-vf] [-keep|-cat|-dup] name key val\n", progname);
  fprintf(stderr, "  %s out [-l] [-kx|-ki] name key\n", progname);
  fprintf(stderr, "  %s get [-nl] [-l] [-kx|-ki] [-ox] [-n] name key\n", progname);
  fprintf(stderr, "  %s list [-nl] [-k|-v] [-kx|-ki] [-ox] [-top key] [-bot key] [-gt] [-lt]"
          " [-max num] [-desc] name\n", progname);
  fprintf(stderr, "  %s optimize name\n", progname);
  fprintf(stderr, "  %s inform [-nl] name\n", progname);
  fprintf(stderr, "  %s remove name\n", progname);
  fprintf(stderr, "  %s repair [-ki] name\n", progname);
  fprintf(stderr, "  %s exportdb [-ki] name file\n", progname);
  fprintf(stderr, "  %s importdb [-ki] name file\n", progname);
  fprintf(stderr, "  %s version\n", progname);
  fprintf(stderr, "\n");
  exit(1);
}


/* create a binary object from a hexadecimal string */
char *hextoobj(const char *str, int *sp){
  char *buf, mbuf[3];
  int len, i, j;
  len = strlen(str);
  if(!(buf = malloc(len + 1))) return NULL;
  j = 0;
  for(i = 0; i < len; i += 2){
    while(strchr(" \n\r\t\f\v", str[i])){
      i++;
    }
    if((mbuf[0] = str[i]) == '\0') break;
    if((mbuf[1] = str[i+1]) == '\0') break;
    mbuf[2] = '\0';
    buf[j++] = (char)strtol(mbuf, NULL, 16);
  }
  buf[j] = '\0';
  *sp = j;
  return buf;
}


/* create a integer object from a decimal string */
char *dectoiobj(const char *str, int *sp){
  char *buf;
  int num;
  num = atoi(str);
  if(!(buf = malloc(sizeof(int)))) return NULL;
  *(int *)buf = num;
  *sp = sizeof(int);
  return buf;
}


/* parse arguments of create command */
int runcreate(int argc, char **argv){
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
  rv = docreate(name, cmode);
  return rv;
}


/* parse arguments of put command */
int runput(int argc, char **argv){
  char *name, *key, *val, *kbuf, *vbuf;
  int i, kx, ki, vx, vi, vf, ksiz, vsiz, rv;
  int dmode;
  name = NULL;
  kx = FALSE;
  ki = FALSE;
  vx = FALSE;
  vi = FALSE;
  vf = FALSE;
  key = NULL;
  val = NULL;
  dmode = VL_DOVER;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-kx")){
        kx = TRUE;
      } else if(!strcmp(argv[i], "-ki")){
        ki = TRUE;
      } else if(!strcmp(argv[i], "-vx")){
        vx = TRUE;
      } else if(!strcmp(argv[i], "-vi")){
        vi = TRUE;
      } else if(!strcmp(argv[i], "-vf")){
        vf = TRUE;
      } else if(!strcmp(argv[i], "-keep")){
        dmode = VL_DKEEP;
      } else if(!strcmp(argv[i], "-cat")){
        dmode = VL_DCAT;
      } else if(!strcmp(argv[i], "-dup")){
        dmode = VL_DDUP;
      } else {
        usage();
      }
    } else if(!name){
      name = argv[i];
    } else if(!key){
      key = argv[i];
    } else if(!val){
      val = argv[i];
    } else {
      usage();
    }
  }
  if(!name || !key || !val) usage();
  if(kx){
    kbuf = hextoobj(key, &ksiz);
  } else if(ki){
    kbuf = dectoiobj(key, &ksiz);
  } else {
    kbuf = cbmemdup(key, -1);
    ksiz = -1;
  }
  if(vx){
    vbuf = hextoobj(val, &vsiz);
  } else if(vi){
    vbuf = dectoiobj(val, &vsiz);
  } else if(vf){
    vbuf = cbreadfile(val, &vsiz);
  } else {
    vbuf = cbmemdup(val, -1);
    vsiz = -1;
  }
  if(kbuf && vbuf){
    rv = doput(name, kbuf, ksiz, vbuf, vsiz, dmode, ki ? VL_CMPINT : VL_CMPLEX);
  } else {
    if(vf){
      fprintf(stderr, "%s: %s: cannot read\n", progname, val);
    } else {
      fprintf(stderr, "%s: out of memory\n", progname);
    }
    rv = 1;
  }
  free(kbuf);
  free(vbuf);
  return rv;
}


/* parse arguments of out command */
int runout(int argc, char **argv){
  char *name, *key, *kbuf;
  int i, kx, ki, lb, ksiz, rv;
  name = NULL;
  kx = FALSE;
  ki = FALSE;
  lb = FALSE;
  key = NULL;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-l")){
        lb = TRUE;
      } else if(!strcmp(argv[i], "-kx")){
        kx = TRUE;
      } else if(!strcmp(argv[i], "-ki")){
        ki = TRUE;
      } else {
        usage();
      }
    } else if(!name){
      name = argv[i];
    } else if(!key){
      key = argv[i];
    } else {
      usage();
    }
  }
  if(!name || !key) usage();
  if(kx){
    kbuf = hextoobj(key, &ksiz);
  } else if(ki){
    kbuf = dectoiobj(key, &ksiz);
  } else {
    kbuf = cbmemdup(key, -1);
    ksiz = -1;
  }
  if(kbuf){
    rv = doout(name, kbuf, ksiz, ki ? VL_CMPINT : VL_CMPLEX, lb);
  } else {
    fprintf(stderr, "%s: out of memory\n", progname);
    rv = 1;
  }
  free(kbuf);
  return rv;
}


/* parse arguments of get command */
int runget(int argc, char **argv){
  char *name, *key, *kbuf;
  int i, opts, lb, kx, ki, ox, nb, ksiz, rv;
  name = NULL;
  opts = 0;
  lb = FALSE;
  kx = FALSE;
  ki = FALSE;
  ox = FALSE;
  nb = FALSE;
  key = NULL;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-nl")){
        opts |= VL_ONOLCK;
      } else if(!strcmp(argv[i], "-l")){
        lb = TRUE;
      } else if(!strcmp(argv[i], "-kx")){
        kx = TRUE;
      } else if(!strcmp(argv[i], "-ki")){
        ki = TRUE;
      } else if(!strcmp(argv[i], "-ox")){
        ox = TRUE;
      } else if(!strcmp(argv[i], "-n")){
        nb = TRUE;
      } else {
        usage();
      }
    } else if(!name){
      name = argv[i];
    } else if(!key){
      key = argv[i];
    } else {
      usage();
    }
  }
  if(!name || !key) usage();
  if(kx){
    kbuf = hextoobj(key, &ksiz);
  } else if(ki){
    kbuf = dectoiobj(key, &ksiz);
  } else {
    kbuf = cbmemdup(key, -1);
    ksiz = -1;
  }
  if(kbuf){
    rv = doget(name, opts, kbuf, ksiz, ki ? VL_CMPINT : VL_CMPLEX, lb, ox, nb);
  } else {
    fprintf(stderr, "%s: out of memory\n", progname);
    rv = 1;
  }
  free(kbuf);
  return rv;
}


/* parse arguments of list command */
int runlist(int argc, char **argv){
  char *name, *top, *bot, *tbuf, *bbuf, *nstr;
  int i, opts, kb, vb, kx, ki, ox, gt, lt, max, desc, tsiz, bsiz, rv;
  name = NULL;
  opts = 0;
  kb = FALSE;
  vb = FALSE;
  kx = FALSE;
  ki = FALSE;
  ox = FALSE;
  gt = FALSE;
  lt = FALSE;
  max = -1;
  desc = FALSE;
  top = NULL;
  bot = NULL;
  nstr = NULL;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-nl")){
        opts |= VL_ONOLCK;
      } else if(!strcmp(argv[i], "-k")){
        kb = TRUE;
      } else if(!strcmp(argv[i], "-v")){
        vb = TRUE;
      } else if(!strcmp(argv[i], "-kx")){
        kx = TRUE;
      } else if(!strcmp(argv[i], "-ki")){
        ki = TRUE;
      } else if(!strcmp(argv[i], "-ox")){
        ox = TRUE;
      } else if(!strcmp(argv[i], "-top")){
        if(++i >= argc) usage();
        top = argv[i];
      } else if(!strcmp(argv[i], "-bot")){
        if(++i >= argc) usage();
        bot = argv[i];
      } else if(!strcmp(argv[i], "-gt")){
        gt = TRUE;
      } else if(!strcmp(argv[i], "-lt")){
        lt = TRUE;
      } else if(!strcmp(argv[i], "-max")){
        if(++i >= argc) usage();
        max = atoi(argv[i]);
      } else if(!strcmp(argv[i], "-desc")){
        desc = TRUE;
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
  tbuf = NULL;
  bbuf = NULL;
  if(kx){
    if(top) tbuf = hextoobj(top, &tsiz);
    if(bot) bbuf = hextoobj(bot, &bsiz);
  } else if(ki){
    if(top) tbuf = dectoiobj(top, &tsiz);
    if(bot) bbuf = dectoiobj(bot, &bsiz);
  } else {
    if(top){
      tbuf = cbmemdup(top, -1);
      tsiz = strlen(tbuf);
    }
    if(bot){
      bbuf = cbmemdup(bot, -1);
      bsiz = strlen(bbuf);
    }
  }
  rv = dolist(name, opts, tbuf, tsiz, bbuf, bsiz, ki ? VL_CMPINT : VL_CMPLEX, ki,
              kb, vb, ox, gt, lt, max, desc);
  free(tbuf);
  free(bbuf);
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
  int i, opts, rv;
  name = NULL;
  opts = 0;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-nl")){
        opts |= VL_ONOLCK;
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
  rv = doinform(name, opts);
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


/* parse arguments of repair command */
int runrepair(int argc, char **argv){
  char *name;
  int i, ki, rv;
  name = NULL;
  ki = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-ki")){
        ki = TRUE;
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
  rv = dorepair(name, ki ? VL_CMPINT : VL_CMPLEX);
  return rv;
}


/* parse arguments of exportdb command */
int runexportdb(int argc, char **argv){
  char *name, *file;
  int i, ki, rv;
  name = NULL;
  file = NULL;
  ki = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-ki")){
        ki = TRUE;
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
  if(!name || !file) usage();
  rv = doexportdb(name, file, ki ? VL_CMPINT : VL_CMPLEX);
  return rv;
}


/* parse arguments of importdb command */
int runimportdb(int argc, char **argv){
  char *name, *file;
  int i, ki, rv;
  name = NULL;
  file = NULL;
  ki = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-ki")){
        ki = TRUE;
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
  if(!name || !file) usage();
  rv = doimportdb(name, file, ki ? VL_CMPINT : VL_CMPLEX);
  return rv;
}


/* print an error message */
void pdperror(const char *name){
  fprintf(stderr, "%s: %s: %s\n", progname, name, dperrmsg(dpecode));
}


/* print an object */
void printobj(const char *obj, int size){
  int i;
  for(i = 0; i < size; i++){
    putchar(obj[i]);
  }
}


/* print an object as a hexadecimal string */
void printobjhex(const char *obj, int size){
  int i;
  for(i = 0; i < size; i++){
    printf("%s%02X", i > 0 ? " " : "", ((const unsigned char *)obj)[i]);
  }
}


/* perform create command */
int docreate(const char *name, int cmode){
  VILLA *villa;
  int omode;
  omode = VL_OWRITER | VL_OCREAT | VL_OTRUNC | cmode;
  if(!(villa = vlopen(name, omode, VL_CMPLEX))){
    pdperror(name);
    return 1;
  }
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform put command */
int doput(const char *name, const char *kbuf, int ksiz, const char *vbuf, int vsiz,
          int dmode, VLCFUNC cmp){
  VILLA *villa;
  if(!(villa = vlopen(name, VL_OWRITER, cmp))){
    pdperror(name);
    return 1;
  }
  if(!vlput(villa, kbuf, ksiz, vbuf, vsiz, dmode)){
    pdperror(name);
    vlclose(villa);
    return 1;
  }
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform out command */
int doout(const char *name, const char *kbuf, int ksiz, VLCFUNC cmp, int lb){
  VILLA *villa;
  if(!(villa = vlopen(name, VL_OWRITER, cmp))){
    pdperror(name);
    return 1;
  }
  if(lb){
    if(!vloutlist(villa, kbuf, ksiz)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  } else {
    if(!vlout(villa, kbuf, ksiz)){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
  }
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform get command */
int doget(const char *name, int opts, const char *kbuf, int ksiz, VLCFUNC cmp,
          int lb, int ox, int nb){
  VILLA *villa;
  CBLIST *vals;
  char *vbuf;
  int vsiz;
  if(!(villa = vlopen(name, VL_OREADER | opts, cmp))){
    pdperror(name);
    return 1;
  }
  if(lb){
    if(!(vals = vlgetlist(villa, kbuf, ksiz))){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
    while((vbuf = cblistshift(vals, &vsiz)) != NULL){
      if(ox){
        printobjhex(vbuf, vsiz);
      } else {
        printobj(vbuf, vsiz);
      }
      free(vbuf);
      putchar('\n');
    }
    cblistclose(vals);
  } else {
    if(!(vbuf = vlget(villa, kbuf, ksiz, &vsiz))){
      pdperror(name);
      vlclose(villa);
      return 1;
    }
    if(ox){
      printobjhex(vbuf, vsiz);
    } else {
      printobj(vbuf, vsiz);
    }
    free(vbuf);
    if(!nb) putchar('\n');
  }
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform list command */
int dolist(const char *name, int opts, const char *tbuf, int tsiz, const char *bbuf, int bsiz,
           VLCFUNC cmp, int ki, int kb, int vb, int ox, int gt, int lt, int max, int desc){
  VILLA *villa;
  char *kbuf, *vbuf;
  int ksiz, vsiz, show, rv;
  if(!(villa = vlopen(name, VL_OREADER | opts, cmp))){
    pdperror(name);
    return 1;
  }
  if(max < 0) max = vlrnum(villa);
  if(desc){
    if(bbuf){
      vlcurjump(villa, bbuf, bsiz, VL_JBACKWARD);
    } else {
      vlcurlast(villa);
    }
    show = 0;
    while(show < max && (kbuf = vlcurkey(villa, &ksiz)) != NULL){
      if(bbuf && lt){
        if(cmp(kbuf, ksiz, bbuf, bsiz) == 0){
          free(kbuf);
          vlcurnext(villa);
          continue;
        }
        lt = FALSE;
      }
      if(tbuf){
        rv = cmp(kbuf, ksiz, tbuf, tsiz);
        if(rv < 0 || (gt && rv == 0)){
          free(kbuf);
          break;
        }
      }
      if(!(vbuf = vlcurval(villa, &vsiz))){
        free(kbuf);
        break;
      }
      if(ox){
        if(!vb) printobjhex(kbuf, ksiz);
        if(!kb && !vb) putchar('\t');
        if(!kb) printobjhex(vbuf, vsiz);
      } else {
        if(!vb) printobj(kbuf, ksiz);
        if(!kb && !vb) putchar('\t');
        if(!kb) printobj(vbuf, vsiz);
      }
      putchar('\n');
      free(kbuf);
      free(vbuf);
      show++;
      vlcurprev(villa);
    }
  } else {
    if(tbuf){
      vlcurjump(villa, tbuf, tsiz, VL_JFORWARD);
    } else {
      vlcurfirst(villa);
    }
    show = 0;
    while(show < max && (kbuf = vlcurkey(villa, &ksiz)) != NULL){
      if(tbuf && gt){
        if(cmp(kbuf, ksiz, tbuf, tsiz) == 0){
          free(kbuf);
          vlcurnext(villa);
          continue;
        }
        gt = FALSE;
      }
      if(bbuf){
        rv = cmp(kbuf, ksiz, bbuf, bsiz);
        if(rv > 0 || (lt && rv == 0)){
          free(kbuf);
          break;
        }
      }
      if(!(vbuf = vlcurval(villa, &vsiz))){
        free(kbuf);
        break;
      }
      if(ox){
        if(!vb) printobjhex(kbuf, ksiz);
        if(!kb && !vb) putchar('\t');
        if(!kb) printobjhex(vbuf, vsiz);
      } else {
        if(!vb) printobj(kbuf, ksiz);
        if(!kb && !vb) putchar('\t');
        if(!kb) printobj(vbuf, vsiz);
      }
      putchar('\n');
      free(kbuf);
      free(vbuf);
      show++;
      vlcurnext(villa);
    }
  }
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform optimize command */
int dooptimize(const char *name){
  VILLA *villa;
  if(!(villa = vlopen(name, VL_OWRITER, VL_CMPLEX))){
    pdperror(name);
    return 1;
  }
  if(!vloptimize(villa)){
    pdperror(name);
    vlclose(villa);
    return 1;
  }
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform inform command */
int doinform(const char *name, int opts){
  VILLA *villa;
  char *tmp;
  if(!(villa = vlopen(name, VL_OREADER | opts, VL_CMPLEX))){
    pdperror(name);
    return 1;
  }
  tmp = vlname(villa);
  printf("name: %s\n", tmp ? tmp : "(null)");
  free(tmp);
  printf("file size: %d\n", vlfsiz(villa));
  printf("leaf nodes: %d\n", vllnum(villa));
  printf("non-leaf nodes: %d\n", vlnnum(villa));
  printf("records: %d\n", vlrnum(villa));
  printf("inode number: %d\n", vlinode(villa));
  printf("modified time: %.0f\n", (double)vlmtime(villa));
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform remove command */
int doremove(const char *name){
  if(!vlremove(name)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform repair command */
int dorepair(const char *name, VLCFUNC cmp){
  if(!vlrepair(name, cmp)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform exportdb command */
int doexportdb(const char *name, const char *file, VLCFUNC cmp){
  VILLA *villa;
  if(!(villa = vlopen(name, VL_OREADER, cmp))){
    pdperror(name);
    return 1;
  }
  if(!vlexportdb(villa, file)){
    pdperror(name);
    vlclose(villa);
    return 1;
  }
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  return 0;
}


/* perform importdb command */
int doimportdb(const char *name, const char *file, VLCFUNC cmp){
  VILLA *villa;
  if(!(villa = vlopen(name, VL_OWRITER | VL_OCREAT | VL_OTRUNC, cmp))){
    pdperror(name);
    return 1;
  }
  if(!vlimportdb(villa, file)){
    pdperror(name);
    vlclose(villa);
    return 1;
  }
  if(!vlclose(villa)){
    pdperror(name);
    return 1;
  }
  return 0;
}



/* END OF FILE */
