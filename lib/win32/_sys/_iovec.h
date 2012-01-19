#ifndef EMU_SYS_IOVEC_H
#	define EMU_SYS_IOVEC_H

#	ifdef WIN32
#		pragma once

		struct iovec {
			void *iov_base;   /* Starting address */
			size_t iov_len;   /* Number of bytes */
		};
#	endif

#endif
