/*************************************************************************************************
 * Utility for indexing document files into a database of Odeum
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
#include <time.h>
#include <signal.h>

#undef TRUE
#define TRUE           1                 /* boolean true */
#undef FALSE
#define FALSE          0                 /* boolean false */

#define PATHCHR        '/'               /* delimiter character of path */
#define EXTCHR         '.'               /* delimiter character of extension */
#define CDIRSTR        "."               /* string of current directory */
#define PDIRSTR        ".."              /* string of parent directory */
#define MTDBNAME       "_mtime"          /* name of the database for last modified times */
#define MTDBLRM        81                /* records in a leaf node of time database */
#define MTDBNIM        192               /* records in a non-leaf node of time database */
#define MTDBLCN        64                /* number of leaf cache of time database */
#define MTDBNCN        32                /* number of non-leaf cache of time database */
#define SCDBNAME       "_score"          /* name of the database for scores */
#define SCDBBNUM       32749             /* bucket number of the score database */
#define SCDBALIGN      -3                /* alignment of the score database */
#define PATHBUFSIZ     2048              /* size of a path buffer */
#define MAXLOAD        0.85              /* max ratio of bucket loading */
#define KEYNUM         32                /* number of keywords to store */


/* for Win32 and RISC OS */
#if defined(_WIN32)
#undef PATHCHR
#define PATHCHR        '\\'
#undef EXTCHR
#define EXTCHR         '.'
#undef CDIRSTR
#define CDIRSTR        "."
#undef PDIRSTR
#define PDIRSTR        ".."
#elif defined(__riscos__) || defined(__riscos)
#include <unixlib/local.h>
int __riscosify_control = __RISCOSIFY_NO_PROCESS;
#undef PATHCHR
#define PATHCHR        '.'
#undef EXTCHR
#define EXTCHR         '/'
#undef CDIRSTR
#define CDIRSTR        "@"
#undef PDIRSTR
#define PDIRSTR        "^"
#endif


/* global variables */
const char *progname;                    /* program name */
int sigterm;                             /* flag for termination signal */


/* function prototypes */
int main(int argc, char **argv);
void setsignals(void);
void sigtermhandler(int num);
void usage(void);
int runregister(int argc, char **argv);
int runrelate(int argc, char **argv);
int runpurge(int argc, char **argv);
int bwimatchlist(const char *str, const CBLIST *keys);
char *fgetl(FILE *ifp);
void otcb(const char *fname, ODEUM *odeum, const char *msg);
void pdperror(const char *name);
void printferror(const char *format, ...);
void printfinfo(const char *format, ...);
const char *datestr(time_t t);
int proclist(const char *name, const char *lfile, int wmax,
             const CBLIST *tsuflist, const CBLIST *hsuflist);
int procdir(const char *name, const char *dir, int wmax,
            const CBLIST *tsuflist, const CBLIST *hsuflist);
int indexdir(ODEUM *odeum, VILLA *mtdb, const char *name, const char *dir, int wmax,
             const CBLIST *tsuflist, const CBLIST *hsuflist);
int indexfile(ODEUM *odeum, VILLA *mtdb, const char *name, const char *file, int wmax,
              const CBLIST *tsuflist, const CBLIST *hsuflist);
char *filetouri(const char *file);
ODDOC *makedocplain(const char *uri, const char *text, const char *date);
ODDOC *makedochtml(const char *uri, const char *html, const char *date);
CBMAP *htmlescpairs(void);
int procrelate(const char *name);
int procpurge(const char *name);


/* main routine */
int main(int argc, char **argv){
  int rv;
  cbstdiobin();
  progname = argv[0];
  sigterm = FALSE;
  setsignals();
  if(argc < 2) usage();
  odsetotcb(otcb);
  rv = 0;
  if(!strcmp(argv[1], "register")){
    rv = runregister(argc, argv);
  } else if(!strcmp(argv[1], "relate")){
    rv = runrelate(argc, argv);
  } else if(!strcmp(argv[1], "purge")){
    rv = runpurge(argc, argv);
  } else {
    usage();
  }
  return rv;
}


/* set signal handlers */
void setsignals(void){
  signal(1, sigtermhandler);
  signal(2, sigtermhandler);
  signal(3, sigtermhandler);
  signal(13, sigtermhandler);
  signal(15, sigtermhandler);
}


