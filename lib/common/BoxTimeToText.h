// --------------------------------------------------------------------------
//
// File
//		Name:    BoxTimeToText.h
//		Purpose: Convert box time to text
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------

#ifndef BOXTIMETOTEXT__H
#define BOXTIMETOTEXT__H

#include <string>
#include "BoxTime.h"

std::string BoxTimeToISO8601String(box_time_t Time, bool localTime);

#endif // BOXTIMETOTEXT__H

