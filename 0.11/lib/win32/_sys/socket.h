#ifndef EMU_SYS_SOCKET_H
#	define EMU_SYS_SOCKET_H

#	ifdef WIN32
#		pragma once

#		define SHUT_RDWR SD_BOTH
#		define SHUT_RD SD_RECEIVE
#		define SHUT_WR SD_SEND
#	endif

#endif