/* handler of termination signal */
void sigtermhandler(int num){
  signal(num, SIG_DFL);
  sigterm = TRUE;
  printfinfo("the termination signal %d catched", num);
}


/* print the usage and exit */
void usage(void){
  fprintf(stderr, "%s: indexer of document files\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s register [-l file] [-wmax num] [-tsuf sufs] [-hsuf sufs] name [dir]\n",
          progname);
  fprintf(stderr, "  %s relate name\n", progname);
  fprintf(stderr, "  %s purge name\n", progname);
  fprintf(stderr, "\n");
  exit(1);
}


/* parse arguments of register command */
int runregister(int argc, char **argv){
  char *name, *dir, *lfile, *tsuf, *hsuf, path[PATHBUFSIZ];
  int i, wmax, plen, rv;
  CBLIST *tsuflist, *hsuflist;
  name = NULL;
  dir = NULL;
  lfile = NULL;
  tsuf = NULL;
  hsuf = NULL;
  wmax = -1;
  for(i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-l")){
        if(++i >= argc) usage();
        lfile = argv[i];
      } else if(!strcmp(argv[i], "-wmax")){
        if(++i >= argc) usage();
        wmax = atoi(argv[i]);
      } else if(!strcmp(argv[i], "-tsuf")){
        if(++i >= argc) usage();
        tsuf = argv[i];
      } else if(!strcmp(argv[i], "-hsuf")){
        if(++i >= argc) usage();
        hsuf = argv[i];
      } else {
        usage();
      }
    } else if(!name){
      name = argv[i];
    } else if(!dir){
      dir = argv[i];
    } else {
      usage();
    }
  }
  if(!name) usage();
  if(!dir) dir = CDIRSTR;
  plen = sprintf(path, "%s", dir);
  if(plen > 1 && path[plen-1] == PATHCHR) path[plen-1] = '\0';
  tsuflist = cbsplit(tsuf ? tsuf : ".txt,.text", -1, ",");
  hsuflist = cbsplit(hsuf ? hsuf : ".html,.htm", -1, ",");
  if(lfile){
    rv = proclist(name, lfile, wmax, tsuflist, hsuflist);
  } else {
    rv = procdir(name, path, wmax, tsuflist, hsuflist);
  }
  cblistclose(hsuflist);
  cblistclose(tsuflist);
  return rv;
}


/* parse arguments of relate command */
int runrelate(int argc, char **argv){
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
  rv = procrelate(name);
  return rv;
}


/* parse arguments of purge command */
int runpurge(int argc, char **argv){
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
  rv = procpurge(name);
  return rv;
}


/* case insensitive backward matching with a list */
int bwimatchlist(const char *str, const CBLIST *keys){
  int i;
  for(i = 0; i < cblistnum(keys); i++){
    if(cbstrbwimatch(str, cblistval(keys, i, NULL))) return TRUE;
  }
  return FALSE;
}


