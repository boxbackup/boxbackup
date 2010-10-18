/*************************************************************************************************
 * Utility for debugging Hovel and its applications
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


#include <hovel.h>
#include <cabin.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#undef TRUE
#define TRUE           1                 /* boolean true */
#undef FALSE
#define FALSE          0                 /* boolean false */

#define ALIGNSIZ       16                /* basic size of alignment */


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
int runoptimize(int argc, char **argv);
void pgerror(const char *name);
void printobj(const char *obj, int size);
void printobjhex(const char *obj, int size);
int docreate(char *name, int qdbm, int bnum, int dnum, int sparse);
int dostore(char *name, int qdbm, const char *kbuf, int ksiz, const char *vbuf, int vsiz, int ins);
int dodelete(char *name, int qdbm, const char *kbuf, int ksiz);
int dofetch(char *name, int qdbm, const char *kbuf, int ksiz, int ox, int nb);
int dolist(char *name, int qdbm, int ox);
int dooptimize(char *name, int qdbm);


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
  } else if(!strcmp(argv[1], "optimize")){
    rv = runoptimize(argc, argv);
  } else {
    usage();
  }
  return rv;
}


/* print the usage and exit */
void usage(void){
  fprintf(stderr, "%s: administration utility for Hovel\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s create [-qdbm bnum dnum] [-s] name\n", progname);
  fprintf(stderr, "  %s store [-qdbm] [-kx] [-vx|-vf] [-insert] name key val\n", progname);
  fprintf(stderr, "  %s delete [-qdbm] [-kx] name key\n", progname);
  fprintf(stderr, "  %s fetch [-qdbm] [-kx] [-ox] [-n] name key\n", progname);
  fprintf(stderr, "  %s list [-qdbm] [-ox] name\n", progname);
  fprintf(stderr, "  %s optimize [-qdbm] name\n", progname);
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
  int i, sb, qdbm, bnum, dnum, rv;
  name = NULL;
  sb = FALSE;
  qdbm = FALSE;
  bnum = -1;
  dnum = -1;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-qdbm")){
        qdbm = TRUE;
        if(++i >= argc) usage();
        bnum = atoi(argv[i]);
        if(++i >= argc) usage();
        dnum = atoi(argv[i]);
      } else if(!strcmp(argv[i], "-s")){
        sb = TRUE;
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
  rv = docreate(name, qdbm, bnum, dnum, sb);
  return rv;
}


/* parse arguments of store command */
int runstore(int argc, char **argv){
  char *name, *key, *val, *kbuf, *vbuf;
  int i, qdbm, kx, vx, vf, ins, ksiz, vsiz, rv;
  name = NULL;
  qdbm = FALSE;
  kx = FALSE;
  vx = FALSE;
  vf = FALSE;
  ins = FALSE;
  key = NULL;
  val = NULL;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-qdbm")){
        qdbm = TRUE;
      } else if(!strcmp(argv[i], "-kx")){
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
    rv = dostore(name, qdbm, kbuf, ksiz, vbuf, vsiz, ins);
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
  int i, qdbm, kx, ksiz, rv;
  name = NULL;
  qdbm = FALSE;
  kx = FALSE;
  key = NULL;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-qdbm")){
        qdbm = TRUE;
      } else if(!strcmp(argv[i], "-kx")){
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
    rv = dodelete(name, qdbm, kbuf, ksiz);
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
  int i, qdbm, kx, ox, nb, ksiz, rv;
  name = NULL;
  qdbm = FALSE;
  kx = FALSE;
  ox = FALSE;
  nb = FALSE;
  key = NULL;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-qdbm")){
        qdbm = TRUE;
      } else if(!strcmp(argv[i], "-kx")){
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
    rv = dofetch(name, qdbm, kbuf, ksiz, ox, nb);
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
  int i, qdbm, ox, rv;
  name = NULL;
  qdbm = FALSE;
  ox = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-qdbm")){
        qdbm = TRUE;
      } else if(!strcmp(argv[i], "-ox")){
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
  rv = dolist(name, qdbm, ox);
  return rv;
}


/* parse arguments of optimize command */
int runoptimize(int argc, char **argv){
  char *name;
  int i, qdbm, rv;
  name = NULL;
  qdbm = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-qdbm")){
        qdbm = TRUE;
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
  rv = dooptimize(name, qdbm);
  return rv;
}


