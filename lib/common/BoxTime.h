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