/* read a line */
char *fgetl(FILE *ifp){
  char *buf;
  int c, len, blen;
  buf = NULL;
  len = 0;
  blen = 256;
  while((c = fgetc(ifp)) != EOF){
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


/* report the outturn */
void otcb(const char *fname, ODEUM *odeum, const char *msg){
  char *name;
  name = odname(odeum);
  printf("%s: %s: %s: %s\n", progname, fname, name, msg);
  free(name);
}


/* print an error message */
void pdperror(const char *name){
  printf("%s: ERROR: %s: %s\n", progname, name, dperrmsg(dpecode));
  fflush(stdout);
}


/* print formatted error string and flush the buffer */
void printferror(const char *format, ...){
  va_list ap;
  va_start(ap, format);
  printf("%s: ERROR: ", progname);
  vprintf(format, ap);
  putchar('\n');
  fflush(stdout);
  va_end(ap);
}


/* print formatted information string and flush the buffer */
void printfinfo(const char *format, ...){
  va_list ap;
  va_start(ap, format);
  printf("%s: INFO: ", progname);
  vprintf(format, ap);
  putchar('\n');
  fflush(stdout);
  va_end(ap);
}


/* get static string of the date */
const char *datestr(time_t t){
  static char buf[32];
  struct tm *stp;
  if(!(stp = localtime(&t))) return "0000/00/00 00:00:00";
  sprintf(buf, "%04d/%02d/%02d %02d:%02d:%02d",
          stp->tm_year + 1900, stp->tm_mon + 1, stp->tm_mday,
          stp->tm_hour, stp->tm_min, stp->tm_sec);
  return buf;
}


/* processing with finding files in a list file */
int proclist(const char *name, const char *lfile, int wmax,
             const CBLIST *tsuflist, const CBLIST *hsuflist){
  ODEUM *odeum;
  VILLA *mtdb;
  FILE *ifp;
  char *line, path[PATHBUFSIZ];
  int err, fatal;
  if(!strcmp(lfile, "-")){
    ifp = stdin;
  } else {
    if(!(ifp = fopen(lfile, "rb"))){
      printferror("%s: file cannot be opened", lfile);
      return 1;
    }
  }
  printfinfo("%s: registration started", name);
  if(!(odeum = odopen(name, OD_OWRITER | OD_OCREAT))){
    pdperror(name);
    if(ifp != stdin) fclose(ifp);
    return 1;
  }
  sprintf(path, "%s%c%s", name, PATHCHR, MTDBNAME);
  if(!(mtdb = vlopen(path, VL_OWRITER | VL_OCREAT, VL_CMPLEX))){
    pdperror(name);
    odclose(odeum);
    if(ifp != stdin) fclose(ifp);
    return 1;
  }
  vlsettuning(mtdb, MTDBLRM, MTDBNIM, MTDBLCN, MTDBNCN);
  printfinfo("%s: database opened: fsiz=%.0f dnum=%d wnum=%d bnum=%d",
             name, odfsiz(odeum), oddnum(odeum), odwnum(odeum), odbnum(odeum));
  err = FALSE;
  while((line = fgetl(ifp)) != NULL){
    if(sigterm){
      printferror("aborting due to a termination signal");
      free(line);
      err = TRUE;
      break;
    }
    if(!indexfile(odeum, mtdb, name, line, wmax, tsuflist, hsuflist)) err = TRUE;
    free(line);
  }
  fatal = odfatalerror(odeum);
  printfinfo("%s: database closing: fsiz=%.0f dnum=%d wnum=%d bnum=%d",
             name, odfsiz(odeum), oddnum(odeum), odwnum(odeum), odbnum(odeum));
  if(!vlclose(mtdb)){
    pdperror(name);
    err = TRUE;
  }
  if(!odclose(odeum)){
    pdperror(name);
    err = TRUE;
  }
  if(ifp != stdin) fclose(ifp);
  if(err){
    printfinfo("%s: registration was over%s", name, fatal ? " with fatal error" : "");
  } else {
    printfinfo("%s: registration completed successfully", name);
  }
  return err ? 1 : 0;
}


/* processing with finding files in a directory */
int procdir(const char *name, const char *dir, int wmax,
            const CBLIST *tsuflist, const CBLIST *hsuflist){
  ODEUM *odeum;
  VILLA *mtdb;
  char path[PATHBUFSIZ];
  int err, fatal;
  printfinfo("%s: registration started", name);
  if(!(odeum = odopen(name, OD_OWRITER | OD_OCREAT))){
    pdperror(name);
    return 1;
  }
  sprintf(path, "%s%c%s", name, PATHCHR, MTDBNAME);
  if(!(mtdb = vlopen(path, VL_OWRITER | VL_OCREAT, VL_CMPLEX))){
    pdperror(name);
    odclose(odeum);
    return 1;
  }
  vlsettuning(mtdb, MTDBLRM, MTDBNIM, MTDBLCN, MTDBNCN);
  printfinfo("%s: database opened: fsiz=%.0f dnum=%d wnum=%d bnum=%d",
             name, odfsiz(odeum), oddnum(odeum), odwnum(odeum), odbnum(odeum));
  err = FALSE;
  if(!indexdir(odeum, mtdb, name, dir, wmax, tsuflist, hsuflist)) err = TRUE;
  fatal = odfatalerror(odeum);
  printfinfo("%s: database closing: fsiz=%.0f dnum=%d wnum=%d bnum=%d",
             name, odfsiz(odeum), oddnum(odeum), odwnum(odeum), odbnum(odeum));
  if(!vlclose(mtdb)){
    pdperror(name);
    err = TRUE;
  }
  if(!odclose(odeum)){
    pdperror(name);
    err = TRUE;
  }
  if(err){
    printfinfo("%s: registration was over%s", name, fatal ? " with fatal error" : "");
  } else {
    printfinfo("%s: registration completed successfully", name);
  }
  return err ? 1 : 0;
}


/* find and index files in a directory */
int indexdir(ODEUM *odeum, VILLA *mtdb, const char *name, const char *dir, int wmax,
             const CBLIST *tsuflist, const CBLIST *hsuflist){
  CBLIST *files;
  const char *file;
  char path[PATHBUFSIZ];
  int i, isroot, isdir, err;
  if(!(files = cbdirlist(dir))){
    printferror("%s: directory cannot be opened", dir);
    return FALSE;
  }
  isroot = dir[0] == PATHCHR && dir[1] == '\0';
  err = FALSE;
  for(i = 0; i < cblistnum(files); i++){
    if(sigterm){
      printferror("aborting due to a termination signal");
      cblistclose(files);
      return FALSE;
    }
    file = cblistval(files, i, NULL);
    if(!strcmp(file, CDIRSTR) || !strcmp(file, PDIRSTR)) continue;
    if(isroot){
      sprintf(path, "%s%s", dir, file);
    } else {
      sprintf(path, "%s%c%s", dir, PATHCHR, file);
    }
    if(!cbfilestat(path, &isdir, NULL, NULL)){
      printferror("%s: file does not exist", file);
      err = TRUE;
      continue;
    }
    if(isdir){
      if(!indexdir(odeum, mtdb, name, path, wmax, tsuflist, hsuflist)) err = TRUE;
    } else {
      if(!indexfile(odeum, mtdb, name, path, wmax, tsuflist, hsuflist)) err = TRUE;
    }
  }
  cblistclose(files);
  return err ? FALSE : TRUE;
}


/* index a file into the database */
int indexfile(ODEUM *odeum, VILLA *mtdb, const char *name, const char *file, int wmax,
              const CBLIST *tsuflist, const CBLIST *hsuflist){
  static int cnt = 0;
  char *vbuf, *buf, *uri;
  const char *title;
  int size, hot, vsiz, wnum, bnum;
  time_t mtime;
  ODDOC *doc;
  if(!cbfilestat(file, NULL, &size, &mtime)){
    printferror("%s: file does not exist", file);
    return FALSE;
  }
  hot = TRUE;
  if((vbuf = vlget(mtdb, file, -1, &vsiz)) != NULL){
    if(vsiz == sizeof(int) && mtime <= *(int *)vbuf) hot = FALSE;
    free(vbuf);
  }
  if(!hot){
    printfinfo("%s: passed", file);
    return TRUE;
  }
  doc = NULL;
  uri = filetouri(file);
  if(bwimatchlist(file, tsuflist)){
    if(!(buf = cbreadfile(file, NULL))){
      printferror("%s: file cannot be opened", file);
      return FALSE;
    }
    doc = makedocplain(uri, buf, datestr(mtime));
    free(buf);
  } else if(bwimatchlist(file, hsuflist)){
    if(!(buf = cbreadfile(file, NULL))){
      printferror("%s: file cannot be opened", file);
      return FALSE;
    }
    doc = makedochtml(uri, buf, datestr(mtime));
    free(buf);
  }
  free(uri);
  if(doc){
    if(!(title = oddocgetattr(doc, "title")) || strlen(title) < 1){
      if((title = strrchr(file, PATHCHR)) != NULL){
        title++;
      }  else {
        title = file;
      }
      oddocaddattr(doc, "title", title);
    }
    if(odput(odeum, doc, wmax, TRUE) &&
       vlput(mtdb, file, -1, (char *)&mtime, sizeof(int), VL_DOVER)){
      printfinfo("%s: registered: id=%d wnum=%d",
                 file, oddocid(doc), cblistnum(oddocnwords(doc)));
      cnt++;
    } else {
      pdperror(file);
    }
    oddocclose(doc);
  }
  wnum = odwnum(odeum);
  bnum = odbnum(odeum);
  if(wnum != -1 && bnum != -1 && (double)wnum / (double)bnum > MAXLOAD){
    printfinfo("%s: optimizing started: fsiz=%.0f dnum=%d wnum=%d bnum=%d",
               name, odfsiz(odeum), oddnum(odeum), odwnum(odeum), odbnum(odeum));
    if(!odoptimize(odeum)){
      pdperror(file);
      return FALSE;
    }
    printfinfo("%s: optimizing completed: fsiz=%.0f dnum=%d wnum=%d bnum=%d",
               name, odfsiz(odeum), oddnum(odeum), odwnum(odeum), odbnum(odeum));
  }
  if(cnt >= 256){
    printfinfo("%s: database status: fsiz=%.0f dnum=%d wnum=%d bnum=%d",
               name, odfsiz(odeum), oddnum(odeum), odwnum(odeum), odbnum(odeum));
    cnt = 0;
  }
  return TRUE;
}


/* make the url from file path */
char *filetouri(const char *file){
  CBLIST *list;
  char str[PATHBUFSIZ], *wp, *enc;
  const char *name;
  int i, nsiz;
  sprintf(str, "%c", PATHCHR);
  list = cbsplit(file, -1, str);
  wp = str;
  for(i = 0; i < cblistnum(list); i++){
    if(i > 0) *(wp++) = '/';
    name = cblistval(list, i, &nsiz);
    enc = cburlencode(name, nsiz);
    wp += sprintf(wp, "%s", enc);
    free(enc);
  }
  cblistclose(list);
  *wp = '\0';
  return cbmemdup(str, -1);
}


/* make a document of plain text */
ODDOC *makedocplain(const char *uri, const char *text, const char *date){
  ODDOC *doc;
  CBLIST *awords;
  const char *asis;
  char *normal;
  int i;
  doc = oddocopen(uri);
  if(date) oddocaddattr(doc, "date", date);
  awords = odbreaktext(text);
  for(i = 0; i < cblistnum(awords); i++){
    asis = cblistval(awords, i, NULL);
    normal = odnormalizeword(asis);
    oddocaddword(doc, normal, asis);
    free(normal);
  }
  cblistclose(awords);
  return doc;
}


/* make a document of HTML */
ODDOC *makedochtml(const char *uri, const char *html, const char *date){
  ODDOC *doc;
  CBMAP *pairs;
  CBLIST *elems, *awords;
  const char *text, *asis;
  char *rtext, *normal;
  int i, j, body;
  pairs = htmlescpairs();
  doc = oddocopen(uri);
  if(date) oddocaddattr(doc, "date", date);
  elems = cbxmlbreak(html, TRUE);
  body = FALSE;
  for(i = 0; i < cblistnum(elems); i++){
    text = cblistval(elems, i, NULL);
    if(cbstrfwimatch(text, "<title")){
      i++;
      if(i < cblistnum(elems)){
        text = cblistval(elems, i, NULL);
        if(text[0] == '<') text = "";
        rtext = cbreplace(text, pairs);
        for(j = 0; rtext[j] != '\0'; j++){
          if(strchr("\t\n\v\f\r", rtext[j])) rtext[j] = ' ';
        }
        while(--j >= 0){
          if(rtext[j] != ' ') break;
          rtext[j] = '\0';
        }
        for(j = 0; rtext[j] != '\0'; j++){
          if(rtext[j] != ' ') break;
        }
        oddocaddattr(doc, "title", rtext + j);
        awords = odbreaktext(rtext);
        for(j = 0; j < cblistnum(awords); j++){
          asis = cblistval(awords, j, NULL);
          normal = odnormalizeword(asis);
          oddocaddword(doc, normal, "");
          free(normal);
        }
        cblistclose(awords);
        free(rtext);
      }
    } else if(cbstrfwimatch(text, "<body")){
      body = TRUE;
    } else if(body && text[0] != '<'){
      rtext = cbreplace(text, pairs);
      awords = odbreaktext(rtext);
      for(j = 0; j < cblistnum(awords); j++){
        asis = cblistval(awords, j, NULL);
        normal = odnormalizeword(asis);
        oddocaddword(doc, normal, asis);
        free(normal);
      }
      cblistclose(awords);
      free(rtext);
    }
  }
  if(!body){
    for(i = 0; i < cblistnum(elems); i++){
      text = cblistval(elems, i, NULL);
      if(cbstrfwimatch(text, "<title")){
        i++;
      } else if(text[0] != '<'){
        rtext = cbreplace(text, pairs);
        awords = odbreaktext(rtext);
        for(j = 0; j < cblistnum(awords); j++){
          asis = cblistval(awords, j, NULL);
          normal = odnormalizeword(asis);
          oddocaddword(doc, normal, asis);
          free(normal);
        }
        cblistclose(awords);
        free(rtext);
      }
    }
  }
  cblistclose(elems);
  return doc;
}


/* get pairs of escaping characters */
CBMAP *htmlescpairs(void){
  char *latinext[] = {
    " ", "!", "(cent)", "(pound)", "(currency)", "(yen)", "|", "(section)", "\"", "(C)",
    "", "<<", "(not)", "-", "(R)", "~", "(degree)", "+-", "^2", "^3",
    "'", "(u)", "(P)", "*", ",", "^1", "", ">>", "(1/4)", "(1/2)",
    "(3/4)", "?", "A", "A", "A", "A", "A", "A", "AE", "C",
    "E", "E", "E", "E", "I", "I", "I", "I", "D", "N",
    "O", "O", "O", "O", "O", "*", "O", "U", "U", "U",
    "U", "Y", "P", "s", "a", "a", "a", "a", "a", "a",
    "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "o", "n", "o", "o", "o", "o", "o", "/", "o", "u",
    "u", "u", "u", "y", "p", "y", NULL
  };
  static CBMAP *pairs = NULL;
  char kbuf[8], vbuf[8];
  int i, ksiz, vsiz;
  if(pairs) return pairs;
  pairs = cbmapopen();
  cbglobalgc(pairs, (void (*)(void *))cbmapclose);
  cbmapput(pairs, "&amp;", -1, "&", -1, TRUE);
  cbmapput(pairs, "&lt;", -1, "<", -1, TRUE);
  cbmapput(pairs, "&gt;", -1, ">", -1, TRUE);
  cbmapput(pairs, "&quot;", -1, "\"", -1, TRUE);
  cbmapput(pairs, "&apos;", -1, "'", -1, TRUE);
  cbmapput(pairs, "&nbsp;", -1, " ", -1, TRUE);
  cbmapput(pairs, "&copy;", -1, "(C)", -1, TRUE);
  cbmapput(pairs, "&reg;", -1, "(R)", -1, TRUE);
  cbmapput(pairs, "&trade;", -1, "(TM)", -1, TRUE);
  for(i = 1; i <= 127; i++){
    ksiz = sprintf(kbuf, "&#%d;", i);
    vsiz = sprintf(vbuf, "%c", i);
    cbmapput(pairs, kbuf, ksiz, vbuf, vsiz, TRUE);
  }
  cbmapput(pairs, "&#130;", -1, ",", -1, TRUE);
  cbmapput(pairs, "&#132;", -1, ",,", -1, TRUE);
  cbmapput(pairs, "&#133;", -1, "...", -1, TRUE);
  cbmapput(pairs, "&#139;", -1, "<", -1, TRUE);
  cbmapput(pairs, "&#145;", -1, "'", -1, TRUE);
  cbmapput(pairs, "&#146;", -1, "'", -1, TRUE);
  cbmapput(pairs, "&#147;", -1, "\"", -1, TRUE);
  cbmapput(pairs, "&#148;", -1, "\"", -1, TRUE);
  cbmapput(pairs, "&#150;", -1, "-", -1, TRUE);
  cbmapput(pairs, "&#151;", -1, "-", -1, TRUE);
  cbmapput(pairs, "&#152;", -1, "~", -1, TRUE);
  cbmapput(pairs, "&#153;", -1, "(TM)", -1, TRUE);
  cbmapput(pairs, "&#155;", -1, ">", -1, TRUE);
  for(i = 0; latinext[i]; i++){
    ksiz = sprintf(kbuf, "&#%d;", i + 160);
    cbmapput(pairs, kbuf, ksiz, latinext[i], -1, TRUE);
  }
  return pairs;
}


/* register scores of documents */
int procrelate(const char *name){
  ODEUM *odeum;
  DEPOT *scdb;
  ODDOC *doc;
  CBMAP *scores;
  const char *file;
  char path[PATHBUFSIZ], *mbuf;
  int err, fatal, id, msiz;
  printfinfo("%s: relating started", name);
  if(!(odeum = odopen(name, OD_OWRITER))){
    pdperror(name);
    return 1;
  }
  sprintf(path, "%s%c%s", name, PATHCHR, SCDBNAME);
  if(!(scdb = dpopen(path, OD_OWRITER | OD_OCREAT, SCDBBNUM))){
    pdperror(name);
    odclose(odeum);
    return 1;
  }
  if(!dpsetalign(scdb, SCDBALIGN)){
    pdperror(name);
    dpclose(scdb);
    odclose(odeum);
    return 1;
  }
  printfinfo("%s: database opened: fsiz=%.0f dnum=%d wnum=%d bnum=%d",
             name, odfsiz(odeum), oddnum(odeum), odwnum(odeum), odbnum(odeum));
  err = FALSE;
  if(!oditerinit(odeum)){
    pdperror(name);
    err = TRUE;
  } else {
    while(TRUE){
      if(sigterm){
        printferror("aborting due to a termination signal");
        err = TRUE;
        break;
      }
      if(!(doc = oditernext(odeum))){
        if(dpecode != DP_ENOITEM){
          pdperror(name);
          err = TRUE;
        }
        break;
      }
      file = oddocuri(doc);
      id = oddocid(doc);
      scores = oddocscores(doc, KEYNUM, odeum);
      mbuf = cbmapdump(scores, &msiz);
      if(!dpput(scdb, (char *)&id, sizeof(int), mbuf, msiz, DP_DOVER)){
        pdperror(name);
        err = TRUE;
      } else {
        printfinfo("%s: related", file);
      }
      free(mbuf);
      cbmapclose(scores);
      oddocclose(doc);
      if(err) break;
    }
  }
  fatal = odfatalerror(odeum);
  printfinfo("%s: database closing: fsiz=%.0f dnum=%d wnum=%d bnum=%d",
             name, odfsiz(odeum), oddnum(odeum), odwnum(odeum), odbnum(odeum));
  if(!dpclose(scdb)){
    pdperror(name);
    err = TRUE;
  }
  if(!odclose(odeum)){
    pdperror(name);
    err = TRUE;
  }
  if(err){
    printfinfo("%s: relating was over%s", name, fatal ? " with fatal error" : "");
  } else {
    printfinfo("%s: relating completed successfully", name);
  }
  return err ? 1 : 0;
}


/* purge documents which is not existing. */
int procpurge(const char *name){
  ODEUM *odeum;
  ODDOC *doc;
  const char *file;
  int err, fatal;
  printfinfo("%s: purging started", name);
  if(!(odeum = odopen(name, OD_OWRITER))){
    pdperror(name);
    return 1;
  }
  printfinfo("%s: database opened: fsiz=%.0f dnum=%d wnum=%d bnum=%d",
             name, odfsiz(odeum), oddnum(odeum), odwnum(odeum), odbnum(odeum));
  err = FALSE;
  if(!oditerinit(odeum)){
    pdperror(name);
    err = TRUE;
  } else {
    while(TRUE){
      if(sigterm){
        printferror("aborting due to a termination signal");
        err = TRUE;
        break;
      }
      if(!(doc = oditernext(odeum))){
        if(dpecode != DP_ENOITEM){
          pdperror(name);
          err = TRUE;
        }
        break;
      }
      file = oddocuri(doc);
      if(cbfilestat(file, NULL, NULL, NULL)){
        printfinfo("%s: passed", file);
      } else {
        if(!odout(odeum, file)){
          pdperror(file);
          err = TRUE;
        }
        printfinfo("%s: purged", file);
      }
      oddocclose(doc);
    }
  }
  fatal = odfatalerror(odeum);
  printfinfo("%s: database closing: fsiz=%.0f dnum=%d wnum=%d bnum=%d",
             name, odfsiz(odeum), oddnum(odeum), odwnum(odeum), odbnum(odeum));
  if(!odclose(odeum)){
    pdperror(name);
    err = TRUE;
  }
  if(err){
    printfinfo("%s: purging was over%s", name, fatal ? " with fatal error" : "");
  } else {
    printfinfo("%s: purging completed successfully", name);
  }
  return err ? 1 : 0;
}



/* END OF FILE */
