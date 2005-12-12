
pwd = /usr/src/boxbackup0.08/

# quick and dirty to get up and running by generating the required files 
# using Cygwin and Perl

cd ./bin/bbackupquery/ & perl ./../../bin/bbackupquery/makedocumentation.pl
cd ../../

cd ./lib/backupclient & perl ./../../lib/common/makeexception.pl BackupStoreException.txt
perl ./../../lib/server/makeprotocol.pl Client ./../../bin/bbstored/backupprotocol.txt
cd ../../

cd ./lib/compress & perl ./../../lib/common/makeexception.pl CompressException.txt
cd ../../

cd ./lib/common & perl ./../../lib/common/makeexception.pl CommonException.txt & perl ./../../lib/common/makeexception.pl ConversionException.txt

cd ../../

cd ./lib/crypto & perl ./../../lib/common/makeexception.pl CipherException.txt
cd ../../

# server parts - which appears as though some of the clients rely on

cd ./lib/server & perl ./../../lib/common/makeexception.pl ServerException.txt & perl ./../../lib/common/makeexception.pl ConnectionException.txt
cd ../../
