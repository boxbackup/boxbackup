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

#ifdef ENABLE_VSS
#	define VSS_TEXT " (VSS)"
#else
#	define VSS_TEXT ""
#endif

#define BANNER_TEXT(UtilityName) \
	"Box " UtilityName " v" BOX_VERSION VSS_TEXT \
	", (c) Ben Summers and contributors 2003-2019"

#endif // BANNERTEXT__H

