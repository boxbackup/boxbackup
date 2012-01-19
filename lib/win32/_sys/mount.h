#ifndef EMU_SYS_MOUNT_H
#	define EMU_SYS_MOUNT_H

#	ifdef WIN32
#		pragma once

		extern int statfs(const char * name, struct statfs * s) throw();
#	endif

#endif
