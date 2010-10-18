/*************************************************************************************************
 * Popular encoders and decoders
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
#include <stdlib.h>
#include <string.h>
#include <time.h>

#undef TRUE
#define TRUE           1                 /* boolean true */
#undef FALSE
#define FALSE          0                 /* boolean false */

#define DEFCODE        "UTF-8"           /* default encoding */


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
char *readstdin(int *sp);
int runurl(int argc, char **argv);
int runbase(int argc, char **argv);
int runquote(int argc, char **argv);
int runmime(int argc, char **argv);
int runcsv(int argc, char **argv);
int runxml(int argc, char **argv);
int runzlib(int argc, char **argv);
int runlzo(int argc, char **argv);
int runbzip(int argc, char **argv);
int runiconv(int argc, char **argv);
int rundate(int argc, char **argv);
void shouucsmap(void);


/* main routine */
int main(int argc, char **argv){
  int rv;
  cbstdiobin();
  progname = argv[0];
  if(argc < 2) usage();
  rv = 0;
  if(!strcmp(argv[1], "url")){
    rv = runurl(argc, argv);
  } else if(!strcmp(argv[1], "base")){
    rv = runbase(argc, argv);
  } else if(!strcmp(argv[1], "quote")){
    rv = runquote(argc, argv);
  } else if(!strcmp(argv[1], "mime")){
    rv = runmime(argc, argv);
  } else if(!strcmp(argv[1], "csv")){
    rv = runcsv(argc, argv);
  } else if(!strcmp(argv[1], "xml")){
    rv = runxml(argc, argv);
  } else if(!strcmp(argv[1], "zlib")){
    rv = runzlib(argc, argv);
  } else if(!strcmp(argv[1], "lzo")){
    rv = runlzo(argc, argv);
  } else if(!strcmp(argv[1], "bzip")){
    rv = runbzip(argc, argv);
  } else if(!strcmp(argv[1], "iconv")){
    rv = runiconv(argc, argv);
  } else if(!strcmp(argv[1], "date")){
    rv = rundate(argc, argv);
  } else {
    usage();
  }
  return rv;
}


/* print the usage and exit */
void usage(void){
  char *tmp;
  int tsiz;
  fprintf(stderr, "%s: popular encoders and decoders\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s url [-d] [-br] [-rs base target] [-l] [-e expr] [file]\n", progname);
  fprintf(stderr, "  %s base [-d] [-l] [-c num] [-e expr] [file]\n", progname);
  fprintf(stderr, "  %s quote [-d] [-l] [-c num] [-e expr] [file]\n", progname);
  fprintf(stderr, "  %s mime [-d] [-hd] [-bd] [-part num] [-l] [-ec code] [-qp] [-dc] [-e expr]"
          " [file]\n", progname);
  fprintf(stderr, "  %s csv [-d] [-t] [-l] [-e expr] [-html] [file]\n", progname);
  fprintf(stderr, "  %s xml [-d] [-p] [-l] [-e expr] [-tsv] [file]\n", progname);
  if((tmp = cbdeflate("", 0, &tsiz)) != NULL){
    fprintf(stderr, "  %s zlib [-d] [-gz] [-crc] [file]\n", progname);
    free(tmp);
  }
  if((tmp = cblzoencode("", 0, &tsiz)) != NULL){
    fprintf(stderr, "  %s lzo [-d] [file]\n", progname);
    free(tmp);
  }
  if((tmp = cbbzencode("", 0, &tsiz)) != NULL){
    fprintf(stderr, "  %s bzip [-d] [file]\n", progname);
    free(tmp);
  }
  if((tmp = cbiconv("", 0, "US-ASCII", "US-ASCII", NULL, NULL)) != NULL){
    fprintf(stderr, "  %s iconv [-ic code] [-oc code] [-ol ltype] [-cn] [-wc] [-um] [file]\n",
            progname);
    free(tmp);
  }
  fprintf(stderr, "  %s date [-wf] [-rf] [-utc] [str]\n", progname);
  fprintf(stderr, "\n");
  exit(1);
}


