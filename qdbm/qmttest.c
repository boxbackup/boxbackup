/*************************************************************************************************
 * Test cases for thread-safety
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
#include <curia.h>
#include <cabin.h>
#include <villa.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <time.h>

#if defined(MYPTHREAD)
#include <sys/types.h>
#include <pthread.h>
#endif

#undef TRUE
#define TRUE           1                 /* boolean true */
#undef FALSE
#define FALSE          0                 /* boolean false */

#define PATHBUFSIZ     1024              /* buffer for paths */
#define RECBUFSIZ      32                /* buffer for records */

typedef struct {                         /* type of structure of thread arguments */
  int id;                                /* ID of the thread */
  const char *name;                      /* prefix of the database */
  int rnum;                              /* number of records */
  int alive;                             /* alive or not */
} MYARGS;


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
void pdperror(const char *name);
void *procthread(void *args);
int dotest(const char *name, int rnum, int tnum);


/* main routine */
int main(int argc, char **argv){
  char *env, *name;
  int rv, rnum, tnum;
  cbstdiobin();
  if((env = getenv("QDBMDBGFD")) != NULL) dpdbgfd = atoi(env);
  progname = argv[0];
  srand(time(NULL));
  if(argc < 4) usage();
  name = argv[1];
  if((rnum = atoi(argv[2])) < 1) usage();
  if((tnum = atoi(argv[3])) < 1) usage();
  rv = dotest(name, rnum, tnum);
  return rv;
}


/* print the usage and exit */
void usage(void){
  fprintf(stderr, "%s: test cases for thread-safety\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s name rnum tnum\n", progname);
  fprintf(stderr, "\n");
  exit(1);
}


/* print formatted string and flush the buffer */
int printfflush(const char *format, ...){
#if defined(MYPTHREAD)
  static pthread_mutex_t mymutex = PTHREAD_MUTEX_INITIALIZER;
  va_list ap;
  int rv;
  if(pthread_mutex_lock(&mymutex) != 0) return -1;
  va_start(ap, format);
  rv = vprintf(format, ap);
  if(fflush(stdout) == EOF) rv = -1;
  va_end(ap);
  pthread_mutex_unlock(&mymutex);
  return rv;
#else
  va_list ap;
  int rv;
  va_start(ap, format);
  rv = vprintf(format, ap);
  if(fflush(stdout) == EOF) rv = -1;
  va_end(ap);
  return rv;
#endif
}


/* print an error message */
void pdperror(const char *name){
  fprintf(stderr, "%s: %s: %s\n", progname, name, dperrmsg(dpecode));
}


/* pseudo random number generator */
int myrand(void){
  static int cnt = 0;
  return (rand() * rand() + (rand() >> (sizeof(int) * 4)) + (cnt++)) & INT_MAX;
}


