#ifndef EMU_POLL_H
#	define EMU_POLL_H

#	ifdef WIN32
#		pragma once

#		define INFTIM -1

#		if(_WIN32_WINNT >= 0x0600)
#			define EMU_POLL ::WSAPoll
#		else
#			define POLLIN 0x1
#			define POLLERR 0x8
#			define POLLOUT 0x4

#			define EMU_POLL ::emu_poll

			struct pollfd
			{
				SOCKET fd;
				short int events;
				short int revents;
			};

			extern int emu_poll(struct pollfd *ufds, unsigned long nfds, int timeout) throw();
#		endif
#	else
#		define EMU_POLL ::poll
#	endif

#endif
