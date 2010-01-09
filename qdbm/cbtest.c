/*************************************************************************************************
 * Test cases of Cabin
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


#include <cabin.h>
#include <stdio.h>
#include <cabin.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <time.h>

#undef TRUE
#define TRUE           1                 /* boolean true */
#undef FALSE
#define FALSE          0                 /* boolean false */

#define RECBUFSIZ      32                /* buffer for records */
#define TEXTBUFSIZ     262144            /* buffer for text */


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
int runsort(int argc, char **argv);
int runstrstr(int argc, char **argv);
int runlist(int argc, char **argv);
int runmap(int argc, char **argv);
int runheap(int argc, char **argv);
int runwicked(int argc, char **argv);
int runmisc(int argc, char **argv);
int printfflush(const char *format, ...);
int strpcmp(const void *ap, const void *bp);
int intpcmp(const void *ap, const void *bp);
int myrand(void);
int dosort(int rnum, int disp);
int dostrstr(int rnum, int disp);
int dolist(int rnum, int disp);
int domap(int rnum, int bnum, int disp);
int doheap(int rnum, int max, int disp);
int dowicked(int rnum);
int domisc(void);


/* main routine */
int main(int argc, char **argv){
  int rv;
  cbstdiobin();
  progname = argv[0];
  if(argc < 2) usage();
  rv = 0;
  if(!strcmp(argv[1], "sort")){
    rv = runsort(argc, argv);
  } else if(!strcmp(argv[1], "strstr")){
    rv = runstrstr(argc, argv);
  } else if(!strcmp(argv[1], "list")){
    rv = runlist(argc, argv);
  } else if(!strcmp(argv[1], "map")){
    rv = runmap(argc, argv);
  } else if(!strcmp(argv[1], "heap")){
    rv = runheap(argc, argv);
  } else if(!strcmp(argv[1], "wicked")){
    rv = runwicked(argc, argv);
  } else if(!strcmp(argv[1], "misc")){
    rv = runmisc(argc, argv);
  } else {
    usage();
  }
  return rv;
}


/* print the usage and exit */
void usage(void){
  fprintf(stderr, "%s: test cases for Cabin\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s sort [-d] rnum\n", progname);
  fprintf(stderr, "  %s strstr [-d] rnum\n", progname);
  fprintf(stderr, "  %s list [-d] rnum\n", progname);
  fprintf(stderr, "  %s map [-d] rnum [bnum]\n", progname);
  fprintf(stderr, "  %s heap [-d] rnum [top]\n", progname);
  fprintf(stderr, "  %s wicked rnum\n", progname);
  fprintf(stderr, "  %s misc\n", progname);
  fprintf(stderr, "\n");
  exit(1);
}


/* parse arguments of sort command */
int runsort(int argc, char **argv){
  int i, rnum, disp, rv;
  char *rstr;
  rstr = NULL;
  rnum = 0;
  disp = FALSE;
  for(i = 2; i < argc; i++){
    if(argv[i][0] == '-'){
      if(!strcmp(argv[i], "-d")){
        disp = TRUE;
      } else {
        usage();
      }
    } else if(!rstr){
      rstr = argv[i];
    } else {
      usage();
    }
  }
  if(!rstr) usage();
  rnum = atoi(rstr);
  if(rnum < 1) usage();
  rv = dosort(rnum, disp);
  return rv;
}


/* parse arguments of strstr command */
int runstrstr(int argc, char **argv){
  int i, rnum, disp, rv;
  char *rstr;
  rstr = NULL;
  rnum = 0;
  disp = FALSE;
  for(i = 2; i < argc; i++){
    if(argv[i][0] == '-'){
      if(!strcmp(argv[i], "-d")){
        disp = TRUE;
      } else {
        usage();
      }
    } else if(!rstr){
      rstr = argv[i];
    } else {
      usage();
    }
  }
  if(!rstr) usage();
  rnum = atoi(rstr);
  if(rnum < 1) usage();
  rv = dostrstr(rnum, disp);
  return rv;
}