/* read the standard input */
char *readstdin(int *sp){
  char *buf;
  int i, blen, c;
  blen = 256;
  buf = cbmalloc(blen);
  for(i = 0; (c = getchar()) != EOF; i++){
    if(i >= blen - 1) buf = cbrealloc(buf, blen *= 2);
    buf[i] = c;
  }
  buf[i] = '\0';
  if(sp) *sp = i;
  return buf;
}


/* parse arguments of url command */
int runurl(int argc, char **argv){
  CBMAP *map;
  int i, size, dec, br, line;
  const char *val;
  char *base, *target, *expr, *file, *buf, *res;
  dec = FALSE;
  br = FALSE;
  line = FALSE;
  base = NULL;
  target = NULL;
  expr = NULL;
  file = NULL;
  for(i = 2; i < argc; i++){
    if(!file && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-d")){
        dec = TRUE;
      } else if(!strcmp(argv[i], "-br")){
        br = TRUE;
      } else if(!strcmp(argv[i], "-rs")){
        if(++i >= argc) usage();
        base = argv[i];
        if(++i >= argc) usage();
        target = argv[i];
      } else if(!strcmp(argv[i], "-l")){
        line = TRUE;
      } else if(!strcmp(argv[i], "-e")){
        if(++i >= argc) usage();
        expr = argv[i];
      } else {
        usage();
      }
    } else if(!file){
      file = argv[i];
    } else {
      usage();
    }
  }
  buf = NULL;
  if(base){
    size = strlen(base);
    buf = cbmemdup(base, size);
  } else if(expr){
    size = strlen(expr);
    buf = cbmemdup(expr, size);
  } else if(file){
    if(!(buf = cbreadfile(file, &size))){
      fprintf(stderr, "%s: %s: cannot open\n", progname, file);
      return 1;
    }
  } else {
    buf = readstdin(&size);
  }
  if(target){
    res = cburlresolve(base, target);
    printf("%s", res);
    free(res);
  } else if(br){
    map = cburlbreak(buf);
    if((val = cbmapget(map, "self", -1, NULL))) printf("self\t%s\n", val);
    if((val = cbmapget(map, "scheme", -1, NULL))) printf("scheme\t%s\n", val);
    if((val = cbmapget(map, "host", -1, NULL))) printf("host\t%s\n", val);
    if((val = cbmapget(map, "port", -1, NULL))) printf("port\t%s\n", val);
    if((val = cbmapget(map, "authority", -1, NULL))) printf("authority\t%s\n", val);
    if((val = cbmapget(map, "path", -1, NULL))) printf("path\t%s\n", val);
    if((val = cbmapget(map, "file", -1, NULL))) printf("file\t%s\n", val);
    if((val = cbmapget(map, "query", -1, NULL))) printf("query\t%s\n", val);
    if((val = cbmapget(map, "fragment", -1, NULL))) printf("fragment\t%s\n", val);
    cbmapclose(map);
  } else if(dec){
    res = cburldecode(buf, &size);
    for(i = 0; i < size; i++){
      putchar(res[i]);
    }
    free(res);
  } else {
    res = cburlencode(buf, size);
    for(i = 0; res[i] != '\0'; i++){
      putchar(res[i]);
    }
    free(res);
  }
  if(line) putchar('\n');
  free(buf);
  return 0;
}


