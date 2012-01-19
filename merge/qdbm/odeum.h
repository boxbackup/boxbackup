/*************************************************************************************************
 * The inverted API of QDBM
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


#ifndef _ODEUM_H                         /* duplication check */
#define _ODEUM_H

#if defined(__cplusplus)                 /* export for C++ */
extern "C" {
#endif


#include <depot.h>
#include <curia.h>
#include <cabin.h>
#include <villa.h>
#include <stdlib.h>
#include <time.h>


#if defined(_MSC_VER) && !defined(QDBM_INTERNAL) && !defined(QDBM_STATIC)
#define MYEXTERN extern __declspec(dllimport)
#else
#define MYEXTERN extern
#endif



/*************************************************************************************************
 * API
 *************************************************************************************************/


typedef struct {                         /* type of structure for a database handle */
  char *name;                            /* name of the database directory */
  int wmode;                             /* whether to be writable */
  int fatal;                             /* whether a fatal error occured */
  int inode;                             /* inode of the database directory */
  CURIA *docsdb;                         /* database handle for documents */
  CURIA *indexdb;                        /* database handle for the inverted index */
  VILLA *rdocsdb;                        /* database handle for the reverse dictionary */
  CBMAP *cachemap;                       /* cache for dirty buffers of words */
  int cacheasiz;                         /* total allocated size of dirty buffers */
  CBMAP *sortmap;                        /* map handle for candidates of sorting */
  int dmax;                              /* max number of the document ID */
  int dnum;                              /* number of the documents */
  int ldid;                              /* ID number of the last registered document */
  char statechars[256];                  /* state of single byte characters */
} ODEUM;

typedef struct {                         /* type of structure for a document handle */
  int id;                                /* ID number */
  char *uri;                             /* uniform resource identifier */
  CBMAP *attrs;                          /* map handle for attrubutes */
  CBLIST *nwords;                        /* list handle for words in normalized form */
  CBLIST *awords;                        /* list handle for words in appearance form */
} ODDOC;

typedef struct {                         /* type of structure for an element of search result */
  int id;                                /* ID number of the document */
  int score;                             /* score of the document */
} ODPAIR;

enum {                                   /* enumeration for open modes */
  OD_OREADER = 1 << 0,                   /* open as a reader */
  OD_OWRITER = 1 << 1,                   /* open as a writer */
  OD_OCREAT = 1 << 2,                    /* a writer creating */
  OD_OTRUNC = 1 << 3,                    /* a writer truncating */
  OD_ONOLCK = 1 << 4,                    /* open without locking */
  OD_OLCKNB = 1 << 5                     /* lock without blocking */
};


/* Get a database handle.
   `name' specifies the name of a database directory.
   `omode' specifies the connection mode: `OD_OWRITER' as a writer, `OD_OREADER' as a reader.
   If the mode is `OD_OWRITER', the following may be added by bitwise or: `OD_OCREAT', which
   means it creates a new database if not exist, `OD_OTRUNC', which means it creates a new
   database regardless if one exists.  Both of `OD_OREADER' and `OD_OWRITER' can be added to by
   bitwise or: `OD_ONOLCK', which means it opens a database directory without file locking, or
   `OD_OLCKNB', which means locking is performed without blocking.
   The return value is the database handle or `NULL' if it is not successful.
   While connecting as a writer, an exclusive lock is invoked to the database directory.
   While connecting as a reader, a shared lock is invoked to the database directory.
   The thread blocks until the lock is achieved.  If `OD_ONOLCK' is used, the application is
   responsible for exclusion control. */
ODEUM *odopen(const char *name, int omode);


/* Close a database handle.
   `odeum' specifies a database handle.
   If successful, the return value is true, else, it is false.
   Because the region of a closed handle is released, it becomes impossible to use the handle.
   Updating a database is assured to be written when the handle is closed.  If a writer opens
   a database but does not close it appropriately, the database will be broken. */
int odclose(ODEUM *odeum);


/* Store a document.
   `odeum' specifies a database handle connected as a writer.
   `doc' specifies a document handle.
   `wmax' specifies the max number of words to be stored in the document database.  If it is
   negative, the number is unlimited.
   `over' specifies whether the data of the duplicated document is overwritten or not.  If it
   is false and the URI of the document is duplicated, the function returns as an error.
   If successful, the return value is true, else, it is false. */
int odput(ODEUM *odeum, ODDOC *doc, int wmax, int over);


/* Delete a document specified by a URI.
   `odeum' specifies a database handle connected as a writer.
   `uri' specifies the string of the URI of a document.
   If successful, the return value is true, else, it is false.  False is returned when no
   document corresponds to the specified URI. */
int odout(ODEUM *odeum, const char *uri);


/* Delete a document specified by an ID number.
   `odeum' specifies a database handle connected as a writer.
   `id' specifies the ID number of a document.
   If successful, the return value is true, else, it is false.  False is returned when no
   document corresponds to the specified ID number. */
int odoutbyid(ODEUM *odeum, int id);


/* Retrieve a document specified by a URI.
   `odeum' specifies a database handle.
   `uri' specifies the string the URI of a document.
   If successful, the return value is the handle of the corresponding document, else, it is
   `NULL'.  `NULL' is returned when no document corresponds to the specified URI.
   Because the handle of the return value is opened with the function `oddocopen', it should
   be closed with the function `oddocclose'. */
ODDOC *odget(ODEUM *odeum, const char *uri);


/* Retrieve a document by an ID number.
   `odeum' specifies a database handle.
   `id' specifies the ID number of a document.
   If successful, the return value is the handle of the corresponding document, else, it is
   `NULL'.  `NULL' is returned when no document corresponds to the specified ID number.
   Because the handle of the return value is opened with the function `oddocopen', it should
   be closed with the function `oddocclose'. */
ODDOC *odgetbyid(ODEUM *odeum, int id);


/* Retrieve the ID of the document specified by a URI.
   `odeum' specifies a database handle.
   `uri' specifies the string the URI of a document.
   If successful, the return value is the ID number of the document, else, it is -1.  -1 is
   returned when no document corresponds to the specified URI. */
int odgetidbyuri(ODEUM *odeum, const char *uri);


/* Check whether the document specified by an ID number exists.
   `odeum' specifies a database handle.
   `id' specifies the ID number of a document.
   The return value is true if the document exists, else, it is false. */
int odcheck(ODEUM *odeum, int id);


/* Search the inverted index for documents including a particular word.
   `odeum' specifies a database handle.
   `word' specifies a searching word.
   `max' specifies the max number of documents to be retrieve.
   `np' specifies the pointer to a variable to which the number of the elements of the return
   value is assigned.
   If successful, the return value is the pointer to an array, else, it is `NULL'.  Each
   element of the array is a pair of the ID number and the score of a document, and sorted in
   descending order of their scores.  Even if no document corresponds to the specified word,
   it is not error but returns an dummy array.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use.  Note that each element of the array
   of the return value can be data of a deleted document. */
ODPAIR *odsearch(ODEUM *odeum, const char *word, int max, int *np);


/* Get the number of documents including a word.
   `odeum' specifies a database handle.
   `word' specifies a searching word.
   If successful, the return value is the number of documents including the word, else, it is -1.
   Because this function does not read the entity of the inverted index, it is faster than
   `odsearch'. */
int odsearchdnum(ODEUM *odeum, const char *word);


/* Initialize the iterator of a database handle.
   `odeum' specifies a database handle.
   If successful, the return value is true, else, it is false.
   The iterator is used in order to access every document stored in a database. */
int oditerinit(ODEUM *odeum);


/* Get the next key of the iterator.
   `odeum' specifies a database handle.
   If successful, the return value is the handle of the next document, else, it is `NULL'.
   `NULL' is returned when no document is to be get out of the iterator.
   It is possible to access every document by iteration of calling this function.  However,
   it is not assured if updating the database is occurred while the iteration.  Besides, the
   order of this traversal access method is arbitrary, so it is not assured that the order of
   string matches the one of the traversal access.  Because the handle of the return value is
   opened with the function `oddocopen', it should be closed with the function `oddocclose'. */
ODDOC *oditernext(ODEUM *odeum);


/* Synchronize updating contents with the files and the devices.
   `odeum' specifies a database handle connected as a writer.
   If successful, the return value is true, else, it is false.
   This function is useful when another process uses the connected database directory. */
int odsync(ODEUM *odeum);


/* Optimize a database.
   `odeum' specifies a database handle connected as a writer.
   If successful, the return value is true, else, it is false.
   Elements of the deleted documents in the inverted index are purged. */
int odoptimize(ODEUM *odeum);


/* Get the name of a database.
   `odeum' specifies a database handle.
   If successful, the return value is the pointer to the region of the name of the database,
   else, it is `NULL'.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
char *odname(ODEUM *odeum);


/* Get the total size of database files.
   `odeum' specifies a database handle.
   If successful, the return value is the total size of the database files, else, it is -1.0. */
double odfsiz(ODEUM *odeum);


/* Get the total number of the elements of the bucket arrays in the inverted index.
   `odeum' specifies a database handle.
   If successful, the return value is the total number of the elements of the bucket arrays,
   else, it is -1. */
int odbnum(ODEUM *odeum);


/* Get the total number of the used elements of the bucket arrays in the inverted index.
   `odeum' specifies a database handle.
   If successful, the return value is the total number of the used elements of the bucket
   arrays, else, it is -1. */
int odbusenum(ODEUM *odeum);


/* Get the number of the documents stored in a database.
   `odeum' specifies a database handle.
   If successful, the return value is the number of the documents stored in the database, else,
   it is -1. */
int oddnum(ODEUM *odeum);


/* Get the number of the words stored in a database.
   `odeum' specifies a database handle.
   If successful, the return value is the number of the words stored in the database, else,
   it is -1.
   Because of the I/O buffer, the return value may be less than the hard number. */
int odwnum(ODEUM *odeum);


/* Check whether a database handle is a writer or not.
   `odeum' specifies a database handle.
   The return value is true if the handle is a writer, false if not. */
int odwritable(ODEUM *odeum);


/* Check whether a database has a fatal error or not.
   `odeum' specifies a database handle.
   The return value is true if the database has a fatal error, false if not. */
int odfatalerror(ODEUM *odeum);


/* Get the inode number of a database directory.
   `odeum' specifies a database handle.
   The return value is the inode number of the database directory. */
int odinode(ODEUM *odeum);


/* Get the last modified time of a database.
   `odeum' specifies a database handle.
   The return value is the last modified time of the database. */
time_t odmtime(ODEUM *odeum);


/* Merge plural database directories.
   `name' specifies the name of a database directory to create.
   `elemnames' specifies a list of names of element databases.
   If successful, the return value is true, else, it is false.
   If two or more documents which have the same URL come in, the first one is adopted and the
   others are ignored. */
int odmerge(const char *name, const CBLIST *elemnames);


/* Remove a database directory.
   `name' specifies the name of a database directory.
   If successful, the return value is true, else, it is false.
   A database directory can contain databases of other APIs of QDBM, they are also removed by
   this function. */
int odremove(const char *name);


/* Get a document handle.
   `uri' specifies the URI of a document.
   The return value is a document handle.
   The ID number of a new document is not defined.  It is defined when the document is stored
   in a database. */
ODDOC *oddocopen(const char *uri);


/* Close a document handle.
   `doc' specifies a document handle.
   Because the region of a closed handle is released, it becomes impossible to use the handle. */
void oddocclose(ODDOC *doc);


/* Add an attribute to a document.
   `doc' specifies a document handle.
   `name' specifies the string of the name of an attribute.
   `value' specifies the string of the value of the attribute. */
void oddocaddattr(ODDOC *doc, const char *name, const char *value);


/* Add a word to a document.
   `doc' specifies a document handle.
   `normal' specifies the string of the normalized form of a word.  Normalized forms are
   treated as keys of the inverted index.  If the normalized form of a word is an empty
   string, the word is not reflected in the inverted index.
   `asis' specifies the string of the appearance form of the word.  Appearance forms are used
   after the document is retrieved by an application. */
void oddocaddword(ODDOC *doc, const char *normal, const char *asis);


/* Get the ID number of a document.
   `doc' specifies a document handle.
   The return value is the ID number of a document. */
int oddocid(const ODDOC *doc);


/* Get the URI of a document.
   `doc' specifies a document handle.
   The return value is the string of the URI of a document. */
const char *oddocuri(const ODDOC *doc);


/* Get the value of an attribute of a document.
   `doc' specifies a document handle.
   `name' specifies the string of the name of an attribute.
   The return value is the string of the value of the attribute, or `NULL' if no attribute
   corresponds. */
const char *oddocgetattr(const ODDOC *doc, const char *name);


/* Get the list handle contains words in normalized form of a document.
   `doc' specifies a document handle.
   The return value is the list handle contains words in normalized form. */
const CBLIST *oddocnwords(const ODDOC *doc);


/* Get the list handle contains words in appearance form of a document.
   `doc' specifies a document handle.
   The return value is the list handle contains words in appearance form. */
const CBLIST *oddocawords(const ODDOC *doc);


/* Get the map handle contains keywords in normalized form and their scores.
   `doc' specifies a document handle.
   `max' specifies the max number of keywords to get.
   `odeum' specifies a database handle with which the IDF for weighting is calculate.
   If it is `NULL', it is not used.
   The return value is the map handle contains keywords and their scores.  Scores are expressed
   as decimal strings.
   Because the handle of the return value is opened with the function `cbmapopen', it should
   be closed with the function `cbmapclose' if it is no longer in use. */
CBMAP *oddocscores(const ODDOC *doc, int max, ODEUM *odeum);


/* Break a text into words in appearance form.
   `text' specifies the string of a text.
   The return value is the list handle contains words in appearance form.
   Words are separated with space characters and such delimiters as period, comma and so on.
   Because the handle of the return value is opened with the function `cblistopen', it should
   be closed with the function `cblistclose' if it is no longer in use. */
CBLIST *odbreaktext(const char *text);


/* Make the normalized form of a word.
   `asis' specifies the string of the appearance form of a word.
   The return value is is the string of the normalized form of the word.
   Alphabets of the ASCII code are unified into lower cases.  Words composed of only delimiters
   are treated as empty strings.  Because the region of the return value is allocated with the
   `malloc' call, it should be released with the `free' call if it is no longer in use. */
char *odnormalizeword(const char *asis);


/* Get the common elements of two sets of documents.
   `apairs' specifies the pointer to the former document array.
   `anum' specifies the number of the elements of the former document array.
   `bpairs' specifies the pointer to the latter document array.
   `bnum' specifies the number of the elements of the latter document array.
   `np' specifies the pointer to a variable to which the number of the elements of the return
   value is assigned.
   The return value is the pointer to a new document array whose elements commonly belong to
   the specified two sets.
   Elements of the array are sorted in descending order of their scores.  Because the region of
   the return value is allocated with the `malloc' call, it should be released with the `free'
   call if it is no longer in use. */
ODPAIR *odpairsand(ODPAIR *apairs, int anum, ODPAIR *bpairs, int bnum, int *np);


/* Get the sum of elements of two sets of documents.
   `apairs' specifies the pointer to the former document array.
   `anum' specifies the number of the elements of the former document array.
   `bpairs' specifies the pointer to the latter document array.
   `bnum' specifies the number of the elements of the latter document array.
   `np' specifies the pointer to a variable to which the number of the elements of the return
   value is assigned.
   The return value is the pointer to a new document array whose elements belong to both or
   either of the specified two sets.
   Elements of the array are sorted in descending order of their scores.  Because the region of
   the return value is allocated with the `malloc' call, it should be released with the `free'
   call if it is no longer in use. */
ODPAIR *odpairsor(ODPAIR *apairs, int anum, ODPAIR *bpairs, int bnum, int *np);


/* Get the difference set of documents.
   `apairs' specifies the pointer to the former document array.
   `anum' specifies the number of the elements of the former document array.
   `bpairs' specifies the pointer to the latter document array of the sum of elements.
   `bnum' specifies the number of the elements of the latter document array.
   `np' specifies the pointer to a variable to which the number of the elements of the return
   value is assigned.
   The return value is the pointer to a new document array whose elements belong to the former
   set but not to the latter set.
   Elements of the array are sorted in descending order of their scores.  Because the region of
   the return value is allocated with the `malloc' call, it should be released with the `free'
   call if it is no longer in use. */
ODPAIR *odpairsnotand(ODPAIR *apairs, int anum, ODPAIR *bpairs, int bnum, int *np);


/* Sort a set of documents in descending order of scores.
   `pairs' specifies the pointer to a document array.
   `pnum' specifies the number of the elements of the document array. */
void odpairssort(ODPAIR *pairs, int pnum);


/* Get the natural logarithm of a number.
   `x' specifies a number.
   The return value is the natural logarithm of the number.  If the number is equal to or less
   than 1.0, the return value is 0.0.
   This function is useful when an application calculates the IDF of search results. */
double odlogarithm(double x);


/* Get the cosine of the angle of two vectors.
   `avec' specifies the pointer to one array of numbers.
   `bvec' specifies the pointer to the other array of numbers.
   `vnum' specifies the number of elements of each array.
   The return value is the cosine of the angle of two vectors.
   This function is useful when an application calculates similarity of documents. */
double odvectorcosine(const int *avec, const int *bvec, int vnum);


/* Set the global tuning parameters.
   `ibnum' specifies the number of buckets for inverted indexes.
   `idnum' specifies the division number of inverted index.
   `cbnum' specifies the number of buckets for dirty buffers.
   `csiz' specifies the maximum bytes to use memory for dirty buffers.
   The default setting is equivalent to `odsettuning(32749, 7, 262139, 8388608)'.  This function
   should be called before opening a handle. */
void odsettuning(int ibnum, int idnum, int cbnum, int csiz);


/* Break a text into words and store appearance forms and normalized form into lists.
   `odeum' specifies a database handle.
   `text' specifies the string of a text.
   `awords' specifies a list handle into which appearance form is store.
   `nwords' specifies a list handle into which normalized form is store.  If it is `NULL', it is
   ignored.
   Words are separated with space characters and such delimiters as period, comma and so on. */
void odanalyzetext(ODEUM *odeum, const char *text, CBLIST *awords, CBLIST *nwords);


/* Set the classes of characters used by `odanalyzetext'.
   `odeum' specifies a database handle.
   `spacechars' spacifies a string contains space characters.
   `delimchars' spacifies a string contains delimiter characters.
   `gluechars' spacifies a string contains glue characters. */
void odsetcharclass(ODEUM *odeum, const char *spacechars, const char *delimchars,
                    const char *gluechars);


/* Query a database using a small boolean query language.
   `odeum' specifies a database handle.
   'query' specifies the text of the query.
   `np' specifies the pointer to a variable to which the number of the elements of the return
   value is assigned.
   `errors' specifies a list handle into which error messages are stored.  If it is `NULL', it
   is ignored.
   If successful, the return value is the pointer to an array, else, it is `NULL'.  Each
   element of the array is a pair of the ID number and the score of a document, and sorted in
   descending order of their scores.  Even if no document corresponds to the specified condition,
   it is not error but returns an dummy array.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use.  Note that each element of the array
   of the return value can be data of a deleted document. */
ODPAIR *odquery(ODEUM *odeum, const char *query, int *np, CBLIST *errors);



/*************************************************************************************************
 * features for experts
 *************************************************************************************************/


/* Get the internal database handle for documents.
   `odeum' specifies a database handle.
   The return value is the internal database handle for documents.
   Note that the the returned handle should not be updated. */
CURIA *odidbdocs(ODEUM *odeum);


/* Get the internal database handle for the inverted index.
   `odeum' specifies a database handle.
   The return value is the internal database handle for the inverted index.
   Note that the the returned handle should not be updated. */
CURIA *odidbindex(ODEUM *odeum);


/* Get the internal database handle for the reverse dictionary.
   `odeum' specifies a database handle.
   The return value is the internal database handle for the reverse dictionary.
   Note that the the returned handle should not be updated. */
VILLA *odidbrdocs(ODEUM *odeum);


/* Set the call back function called in merging.
   `otcb' specifires the pointer to a function to report outturn.  Its first argument is the name
   of processing function.  Its second argument is the handle of the database being processed.
   Its third argument is ths string of a log message.  If it is `NULL', the call back function is
   cleared. */
void odsetotcb(void (*otcb)(const char *, ODEUM *, const char *));


/* Get the positive one of square roots of a number.
   `x' specifies a number.
   The return value is the positive one of square roots of a number.  If the number is equal to
   or less than 0.0, the return value is 0.0. */
double odsquareroot(double x);


/* Get the absolute of a vector.
   `vec' specifies the pointer to an array of numbers.
   `vnum' specifies the number of elements of the array.
   The return value is the absolute of a vector. */
double odvecabsolute(const int *vec, int vnum);


/* Get the inner product of two vectors.
   `avec' specifies the pointer to one array of numbers.
   `bvec' specifies the pointer to the other array of numbers.
   `vnum' specifies the number of elements of each array.
   The return value is the inner product of two vectors. */
double odvecinnerproduct(const int *avec, const int *bvec, int vnum);



#undef MYEXTERN

#if defined(__cplusplus)                 /* export for C++ */
}
#endif

#endif                                   /* duplication check */


/* END OF FILE */