/* parse arguments of list command */
int runlist(int argc, char **argv){
  int i, rnum, disp, rv;
  char *rstr;
  rstr = NULL;
  rnum = 0;
  disp = FALSE;
  for(i = 2; i < argc; i++){
    if(argv[i][0] == '-'){
      if(!strcmp(argv[i], "-d")){
        disp = TRUE;
      } else {
        usage();
      }
    } else if(!rstr){
      rstr = argv[i];
    } else {
      usage();
    }
  }
  if(!rstr) usage();
  rnum = atoi(rstr);
  if(rnum < 1) usage();
  rv = dolist(rnum, disp);
  return rv;
}


/* parse arguments of map command */
int runmap(int argc, char **argv){
  int i, rnum, bnum, disp, rv;
  char *rstr, *bstr;
  rstr = NULL;
  bstr = NULL;
  rnum = 0;
  bnum = -1;
  disp = FALSE;
  for(i = 2; i < argc; i++){
    if(argv[i][0] == '-'){
      if(!strcmp(argv[i], "-d")){
        disp = TRUE;
      } else {
        usage();
      }
    } else if(!rstr){
      rstr = argv[i];
    } else if(!bstr){
      bstr = argv[i];
    } else {
      usage();
    }
  }
  if(!rstr) usage();
  rnum = atoi(rstr);
  if(rnum < 1) usage();
  if(bstr) bnum = atoi(bstr);
  rv = domap(rnum, bnum, disp);
  return rv;
}


/* parse arguments of heap command */
int runheap(int argc, char **argv){
  int i, rnum, max, disp, rv;
  char *rstr, *mstr;
  rstr = NULL;
  mstr = NULL;
  rnum = 0;
  max = -1;
  disp = FALSE;
  for(i = 2; i < argc; i++){
    if(argv[i][0] == '-'){
      if(!strcmp(argv[i], "-d")){
        disp = TRUE;
      } else {
        usage();
      }
    } else if(!rstr){
      rstr = argv[i];
    } else if(!mstr){
      mstr = argv[i];
    } else {
      usage();
    }
  }
  if(!rstr) usage();
  rnum = atoi(rstr);
  if(rnum < 1) usage();
  if(mstr) max = atoi(mstr);
  if(max < 0) max = rnum;
  rv = doheap(rnum, max, disp);
  rv = 0;
  return rv;
}


/* parse arguments of wicked command */
int runwicked(int argc, char **argv){
  int i, rnum, rv;
  char *rstr;
  rstr = NULL;
  rnum = 0;
  for(i = 2; i < argc; i++){
    if(argv[i][0] == '-'){
      usage();
    } else if(!rstr){
      rstr = argv[i];
    } else {
      usage();
    }
  }
  if(!rstr) usage();
  rnum = atoi(rstr);
  if(rnum < 1) usage();
  rv = dowicked(rnum);
  return rv;
}