/* parse arguments of base command */
int runbase(int argc, char **argv){
  int i, ci, size, dec, line, cols;
  char *expr, *file, *buf, *res;
  dec = FALSE;
  line = FALSE;
  cols = -1;
  expr = NULL;
  file = NULL;
  for(i = 2; i < argc; i++){
    if(!file && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-d")){
        dec = TRUE;
      } else if(!strcmp(argv[i], "-l")){
        line = TRUE;
      } else if(!strcmp(argv[i], "-c")){
        if(++i >= argc) usage();
        cols = atoi(argv[i]);
      } else if(!strcmp(argv[i], "-e")){
        if(++i >= argc) usage();
        expr = argv[i];
      } else {
        usage();
      }
    } else if(!file){
      file = argv[i];
    } else {
      usage();
    }
  }
  buf = NULL;
  if(expr){
    size = strlen(expr);
    buf = cbmemdup(expr, size);
  } else if(file){
    if(!(buf = cbreadfile(file, &size))){
      fprintf(stderr, "%s: %s: cannot open\n", progname, file);
      return 1;
    }
  } else {
    buf = readstdin(&size);
  }
  if(dec){
    res = cbbasedecode(buf, &size);
    for(i = 0; i < size; i++){
      putchar(res[i]);
    }
    free(res);
  } else {
    res = cbbaseencode(buf, size);
    ci = 0;
    for(i = 0; res[i] != '\0'; i++){
      if(cols > 0 && ci >= cols){
        putchar('\n');
        ci = 0;
      }
      putchar(res[i]);
      ci++;
    }
    free(res);
  }
  if(line) putchar('\n');
  free(buf);
  return 0;
}


/* parse arguments of quote command */
int runquote(int argc, char **argv){
  int i, ci, size, dec, line, cols;
  char *expr, *file, *buf, *res;
  dec = FALSE;
  line = FALSE;
  cols = -1;
  expr = NULL;
  file = NULL;
  for(i = 2; i < argc; i++){
    if(!file && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-d")){
        dec = TRUE;
      } else if(!strcmp(argv[i], "-l")){
        line = TRUE;
      } else if(!strcmp(argv[i], "-c")){
        if(++i >= argc) usage();
        cols = atoi(argv[i]);
      } else if(!strcmp(argv[i], "-e")){
        if(++i >= argc) usage();
        expr = argv[i];
      } else {
        usage();
      }
    } else if(!file){
      file = argv[i];
    } else {
      usage();
    }
  }
  buf = NULL;
  if(expr){
    size = strlen(expr);
    buf = cbmemdup(expr, size);
  } else if(file){
    if(!(buf = cbreadfile(file, &size))){
      fprintf(stderr, "%s: %s: cannot open\n", progname, file);
      return 1;
    }
  } else {
    buf = readstdin(&size);
  }
  if(dec){
    res = cbquotedecode(buf, &size);
    for(i = 0; i < size; i++){
      putchar(res[i]);
    }
    free(res);
  } else {
    res = cbquoteencode(buf, size);
    ci = 0;
    for(i = 0; res[i] != '\0'; i++){
      if(cols > 0 && (ci >= cols || (ci >= cols - 2 && res[i] == '='))){
        printf("=\n");
        ci = 0;
      }
      if(res[i] == '\r' || res[i] == '\n') ci = 0;
      putchar(res[i]);
      ci++;
    }
    free(res);
  }
  if(line) putchar('\n');
  free(buf);
  return 0;
}


