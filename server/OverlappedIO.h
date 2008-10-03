// --------------------------------------------------------------------------
//
// File
//		Name:    OverlappedIO.h
//		Purpose: Windows overlapped IO handle guard
//		Created: 2008/09/30
//
// --------------------------------------------------------------------------

#ifndef OVERLAPPEDIO__H
#define OVERLAPPEDIO__H

class OverlappedIO
{
public:
	OVERLAPPED mOverlapped;

	OverlappedIO()
	{
		ZeroMemory(&mOverlapped, sizeof(mOverlapped));
		mOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE,
			NULL);
		if (mOverlapped.hEvent == INVALID_HANDLE_VALUE)
		{
			BOX_LOG_WIN_ERROR("Failed to create event for "
				"overlapped I/O");
			THROW_EXCEPTION(ServerException, BadSocketHandle);
		}
	}

	~OverlappedIO()
	{
		if (CloseHandle(mOverlapped.hEvent) != TRUE)
		{
			BOX_LOG_WIN_ERROR("Failed to delete event for "
				"overlapped I/O");
			THROW_EXCEPTION(ServerException, BadSocketHandle);
		}
	}
};

#endif // !OVERLAPPEDIO__H