/* parse arguments of misc command */
int runmisc(int argc, char **argv){
  int rv;
  rv = domisc();
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


/* comparing function for strings */
int strpcmp(const void *ap, const void *bp){
  return strcmp(*(char **)ap, *(char **)bp);
}


/* comparing function for integers */
int intpcmp(const void *ap, const void *bp){
  return *(int *)ap - *(int *)bp;
}


/* pseudo random number generator */
int myrand(void){
  static int cnt = 0;
  if(cnt == 0) srand(time(NULL));
  return (rand() * rand() + (rand() >> (sizeof(int) * 4)) + (cnt++)) & INT_MAX;
}


/* perform sort command */
int dosort(int rnum, int disp){
  char **ivector1, **ivector2, **ivector3, **ivector4, **ivector5;
  char buf[RECBUFSIZ];
  int i, len, err;
  if(!disp) printfflush("<Sorting Test>\n  rnum=%d\n\n", rnum);
  ivector1 = cbmalloc(rnum * sizeof(ivector1[0]));
  ivector2 = cbmalloc(rnum * sizeof(ivector2[0]));
  ivector3 = cbmalloc(rnum * sizeof(ivector3[0]));
  ivector4 = cbmalloc(rnum * sizeof(ivector4[0]));
  ivector5 = cbmalloc(rnum * sizeof(ivector5[0]));
  err = FALSE;
  for(i = 0; i < rnum; i++){
    len = sprintf(buf, "%08d", myrand() % rnum + 1);
    ivector1[i] = cbmemdup(buf, len);
    ivector2[i] = cbmemdup(buf, len);
    ivector3[i] = cbmemdup(buf, len);
    ivector4[i] = cbmemdup(buf, len);
    ivector5[i] = cbmemdup(buf, len);
  }
  if(!disp) printfflush("Sorting with insert sort ... ");
  cbisort(ivector1, rnum, sizeof(ivector1[0]), strpcmp);
  if(!disp) printfflush("ok\n");
  if(!disp) printfflush("Sorting with shell sort ... ");
  cbssort(ivector2, rnum, sizeof(ivector2[0]), strpcmp);
  if(!disp) printfflush("ok\n");
  if(!disp) printfflush("Sorting with heap sort ... ");
  cbhsort(ivector3, rnum, sizeof(ivector3[0]), strpcmp);
  if(!disp) printfflush("ok\n");
  if(!disp) printfflush("Sorting with quick sort ... ");
  cbqsort(ivector4, rnum, sizeof(ivector4[0]), strpcmp);
  if(!disp) printfflush("ok\n");
  for(i = 0; i < rnum; i++){
    if(disp) printfflush("%s\t%s\t%s\t%s\t[%s]\n",
                        ivector1[i], ivector2[i], ivector3[i], ivector4[i], ivector5[i]);
    if(strcmp(ivector1[i], ivector2[i])) err = TRUE;
    if(strcmp(ivector1[i], ivector3[i])) err = TRUE;
    if(strcmp(ivector1[i], ivector4[i])) err = TRUE;
    free(ivector1[i]);
    free(ivector2[i]);
    free(ivector3[i]);
    free(ivector4[i]);
    free(ivector5[i]);
  }
  free(ivector1);
  free(ivector2);
  free(ivector3);
  free(ivector4);
  free(ivector5);
  if(err) fprintf(stderr, "%s: sorting failed\n", progname);
  if(!disp && !err) printfflush("all ok\n\n");
  return err ? 1 : 0;
}


/* perform strstr command */
int dostrstr(int rnum, int disp){
  char *text, buf[RECBUFSIZ], *std, *kmp, *bm;
  int i, j, len, err;
  text = cbmalloc(TEXTBUFSIZ);
  for(i = 0; i < TEXTBUFSIZ - 1; i++){
    text[i] = 'a' + myrand() % ('z' - 'a');
  }
  text[i] = '\0';
  err = FALSE;
  if(!disp) printfflush("Locating substrings ... ");
  for(i = 0; i < rnum; i++){
    len = myrand() % (RECBUFSIZ - 1);
    for(j = 0; j < len; j++){
      buf[j] = 'a' + myrand() % ('z' - 'a');
    }
    buf[j] = 0;
    std = strstr(text, buf);
    kmp = cbstrstrkmp(text, buf);
    bm = cbstrstrbm(text, buf);
    if(kmp != std || bm != std){
      err = TRUE;
      break;
    }
    if(disp && std) printf("%s\n", buf);
  }
  if(err) fprintf(stderr, "%s: string scanning failed\n", progname);
  if(!disp && !err){
    printfflush("ok\n");
    printfflush("all ok\n\n");
  }
  free(text);
  return err ? 1 : 0;
}


/* perform list command */
int dolist(int rnum, int disp){
  CBLIST *list;
  const char *vbuf;
  char buf[RECBUFSIZ], *tmp;
  int i, err, len, vsiz;
  if(!disp) printfflush("<List Writing Test>\n  rnum=%d\n\n", rnum);
  list = cblistopen();
  err = FALSE;
  for(i = 1; i <= rnum; i++){
    len = sprintf(buf, "%08d", i);
    cblistpush(list, buf, len);
    if(!disp && rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0){
        printfflush(" (%08d)\n", i);
      }
    }
  }
  if(disp){
    for(i = 0; i < cblistnum(list); i++){
      if((vbuf = cblistval(list, i, &vsiz)) != NULL){
        printfflush("%s:%d\n", vbuf, vsiz);
      } else {
        fprintf(stderr, "%s: val error\n", progname);
        err = TRUE;
        break;
      }
    }
    printfflush("\n");
    while((tmp = cblistpop(list, &vsiz)) != NULL){
      printfflush("%s:%d\n", tmp, vsiz);
      free(tmp);
    }
  }
  cblistclose(list);
  if(!disp && !err) printfflush("ok\n\n");
  return err ? 1 : 0;
}


