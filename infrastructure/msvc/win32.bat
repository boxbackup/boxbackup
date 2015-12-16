@echo off

echo quick and dirty to get up and running by generating the required files 
echo using Cygwin and Perl

cd ..\..

copy .\infrastructure\BoxPlatform.pm.in .\infrastructure\BoxPlatform.pm
copy .\lib\common\BoxPortsAndFiles.h.in .\lib\common\BoxPortsAndFiles.h
copy .\lib\common\BoxConfig-MSVC.h .\lib\common\BoxConfig.h

cd .\bin\bbackupquery\ & perl ./../../bin/bbackupquery/makedocumentation.pl.in
cd ..\..\

cd .\lib\backupstore & perl ./../../lib/common/makeexception.pl.in BackupStoreException.txt & perl ./../../lib/server/makeprotocol.pl.in backupprotocol.txt
cd ..\..\

cd .\lib\compress & perl ./../../lib/common/makeexception.pl.in CompressException.txt
cd ..\..\

cd .\lib\common & perl ./../../lib/common/makeexception.pl.in CommonException.txt & perl ./../../lib/common/makeexception.pl.in ConversionException.txt
cd ..\..\

cd .\lib\raidfile & perl ./../../lib/common/makeexception.pl.in RaidFileException.txt
cd ..\..\

cd .\lib\backupclient & perl ./../../lib/common/makeexception.pl.in ClientException.txt
cd ..\..\

cd .\lib\crypto & perl ./../../lib/common/makeexception.pl.in CipherException.txt
cd ..\..\

cd .\lib\httpserver & perl ./../../lib/common/makeexception.pl.in HTTPException.txt
cd ..\..\

echo server parts - which appears as though some of the clients rely on

cd .\lib\server & perl ./../../lib/common/makeexception.pl.in ServerException.txt & perl ./../../lib/common/makeexception.pl.in ConnectionException.txt
cd ..\..\

perl -pe 's/@PERL@/perl/' ./test/bbackupd/testfiles/bbackupd.conf.in > .\test\bbackupd\testfiles\bbackupd.conf

echo Generating InstallJammer configuration file
perl infrastructure/msvc/fake-config.sub.pl ./contrib/windows/installer/boxbackup.mpi.in > ./contrib/windows/installer/boxbackup.mpi
