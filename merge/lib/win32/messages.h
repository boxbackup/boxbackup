 // Message source file, to be compiled to a resource file with
 // Microsoft Message Compiler (MC), to an object file with a Resource
 // Compiler, and linked into the application.

 // The main reason for this file is to work around Windows' stupid
 // messages in the Event Log, which say:

 // The description for Event ID ( 4 ) in Source ( Box Backup (bbackupd) ) 
 // cannot be found. The local computer may not have the necessary 
 // registry information or message DLL files to display messages from a 
 // remote computer. The following information is part of the event:
 // Message definitions follow
//
//  Values are 32 bit values layed out as follows:
//
//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
//  +---+-+-+-----------------------+-------------------------------+
//  |Sev|C|R|     Facility          |               Code            |
//  +---+-+-+-----------------------+-------------------------------+
//
//  where
//
//      Sev - is the severity code
//
//          00 - Success
//          01 - Informational
//          10 - Warning
//          11 - Error
//
//      C - is the Customer code flag
//
//      R - is a reserved bit
//
//      Facility - is the facility code
//
//      Code - is the facility's status code
//
//
// Define the facility codes
//


//
// Define the severity codes
//


//
// MessageId: MSG_ERR
//
// MessageText:
//
//  %1
//
#define MSG_ERR                          ((DWORD)0x40000001L)