/* parse arguments of mime command */
int runmime(int argc, char **argv){
  CBMAP *attrs;
  CBLIST *parts;
  int i, size, dec, line, qp, dc, hd, bd, pnum, rsiz, bsiz;
  const char *key, *body;
  char *code, *expr, *file, *buf, *res, renc[64];
  dec = FALSE;
  hd = FALSE;
  bd = FALSE;
  pnum = 0;
  line = FALSE;
  dc = FALSE;
  qp = FALSE;
  code = NULL;
  expr = NULL;
  file = NULL;
  for(i = 2; i < argc; i++){
    if(!file && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-d")){
        dec = TRUE;
      } else if(!strcmp(argv[i], "-hd")){
        hd = TRUE;
      } else if(!strcmp(argv[i], "-bd")){
        bd = TRUE;
      } else if(!strcmp(argv[i], "-part")){
        if(++i >= argc) usage();
        pnum = atoi(argv[i]);
      } else if(!strcmp(argv[i], "-l")){
        line = TRUE;
      } else if(!strcmp(argv[i], "-ec")){
        if(++i >= argc) usage();
        code = argv[i];
      } else if(!strcmp(argv[i], "-qp")){
        qp = TRUE;
      } else if(!strcmp(argv[i], "-dc")){
        dc = TRUE;
      } else if(!strcmp(argv[i], "-e")){
        if(++i >= argc) usage();
        expr = argv[i];
      } else {
        usage();
      }
    } else if(!file){
      file = argv[i];
    } else {
      usage();
    }
  }
  buf = NULL;
  if(expr){
    size = strlen(expr);
    buf = cbmemdup(expr, size);
  } else if(file){
    if(!(buf = cbreadfile(file, &size))){
      fprintf(stderr, "%s: %s: cannot open\n", progname, file);
      return 1;
    }
  } else {
    buf = readstdin(&size);
  }
  if(hd || bd || pnum > 0){
    attrs = cbmapopen();
    res = cbmimebreak(buf, size, attrs, &rsiz);
    if(pnum > 0){
      parts = NULL;
      if(!(key = cbmapget(attrs, "TYPE", -1, NULL)) || !cbstrfwimatch(key, "multipart/") ||
         !(key = cbmapget(attrs, "BOUNDARY", -1, NULL)) ||
         !(parts = cbmimeparts(res, rsiz, key)) || cblistnum(parts) < pnum){
        fprintf(stderr, "%s: not multipart or no such part\n", progname);
        if(parts) cblistclose(parts);
        free(res);
        cbmapclose(attrs);
        free(buf);
        return 1;
      }
      body = cblistval(parts, pnum - 1, &bsiz);
      for(i = 0; i < bsiz; i++){
        putchar(body[i]);
      }
      cblistclose(parts);
    } else if(hd){
      cbmapiterinit(attrs);
      while((key = cbmapiternext(attrs, NULL)) != NULL){
        printf("%s\t%s\n", key, cbmapget(attrs, key, -1, NULL));
      }
    } else {
      for(i = 0; i < rsiz; i++){
        putchar(res[i]);
      }
    }
    free(res);
    cbmapclose(attrs);
  } else if(dec){
    res = cbmimedecode(buf, renc);
    printf("%s", dc ? renc : res);
    free(res);
  } else {
    res = cbmimeencode(buf, code ? code : DEFCODE, !qp);
    printf("%s", res);
    free(res);
  }
  if(line) putchar('\n');
  free(buf);
  return 0;
}


