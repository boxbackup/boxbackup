#include "Box.h"


ssize_t readv(int filedes, const struct iovec *vector, size_t count)
{
	int bytes = 0;

	for (size_t i = 0; i < count; i++)
	{
		if (vector[i].iov_len > INT_MAX)
			return -1;
		int result = read(filedes, vector[i].iov_base, static_cast<int>(vector[i].iov_len));
		if (result < 0)
		{
			return result;
		}
		bytes += result;
	}

	return bytes;
}

ssize_t writev(int filedes, const struct iovec *vector, size_t count)
{
	int bytes = 0;

	for (size_t i = 0; i < count; i++)
	{
		if (vector[i].iov_len > INT_MAX)
			return -1;
		int result = write(filedes, vector[i].iov_base, static_cast<int>(vector[i].iov_len));
		if (result < 0)
		{
			return result;
		}
		bytes += result;
	}

	return bytes;
}