/* perform list command */
int domap(int rnum, int bnum, int disp){
  CBMAP *map;
  const char *kbuf, *vbuf;
  char buf[RECBUFSIZ];
  int i, err, len, ksiz, vsiz;
  if(!disp) printfflush("<Map Writing Test>\n  rnum=%d  bnum=%d\n\n", rnum, bnum);
  map = bnum > 0 ? cbmapopenex(bnum) : cbmapopen();
  err = FALSE;
  for(i = 1; i <= rnum; i++){
    len = sprintf(buf, "%08d", i);
    if(!cbmapput(map, buf, len, buf, len, FALSE)){
      fprintf(stderr, "%s: put error\n", progname);
      err = TRUE;
      break;
    }
    if(!disp && rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0){
        printfflush(" (%08d)\n", i);
      }
    }
  }
  if(disp){
    for(i = 1; i <= rnum; i++){
      len = sprintf(buf, "%08d", i);
      if((vbuf = cbmapget(map, buf, len, &vsiz)) != NULL){
        printfflush("%s:%d\t%s:%d\n", buf, len, vbuf, vsiz);
      } else {
        fprintf(stderr, "%s: get error\n", progname);
      }
    }
    printfflush("\n");
    cbmapiterinit(map);
    while((kbuf = cbmapiternext(map, &ksiz)) != NULL){
      vbuf = cbmapiterval(kbuf, &vsiz);
      printfflush("%s:%d\t%s:%d\n", kbuf, ksiz, vbuf, vsiz);
    }
  }
  cbmapclose(map);
  if(!disp && !err) printfflush("ok\n\n");
  return err ? 1 : 0;
}


/* perform heap command */
int doheap(int rnum, int max, int disp){
  CBHEAP *heap;
  int *orig, *ary;
  int i, err, num, anum;
  if(!disp) printfflush("<Heap Writing Test>\n  rnum=%d  max=%d\n\n", rnum, max);
  orig = disp ? cbmalloc(rnum * sizeof(int) + 1) : NULL;
  heap = cbheapopen(sizeof(int), max, intpcmp);
  err = FALSE;
  for(i = 1; i <= rnum; i++){
    num = myrand() % rnum + 1;
    if(orig) orig[i-1] = num;
    cbheapinsert(heap, &num);
    if(!disp && rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0){
        printfflush(" (%08d)\n", i);
      }
    }
  }
  if(disp){
    for(i = 0; i < cbheapnum(heap); i++){
      printf("%d\n", *(int *)cbheapval(heap, i));
    }
    printf("\n");
    qsort(orig, rnum, sizeof(int), intpcmp);
    ary = (int *)cbheaptomalloc(heap, &anum);
    if(anum != rnum && anum != max) err = TRUE;
    for(i = 0; i < anum; i++){
      printf("%d\t%d\n", ary[i], orig[i]);
      if(ary[i] != orig[i]) err = TRUE;
    }
    free(ary);
  } else {
    cbheapclose(heap);
  }
  free(orig);
  if(!disp && !err) printfflush("ok\n\n");
  return err ? 1 : 0;
}