/* parse arguments of csv command */
int runcsv(int argc, char **argv){
  CBLIST *rows, *cells;
  int i, j, k, dec, tb, line, html;
  const char *row, *cell;
  char *expr, *file, *buf, *res;
  dec = FALSE;
  tb = FALSE;
  line = FALSE;
  html = FALSE;
  expr = NULL;
  file = NULL;
  for(i = 2; i < argc; i++){
    if(!file && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-d")){
        dec = TRUE;
      } else if(!strcmp(argv[i], "-t")){
        tb = TRUE;
      } else if(!strcmp(argv[i], "-l")){
        line = TRUE;
      } else if(!strcmp(argv[i], "-e")){
        if(++i >= argc) usage();
        expr = argv[i];
      } else if(!strcmp(argv[i], "-html")){
        html = TRUE;
      } else {
        usage();
      }
    } else if(!file){
      file = argv[i];
    } else {
      usage();
    }
  }
  buf = NULL;
  if(expr){
    buf = cbmemdup(expr, -1);
  } else if(file){
    if(!(buf = cbreadfile(file, NULL))){
      fprintf(stderr, "%s: %s: cannot open\n", progname, file);
      return 1;
    }
  } else {
    buf = readstdin(NULL);
  }
  if(tb || html){
    if(html) printf("<table border=\"1\">\n");
    rows = cbcsvrows(buf);
    for(i = 0; i < cblistnum(rows); i++){
      if(html) printf("<tr>");
      row = cblistval(rows, i, NULL);
      cells = cbcsvcells(row);
      for(j = 0; j < cblistnum(cells); j++){
        cell = cblistval(cells, j, NULL);
        if(html){
          printf("<td>");
          for(k = 0; cell[k] != '\0'; k++){
            if(cell[k] == '\r' || cell[k] == '\n'){
              printf("<br>");
              if(cell[k] == '\r' && cell[k] == '\n') k++;
            } else {
              switch(cell[k]){
              case '&': printf("&amp;"); break;
              case '<': printf("&lt;"); break;
              case '>': printf("&gt;"); break;
              default: putchar(cell[k]); break;
              }
            }
          }
          printf("</td>");
        } else {
          if(j > 0) putchar('\t');
          for(k = 0; cell[k] != '\0'; k++){
            if(((unsigned char *)cell)[k] >= 0x20) putchar(cell[k]);
          }
        }
      }
      cblistclose(cells);
      if(html) printf("</tr>");
      putchar('\n');
    }
    cblistclose(rows);
    if(html) printf("</table>\n");
  } else if(dec){
    res = cbcsvunescape(buf);
    for(i = 0; res[i] != '\0'; i++){
      putchar(res[i]);
    }
    free(res);
  } else {
    res = cbcsvescape(buf);
    for(i = 0; res[i] != '\0'; i++){
      putchar(res[i]);
    }
    free(res);
  }
  if(line) putchar('\n');
  free(buf);
  return 0;
}


/* parse arguments of xml command */
int runxml(int argc, char **argv){
  CBLIST *elems;
  CBMAP *attrs;
  int i, j, dec, pb, line, tsv, div;
  const char *elem, *attr;
  char *expr, *file, *buf, *res;
  dec = FALSE;
  pb = FALSE;
  line = FALSE;
  tsv = FALSE;
  expr = NULL;
  file = NULL;
  for(i = 2; i < argc; i++){
    if(!file && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-d")){
        dec = TRUE;
      } else if(!strcmp(argv[i], "-p")){
        pb = TRUE;
      } else if(!strcmp(argv[i], "-l")){
        line = TRUE;
      } else if(!strcmp(argv[i], "-e")){
        if(++i >= argc) usage();
        expr = argv[i];
      } else if(!strcmp(argv[i], "-tsv")){
        tsv = TRUE;
      } else {
        usage();
      }
    } else if(!file){
      file = argv[i];
    } else {
      usage();
    }
  }
  buf = NULL;
  if(expr){
    buf = cbmemdup(expr, -1);
  } else if(file){
    if(!(buf = cbreadfile(file, NULL))){
      fprintf(stderr, "%s: %s: cannot open\n", progname, file);
      return 1;
    }
  } else {
    buf = readstdin(NULL);
  }
  if(pb || tsv){
    elems = cbxmlbreak(buf, FALSE);
    for(i = 0; i < cblistnum(elems); i++){
      elem = cblistval(elems, i, NULL);
      div = FALSE;
      if(elem[0] == '<'){
        if(cbstrfwimatch(elem, "<?xml")){
          printf("XMLDECL");
          div = TRUE;
        } else if(cbstrfwimatch(elem, "<!DOCTYPE")){
          printf("DOCTYPE");
        } else if(cbstrfwimatch(elem, "<!--")){
          printf("COMMENT");
        } else if(cbstrfwimatch(elem, "</")){
          printf("ENDTAG");
          div = TRUE;
        } else if(cbstrbwimatch(elem, "/>")){
          printf("EMPTAG");
          div = TRUE;
        } else {
          printf("BEGTAG");
          div = TRUE;
        }
      } else {
        printf("TEXT");
      }
      putchar('\t');
      if(tsv){
        if(div){
          attrs = cbxmlattrs(elem);
          cbmapiterinit(attrs);
          for(j = 0; (attr = cbmapiternext(attrs, NULL)) != NULL; j++){
            if(j < 1){
              printf("%s", cbmapget(attrs, attr, -1, NULL));
            } else {
              printf("\t%s\t%s", attr, cbmapget(attrs, attr, -1, NULL));
            }
          }
          cbmapclose(attrs);
        } else {
          res = cbxmlunescape(elem);
          for(j = 0; elem[j] != '\0'; j++){
            if(((unsigned char *)elem)[j] < 0x20 || elem[j] == '%'){
              printf("%%%02X", elem[j]);
            } else {
              putchar(elem[j]);
            }
          }
          free(res);
        }
      } else {
        printf("%s", elem);
      }
      putchar('\n');
    }
    cblistclose(elems);
  } else if(dec){
    res = cbxmlunescape(buf);
    for(i = 0; res[i] != '\0'; i++){
      putchar(res[i]);
    }
    free(res);
  } else {
    res = cbxmlescape(buf);
    for(i = 0; res[i] != '\0'; i++){
      putchar(res[i]);
    }
    free(res);
  }
  if(line) putchar('\n');
  free(buf);
  return 0;
}


