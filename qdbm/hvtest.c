/*************************************************************************************************
 * Test cases of Hovel
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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#undef TRUE
#define TRUE           1                 /* boolean true */
#undef FALSE
#define FALSE          0                 /* boolean false */

#define DIVNUM         5                 /* number of division */
#define ALIGNSIZ       16                /* basic size of alignment */
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
int printfflush(const char *format, ...);
void pgerror(const char *name);
int dowrite(char *name, int rnum, int qdbm, int sparse);
int doread(char *name, int rnum, int qdbm);


/* main routine */
int main(int argc, char **argv){
  int rv;
  cbstdiobin();
  progname = argv[0];
  if(argc < 2) usage();
  rv = 0;
  if(!strcmp(argv[1], "write")){
    rv = runwrite(argc, argv);
  } else if(!strcmp(argv[1], "read")){
    rv = runread(argc, argv);
  } else {
    usage();
  }
  return rv;
}


/* print the usage and exit */
void usage(void){
  fprintf(stderr, "%s: test cases for Hovel\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s write [-qdbm] [-s] name rnum\n", progname);
  fprintf(stderr, "  %s read [-qdbm] name rnum\n", progname);
  fprintf(stderr, "\n");
  exit(1);
}


/* parse arguments of write command */
int runwrite(int argc, char **argv){
  char *name, *rstr;
  int i, sb, qdbm, rnum, rv;
  name = NULL;
  rstr = NULL;
  rnum = 0;
  sb = FALSE;
  qdbm = FALSE;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-qdbm")){
        qdbm = TRUE;
      } else if(!strcmp(argv[i], "-s")){
        sb = TRUE;
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
  rv = dowrite(name, rnum, qdbm, sb);
  return rv;
}


/* parse arguments of read command */
int runread(int argc, char **argv){
  char *name, *rstr;
  int i, qdbm, rnum, rv;
  name = NULL;
  rstr = NULL;
  rnum = 0;
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
    } else if(!rstr){
      rstr = argv[i];
    } else {
      usage();
    }
  }
  if(!name || !rstr) usage();
  rnum = atoi(rstr);
  if(rnum < 1) usage();
  rv = doread(name, rnum, qdbm);
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
void pgerror(const char *name){
  fprintf(stderr, "%s: %s: %s\n", progname, name, gdbm_strerror(gdbm_errno));
}


/* perform write command */
int dowrite(char *name, int rnum, int qdbm, int sparse){
  GDBM_FILE dbf;
  datum key, content;
  int i, rwmode, err, len;
  char buf[RECBUFSIZ];
  printfflush("<Writing Test>\n  name=%s  rnum=%d  qdbm=%d\n\n", name, rnum, qdbm);
  /* open a database */
  rwmode = GDBM_NEWDB | (sparse ? GDBM_SPARSE : 0);
  if(qdbm){
    if(!(dbf = gdbm_open2(name, rwmode, 00644, rnum / DIVNUM, DIVNUM, ALIGNSIZ))){
      pgerror(name);
      return 1;
    }
  } else {
    if(!(dbf = gdbm_open(name, 0, rwmode, 00644, NULL))){
      pgerror(name);
      return 1;
    }
  }
  err = FALSE;
  /* loop for each record */
  for(i = 1; i <= rnum; i++){
    len = sprintf(buf, "%08d", i);
    key.dptr = buf;
    key.dsize = len;
    content.dptr = buf;
    content.dsize = len;
    /* store a record */
    if(gdbm_store(dbf, key, content, GDBM_REPLACE) != 0){
      pgerror(name);
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
  gdbm_close(dbf);
  if(!err) printfflush("ok\n\n");
  return err ? 1 : 0;
}


/* perform read command */
int doread(char *name, int rnum, int qdbm){
  GDBM_FILE dbf;
  datum key, content;
  int i, err, len;
  char buf[RECBUFSIZ];
  printfflush("<Reading Test>\n  name=%s  rnum=%d  qdbm=%d\n\n", name, rnum, qdbm);
  /* open a database */
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
  err = FALSE;
  /* loop for each record */
  for(i = 1; i <= rnum; i++){
    /* retrieve a record */
    len = sprintf(buf, "%08d", i);
    key.dptr = buf;
    key.dsize = len;
    content = gdbm_fetch(dbf, key);
    if(!content.dptr){
      pgerror(name);
      err = TRUE;
      break;
    }
    free(content.dptr);
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
  gdbm_close(dbf);
  if(!err) printfflush("ok\n\n");
  return err ? 1 : 0;
}



/* END OF FILE */
