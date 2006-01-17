// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
// --------------------------------------------------------------------------
//
// File
//		Name:    BoxTime.h
//		Purpose: How time is represented
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#ifndef BOXTIME__H
#define BOXTIME__H

// Time is presented as an unsigned 64 bit integer, in microseconds
typedef uint64_t	box_time_t;

#define NANO_SEC_IN_SEC		(1000000000LL)
#define NANO_SEC_IN_USEC 	(1000)
#define NANO_SEC_IN_USEC_LL (1000LL)
#define MICRO_SEC_IN_SEC 	(1000000)
#define MICRO_SEC_IN_SEC_LL	(1000000LL)
#define MILLI_SEC_IN_NANO_SEC		(1000)
#define MILLI_SEC_IN_NANO_SEC_LL	(1000LL)

box_time_t GetCurrentBoxTime();

inline box_time_t SecondsToBoxTime(uint32_t Seconds)
{
	return ((box_time_t)Seconds * MICRO_SEC_IN_SEC_LL);
}
inline box_time_t SecondsToBoxTime(uint64_t Seconds)
{
	return ((box_time_t)Seconds * MICRO_SEC_IN_SEC_LL);
}
inline int64_t BoxTimeToSeconds(box_time_t Time)
{
	return Time / MICRO_SEC_IN_SEC_LL;
}
inline int64_t BoxTimeToMilliSeconds(box_time_t Time)
{
	return Time / MILLI_SEC_IN_NANO_SEC_LL;
}

#endif // BOXTIME__H