/* parse arguments of zlib command */
int runzlib(int argc, char **argv){
  unsigned int sum;
  int i, bsiz, rsiz, dec, gz, crc;
  char *file, *buf, *res;
  dec = FALSE;
  gz = FALSE;
  crc = FALSE;
  file = NULL;
  for(i = 2; i < argc; i++){
    if(!file && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-d")){
        dec = TRUE;
      } else if(!strcmp(argv[i], "-gz")){
        gz = TRUE;
      } else if(!strcmp(argv[i], "-crc")){
        crc = TRUE;
      } else {
        usage();
      }
    } else if(!file){
      file = argv[i];
    } else {
      usage();
    }
  }
  buf = NULL;
  if(file){
    if(!(buf = cbreadfile(file, &bsiz))){
      fprintf(stderr, "%s: %s: cannot open\n", progname, file);
      return 1;
    }
  } else {
    buf = readstdin(&bsiz);
  }
  if(crc){
    sum = cbgetcrc(buf, bsiz);
    for(i = 0; i < sizeof(int); i++){
      printf("%02x", sum / 0x1000000);
      sum <<= 8;
    }
    putchar('\n');
  } else if(dec){
    if(!(res = gz ? cbgzdecode(buf, bsiz, &rsiz) : cbinflate(buf, bsiz, &rsiz))){
      fprintf(stderr, "%s: inflate failed\n", progname);
      free(buf);
      return 1;
    }
    for(i = 0; i < rsiz; i++){
      putchar(res[i]);
    }
    free(res);
  } else {
    if(!(res = gz ? cbgzencode(buf, bsiz, &rsiz) : cbdeflate(buf, bsiz, &rsiz))){
      fprintf(stderr, "%s: deflate failed\n", progname);
      free(buf);
      return 1;
    }
    for(i = 0; i < rsiz; i++){
      putchar(res[i]);
    }
    free(res);
  }
  free(buf);
  return 0;
}


/* parse arguments of lzo command */
int runlzo(int argc, char **argv){
  int i, bsiz, rsiz, dec;
  char *file, *buf, *res;
  dec = FALSE;
  file = NULL;
  for(i = 2; i < argc; i++){
    if(!file && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-d")){
        dec = TRUE;
      } else {
        usage();
      }
    } else if(!file){
      file = argv[i];
    } else {
      usage();
    }
  }
  buf = NULL;
  if(file){
    if(!(buf = cbreadfile(file, &bsiz))){
      fprintf(stderr, "%s: %s: cannot open\n", progname, file);
      return 1;
    }
  } else {
    buf = readstdin(&bsiz);
  }
  if(dec){
    if(!(res = cblzodecode(buf, bsiz, &rsiz))){
      fprintf(stderr, "%s: decode failed\n", progname);
      free(buf);
      return 1;
    }
    for(i = 0; i < rsiz; i++){
      putchar(res[i]);
    }
    free(res);
  } else {
    if(!(res = cblzoencode(buf, bsiz, &rsiz))){
      fprintf(stderr, "%s: encode failed\n", progname);
      free(buf);
      return 1;
    }
    for(i = 0; i < rsiz; i++){
      putchar(res[i]);
    }
    free(res);
  }
  free(buf);
  return 0;
}