/* print an error message */
void pgerror(const char *name){
  fprintf(stderr, "%s: %s: %s\n", progname, name, gdbm_strerror(gdbm_errno));
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
int docreate(char *name, int qdbm, int bnum, int dnum, int sparse){
  GDBM_FILE dbf;
  int rwmode;
  rwmode = GDBM_NEWDB | (sparse ? GDBM_SPARSE : 0);
  if(qdbm){
    if(!(dbf = gdbm_open2(name, rwmode, 00644, bnum, dnum, ALIGNSIZ))){
      pgerror(name);
      return 1;
    }
  } else {
    if(!(dbf = gdbm_open(name, 0, rwmode, 00644, NULL))){
      pgerror(name);
      return 1;
    }
  }
  gdbm_close(dbf);
  return 0;
}


/* perform store command */
int dostore(char *name, int qdbm, const char *kbuf, int ksiz, const char *vbuf, int vsiz, int ins){
  GDBM_FILE dbf;
  datum key, content;
  int rv;
  if(qdbm){
    if(!(dbf = gdbm_open2(name, GDBM_WRITER, 00644, -1, -1, ALIGNSIZ))){
      pgerror(name);
      return 1;
    }
  } else {
    if(!(dbf = gdbm_open(name, 0, GDBM_WRITER, 00644, NULL))){
      pgerror(name);
      return 1;
    }
  }
  key.dptr = (char *)kbuf;
  key.dsize = ksiz;
  content.dptr = (char *)vbuf;
  content.dsize = vsiz;
  rv = 0;
  if(gdbm_store(dbf, key, content, ins ? GDBM_INSERT : GDBM_REPLACE) != 0){
    pgerror(name);
    rv = 1;
  }
  gdbm_close(dbf);
  return rv;
}


/* perform delete command */
int dodelete(char *name, int qdbm, const char *kbuf, int ksiz){
  GDBM_FILE dbf;
  datum key;
  int rv;
  if(qdbm){
    if(!(dbf = gdbm_open2(name, GDBM_WRITER, 00644, -1, -1, ALIGNSIZ))){
      pgerror(name);
      return 1;
    }
  } else {
    if(!(dbf = gdbm_open(name, 0, GDBM_WRITER, 00644, NULL))){
      pgerror(name);
      return 1;
    }
  }
  key.dptr = (char *)kbuf;
  key.dsize = ksiz;
  if(gdbm_delete(dbf, key) == 0){
    rv = 0;
  } else {
    pgerror(name);
    rv = 1;
  }
  gdbm_close(dbf);
  return rv;
}


/* perform fetch command */
int dofetch(char *name, int qdbm, const char *kbuf, int ksiz, int ox, int nb){
  GDBM_FILE dbf;
  datum key, content;
  int rv;
  if(qdbm){
    if(!(dbf = gdbm_open2(name, GDBM_READER, 00644, -1, -1, -1))){
      pgerror(name);
      return 1;
    }
  } else {
    if(!(dbf = gdbm_open(name, 0, GDBM_READER, 00644, NULL))){
      pgerror(name);
      return 1;
    }
  }
  key.dptr = (char *)kbuf;
  key.dsize = ksiz;
  content = gdbm_fetch(dbf, key);
  if(content.dptr){
    if(ox){
      printobjhex(content.dptr, content.dsize);
    } else {
      printobj(content.dptr, content.dsize);
    }
    if(!nb) putchar('\n');
    rv = 0;
    free(content.dptr);
  } else {
    pgerror(name);
    rv = 1;
  }
  gdbm_close(dbf);
  return rv;
}


/* perform list command */
int dolist(char *name, int qdbm, int ox){
  GDBM_FILE dbf;
  datum key, val;
  if(qdbm){
    if(!(dbf = gdbm_open2(name, GDBM_READER, 00644, -1, -1, -1))){
      pgerror(name);
      return 1;
    }
  } else {
    if(!(dbf = gdbm_open(name, 0, GDBM_READER, 00644, NULL))){
      pgerror(name);
      return 1;
    }
  }
  for(key = gdbm_firstkey(dbf); key.dptr != NULL; key = gdbm_nextkey(dbf, key)){
    val = gdbm_fetch(dbf, key);
    if(!val.dptr){
      free(key.dptr);
      break;
    }
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
    free(val.dptr);
    free(key.dptr);
  }
  gdbm_close(dbf);
  return 0;
}


/* perform optimize command */
int dooptimize(char *name, int qdbm){
  GDBM_FILE dbf;
  int rv;
  if(qdbm){
    if(!(dbf = gdbm_open2(name, GDBM_WRITER, 00644, -1, -1, ALIGNSIZ))){
      pgerror(name);
      return 1;
    }
  } else {
    if(!(dbf = gdbm_open(name, 0, GDBM_WRITER, 00644, NULL))){
      pgerror(name);
      return 1;
    }
  }
  rv = 0;
  if(gdbm_reorganize(dbf) != 0){
    pgerror(name);
    rv = 1;
  }
  gdbm_close(dbf);
  return rv;
}



/* END OF FILE */
