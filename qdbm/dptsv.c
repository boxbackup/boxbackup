/*************************************************************************************************
 * Mutual converter between a database of Depot and a TSV text
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
int runimport(int argc, char **argv);
int runexport(int argc, char **argv);
void pdperror(const char *name);
char *getl(void);
int doimport(const char *name, int bnum, int bin);
int doexport(const char *name, int bin);


/* main routine */
int main(int argc, char **argv){
  int rv;
  cbstdiobin();
  progname = argv[0];
  if(argc < 2) usage();
  rv = 0;
  if(!strcmp(argv[1], "import")){
    rv = runimport(argc, argv);
  } else if(!strcmp(argv[1], "export")){
    rv = runexport(argc, argv);
  } else {
    usage();
  }
  return rv;
}


/* print the usage and exit */
void usage(void){
  fprintf(stderr, "%s: mutual converter between TSV and Depot database\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s import [-bnum num] [-bin] name\n", progname);
  fprintf(stderr, "  %s export [-bin] name\n", progname);
  fprintf(stderr, "\n");
  exit(1);
}


/* parse arguments of import command */
int runimport(int argc, char **argv){
  char *name;
  int i, bnum, bin, rv;
  name = NULL;
  bnum = -1;
  bin = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-bnum")){
        if(++i >= argc) usage();
        bnum = atoi(argv[i]);
      } else if(!strcmp(argv[i], "-bin")){
        bin = TRUE;
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
  rv = doimport(name, bnum, bin);
  return rv;
}


/* parse arguments of export command */
int runexport(int argc, char **argv){
  char *name;
  int i, bin, rv;
  name = NULL;
  bin = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-bin")){
        bin = TRUE;
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
  rv = doexport(name, bin);
  return rv;
}


/* print an error message */
void pdperror(const char *name){
  fprintf(stderr, "%s: %s: %s\n", progname, name, dperrmsg(dpecode));
}


/* read a line */
char *getl(void){
  char *buf;
  int c, len, blen;
  buf = NULL;
  len = 0;
  blen = 256;
  while((c = getchar()) != EOF){
    if(blen <= len) blen *= 2;
    buf = cbrealloc(buf, blen + 1);
    if(c == '\n') c = '\0';
    buf[len++] = c;
    if(c == '\0') break;
  }
  if(!buf) return NULL;
  buf[len] = '\0';
  return buf;
}


/* perform import command */
int doimport(const char *name, int bnum, int bin){
  DEPOT *depot;
  char *buf, *kbuf, *vbuf, *ktmp, *vtmp;
  int i, err, ktsiz, vtsiz;
  /* open a database */
  if(!(depot = dpopen(name, DP_OWRITER | DP_OCREAT, bnum))){
    pdperror(name);
    return 1;
  }
  /* loop for each line */
  err = FALSE;
  for(i = 1; (buf = getl()) != NULL; i++){
    kbuf = buf;
    if((vbuf = strchr(buf, '\t')) != NULL){
      *vbuf = '\0';
      vbuf++;
      /* store a record */
      if(bin){
        ktmp = cbbasedecode(kbuf, &ktsiz);
        vtmp = cbbasedecode(vbuf, &vtsiz);
        if(!dpput(depot, ktmp, ktsiz, vtmp, vtsiz, DP_DOVER)){
          pdperror(name);
          err = TRUE;
        }
        free(vtmp);
        free(ktmp);
      } else {
        if(!dpput(depot, kbuf, -1, vbuf, -1, DP_DOVER)){
          pdperror(name);
          err = TRUE;
        }
      }
    } else {
      fprintf(stderr, "%s: %s: invalid format in line %d\n", progname, name, i);
    }
    free(buf);
    if(err) break;
  }
  /* close the database */
  if(!dpclose(depot)){
    pdperror(name);
    return 1;
  }
  return err ? 1 : 0;
}


/* perform export command */
int doexport(const char *name, int bin){
  DEPOT *depot;
  char *kbuf, *vbuf, *tmp;
  int err, ksiz, vsiz;
  /* open a database */
  if(!(depot = dpopen(name, DP_OREADER, -1))){
    pdperror(name);
    return 1;
  }
  /* initialize the iterator */
  dpiterinit(depot);
  /* loop for each key */
  err = FALSE;
  while((kbuf = dpiternext(depot, &ksiz)) != NULL){
    /* retrieve a value with a key */
    if(!(vbuf = dpget(depot, kbuf, ksiz, 0, -1, &vsiz))){
      pdperror(name);
      free(kbuf);
      err = TRUE;
      break;
    }
    /* output data */
    if(bin){
      tmp = cbbaseencode(kbuf, ksiz);
      printf("%s\t", tmp);
      free(tmp);
      tmp = cbbaseencode(vbuf, vsiz);
      printf("%s\n", tmp);
      free(tmp);
    } else {
      printf("%s\t%s\n", kbuf, vbuf);
    }
    /* free resources */
    free(vbuf);
    free(kbuf);
  }
  /* check whether all records were retrieved */
  if(dpecode != DP_ENOITEM){
    pdperror(name);
    err = TRUE;
  }
  /* close the database */
  if(!dpclose(depot)){
    pdperror(name);
    return 1;
  }
  return err ? 1 : 0;
}



/* END OF FILE */
