/*************************************************************************************************
 * Emulation of system calls
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


#include "myconf.h"



/*************************************************************************************************
 * for dosish filesystems
 *************************************************************************************************/


#if defined(_SYS_MSVC_) || defined(_SYS_MINGW_) || defined(_SYS_CYGWIN_)


#define DOSPATHBUFSIZ  8192


int _qdbm_win32_lstat(const char *pathname, struct stat *buf){
  char pbuf[DOSPATHBUFSIZ], *p;
  int inode;
  if(stat(pathname, buf) == -1) return -1;
  if(GetFullPathName(pathname, DOSPATHBUFSIZ, pbuf, &p) != 0){
    inode = 11003;
    for(p = pbuf; *p != '\0'; p++){
      inode = inode * 31 + *(unsigned char *)p;
    }
    buf->st_ino = (inode * 911) & 0x7FFF;
  }
  return 0;
}


#endif



/*************************************************************************************************
 * for POSIX thread
 *************************************************************************************************/


#if defined(MYPTHREAD)


#include <pthread.h>


#define PTKEYMAX       8


struct { void *ptr; pthread_key_t key; } _qdbm_ptkeys[PTKEYMAX];
int _qdbm_ptknum = 0;


static void *_qdbm_gettsd(void *ptr, int size, const void *initval);


void *_qdbm_settsd(void *ptr, int size, const void *initval){
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  char *val;
  if((val = _qdbm_gettsd(ptr, size, initval)) != NULL) return val;
  if(pthread_mutex_lock(&mutex) != 0) return NULL;
  if((val = _qdbm_gettsd(ptr, size, initval)) != NULL){
    pthread_mutex_unlock(&mutex);
    return val;
  }
  if(_qdbm_ptknum >= PTKEYMAX){
    pthread_mutex_unlock(&mutex);
    return NULL;
  }
  _qdbm_ptkeys[_qdbm_ptknum].ptr = ptr;
  if(pthread_key_create(&(_qdbm_ptkeys[_qdbm_ptknum].key), free) != 0){
    pthread_mutex_unlock(&mutex);
    return NULL;
  }
  if(!(val = malloc(size))){
    pthread_key_delete(_qdbm_ptkeys[_qdbm_ptknum].key);
    pthread_mutex_unlock(&mutex);
    return NULL;
  }
  memcpy(val, initval, size);
  if(pthread_setspecific(_qdbm_ptkeys[_qdbm_ptknum].key, val) != 0){
    free(val);
    pthread_key_delete(_qdbm_ptkeys[_qdbm_ptknum].key);
    pthread_mutex_unlock(&mutex);
    return NULL;
  }
  _qdbm_ptknum++;
  pthread_mutex_unlock(&mutex);
  return val;
}


static void *_qdbm_gettsd(void *ptr, int size, const void *initval){
  char *val;
  int i;
  for(i = 0; i < _qdbm_ptknum; i++){
    if(_qdbm_ptkeys[i].ptr == ptr){
      if(!(val = pthread_getspecific(_qdbm_ptkeys[i].key))){
        if(!(val = malloc(size))) return NULL;
        memcpy(val, initval, size);
        if(pthread_setspecific(_qdbm_ptkeys[i].key, val) != 0){
          free(val);
          return NULL;
        }
      }
      return val;
    }
  }
  return NULL;
}


#endif



/*************************************************************************************************
 * for systems without mmap
 *************************************************************************************************/


#if defined(_SYS_MSVC_) || defined(_SYS_MINGW_)


#define MMFDESCMAX     2048


struct { void *start; HANDLE handle; } mmhandles[MMFDESCMAX];
int mmhnum = 0;
CRITICAL_SECTION mmcsec;


static void _qdbm_delete_mmap_env(void);


