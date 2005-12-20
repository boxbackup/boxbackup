// --------------------------------------------------------------------------
//
// File
//		Name:    BoxTime.cpp
//		Purpose: Time for the box
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <time.h>

#include "BoxTime.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    GetCurrentBoxTime()
//		Purpose: Returns the current time as a box time. (1 sec precision)
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
box_time_t GetCurrentBoxTime()
{
	return SecondsToBoxTime(time(0));
}