/* process the test */
void *procthread(void *args){
  MYARGS *myargs;
  DEPOT *depot;
  CURIA *curia;
  VILLA *villa;
  CBLIST *list;
  CBMAP *map;
  char name[PATHBUFSIZ], buf[RECBUFSIZ];
  int i, err, len;
  myargs = (MYARGS *)args;
  err = FALSE;
  sprintf(name, "%s-%04d", myargs->name, myargs->id);
  dpremove(name);
  crremove(name);
  vlremove(name);
  switch(myrand() % 4){
  case 0:
    printfflush("\n[Depot Test]  name=%s  rnum=%d\n", name, myargs->rnum);
    if(!(depot = dpopen(name, DP_OWRITER | DP_OCREAT | DP_OTRUNC, -1))){
      pdperror(name);
      return "error";
    }
    for(i = 1; i <= myargs->rnum; i++){
      len = sprintf(buf, "%d", myrand() % i + 1);
      if(!dpput(depot, buf, len, buf, len, i % 2 == 0 ? DP_DOVER : DP_DCAT)){
        pdperror(name);
        err = TRUE;
      }
      if(myargs->rnum > 250 && i % (myargs->rnum / 250) == 0){
        printfflush(".");
        if(i == myargs->rnum || i % (myargs->rnum / 10) == 0){
          printfflush("\n%s: (%d)\n", name, i);
        }
      }
    }
    if(!dpclose(depot)){
      pdperror(name);
      err = TRUE;
    }
    printfflush("\n%s: finished\n", name);
    break;
  case 1:
    printfflush("\n[Curia Test]  name=%s  rnum=%d\n", name, myargs->rnum);
    if(!(curia = cropen(name, CR_OWRITER | CR_OCREAT | CR_OTRUNC, -1, -1))){
      pdperror(name);
      return "error";
    }
    for(i = 1; i <= myargs->rnum; i++){
      len = sprintf(buf, "%d", myrand() % i + 1);
      if(!crput(curia, buf, len, buf, len, i % 2 == 0 ? CR_DOVER : CR_DCAT)){
        pdperror(name);
        err = TRUE;
      }
      if(myargs->rnum > 250 && i % (myargs->rnum / 250) == 0){
        printfflush(".");
        if(i == myargs->rnum || i % (myargs->rnum / 10) == 0){
          printfflush("\n%s: (%d)\n", name, i);
        }
      }
    }
    if(!crclose(curia)){
      pdperror(name);
      err = TRUE;
    }
    printfflush("\n%s: finished\n", name);
    break;
  case 2:
    printfflush("\n[Villa Test]  name=%s  rnum=%d\n", name, myargs->rnum);
    if(!(villa = vlopen(name, VL_OWRITER | VL_OCREAT | VL_OTRUNC, VL_CMPLEX))){
      pdperror(name);
      return "error";
    }
    for(i = 1; i <= myargs->rnum; i++){
      len = sprintf(buf, "%d", myrand() % i + 1);
      if(!vlput(villa, buf, len, buf, len, i % 2 == 0 ? VL_DOVER : VL_DDUP)){
        pdperror(name);
        err = TRUE;
      }
      if(myargs->rnum > 250 && i % (myargs->rnum / 250) == 0){
        printfflush(".");
        if(i == myargs->rnum || i % (myargs->rnum / 10) == 0){
          printfflush("\n%s: (%d)\n", name, i);
        }
      }
    }
    if(!vlclose(villa)){
      pdperror(name);
      err = TRUE;
    }
    printfflush("\n%s: finished\n", name);
    break;
  case 3:
    printfflush("\n[Cabin Test]  name=%s  rnum=%d\n", name, myargs->rnum);
    list = cblistopen();
    map = cbmapopen();
    for(i = 1; i <= myargs->rnum; i++){
      len = sprintf(buf, "%d", myrand() % i + 1);
      cblistpush(list, buf, len);
      cbmapput(map, buf, len, buf, len, i % 2 == 0 ? TRUE : FALSE);
      if(myargs->rnum > 250 && i % (myargs->rnum / 250) == 0){
        printfflush(".");
        if(i == myargs->rnum || i % (myargs->rnum / 10) == 0){
          printfflush("\n%s: (%d)\n", name, i);
        }
      }
    }
    cbmapclose(map);
    cblistclose(list);
    printfflush("\n%s: finished\n", name);
    break;
  }
  return err ? "error" : NULL;
}


/* drive the test */
int dotest(const char *name, int rnum, int tnum){
#if defined(MYPTHREAD)
  pthread_t *thary;
  MYARGS *argsary;
  char *rv;
  int i, err;
  printfflush("<Thread-Safety Test>\n  name=%s  rnum=%d  tnum=%d\n", name, rnum, tnum);
  err = FALSE;
  thary = cbmalloc(tnum * sizeof(pthread_t));
  argsary = cbmalloc(tnum * sizeof(MYARGS));
  for(i = 0; i < tnum; i++){
    argsary[i].id = i + 1;
    argsary[i].name = name;
    argsary[i].rnum = rnum;
    argsary[i].alive = TRUE;
    if(pthread_create(thary + i, NULL, procthread, argsary + i) != 0){
      argsary[i].alive = FALSE;
      err = TRUE;
    }
  }
  for(i = 0; i < tnum; i++){
    if(!argsary[i].alive) continue;
    if(pthread_join(thary[i], (void *)&rv) != 0 || rv) err = TRUE;
  }
  free(argsary);
  free(thary);
  if(!err) printfflush("\nall ok\n");
  return err ? 1 : 0;
#else
  MYARGS *argsary;
  int i, err;
  printfflush("<Thread-Safety Test>\n  name=%s  rnum=%d  tnum=%d\n", name, rnum, tnum);
  err = FALSE;
  argsary = cbmalloc(tnum * sizeof(MYARGS));
  for(i = 0; i < tnum; i++){
    argsary[i].id = i + 1;
    argsary[i].name = name;
    argsary[i].rnum = rnum;
    argsary[i].alive = TRUE;
    if(procthread(argsary + i)) err = TRUE;
  }
  free(argsary);
  if(!err) printfflush("\nall ok\n");
  return err ? 1 : 0;
#endif
}



/* END OF FILE */