void *_qdbm_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset){
  static volatile long first = TRUE;
  static volatile long ready = FALSE;
  HANDLE handle;
  int i;
  if(InterlockedExchange((void *)&first, FALSE)){
    InitializeCriticalSection(&mmcsec);
    atexit(_qdbm_delete_mmap_env);
    InterlockedExchange((void *)&ready, TRUE);
  }
  while(!InterlockedCompareExchange((void *)&ready, TRUE, TRUE)){
    Sleep(1);
  }
  if(fd < 0 || flags & MAP_FIXED) return MAP_FAILED;
  if(!(handle = CreateFileMapping((HANDLE)_get_osfhandle(fd), NULL,
                                  (prot & PROT_WRITE) ? PAGE_READWRITE : PAGE_READONLY,
                                  0, length, NULL))) return MAP_FAILED;
  if(!(start = MapViewOfFile(handle, (prot & PROT_WRITE) ? FILE_MAP_WRITE : FILE_MAP_READ,
                             0, 0, length))){
    CloseHandle(handle);
    return MAP_FAILED;
  }
  EnterCriticalSection(&mmcsec);
  if(mmhnum >= MMFDESCMAX - 1){
    UnmapViewOfFile(start);
    CloseHandle(handle);
    LeaveCriticalSection(&mmcsec);
    return MAP_FAILED;
  }
  for(i = 0; i < MMFDESCMAX; i++){
    if(!mmhandles[i].start){
      mmhandles[i].start = start;
      mmhandles[i].handle = handle;
      break;
    }
  }
  mmhnum++;
  LeaveCriticalSection(&mmcsec);
  return start;
}


int _qdbm_munmap(void *start, size_t length){
  HANDLE handle;
  int i;
  EnterCriticalSection(&mmcsec);
  handle = NULL;
  for(i = 0; i < MMFDESCMAX; i++){
    if(mmhandles[i].start == start){
      handle = mmhandles[i].handle;
      mmhandles[i].start = NULL;
      mmhandles[i].handle = NULL;
      break;
    }
  }
  if(!handle){
    LeaveCriticalSection(&mmcsec);
    return -1;
  }
  mmhnum--;
  LeaveCriticalSection(&mmcsec);
  if(!UnmapViewOfFile(start)){
    CloseHandle(handle);
    return -1;
  }
  if(!CloseHandle(handle)) return -1;
  return 0;
}


int _qdbm_msync(const void *start, size_t length, int flags){
  if(!FlushViewOfFile(start, length)) return -1;
  return 0;
}


static void _qdbm_delete_mmap_env(void){
  DeleteCriticalSection(&mmcsec);
}


#elif defined(_SYS_FREEBSD_) || defined(_SYS_NETBSD_) || defined(_SYS_OPENBSD_) || \
  defined(_SYS_AIX_) || defined(_SYS_RISCOS_) || defined(MYNOMMAP)


void *_qdbm_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset){
  char *buf, *wp;
  int rv, rlen;
  if(flags & MAP_FIXED) return MAP_FAILED;
  if(lseek(fd, SEEK_SET, offset) == -1) return MAP_FAILED;
  if(!(buf = malloc(sizeof(int) * 3 + length))) return MAP_FAILED;
  wp = buf;
  *(int *)wp = fd;
  wp += sizeof(int);
  *(int *)wp = offset;
  wp += sizeof(int);
  *(int *)wp = prot;
  wp += sizeof(int);
  rlen = 0;
  while((rv = read(fd, wp + rlen, length - rlen)) > 0){
    rlen += rv;
  }
  if(rv == -1 || rlen != length){
    free(buf);
    return MAP_FAILED;
  }
  return wp;
}


int _qdbm_munmap(void *start, size_t length){
  char *buf, *rp;
  int fd, offset, prot, rv, wlen;
  buf = (char *)start - sizeof(int) * 3;
  rp = buf;
  fd = *(int *)rp;
  rp += sizeof(int);
  offset = *(int *)rp;
  rp += sizeof(int);
  prot = *(int *)rp;
  rp += sizeof(int);
  if(prot & PROT_WRITE){
    if(lseek(fd, offset, SEEK_SET) == -1){
      free(buf);
      return -1;
    }
    wlen = 0;
    while(wlen < (int)length){
      rv = write(fd, rp + wlen, length - wlen);
      if(rv == -1){
        if(errno == EINTR) continue;
        free(buf);
        return -1;
      }
      wlen += rv;
    }
  }
  free(buf);
  return 0;
}


int _qdbm_msync(const void *start, size_t length, int flags){
  char *buf, *rp;
  int fd, offset, prot, rv, wlen;
  buf = (char *)start - sizeof(int) * 3;
  rp = buf;
  fd = *(int *)rp;
  rp += sizeof(int);
  offset = *(int *)rp;
  rp += sizeof(int);
  prot = *(int *)rp;
  rp += sizeof(int);
  if(prot & PROT_WRITE){
    if(lseek(fd, offset, SEEK_SET) == -1) return -1;
    wlen = 0;
    while(wlen < (int)length){
      rv = write(fd, rp + wlen, length - wlen);
      if(rv == -1){
        if(errno == EINTR) continue;
        return -1;
      }
      wlen += rv;
    }
  }
  return 0;
}


