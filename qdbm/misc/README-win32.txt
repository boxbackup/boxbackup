================================================================
 QDBM: Quick Database Manager
 Copyright (C) 2000-2007 Mikio Hirabayashi
================================================================

This is a package of Win32 binaries of QDBM.  It contains C/Java
APIs, their utility commands, and CGI scripts.

See http://qdbm.sourceforge.net/ for more information.


The following are documents of specifications.

  spex.html       : fundamental specifications
  spex-ja.html    : fundamental specifications in Japanese
  jspex.html      : specifications of Java API
  jspex-ja.html   : specifications of Java API in Japanese
  japidoc/        : documents of Java API


The following are header files of C language.
Include them at source codes of your applications.

  depot.h
  curia.h
  relic.h
  hovel.h
  cabin.h
  villa.h
  vista.h
  odeum.h


The following are dynamic linking libraries for the API of C.
Copy them to the system directory or a directory of your project.

  qdbm.dll        : QDBM itself
  libqdbm.dll.a   : import library for `qdbm.dll'
  mgwz.dll        : ZLIB
  libiconv-2.dll  : ICONV


The following is a dynamic linking library for the API of Java.
Copy it to the system directory or a directory of your project.

  jqdbm.dll


The following is a Java archive of the classes.
Include it in the CLASSPATH of your environment.

  qdbm.jar


The following are utility commands for testing and debugging.

  dpmgr.exe
  dptest.exe
  dptsv.exe
  crmgr.exe
  crtest.exe
  crtsv.exe
  rlmgr.exe
  rltest.exe
  hvmgr.exe
  hvtest.exe
  cbtest.exe
  cbcodec.exe
  vlmgr.exe
  vltest.exe
  vltsv.exe
  odmgr.exe
  odtest.exe
  odidx.exe
  qmttest.exe


The sub directory `cgi' contains CGI scripts, their configuration
files, and their specifications.

If you want an import library or a static library for Visual C++,
please obtain the source package and use VCmakefile in it.


QDBM was released under the terms of the GNU Lesser General Public
License.  See the file `COPYING.txt' for details.

QDBM was written by Mikio Hirabayashi. You can contact the author
by e-mail to `mikio@users.sourceforge.net'.  However, as for
topics which can be shared among other users, pleae send it to
the mailing list. To join the mailing list, refer to the following
URL.

  http://lists.sourceforge.net/lists/listinfo/qdbm-users


Thanks.



== END OF FILE ==
