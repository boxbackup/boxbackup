#include "Box.h"


#if(_WIN32_WINNT < 0x0600)
// --------------------------------------------------------------------------
//
// Function
//		Name:    poll
//		Purpose: a weak implimentation (just enough for box)
//			of the unix poll for winsock2
//		Created: 25th October 2004
//
// --------------------------------------------------------------------------
int emu_poll(struct pollfd *ufds, unsigned long nfds, int timeout) throw()
{
	try
	{
		fd_set readfd;
		fd_set writefd;

		FD_ZERO(&readfd);
		FD_ZERO(&writefd);

		// struct pollfd *ufdsTmp = ufds;

		timeval timOut;
		timeval *tmpptr;

		if (timeout == INFTIM)
			tmpptr = NULL;
		else
			tmpptr = &timOut;

		timOut.tv_sec  = timeout / 1000;
		timOut.tv_usec = timeout * 1000;

		for (unsigned long i = 0; i < nfds; i++)
		{
			struct pollfd* ufd = &(ufds[i]);

			if (ufd->events & POLLIN)
			{
				FD_SET(ufd->fd, &readfd);
			}

			if (ufd->events & POLLOUT)
			{
				FD_SET(ufd->fd, &writefd);
			}

			if (ufd->events & ~(POLLIN | POLLOUT))
			{
				printf("Unsupported poll bits %d",
					ufd->events);
				return -1;
			}
		}

		int nready = select(0, &readfd, &writefd, 0, tmpptr);

		if (nready == SOCKET_ERROR)
		{
			// int errval = WSAGetLastError();

			struct pollfd* pufd = ufds;
			for (unsigned long i = 0; i < nfds; i++)
			{
				pufd->revents = POLLERR;
				pufd++;
			}
			return (-1);
		}
		else if (nready > 0)
		{
			for (unsigned long i = 0; i < nfds; i++)
			{
				struct pollfd *ufd = &(ufds[i]);

				if (FD_ISSET(ufd->fd, &readfd))
				{
					ufd->revents |= POLLIN;
				}

				if (FD_ISSET(ufd->fd, &writefd))
				{
					ufd->revents |= POLLOUT;
				}
			}
		}

		return nready;
	}
	catch (...)
	{
		printf("Caught poll");
	}

	return -1;
}
#endif