#endif



/*************************************************************************************************
 * for reentrant time routines
 *************************************************************************************************/


#if defined(_SYS_LINUX_) || defined(_SYS_FREEBSD_) || defined(_SYS_OPENBSD_) || \
  defined(_SYS_NETBSD_) || defined(_SYS_SUNOS_) || defined(_SYS_HPUX_) || \
  defined(_SYS_MACOSX_) || defined(_SYS_CYGWIN_)


struct tm *_qdbm_gmtime(const time_t *timep, struct tm *result){
  return gmtime_r(timep, result);
}


struct tm *_qdbm_localtime(const time_t *timep, struct tm *result){
  return localtime_r(timep, result);
}


# else


struct tm *_qdbm_gmtime(const time_t *timep, struct tm *result){
  return gmtime(timep);
}


struct tm *_qdbm_localtime(const time_t *timep, struct tm *result){
  return localtime(timep);
}


# endif



/*************************************************************************************************
 * for systems without times
 *************************************************************************************************/


#if defined(_SYS_MSVC_) || defined(_SYS_MINGW_)


clock_t _qdbm_times(struct tms *buf){
  buf->tms_utime = clock();
  buf->tms_stime = 0;
  buf->tms_cutime = 0;
  buf->tms_cstime = 0;
  return 0;
}


#endif



/*************************************************************************************************
 * for Win32
 *************************************************************************************************/


#if defined(_SYS_MSVC_) || defined(_SYS_MINGW_)


#define WINLOCKWAIT    100


int _qdbm_win32_fcntl(int fd, int cmd, struct flock *lock){
  HANDLE fh;
  DWORD opt;
  OVERLAPPED ol;
  fh = (HANDLE)_get_osfhandle(fd);
  opt = (cmd == F_SETLK) ? LOCKFILE_FAIL_IMMEDIATELY : 0;
  if(lock->l_type == F_WRLCK) opt |= LOCKFILE_EXCLUSIVE_LOCK;
  memset(&ol, 0, sizeof(OVERLAPPED));
  ol.Offset = INT_MAX;
  ol.OffsetHigh = 0;
  ol.hEvent = 0;
  if(!LockFileEx(fh, opt, 0, 1, 0, &ol)){
    if(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED){
      while(TRUE){
        if(LockFile(fh, 0, 0, 1, 0)) return 0;
        Sleep(WINLOCKWAIT);
      }
    }
    return -1;
  }
  return 0;
}


#endif


#if defined(_SYS_MSVC_)


DIR *_qdbm_win32_opendir(const char *name){
  char expr[8192];
  int len;
  DIR *dir;
  HANDLE fh;
  WIN32_FIND_DATA data;
  len = strlen(name);
  if(len > 0 && name[len-1] == MYPATHCHR){
    sprintf(expr, "%s*", name);
  } else {
    sprintf(expr, "%s%c*", name, MYPATHCHR);
  }
  if((fh = FindFirstFile(expr, &data)) == INVALID_HANDLE_VALUE) return NULL;
  if(!(dir = malloc(sizeof(DIR)))){
    FindClose(fh);
    return NULL;
  }
  dir->fh = fh;
  dir->data = data;
  dir->first = TRUE;
  return dir;
}


int _qdbm_win32_closedir(DIR *dir){
  if(!FindClose(dir->fh)){
    free(dir);
    return -1;
  }
  free(dir);
  return 0;
}


struct dirent *_qdbm_win32_readdir(DIR *dir){
  if(dir->first){
    sprintf(dir->de.d_name, "%s", dir->data.cFileName);
    dir->first = FALSE;
    return &(dir->de);
  }
  if(!FindNextFile(dir->fh, &(dir->data))) return NULL;
  sprintf(dir->de.d_name, "%s", dir->data.cFileName);
  return &(dir->de);
}


#endif



/*************************************************************************************************
 * for checking information of the system
 *************************************************************************************************/


#if defined(_SYS_LINUX_)


