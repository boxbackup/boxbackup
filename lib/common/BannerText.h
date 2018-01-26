// --------------------------------------------------------------------------
//
// File
//		Name:    BannerText.h
//		Purpose: Banner text for daemons and utilities
//		Created: 1/1/04
//
// --------------------------------------------------------------------------

#ifndef BANNERTEXT__H
#define BANNERTEXT__H

#ifdef NEED_BOX_VERSION_H
#	include "BoxVersion.h"
#endif

#define BANNER_TEXT(UtilityName) \
	"Box Backup " UtilityName " v" BOX_VERSION "\n" \
	"(c) Ben Summers and contributors 2003-2017"

#endif // BANNERTEXT__H

