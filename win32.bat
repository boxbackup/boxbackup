@echo off

echo quick and dirty to get up and running by generating the required files 
echo using Cygwin and Perl

copy .\infrastructure\BoxPlatform.pm.in .\infrastructure\BoxPlatform.pm

cd .\bin\bbackupquery\ & perl ./../../bin/bbackupquery/makedocumentation.pl.in
cd ..\..\

cd .\lib\backupclient & perl ./../../lib/common/makeexception.pl.in BackupStoreException.txt & perl ./../../lib/server/makeprotocol.pl.in Client ./../../bin/bbstored/backupprotocol.txt
cd ..\..\

cd .\lib\compress & perl ./../../lib/common/makeexception.pl.in CompressException.txt
cd ..\..\

cd .\lib\common & perl ./../../lib/common/makeexception.pl.in CommonException.txt & perl ./../../lib/common/makeexception.pl.in ConversionException.txt

cd ..\..\

cd .\bin\bbackupd & perl ./../../lib/common/makeexception.pl.in ClientException.txt

cd ..\..\

cd .\lib\crypto & perl ./../../lib/common/makeexception.pl.in CipherException.txt
cd ..\..\

echo server parts - which appears as though some of the clients rely on

cd .\lib\server & perl ./../../lib/common/makeexception.pl.in ServerException.txt & perl ./../../lib/common/makeexception.pl.in ConnectionException.txt
cd ..\..\

perl -pe 's/@PERL@/perl/' ./test/bbackupd/testfiles/bbackupd.conf.in > .\test\bbackupd\testfiles\bbackupd.conf
