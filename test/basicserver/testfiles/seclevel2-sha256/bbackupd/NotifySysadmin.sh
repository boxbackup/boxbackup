#!/bin/sh

# This script is run whenever bbackupd changes state or encounters a
# problem which requires the system administrator to assist:
#
# 1) The store is full, and no more data can be uploaded.
# 2) Some files or directories were not readable.
# 3) A backup run starts or finishes.
#
# The default script emails the system administrator, except for backups
# starting and stopping, where it does nothing.

SUBJECT="BACKUP PROBLEM on host debian-unstable"
SENDTO="chris"

if [ "$1" = "" ]; then
	echo "Usage: $0 <store-full|read-error|backup-ok|backup-error|backup-start|backup-finish>" >&2
	exit 2
elif [ "$1" = store-full ]; then
	sendmail: $SENDTO <<EOM
Subject: $SUBJECT (store full)
To: $SENDTO


The store account for debian-unstable is full.

=============================
FILES ARE NOT BEING BACKED UP
=============================

Please adjust the limits on account 1234567 on server localhost.

EOM
elif [ "$1" = read-error ]; then
sendmail: $SENDTO <<EOM
Subject: $SUBJECT (read errors)
To: $SENDTO


Errors occured reading some files or directories for backup on debian-unstable.

===================================
THESE FILES ARE NOT BEING BACKED UP
===================================

Check the logs on debian-unstable for the files and directories which caused
these errors, and take appropriate action.

Other files are being backed up.

EOM
elif [ "$1" = backup-start -o "$1" = backup-finish -o "$1" = backup-ok ]; then
	# do nothing by default
	true
else
sendmail: $SENDTO <<EOM
Subject: $SUBJECT (unknown)
To: $SENDTO


The backup daemon on debian-unstable reported an unknown error ($1).

==========================
FILES MAY NOT BE BACKED UP
==========================

Please check the logs on debian-unstable.

EOM
fi