int _qdbm_vmemavail(size_t size){
  char buf[4096], *rp;
  int fd, rv, bsiz;
  double avail;
  if((fd = open("/proc/meminfo", O_RDONLY, 00644)) == -1) return TRUE;
  rv = TRUE;
  if((bsiz = read(fd, buf, sizeof(buf) - 1)) > 0){
    buf[bsiz] = '\0';
    avail = -1;
    if((rp = strstr(buf, "MemFree:")) != NULL){
      rp = strchr(rp, ':') + 1;
      avail = strtod(rp, NULL) * 1024.0;
      if((rp = strstr(buf, "SwapFree:")) != NULL){
        rp = strchr(rp, ':') + 1;
        avail += strtod(rp, NULL) * 1024.0;
      }
      if(size >= avail) rv = FALSE;
    }
  }
  close(fd);
  return rv;
}


#elif defined(_SYS_MSVC_) || defined(_SYS_MINGW_) || defined(_SYS_CYGWIN_)


int _qdbm_vmemavail(size_t size){
  MEMORYSTATUS sbuf;
  sbuf.dwLength = sizeof(MEMORYSTATUS);
  GlobalMemoryStatus(&sbuf);
  return size < sbuf.dwAvailVirtual;
}


#else


int _qdbm_vmemavail(size_t size){
  return TRUE;
}


#endif



/*************************************************************************************************
 * for ZLIB
 *************************************************************************************************/


#if defined(MYZLIB)


#include <zlib.h>

#define ZLIBBUFSIZ     8192


static char *_qdbm_deflate_impl(const char *ptr, int size, int *sp, int mode);
static char *_qdbm_inflate_impl(const char *ptr, int size, int *sp, int mode);
static unsigned int _qdbm_getcrc_impl(const char *ptr, int size);


char *(*_qdbm_deflate)(const char *, int, int *, int) = _qdbm_deflate_impl;
char *(*_qdbm_inflate)(const char *, int, int *, int) = _qdbm_inflate_impl;
unsigned int (*_qdbm_getcrc)(const char *, int) = _qdbm_getcrc_impl;