/* parse arguments of bzip command */
int runbzip(int argc, char **argv){
  int i, bsiz, rsiz, dec;
  char *file, *buf, *res;
  dec = FALSE;
  file = NULL;
  for(i = 2; i < argc; i++){
    if(!file && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-d")){
        dec = TRUE;
      } else {
        usage();
      }
    } else if(!file){
      file = argv[i];
    } else {
      usage();
    }
  }
  buf = NULL;
  if(file){
    if(!(buf = cbreadfile(file, &bsiz))){
      fprintf(stderr, "%s: %s: cannot open\n", progname, file);
      return 1;
    }
  } else {
    buf = readstdin(&bsiz);
  }
  if(dec){
    if(!(res = cbbzdecode(buf, bsiz, &rsiz))){
      fprintf(stderr, "%s: decode failed\n", progname);
      free(buf);
      return 1;
    }
    for(i = 0; i < rsiz; i++){
      putchar(res[i]);
    }
    free(res);
  } else {
    if(!(res = cbbzencode(buf, bsiz, &rsiz))){
      fprintf(stderr, "%s: encode failed\n", progname);
      free(buf);
      return 1;
    }
    for(i = 0; i < rsiz; i++){
      putchar(res[i]);
    }
    free(res);
  }
  free(buf);
  return 0;
}


/* parse arguments of iconv command */
int runiconv(int argc, char **argv){
  CBDATUM *datum;
  const char *rcode;
  char *icode, *ocode, *ltype, *file, *buf, *res, *norm, *orig;
  int i, cn, wc, bsiz, rsiz, nsiz, osiz, miss;
  icode = NULL;
  ocode = NULL;
  ltype = NULL;
  file = NULL;
  cn = FALSE;
  wc = FALSE;
  for(i = 2; i < argc; i++){
    if(!file && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-ic")){
        if(++i >= argc) usage();
        icode = argv[i];
      } else if(!strcmp(argv[i], "-oc")){
        if(++i >= argc) usage();
        ocode = argv[i];
      } else if(!strcmp(argv[i], "-ol")){
        if(++i >= argc) usage();
        ltype = argv[i];
      } else if(!strcmp(argv[i], "-cn")){
        cn = TRUE;
      } else if(!strcmp(argv[i], "-wc")){
        wc = TRUE;
      } else if(!strcmp(argv[i], "-um")){
        shouucsmap();
      } else {
        usage();
      }
    } else if(!file){
      file = argv[i];
    } else {
      usage();
    }
  }
  buf = NULL;
  if(file){
    if(!(buf = cbreadfile(file, &bsiz))){
      fprintf(stderr, "%s: %s: cannot open\n", progname, file);
      return 1;
    }
  } else {
    buf = readstdin(&bsiz);
  }
  miss = 0;
  if(cn){
    printf("%s\n", cbencname(buf, bsiz));
  } else if(wc){
    printf("%d\n", cbstrcountutf(buf));
  } else {
    rcode = icode ? icode : cbencname(buf, bsiz);
    if(!(res = cbiconv(buf, bsiz, rcode, ocode ? ocode : DEFCODE,
                       &rsiz, &miss))){
      fprintf(stderr, "%s: iconv failed\n", progname);
      free(buf);
      return 1;
    }
    if(miss > 0) fprintf(stderr, "%s: missing %d characters\n", progname, miss);
    if(ltype && (!cbstricmp(ltype, "u") || !cbstricmp(ltype, "unix") ||
                 !cbstricmp(ltype, "lf"))){
      ltype = "\n";
    } else if(ltype && (!cbstricmp(ltype, "d") || !cbstricmp(ltype, "dos") ||
                        !cbstricmp(ltype, "crlf"))){
      ltype = "\r\n";
    } else if(ltype && (!cbstricmp(ltype, "m") || !cbstricmp(ltype, "mac") ||
                        !cbstricmp(ltype, "cr"))){
      ltype = "\r";
    } else {
      ltype = NULL;
    }
    if(ltype){
      if(!(norm = cbiconv(res, rsiz, ocode, "UTF-8", &nsiz, NULL))){
        fprintf(stderr, "%s: iconv failed\n", progname);
        free(res);
        free(buf);
        return 1;
      }
      datum = cbdatumopen(NULL, -1);
      for(i = 0; i < nsiz; i++){
        if(norm[i] == '\r'){
          if(norm[i+1] == '\n') i++;
          cbdatumcat(datum, ltype, -1);
        } else if(norm[i] == '\n'){
          cbdatumcat(datum, ltype, -1);
        } else {
          cbdatumcat(datum, norm + i, 1);
        }
      }
      if(!(orig = cbiconv(cbdatumptr(datum), cbdatumsize(datum), "UTF-8", ocode, &osiz, NULL))){
        fprintf(stderr, "%s: iconv failed\n", progname);
        cbdatumclose(datum);
        free(norm);
        free(res);
        free(buf);
        return 1;
      }
      for(i = 0; i < osiz; i++){
        putchar(orig[i]);
      }
      free(orig);
      cbdatumclose(datum);
      free(norm);
    } else {
      for(i = 0; i < rsiz; i++){
        putchar(res[i]);
      }
    }
    free(res);
  }
  free(buf);
  return miss > 0 ? 1 : 0;
}


