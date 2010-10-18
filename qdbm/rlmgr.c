/*************************************************************************************************
 * Utility for debugging Relic and its applications
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


#include <relic.h>
#include <cabin.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
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
int runcreate(int argc, char **argv);
int runstore(int argc, char **argv);
int rundelete(int argc, char **argv);
int runfetch(int argc, char **argv);
int runlist(int argc, char **argv);
void pmyerror(const char *name, const char *msg);
void printobj(const char *obj, int size);
void printobjhex(const char *obj, int size);
int docreate(char *name);
int dostore(char *name, const char *kbuf, int ksiz, const char *vbuf, int vsiz, int ins);
int dodelete(char *name, const char *kbuf, int ksiz);
int dofetch(char *name, const char *kbuf, int ksiz, int ox, int nb);
int dolist(char *name, int ox);


/* main routine */
int main(int argc, char **argv){
  int rv;
  cbstdiobin();
  progname = argv[0];
  if(argc < 2) usage();
  rv = 0;
  if(!strcmp(argv[1], "create")){
    rv = runcreate(argc, argv);
  } else if(!strcmp(argv[1], "store")){
    rv = runstore(argc, argv);
  } else if(!strcmp(argv[1], "delete")){
    rv = rundelete(argc, argv);
  } else if(!strcmp(argv[1], "fetch")){
    rv = runfetch(argc, argv);
  } else if(!strcmp(argv[1], "list")){
    rv = runlist(argc, argv);
  } else {
    usage();
  }
  return rv;
}


/* print the usage and exit */
void usage(void){
  fprintf(stderr, "%s: administration utility for Relic\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s create name\n", progname);
  fprintf(stderr, "  %s store [-kx] [-vx|-vf] [-insert] name key val\n", progname);
  fprintf(stderr, "  %s delete [-kx] name key\n", progname);
  fprintf(stderr, "  %s fetch [-kx] [-ox] [-n] name key\n", progname);
  fprintf(stderr, "  %s list [-ox] name\n", progname);
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


/* parse arguments of store command */
int runstore(int argc, char **argv){
  char *name, *key, *val, *kbuf, *vbuf;
  int i, kx, vx, vf, ins, ksiz, vsiz, rv;
  name = NULL;
  kx = FALSE;
  vx = FALSE;
  vf = FALSE;
  ins = FALSE;
  key = NULL;
  val = NULL;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-kx")){
        kx = TRUE;
      } else if(!strcmp(argv[i], "-vx")){
        vx = TRUE;
      } else if(!strcmp(argv[i], "-vf")){
        vf = TRUE;
      } else if(!strcmp(argv[i], "-insert")){
        ins = TRUE;
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
  } else {
    kbuf = cbmemdup(key, -1);
    ksiz = strlen(kbuf);
  }
  if(vx){
    vbuf = hextoobj(val, &vsiz);
  } else if(vf){
    vbuf = cbreadfile(val, &vsiz);
  } else {
    vbuf = cbmemdup(val, -1);
    vsiz = strlen(vbuf);
  }
  if(kbuf && vbuf){
    rv = dostore(name, kbuf, ksiz, vbuf, vsiz, ins);
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


/* parse arguments of delete command */
int rundelete(int argc, char **argv){
  char *name, *key, *kbuf;
  int i, kx, ksiz, rv;
  name = NULL;
  kx = FALSE;
  key = NULL;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-kx")){
        kx = TRUE;
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
  } else {
    kbuf = cbmemdup(key, -1);
    ksiz = strlen(kbuf);
  }
  if(kbuf){
    rv = dodelete(name, kbuf, ksiz);
  } else {
    fprintf(stderr, "%s: out of memory\n", progname);
    rv = 1;
  }
  free(kbuf);
  return rv;
}


/* parse arguments of fetch command */
int runfetch(int argc, char **argv){
  char *name, *key, *kbuf;
  int i, kx, ox, nb, ksiz, rv;
  name = NULL;
  kx = FALSE;
  ox = FALSE;
  nb = FALSE;
  key = NULL;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-kx")){
        kx = TRUE;
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
  } else {
    kbuf = cbmemdup(key, -1);
    ksiz = strlen(kbuf);
  }
  if(kbuf){
    rv = dofetch(name, kbuf, ksiz, ox, nb);
  } else {
    fprintf(stderr, "%s: out of memory\n", progname);
    rv = 1;
  }
  free(kbuf);
  return rv;
}