/* perform wicked command */
int dowicked(int rnum){
  CBLIST *list;
  CBMAP *map;
  int i, len;
  char buf[RECBUFSIZ], *tmp;
  printfflush("<Wicked Writing Test>\n  rnum=%d\n\n", rnum);
  list = cblistopen();
  for(i = 1; i <= rnum; i++){
    len = sprintf(buf, "%d", myrand() % rnum + 1);
    switch(myrand() % 16){
    case 0:
      free(cblistpop(list, NULL));
      putchar('O');
      break;
    case 1:
      cblistunshift(list, buf, len);
      putchar('U');
      break;
    case 2:
      free(cblistshift(list, NULL));
      putchar('S');
      break;
    case 3:
      cblistinsert(list, myrand() % (cblistnum(list) + 1), buf, len);
      putchar('I');
      break;
    case 4:
      free(cblistremove(list, myrand() % (cblistnum(list) + 1), NULL));
      putchar('R');
      break;
    case 5:
      cblistover(list, myrand() % (cblistnum(list) + 1), buf, len);
      putchar('V');
      break;
    case 6:
      tmp = cbmemdup(buf, len);
      cblistpushbuf(list, tmp, len);
      putchar('B');
      break;
    default:
      cblistpush(list, buf, len);
      putchar('P');
      break;
    }
    if(i % 50 == 0) printfflush(" (%08d)\n", i);
  }
  cblistclose(list);
  map = cbmapopen();
  for(i = 1; i <= rnum; i++){
    len = sprintf(buf, "%d", myrand() % rnum + 1);
    switch(myrand() % 16){
    case 0:
      cbmapput(map, buf, len, buf, len, FALSE);
      putchar('I');
      break;
    case 1:
      cbmapputcat(map, buf, len, buf, len);
      putchar('C');
      break;
    case 2:
      cbmapget(map, buf, len, NULL);
      putchar('V');
      break;
    case 3:
      cbmapout(map, buf, len);
      putchar('D');
      break;
    case 4:
      cbmapmove(map, buf, len, myrand() % 2);
      putchar('M');
      break;
    default:
      cbmapput(map, buf, len, buf, len, TRUE);
      putchar('O');
      break;
    }
    if(i % 50 == 0) printfflush(" (%08d)\n", i);
  }
  cbmapclose(map);
  printfflush("ok\n\n");
  return 0;
}


