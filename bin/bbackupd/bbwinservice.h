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

#ifndef BBWINSERVICE__H
#define BBWINSERVICE__H

void removeService(void);
void installService(void);
void ourService(void);

#endif