/* parse arguments of list command */
int runlist(int argc, char **argv){
  char *name;
  int i, ox, rv;
  name = NULL;
  ox = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-ox")){
        ox = TRUE;
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
  rv = dolist(name, ox);
  return rv;
}


/* print an error message */
void pmyerror(const char *name, const char *msg){
  fprintf(stderr, "%s: %s: %s\n", progname, name, msg);
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
int docreate(char *name){
  DBM *db;
  if(!(db = dbm_open(name, O_RDWR | O_CREAT | O_TRUNC, 00644))){
    pmyerror(name, "dbm_open failed");
    return 1;
  }
  dbm_close(db);
  return 0;
}


/* perform store command */
int dostore(char *name, const char *kbuf, int ksiz, const char *vbuf, int vsiz, int ins){
  DBM *db;
  datum key, content;
  int rv;
  if(!(db = dbm_open(name, O_RDWR, 00644))){
    pmyerror(name, "dbm_open failed");
    return 1;
  }
  key.dptr = (char *)kbuf;
  key.dsize = ksiz;
  content.dptr = (char *)vbuf;
  content.dsize = vsiz;
  switch(dbm_store(db, key, content, ins ? DBM_INSERT : DBM_REPLACE)){
  case 0:
    rv = 0;
    break;
  case 1:
    pmyerror(name, "dbm_store failed by insert");
    rv = 1;
    break;
  default:
    pmyerror(name, "dbm_store failed");
    rv = 1;
    break;
  }
  dbm_close(db);
  return rv;
}


/* perform delete command */
int dodelete(char *name, const char *kbuf, int ksiz){
  DBM *db;
  datum key;
  int rv;
  if(!(db = dbm_open((char *)name, O_RDWR, 00644))){
    pmyerror(name, "dbm_open failed");
    return 1;
  }
  key.dptr = (char *)kbuf;
  key.dsize = ksiz;
  if(dbm_delete(db, key) == 0){
    rv = 0;
  } else {
    pmyerror(name, "dbm_delete failed");
    rv = 1;
  }
  dbm_close(db);
  return rv;
}


/* perform fetch command */
int dofetch(char *name, const char *kbuf, int ksiz, int ox, int nb){
  DBM *db;
  datum key, content;
  int rv;
  if(!(db = dbm_open((char *)name, O_RDONLY, 00644))){
    pmyerror(name, "dbm_open failed");
    return 1;
  }
  key.dptr = (char *)kbuf;
  key.dsize = ksiz;
  content = dbm_fetch(db, key);
  if(content.dptr){
    if(ox){
      printobjhex(content.dptr, content.dsize);
    } else {
      printobj(content.dptr, content.dsize);
    }
    if(!nb) putchar('\n');
    rv = 0;
  } else {
    pmyerror(name, "dbm_fetch failed");
    rv = 1;
  }
  dbm_close(db);
  return rv;
}


/* perform list command */
int dolist(char *name, int ox){
  DBM *db;
  datum key, val;
  if(!(db = dbm_open((char *)name, O_RDONLY, 00644))){
    pmyerror(name, "dbm_open failed");
    return 1;
  }
  for(key = dbm_firstkey(db); key.dptr != NULL; key = dbm_nextkey(db)){
    val = dbm_fetch(db, key);
    if(!val.dptr) break;
    if(ox){
      printobjhex(key.dptr, key.dsize);
      putchar('\t');
      printobjhex(val.dptr, val.dsize);
    } else {
      printobj(key.dptr, key.dsize);
      putchar('\t');
      printobj(val.dptr, val.dsize);
    }
    putchar('\n');
  }
  dbm_close(db);
  return 0;
}



/* END OF FILE */