static char *_qdbm_deflate_impl(const char *ptr, int size, int *sp, int mode){
  z_stream zs;
  char *buf, *swap;
  unsigned char obuf[ZLIBBUFSIZ];
  int rv, asiz, bsiz, osiz;
  if(size < 0) size = strlen(ptr);
  zs.zalloc = Z_NULL;
  zs.zfree = Z_NULL;
  zs.opaque = Z_NULL;
  switch(mode){
  case _QDBM_ZMRAW:
    if(deflateInit2(&zs, 5, Z_DEFLATED, -15, 7, Z_DEFAULT_STRATEGY) != Z_OK)
      return NULL;
    break;
  case _QDBM_ZMGZIP:
    if(deflateInit2(&zs, 6, Z_DEFLATED, 15 + 16, 9, Z_DEFAULT_STRATEGY) != Z_OK)
      return NULL;
    break;
  default:
    if(deflateInit2(&zs, 6, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
      return NULL;
    break;
  }
  asiz = size + 16;
  if(asiz < ZLIBBUFSIZ) asiz = ZLIBBUFSIZ;
  if(!(buf = malloc(asiz))){
    deflateEnd(&zs);
    return NULL;
  }
  bsiz = 0;
  zs.next_in = (unsigned char *)ptr;
  zs.avail_in = size;
  zs.next_out = obuf;
  zs.avail_out = ZLIBBUFSIZ;
  while((rv = deflate(&zs, Z_FINISH)) == Z_OK){
    osiz = ZLIBBUFSIZ - zs.avail_out;
    if(bsiz + osiz > asiz){
      asiz = asiz * 2 + osiz;
      if(!(swap = realloc(buf, asiz))){
        free(buf);
        deflateEnd(&zs);
        return NULL;
      }
      buf = swap;
    }
    memcpy(buf + bsiz, obuf, osiz);
    bsiz += osiz;
    zs.next_out = obuf;
    zs.avail_out = ZLIBBUFSIZ;
  }
  if(rv != Z_STREAM_END){
    free(buf);
    deflateEnd(&zs);
    return NULL;
  }
  osiz = ZLIBBUFSIZ - zs.avail_out;
  if(bsiz + osiz + 1 > asiz){
    asiz = asiz * 2 + osiz;
    if(!(swap = realloc(buf, asiz))){
      free(buf);
      deflateEnd(&zs);
      return NULL;
    }
    buf = swap;
  }
  memcpy(buf + bsiz, obuf, osiz);
  bsiz += osiz;
  buf[bsiz] = '\0';
  if(mode == _QDBM_ZMRAW) bsiz++;
  *sp = bsiz;
  deflateEnd(&zs);
  return buf;
}


static char *_qdbm_inflate_impl(const char *ptr, int size, int *sp, int mode){
  z_stream zs;
  char *buf, *swap;
  unsigned char obuf[ZLIBBUFSIZ];
  int rv, asiz, bsiz, osiz;
  zs.zalloc = Z_NULL;
  zs.zfree = Z_NULL;
  zs.opaque = Z_NULL;
  switch(mode){
  case _QDBM_ZMRAW:
    if(inflateInit2(&zs, -15) != Z_OK) return NULL;
    break;
  case _QDBM_ZMGZIP:
    if(inflateInit2(&zs, 15 + 16) != Z_OK) return NULL;
    break;
  default:
    if(inflateInit2(&zs, 15) != Z_OK) return NULL;
    break;
  }
  asiz = size * 2 + 16;
  if(asiz < ZLIBBUFSIZ) asiz = ZLIBBUFSIZ;
  if(!(buf = malloc(asiz))){
    inflateEnd(&zs);
    return NULL;
  }
  bsiz = 0;
  zs.next_in = (unsigned char *)ptr;
  zs.avail_in = size;
  zs.next_out = obuf;
  zs.avail_out = ZLIBBUFSIZ;
  while((rv = inflate(&zs, Z_NO_FLUSH)) == Z_OK){
    osiz = ZLIBBUFSIZ - zs.avail_out;
    if(bsiz + osiz >= asiz){
      asiz = asiz * 2 + osiz;
      if(!(swap = realloc(buf, asiz))){
        free(buf);
        inflateEnd(&zs);
        return NULL;
      }
      buf = swap;
    }
    memcpy(buf + bsiz, obuf, osiz);
    bsiz += osiz;
    zs.next_out = obuf;
    zs.avail_out = ZLIBBUFSIZ;
  }
  if(rv != Z_STREAM_END){
    free(buf);
    inflateEnd(&zs);
    return NULL;
  }
  osiz = ZLIBBUFSIZ - zs.avail_out;
  if(bsiz + osiz >= asiz){
    asiz = asiz * 2 + osiz;
    if(!(swap = realloc(buf, asiz))){
      free(buf);
      inflateEnd(&zs);
      return NULL;
    }
    buf = swap;
  }
  memcpy(buf + bsiz, obuf, osiz);
  bsiz += osiz;
  buf[bsiz] = '\0';
  if(sp) *sp = bsiz;
  inflateEnd(&zs);
  return buf;
}


static unsigned int _qdbm_getcrc_impl(const char *ptr, int size){
  int crc;
  if(size < 0) size = strlen(ptr);
  crc = crc32(0, Z_NULL, 0);
  return crc32(crc, (unsigned char *)ptr, size);
}


#else


char *(*_qdbm_deflate)(const char *, int, int *, int) = NULL;
char *(*_qdbm_inflate)(const char *, int, int *, int) = NULL;
unsigned int (*_qdbm_getcrc)(const char *, int) = NULL;


#endif



/*************************************************************************************************
 * for LZO
 *************************************************************************************************/


#if defined(MYLZO)


#include <lzo/lzo1x.h>


static char *_qdbm_lzoencode_impl(const char *ptr, int size, int *sp);
static char *_qdbm_lzodecode_impl(const char *ptr, int size, int *sp);


int _qdbm_lzo_init = FALSE;
char *(*_qdbm_lzoencode)(const char *, int, int *) = _qdbm_lzoencode_impl;
char *(*_qdbm_lzodecode)(const char *, int, int *) = _qdbm_lzodecode_impl;


static char *_qdbm_lzoencode_impl(const char *ptr, int size, int *sp){
  char wrkmem[LZO1X_1_MEM_COMPRESS];
  lzo_bytep buf;
  lzo_uint bsiz;
  if(!_qdbm_lzo_init){
    if(lzo_init() != LZO_E_OK) return NULL;
    _qdbm_lzo_init = TRUE;
  }
  if(size < 0) size = strlen(ptr);
  if(!(buf = malloc(size + size / 16 + 80))) return NULL;
  if(lzo1x_1_compress((lzo_bytep)ptr, size, buf, &bsiz, wrkmem) != LZO_E_OK){
    free(buf);
    return NULL;
  }
  buf[bsiz] = '\0';
  *sp = bsiz;
  return (char *)buf;
}


static char *_qdbm_lzodecode_impl(const char *ptr, int size, int *sp){
  lzo_bytep buf;
  lzo_uint bsiz;
  int rat, rv;
  if(!_qdbm_lzo_init){
    if(lzo_init() != LZO_E_OK) return NULL;
    _qdbm_lzo_init = TRUE;
  }
  rat = 6;
  while(TRUE){
    bsiz = (size + 256) * rat + 3;
    if(!(buf = malloc(bsiz + 1))) return NULL;
    rv = lzo1x_decompress_safe((lzo_bytep)(ptr), size, buf, &bsiz, NULL);
    if(rv == LZO_E_OK){
      break;
    } else if(rv == LZO_E_OUTPUT_OVERRUN){
      free(buf);
      rat *= 2;
    } else {
      free(buf);
      return NULL;
    }
  }
  buf[bsiz] = '\0';
  if(sp) *sp = bsiz;
  return (char *)buf;
}


#else


char *(*_qdbm_lzoencode)(const char *, int, int *) = NULL;
char *(*_qdbm_lzodecode)(const char *, int, int *) = NULL;


#endif



/*************************************************************************************************
 * for BZIP2
 *************************************************************************************************/


#if defined(MYBZIP)


#include <bzlib.h>

#define BZIPBUFSIZ     8192


static char *_qdbm_bzencode_impl(const char *ptr, int size, int *sp);
static char *_qdbm_bzdecode_impl(const char *ptr, int size, int *sp);


char *(*_qdbm_bzencode)(const char *, int, int *) = _qdbm_bzencode_impl;
char *(*_qdbm_bzdecode)(const char *, int, int *) = _qdbm_bzdecode_impl;


static char *_qdbm_bzencode_impl(const char *ptr, int size, int *sp){
  bz_stream zs;
  char *buf, *swap, obuf[BZIPBUFSIZ];
  int rv, asiz, bsiz, osiz;
  if(size < 0) size = strlen(ptr);
  zs.bzalloc = NULL;
  zs.bzfree = NULL;
  zs.opaque = NULL;
  if(BZ2_bzCompressInit(&zs, 9, 0, 30) != BZ_OK) return NULL;
  asiz = size + 16;
  if(asiz < BZIPBUFSIZ) asiz = BZIPBUFSIZ;
  if(!(buf = malloc(asiz))){
    BZ2_bzCompressEnd(&zs);
    return NULL;
  }
  bsiz = 0;
  zs.next_in = (char *)ptr;
  zs.avail_in = size;
  zs.next_out = obuf;
  zs.avail_out = BZIPBUFSIZ;
  while((rv = BZ2_bzCompress(&zs, BZ_FINISH)) == BZ_FINISH_OK){
    osiz = BZIPBUFSIZ - zs.avail_out;
    if(bsiz + osiz > asiz){
      asiz = asiz * 2 + osiz;
      if(!(swap = realloc(buf, asiz))){
        free(buf);
        BZ2_bzCompressEnd(&zs);
        return NULL;
      }
      buf = swap;
    }
    memcpy(buf + bsiz, obuf, osiz);
    bsiz += osiz;
    zs.next_out = obuf;
    zs.avail_out = BZIPBUFSIZ;
  }
  if(rv != BZ_STREAM_END){
    free(buf);
    BZ2_bzCompressEnd(&zs);
    return NULL;
  }
  osiz = BZIPBUFSIZ - zs.avail_out;
  if(bsiz + osiz + 1 > asiz){
    asiz = asiz * 2 + osiz;
    if(!(swap = realloc(buf, asiz))){
      free(buf);
      BZ2_bzCompressEnd(&zs);
      return NULL;
    }
    buf = swap;
  }
  memcpy(buf + bsiz, obuf, osiz);
  bsiz += osiz;
  buf[bsiz] = '\0';
  *sp = bsiz;
  BZ2_bzCompressEnd(&zs);
  return buf;
}


static char *_qdbm_bzdecode_impl(const char *ptr, int size, int *sp){
  bz_stream zs;
  char *buf, *swap, obuf[BZIPBUFSIZ];
  int rv, asiz, bsiz, osiz;
  zs.bzalloc = NULL;
  zs.bzfree = NULL;
  zs.opaque = NULL;
  if(BZ2_bzDecompressInit(&zs, 0, 0) != BZ_OK) return NULL;
  asiz = size * 2 + 16;
  if(asiz < BZIPBUFSIZ) asiz = BZIPBUFSIZ;
  if(!(buf = malloc(asiz))){
    BZ2_bzDecompressEnd(&zs);
    return NULL;
  }
  bsiz = 0;
  zs.next_in = (char *)ptr;
  zs.avail_in = size;
  zs.next_out = obuf;
  zs.avail_out = BZIPBUFSIZ;
  while((rv = BZ2_bzDecompress(&zs)) == BZ_OK){
    osiz = BZIPBUFSIZ - zs.avail_out;
    if(bsiz + osiz >= asiz){
      asiz = asiz * 2 + osiz;
      if(!(swap = realloc(buf, asiz))){
        free(buf);
        BZ2_bzDecompressEnd(&zs);
        return NULL;
      }
      buf = swap;
    }
    memcpy(buf + bsiz, obuf, osiz);
    bsiz += osiz;
    zs.next_out = obuf;
    zs.avail_out = BZIPBUFSIZ;
  }
  if(rv != BZ_STREAM_END){
    free(buf);
    BZ2_bzDecompressEnd(&zs);
    return NULL;
  }
  osiz = BZIPBUFSIZ - zs.avail_out;
  if(bsiz + osiz >= asiz){
    asiz = asiz * 2 + osiz;
    if(!(swap = realloc(buf, asiz))){
      free(buf);
      BZ2_bzDecompressEnd(&zs);
      return NULL;
    }
    buf = swap;
  }
  memcpy(buf + bsiz, obuf, osiz);
  bsiz += osiz;
  buf[bsiz] = '\0';
  if(sp) *sp = bsiz;
  BZ2_bzDecompressEnd(&zs);
  return buf;
}


#else


char *(*_qdbm_bzencode)(const char *, int, int *) = NULL;
char *(*_qdbm_bzdecode)(const char *, int, int *) = NULL;


#endif



/*************************************************************************************************
 * for ICONV
 *************************************************************************************************/


#if defined(MYICONV)


#include <iconv.h>

#define ICONVCHECKSIZ  32768
#define ICONVMISSMAX   256
#define ICONVALLWRAT   0.001


static char *_qdbm_iconv_impl(const char *ptr, int size,
                              const char *icode, const char *ocode, int *sp, int *mp);
static const char *_qdbm_encname_impl(const char *ptr, int size);
static int _qdbm_encmiss(const char *ptr, int size, const char *icode, const char *ocode);


char *(*_qdbm_iconv)(const char *, int, const char *, const char *,
                     int *, int *) = _qdbm_iconv_impl;
const char *(*_qdbm_encname)(const char *, int) = _qdbm_encname_impl;


static char *_qdbm_iconv_impl(const char *ptr, int size,
                              const char *icode, const char *ocode, int *sp, int *mp){
  iconv_t ic;
  char *obuf, *wp, *rp;
  size_t isiz, osiz;
  int miss;
  if(size < 0) size = strlen(ptr);
  isiz = size;
  if((ic = iconv_open(ocode, icode)) == (iconv_t)-1) return NULL;
  osiz = isiz * 5;
  if(!(obuf = malloc(osiz + 1))){
    iconv_close(ic);
    return NULL;
  }
  wp = obuf;
  rp = (char *)ptr;
  miss = 0;
  while(isiz > 0){
    if(iconv(ic, (void *)&rp, &isiz, &wp, &osiz) == -1){
      if(errno == EILSEQ && (*rp == 0x5c || *rp == 0x7e)){
        *wp = *rp;
        wp++;
        rp++;
        isiz--;
      } else if(errno == EILSEQ || errno == EINVAL){
        rp++;
        isiz--;
        miss++;
      } else {
        break;
      }
    }
  }
  *wp = '\0';
  if(iconv_close(ic) == -1){
    free(obuf);
    return NULL;
  }
  if(sp) *sp = wp - obuf;
  if(mp) *mp = miss;
  return obuf;
}


static const char *_qdbm_encname_impl(const char *ptr, int size){
  const char *hypo;
  int i, miss, cr;
  if(size < 0) size = strlen(ptr);
  if(size > ICONVCHECKSIZ) size = ICONVCHECKSIZ;
  if(size >= 2 && (!memcmp(ptr, "\xfe\xff", 2) || !memcmp(ptr, "\xff\xfe", 2))) return "UTF-16";
  for(i = 0; i < size - 1; i += 2){
    if(ptr[i] == 0 && ptr[i+1] != 0) return "UTF-16BE";
    if(ptr[i+1] == 0 && ptr[i] != 0) return "UTF-16LE";
  }
  for(i = 0; i < size - 3; i++){
    if(ptr[i] == 0x1b){
      i++;
      if(ptr[i] == '(' && strchr("BJHI", ptr[i+1])) return "ISO-2022-JP";
      if(ptr[i] == '$' && strchr("@B(", ptr[i+1])) return "ISO-2022-JP";
    }
  }
  if(_qdbm_encmiss(ptr, size, "US-ASCII", "UTF-16BE") < 1) return "US-ASCII";
  if(_qdbm_encmiss(ptr, size, "UTF-8", "UTF-16BE") < 1) return "UTF-8";
  hypo = NULL;
  cr = FALSE;
  for(i = 0; i < size; i++){
    if(ptr[i] == 0xd){
      cr = TRUE;
      break;
    }
  }
  if(cr){
    if((miss = _qdbm_encmiss(ptr, size, "Shift_JIS", "EUC-JP")) < 1) return "Shift_JIS";
    if(!hypo && miss / (double)size <= ICONVALLWRAT) hypo = "Shift_JIS";
    if((miss = _qdbm_encmiss(ptr, size, "EUC-JP", "UTF-16BE")) < 1) return "EUC-JP";
    if(!hypo && miss / (double)size <= ICONVALLWRAT) hypo = "EUC-JP";
  } else {
    if((miss = _qdbm_encmiss(ptr, size, "EUC-JP", "UTF-16BE")) < 1) return "EUC-JP";
    if(!hypo && miss / (double)size <= ICONVALLWRAT) hypo = "EUC-JP";
    if((miss = _qdbm_encmiss(ptr, size, "Shift_JIS", "EUC-JP")) < 1) return "Shift_JIS";
    if(!hypo && miss / (double)size <= ICONVALLWRAT) hypo = "Shift_JIS";
  }
  if((miss = _qdbm_encmiss(ptr, size, "UTF-8", "UTF-16BE")) < 1) return "UTF-8";
  if(!hypo && miss / (double)size <= ICONVALLWRAT) hypo = "UTF-8";
  if((miss = _qdbm_encmiss(ptr, size, "CP932", "UTF-16BE")) < 1) return "CP932";
  if(!hypo && miss / (double)size <= ICONVALLWRAT) hypo = "CP932";
  return hypo ? hypo : "ISO-8859-1";
}


static int _qdbm_encmiss(const char *ptr, int size, const char *icode, const char *ocode){
  iconv_t ic;
  char obuf[ICONVCHECKSIZ], *wp, *rp;
  size_t isiz, osiz;
  int miss;
  isiz = size;
  if((ic = iconv_open(ocode, icode)) == (iconv_t)-1) return ICONVMISSMAX;
  miss = 0;
  rp = (char *)ptr;
  while(isiz > 0){
    osiz = ICONVCHECKSIZ;
    wp = obuf;
    if(iconv(ic, (void *)&rp, &isiz, &wp, &osiz) == -1){
      if(errno == EILSEQ || errno == EINVAL){
        rp++;
        isiz--;
        miss++;
        if(miss >= ICONVMISSMAX) break;
      } else {
        break;
      }
    }
  }
  if(iconv_close(ic) == -1) return ICONVMISSMAX;
  return miss;
}


#else


char *(*_qdbm_iconv)(const char *, int, const char *, const char *, int *, int *) = NULL;
const char *(*_qdbm_encname)(const char *, int) = NULL;


#endif



/*************************************************************************************************
 * common settings
 *************************************************************************************************/


int _qdbm_dummyfunc(void){
  return 0;
}



/* END OF FILE */
