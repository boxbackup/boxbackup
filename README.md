# Box Backup

[![Travis Build Status](https://travis-ci.org/boxbackup/boxbackup.svg?branch=master)](https://travis-ci.org/boxbackup/boxbackup)
[![Appveyor Build Status](https://ci.appveyor.com/api/projects/status/ussek6c8mvgxqj2k/branch/master?svg=true)](https://ci.appveyor.com/project/qris/boxbackup/branch/master)

Box Backup is an open source, completely automatic, secure, encrypted on-line backup system.

It has the following key features:

* The client software (included) runs on the computers to be backed up. Linux, Windows, MacOS and other Unixes are supported.

* The server software (also included) runs on a Unix server (Windows is highly not recommended), usually in a datacentre. (A client which does not require special server software is under development.)

* The clients usually run a backup daemon (background process) which detects changes to files, and encrypts and copies changes to the server, so backups are continuous and up-to-date (although traditional ''snapshot'' backups are possible too).

* All backed up data is stored on the server in files on a filesystem - no tape, archive or other special devices are required.

* The server is trusted only to make files available when they are required - all data is encrypted and can be decoded only by the original client. This makes it ideal for backing up over an untrusted network (such as the Internet), or where the server is in an uncontrolled environment.

* Only changes within files are sent to the server, just like rsync, minimising the bandwidth used between clients and server. This makes it particularly suitable for backing up between distant locations, or over the Internet.

* It behaves like tape - old file versions and deleted files are available.

* Old versions of files on the server are stored as changes from the current version, minimising the storage space required on the server. Files are the server are also compressed to minimise their size.

* Choice of backup behaviour - it can be optimised for document or server backup.

* It is designed to be easy and cheap to run a server. It has a portable implementation, and optional RAID implemented in userland for reliability without complex server setup or expensive hardware.

Please see the [website](https://www.boxbackup.org) for more information, including installation instructions.

Box Backup is distributed under a [mixed BSD/GPL license](https://github.com/boxbackup/boxbackup/blob/master/LICENSE.txt).
