#ifndef _EMU_WINVER_H
#define _EMU_WINVER_H

// set up to include the necessary parts of Windows headers

#define WIN32_LEAN_AND_MEAN

#ifndef __MSVCRT_VERSION__
#define __MSVCRT_VERSION__ 0x0601
#endif

// We need WINVER at least 0x0500 to use GetFileSizeEx on Cygwin/MinGW,
// and 0x0501 for FindFirstFile(W) for opendir/readdir.
//
// WIN32_WINNT versions 0x0600 (Vista) and higher enable WSAPoll() in
// winsock2.h, whose struct pollfd conflicts with ours below, so for
// now we just set it lower than that, to Windows XP (0x0501).

#ifdef WINVER
#	if WINVER != 0x0501
// provoke a redefinition warning to track down the offender
#		define WINVER 0x0501
#		error Must include emu.h before setting WINVER
#	endif
#endif
#define WINVER 0x0501

#ifdef _WIN32_WINNT
#	if _WIN32_WINNT != 0x0501
// provoke a redefinition warning to track down the offender
#		define _WIN32_WINNT 0x0501
#		error Must include emu.h before setting _WIN32_WINNT
#	endif
#endif
#define _WIN32_WINNT 0x0501

#endif // _EMU_WINVER_H
