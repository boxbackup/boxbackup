#include "_sys/_iovec.h"

#ifndef EMU_SYS_UIO_H
#	define EMU_SYS_UIO_H

#	ifdef WIN32
#		pragma once

		extern ssize_t readv (int filedes, const struct iovec *vector, size_t count) throw();
		extern ssize_t writev(int filedes, const struct iovec *vector, size_t count) throw();
#	endif

#endif
