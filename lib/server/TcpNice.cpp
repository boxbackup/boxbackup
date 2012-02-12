// --------------------------------------------------------------------------
//
// File
//		Name:    TcpNice.cpp
//		Purpose: Calculator for adaptive TCP window sizing to support
//               low-priority background flows using the stochastic
//               algorithm, as described in
//               http://www.thlab.net/~lmassoul/p18-key.pdf
//		Created: 11/02/2012
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "TcpNice.h"
#include "Logging.h"
#include "BoxTime.h"

#ifdef HAVE_NETINET_TCP_H
#	include <netinet/tcp.h>
#endif

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    TcpNice::TcpNice()
//		Purpose: Initialise state of the calculator
//		Created: 11/02/2012
//
// --------------------------------------------------------------------------
TcpNice::TcpNice()
: mLastWindowSize(1),
  mGammaPercent(100),
  mAlphaStar(100),
  mDeltaPercent(10)
{ }

// --------------------------------------------------------------------------
//
// Function
//		Name:    int GetNextWindowSize(int bytesChange,
//               box_time_t timeElapsed, int rttEstimateMillis)
//		Purpose: Calculate the next recommended window size, given the
//               number of bytes sent since the previous recommendation,
//               and the time elapsed.
//		Created: 11/02/2012
//
// --------------------------------------------------------------------------
int TcpNice::GetNextWindowSize(int bytesChange, box_time_t timeElapsed,
	int rttEstimateMicros)
{
	int epsilon = (mAlphaStar * 1000000) / rttEstimateMicros;
	
	// timeElapsed is in microseconds, so this will fail for T > 2000 seconds
	int rateLastPeriod = ((uint64_t)bytesChange * 1000000 / timeElapsed);
	
	int rawAdjustment = epsilon + rateLastPeriod -
		mRateEstimateMovingAverage[0];
	
	int gammaAdjustment = (rawAdjustment * mGammaPercent) / 100;
	
	int newWindowSize = mLastWindowSize + gammaAdjustment;
	
	int newRateEstimateMovingAverage = 
		(((100 - mDeltaPercent) * mRateEstimateMovingAverage[1]) / 100) +
		((mDeltaPercent * rateLastPeriod) / 100);
	
	BOX_TRACE("TcpNice: "
		"b=" << bytesChange << ", "
		"T=" << timeElapsed << ", "
		"rtt=" << rttEstimateMicros << ", "
		"e=" << epsilon << ", "
		"rb=" << rateLastPeriod << ", "
		"rbhat=" << newRateEstimateMovingAverage << ", "
		"raw=" << rawAdjustment << ", "
		"gamma=" << gammaAdjustment << ", "
		"wb=" << newWindowSize);
	
	mRateEstimateMovingAverage[0] = mRateEstimateMovingAverage[1];
	mRateEstimateMovingAverage[1] = newRateEstimateMovingAverage;
	mLastWindowSize = newWindowSize;

	return newWindowSize;
}

// --------------------------------------------------------------------------
//
// Constructor
//		Name:    NiceSocketStream::NiceSocketStream(
//               std::auto_ptr<SocketStream> apSocket)
//		Purpose: Initialise state of the socket wrapper
//		Created: 11/02/2012
//
// --------------------------------------------------------------------------

NiceSocketStream::NiceSocketStream(std::auto_ptr<SocketStream> apSocket)
: mapSocket(apSocket),
  mTcpNice(),
  mBytesWrittenThisPeriod(0),
  mPeriodStartTime(GetCurrentBoxTime()),
  mTimeIntervalMillis(1000),
  mEnabled(false)
{ }

// --------------------------------------------------------------------------
//
// Function
//		Name:    NiceSocketStream::Write(const void *pBuffer, int NBytes)
//		Purpose: Writes bytes to the underlying stream, adjusting window size
//               using a TcpNice calculator.
//		Created: 2012/02/11
//
// --------------------------------------------------------------------------
void NiceSocketStream::Write(const void *pBuffer, int NBytes)
{
#ifdef HAVE_DECL_SO_SNDBUF
	if(mEnabled && mapTimer.get() && mapTimer->HasExpired())
	{
		box_time_t newPeriodStart = GetCurrentBoxTime();
		box_time_t elapsed = newPeriodStart - mPeriodStartTime;
		struct tcp_info info;
		int socket = mapSocket->GetSocketHandle();
		int rtt = 50; // WAG

#	if defined HAVE_DECL_SOL_TCP && defined HAVE_DECL_TCP_INFO && defined HAVE_STRUCT_TCP_INFO_TCPI_RTT
		socklen_t optlen = sizeof(info);
		if(getsockopt(socket, SOL_TCP, TCP_INFO, &info, &optlen) == -1)
		{
			BOX_LOG_SYS_WARNING("getsockopt(" << socket << ", SOL_TCP, "
				"TCP_INFO) failed");
		}
		else if(optlen != sizeof(info))
		{
			BOX_WARNING("getsockopt(" << socket << ", SOL_TCP, "
				"TCP_INFO) return structure size " << optlen << ", "
				"expected " << sizeof(info));
		}
		else
		{
			rtt = info.tcpi_rtt;
		}
#	endif
		
		int newWindow = mTcpNice.GetNextWindowSize(mBytesWrittenThisPeriod,
			elapsed, rtt);
		
		if(setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &newWindow,
			sizeof(newWindow)) == -1)
		{
			BOX_LOG_SYS_WARNING("getsockopt(" << socket << ", SOL_SOCKET, "
				"SO_SNDBUF, " << newWindow << ") failed");
		}

		StopTimer();
	}

	if(mEnabled && !mapTimer.get())
	{
		// Don't start the timer until we receive the first data to write,
		// as diffing might take a long time and we don't want to bias
		// the TcpNice algorithm by running while we don't have bulk data
		// to send.
		StartTimer();
		mPeriodStartTime = GetCurrentBoxTime();
		mBytesWrittenThisPeriod = 0;
	}
	
	mBytesWrittenThisPeriod += NBytes;
#endif // HAVE_DECL_SO_SNDBUF

	mapSocket->Write(pBuffer, NBytes);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    NiceSocketStream::SetEnabled(bool enabled)
//		Purpose: Update the enabled status, and if disabling, cancel the
//               timer and set a sensible window size.
//		Created: 2012/02/12
//
// --------------------------------------------------------------------------

void NiceSocketStream::SetEnabled(bool enabled)
{
	mEnabled = enabled;

	if(!enabled)
	{
		StopTimer();
#ifdef HAVE_DECL_SO_SNDBUF
		int socket = mapSocket->GetSocketHandle();
		int newWindow = 1<<17;
		if(setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &newWindow,
			sizeof(newWindow)) == -1)
		{
			BOX_LOG_SYS_WARNING("getsockopt(" << socket << ", SOL_SOCKET, "
				"SO_SNDBUF, " << newWindow << ") failed");
		}
#endif
	}
}
