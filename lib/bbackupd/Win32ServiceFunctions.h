//***************************************************************
// From the book "Win32 System Services: The Heart of Windows 98
// and Windows 2000"
// by Marshall Brain
// Published by Prentice Hall
// Copyright 1995 Prentice Hall.
//
// This code implements the Windows API Service interface
// for the Box Backup for Windows native port.
//***************************************************************

#ifndef WIN32SERVICEFUNCTIONS_H
#define WIN32SERVICEFUNCTIONS_H

int RemoveService  (const std::string& rServiceName);
int InstallService (const char* pConfigFilePath, const std::string& rServiceName);
int OurService     (const char* pConfigFileName);

#endif