/* parse arguments of date command */
int rundate(int argc, char **argv){
  int i, wb, rb, utc, jl;
  char *date, *res;
  time_t t;
  wb = FALSE;
  rb = FALSE;
  utc = FALSE;
  date = NULL;
  for(i = 2; i < argc; i++){
    if(!date && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-wf")){
        wb = TRUE;
      } else if(!strcmp(argv[i], "-rf")){
        rb = TRUE;
      } else if(!strcmp(argv[i], "-utc")){
        utc = TRUE;
      } else {
        usage();
      }
    } else if(!date){
      date = argv[i];
    } else {
      usage();
    }
  }
  jl = utc ? 0 : cbjetlag();
  if(date){
    t = cbstrmktime(date);
  } else {
    t = time(NULL);
  }
  if(wb){
    res = cbdatestrwww(t, jl);
  } else if(rb){
    res = cbdatestrhttp(t, jl);
  } else {
    res = cbsprintf("%d", (int)t);
  }
  if(t >= 0){
    printf("%s\n", res);
  } else {
    if(date){
      fprintf(stderr, "%s: %s: invalid date format\n", progname, date);
    } else {
      fprintf(stderr, "%s: invalid time setting\n", progname);
    }
  }
  free(res);
  return 0;
}


/* show mapping of UCS-2 and exit. */
void shouucsmap(void){
  unsigned char buf[2], *tmp;
  int i, j, tsiz;
  for(i = 0; i < 65536; i++){
    buf[0] = i / 256;
    buf[1] = i % 256;
    printf("%d\t", i);
    printf("U+%02X%02X\t", buf[0], buf[1]);
    printf("\"\\x%x\\x%x\"\t", buf[0], buf[1]);
    if((tmp = (unsigned char *)cbiconv((char *)buf, 2, "UTF-16BE", "UTF-8",
                                       &tsiz, NULL)) != NULL){
      if(tsiz > 0){
        printf("\"");
        for(j = 0; j < tsiz; j++){
          printf("\\x%x", tmp[j]);
        }
        printf("\"");
      } else {
        printf("NULL");
      }
      free(tmp);
    }
    printf("\n");
  }
  exit(0);
}



/* END OF FILE */
