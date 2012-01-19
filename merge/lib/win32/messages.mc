; // Message source file, to be compiled to a resource file with
; // Microsoft Message Compiler (MC), to an object file with a Resource
; // Compiler, and linked into the application.
;
; // The main reason for this file is to work around Windows' stupid
; // messages in the Event Log, which say:
;
; // The description for Event ID ( 4 ) in Source ( Box Backup (bbackupd) ) 
; // cannot be found. The local computer may not have the necessary 
; // registry information or message DLL files to display messages from a 
; // remote computer. The following information is part of the event:

MessageIdTypedef = DWORD

; // Message definitions follow

MessageId = 0x1
Severity = Informational
SymbolicName = MSG_ERR
Language = English
%1
.