/* perform misc command */
int domisc(void){
  CBDATUM *odatum, *ndatum;
  CBLIST *olist, *nlist, *elems, *glist;
  CBMAP *omap, *nmap, *pairs, *gmap;
  int i, j, ssiz, osiz, tsiz, jl;
  char kbuf[RECBUFSIZ], vbuf[RECBUFSIZ], *sbuf, spbuf[1024], *tmp, *orig, renc[64];
  const char *op, *np;
  time_t t;
  printfflush("<Miscellaneous Test>\n\n");
  printfflush("Checking memory allocation ... ");
  tmp = cbmalloc(1024);
  for(i = 1; i <= 65536; i *= 2){
    tmp = cbrealloc(tmp, i);
  }
  cbfree(tmp);
  printfflush("ok\n");
  printfflush("Checking basic datum ... ");
  odatum = cbdatumopen("x", -1);
  for(i = 0; i < 1000; i++){
    cbdatumcat(odatum, "x", 1);
  }
  cbdatumclose(odatum);
  tmp = cbmalloc(3);
  memcpy(tmp, "abc", 3);
  odatum = cbdatumopenbuf(tmp, 3);
  for(i = 0; i < 1000; i++){
    cbdatumcat(odatum, ".", 1);
  }
  ndatum = cbdatumdup(odatum);
  for(i = 0; i < 1000; i++){
    cbdatumcat(ndatum, "*", 1);
  }
  for(i = 0; i < 1000; i++){
    tmp = cbmalloc(3);
    memcpy(tmp, "123", 3);
    cbdatumsetbuf(ndatum, tmp, 3);
  }
  cbdatumprintf(ndatum, "[%s\t%08d\t%08o\t%08x\t%08.1f\t%@\t%?\t%:]",
                "mikio", 1978, 1978, 1978, 1978.0211, "<>&#!+-*/%", "<>&#!+-*/%", "<>&#!+-*/%");
  cbdatumclose(ndatum);
  cbdatumclose(odatum);
  printfflush("ok\n");
  printfflush("Checking serialization of list ... ");
  olist = cblistopen();
  for(i = 1; i <= 1000; i++){
    sprintf(vbuf, "%d", i);
    cblistpush(olist, vbuf, -1);
  }
  sbuf = cblistdump(olist, &ssiz);
  nlist = cblistload(sbuf, ssiz);
  free(sbuf);
  for(i = 0; i < cblistnum(olist); i++){
    op = cblistval(olist, i, NULL);
    np = cblistval(nlist, i, NULL);
    if(!op || !np || strcmp(op, np)){
      cblistclose(nlist);
      cblistclose(olist);
      fprintf(stderr, "%s: validation failed\n", progname);
      return 1;
    }
  }
  cblistclose(nlist);
  cblistclose(olist);
  printfflush("ok\n");
  printfflush("Checking serialization of map ... ");
  omap = cbmapopen();
  for(i = 1; i <= 1000; i++){
    sprintf(kbuf, "%X", i);
    sprintf(vbuf, "[%d]", i);
    cbmapput(omap, kbuf, -1, vbuf, -1, TRUE);
  }
  sbuf = cbmapdump(omap, &ssiz);
  nmap = cbmapload(sbuf, ssiz);
  free(cbmaploadone(sbuf, ssiz, "1", 2, &tsiz));
  free(cbmaploadone(sbuf, ssiz, "33", 2, &tsiz));
  free(sbuf);
  cbmapiterinit(omap);
  while((op = cbmapiternext(omap, NULL)) != NULL){
    if(!(np = cbmapget(nmap, op, -1, NULL))){
      cbmapclose(nmap);
      cbmapclose(omap);
      fprintf(stderr, "%s: validation failed\n", progname);
      return 1;
    }
  }
  cbmapclose(nmap);
  cbmapclose(omap);
  printfflush("ok\n");
  printfflush("Checking string utilities ... ");
  sprintf(spbuf, "[%08d/%08o/%08u/%08x/%08X/%08.3e/%08.3E/%08.3f/%08.3g/%08.3G/%c/%s/%%]",
          123456, 123456, 123456, 123456, 123456,
          123456.789, 123456.789, 123456.789, 123456.789, 123456.789,
          'A', "hoge");
  tmp = cbsprintf("[%08d/%08o/%08u/%08x/%08X/%08.3e/%08.3E/%08.3f/%08.3g/%08.3G/%c/%s/%%]",
                  123456, 123456, 123456, 123456, 123456,
                  123456.789, 123456.789, 123456.789, 123456.789, 123456.789,
                  'A', "hoge");
  while(strcmp(spbuf, tmp)){
    free(tmp);
    fprintf(stderr, "%s: cbsprintf is invalid\n", progname);
    return 1;
  }
  free(tmp);
  pairs = cbmapopen();
  cbmapput(pairs, "aa", -1, "AAA", -1, TRUE);
  cbmapput(pairs, "bb", -1, "BBB", -1, TRUE);
  cbmapput(pairs, "cc", -1, "CCC", -1, TRUE);
  cbmapput(pairs, "ZZZ", -1, "z", -1, TRUE);
  tmp = cbreplace("[aaaaabbbbbcccccdddddZZZZ]", pairs);
  if(strcmp(tmp, "[AAAAAAaBBBBBBbCCCCCCcdddddzZ]")){
    free(tmp);
    cbmapclose(pairs);
    fprintf(stderr, "%s: cbreplace is invalid\n", progname);
    return 1;
  }
  free(tmp);
  cbmapclose(pairs);
  elems = cbsplit("aa bb,ccc-dd,", -1, " ,-");
  if(cblistnum(elems) != 5 || strcmp(cblistval(elems, 0, NULL), "aa") ||
     strcmp(cblistval(elems, 1, NULL), "bb") || strcmp(cblistval(elems, 2, NULL), "ccc") ||
     strcmp(cblistval(elems, 3, NULL), "dd") || strcmp(cblistval(elems, 4, NULL), "")){
    cblistclose(elems);
    fprintf(stderr, "%s: cbsplit is invalid\n", progname);
    return 1;
  }
  cblistclose(elems);
  if(cbstricmp("abc", "ABC") || !cbstricmp("abc", "abcd")){
    fprintf(stderr, "%s: cbstricmp is invalid\n", progname);
    return 1;
  }
  if(!cbstrfwmatch("abcd", "abc") || cbstrfwmatch("abc", "abcd")){
    fprintf(stderr, "%s: cbstrfwmatch is invalid\n", progname);
    return 1;
  }
  if(!cbstrfwimatch("abcd", "ABC") || cbstrfwmatch("abc", "ABCD")){
    fprintf(stderr, "%s: cbstrfwimatch is invalid\n", progname);
    return 1;
  }
  if(!cbstrbwmatch("dcba", "cba") || cbstrbwmatch("cba", "dcba")){
    fprintf(stderr, "%s: cbstrbwmatch is invalid\n", progname);
    return 1;
  }
  if(!cbstrbwimatch("dcba", "CBA") || cbstrbwimatch("cba", "DCBA")){
    fprintf(stderr, "%s: cbstrbwimatch is invalid\n", progname);
    return 1;
  }
  tmp = cbmemdup(" \r\n[Quick   Database Manager]\r\n ", -1);
  if(cbstrtoupper(tmp) != tmp || strcmp(tmp, " \r\n[QUICK   DATABASE MANAGER]\r\n ")){
    free(tmp);
    fprintf(stderr, "%s: cbstrtoupper is invalid\n", progname);
    return 1;
  }
  if(cbstrtolower(tmp) != tmp || strcmp(tmp, " \r\n[quick   database manager]\r\n ")){
    free(tmp);
    fprintf(stderr, "%s: cbstrtolower is invalid\n", progname);
    return 1;
  }
  if(cbstrtrim(tmp) != tmp || strcmp(tmp, "[quick   database manager]")){
    free(tmp);
    fprintf(stderr, "%s: cbstrtrim is invalid\n", progname);
    return 1;
  }
  if(cbstrsqzspc(tmp) != tmp || strcmp(tmp, "[quick database manager]")){
    free(tmp);
    fprintf(stderr, "%s: cbstrsqzspc is invalid\n", progname);
    return 1;
  }
  cbstrcututf(tmp, 5);
  if(cbstrcountutf(tmp) != 5){
    free(tmp);
    fprintf(stderr, "%s: cbstrcututf or cbstrcountutf is invalid\n", progname);
    return 1;
  }
  free(tmp);
  printfflush("ok\n");
  printfflush("Checking encoding utilities ... ");
  strcpy(spbuf, "My name is \xca\xbf\xce\xd3\xb4\xb4\xcd\xba.\n\n<Love & Peace!>\n");
  tmp = cbbaseencode(spbuf, -1);
  orig = cbbasedecode(tmp, &osiz);
  if(osiz != strlen(spbuf) || strcmp(orig, spbuf)){
    free(orig);
    free(tmp);
    fprintf(stderr, "%s: Base64 encoding is invalid\n", progname);
    return 1;
  }
  free(orig);
  free(tmp);
  strcpy(spbuf, "My name is \xca\xbf\xce\xd3\xb4\xb4\xcd\xba.\n\n<Love & Peace!>\n");
  tmp = cbquoteencode(spbuf, -1);
  orig = cbquotedecode(tmp, &osiz);
  if(osiz != strlen(spbuf) || strcmp(orig, spbuf)){
    free(orig);
    free(tmp);
    fprintf(stderr, "%s: quoted-printable encoding is invalid\n", progname);
    return 1;
  }
  free(orig);
  free(tmp);
  strcpy(spbuf, "My name is \xca\xbf\xce\xd3\xb4\xb4\xcd\xba.\n\n<Love & Peace!>\n");
  tmp = cbmimeencode(spbuf, "ISO-8859-1", TRUE);
  orig = cbmimedecode(tmp, renc);
  if(osiz != strlen(spbuf) || strcmp(orig, spbuf) || strcmp(renc, "ISO-8859-1")){
    free(orig);
    free(tmp);
    fprintf(stderr, "%s: MIME encoding is invalid\n", progname);
    return 1;
  }
  free(orig);
  free(tmp);
  strcpy(spbuf, "\"He says...\r\n\"\"What PROGRAM are they watching?\"\"\"");
  tmp = cbcsvunescape(spbuf);
  orig = cbcsvescape(tmp);
  if(strcmp(orig, spbuf)){
    free(orig);
    free(tmp);
    fprintf(stderr, "%s: CSV escaping is invalid\n", progname);
    return 1;
  }
  free(orig);
  free(tmp);
  strcpy(spbuf, "&lt;Nuts&amp;Milk&gt; is &quot;very&quot; surfin&apos;!");
  tmp = cbxmlunescape(spbuf);
  orig = cbxmlescape(tmp);
  if(strcmp(orig, spbuf)){
    free(orig);
    free(tmp);
    fprintf(stderr, "%s: XML escaping is invalid\n", progname);
    return 1;
  }
  free(orig);
  free(tmp);
  printfflush("ok\n");
  printfflush("Checking date utilities ... ");
  for(i = 0; i < 200; i++){
    jl = (myrand() % 23) * 1800;
    if(myrand() % 2 == 0) jl *= -1;
    t = myrand() % (INT_MAX - 3600 * 24 * 365 * 6) + 3600 * 24 * 365 * 5;
    tmp = cbdatestrwww(t, jl);
    t = cbstrmktime(tmp);
    orig = cbdatestrwww(t, jl);
    if(strcmp(orig, tmp)){
      free(orig);
      free(tmp);
      fprintf(stderr, "%s: W3CDTF formatter is invalid\n", progname);
      return 1;
    }
    free(orig);
    free(tmp);
    tmp = cbdatestrhttp(t, jl);
    t = cbstrmktime(tmp);
    orig = cbdatestrhttp(t, jl);
    if(strcmp(orig, tmp)){
      free(orig);
      free(tmp);
      fprintf(stderr, "%s: RFC 822 date formatter is invalid\n", progname);
      return 1;
    }
    free(orig);
    free(tmp);
  }
  printfflush("ok\n");
  printfflush("Checking the global garbage collector ... ");
  for(i = 0; i < 512; i++){
    glist = cblistopen();
    cbglobalgc(glist, (void (*)(void *))cblistclose);
    for(j = 0; j < 10; j++){
      sprintf(kbuf, "%08d", j);
      cblistpush(glist, kbuf, -1);
    }
    gmap = cbmapopen();
    cbglobalgc(gmap, (void (*)(void *))cbmapclose);
    for(j = 0; j < 10; j++){
      sprintf(kbuf, "%08d", j);
      cbmapput(gmap, kbuf, -1, kbuf, -1, TRUE);
    }
    if(myrand() % 64 == 0){
      cbvmemavail(100);
      cbggcsweep();
    }
  }
  printfflush("ok\n");
  printfflush("all ok\n\n");
  return 0;
}



/* END OF FILE */
