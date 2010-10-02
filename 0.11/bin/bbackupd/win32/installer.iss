; Script to generate output file for Box Backup client for the Windows Platform
;
; Very important - this is the release process
;
; 1/ Upgrade BOX_VERSION in the file emu.h to the current version for example 0.09eWin32 - then perform a full rebuild
;
; 2/ Upgrade the AppVerName below to reflect the version
;
; 3/ Generate the output file, then rename it to the relevent filename to reflect the version

[Setup]
AppName=Box Backup
AppVerName=BoxWin32 0.09h
AppPublisher=Fluffy & Omniis
AppPublisherURL=http://www.omniis.com
AppSupportURL=http://www.omniis.com
AppUpdatesURL=http://www.omniis.com
DefaultDirName={pf}\Box Backup
DefaultGroupName=Box Backup
Compression=lzma
SolidCompression=yes
PrivilegesRequired=admin

[Files]
Source: "..\..\Release\bbackupd.exe"; DestDir: "{app}"; Flags: ignoreversion restartreplace
Source: "..\..\Release\bbackupctl.exe"; DestDir: "{app}"; Flags: ignoreversion restartreplace
Source: "..\..\Release\bbackupquery.exe"; DestDir: "{app}"; Flags: ignoreversion restartreplace
Source: "..\..\ExceptionCodes.txt"; DestDir: "{app}"; Flags: ignoreversion restartreplace
Source: "icon.ico"; DestDir: "{app}\"; Flags: ignoreversion restartreplace
Source: "msvcr71.dll"; DestDir: "{app}\"; Flags: restartreplace
Source: "bbackupd.conf"; DestDir: "{app}"; Flags: confirmoverwrite
Source: "..\..\..\zlib\zlib1.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace
Source: "..\..\..\openssl\bin\libeay32.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace
Source: "..\..\..\openssl\bin\ssleay32.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace
Source: "ReadMe.txt"; DestDir: "{app}"; Flags: ignoreversion restartreplace

; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Icons]
Name: "{group}\Box Backup Query"; Filename: "{app}\bbackupquery.exe"; IconFilename: "{app}\icon.ico" ;Parameters: "-c bbackupd.conf"; WorkingDir: "{app}"
Name: "{group}\Service\Install Service"; Filename: "{app}\bbackupd.exe"; IconFilename: "{app}\icon.ico" ;Parameters: "-i"; WorkingDir: "{app}"
Name: "{group}\Service\Remove Service"; Filename: "{app}\bbackupd.exe"; IconFilename: "{app}\icon.ico" ;Parameters: "-r"; WorkingDir: "{app}"
Name: "{group}\Initiate Backup Now"; Filename: "{app}\bbackupctl.exe"; IconFilename: "{app}\icon.ico" ;Parameters: "-c bbackupd.conf sync"; WorkingDir: "{app}"

[Dirs]
Name: "{app}\bbackupd"

[Run]
Filename: "{app}\bbackupd.exe"; Description: "Install Boxbackup as service"; Parameters: "-i"; Flags: postinstall
Filename: "{app}\Readme.txt"; Description: "View upgrade notes"; Flags: postinstall shellexec skipifsilent

