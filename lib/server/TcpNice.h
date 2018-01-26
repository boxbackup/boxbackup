// --------------------------------------------------------------------------
//
// File
//		Name:    TcpNice.h
//		Purpose: Calculator for adaptive TCP window sizing to support
//               low-priority background flows using the stochastic
//               algorithm, as described in
//               http://www.thlab.net/~lmassoul/p18-key.pdf
//		Created: 11/02/2012
//
// --------------------------------------------------------------------------

#ifndef TCPNICE__H
#define TCPNICE__H

#include <memory>

#include "SocketStream.h"
#include "Timer.h"

#if HAVE_DECL_SO_SNDBUF
#	if HAVE_DECL_SOL_TCP && defined HAVE_STRUCT_TCP_INFO_TCPI_RTT
#		define ENABLE_TCP_NICE
#	elif HAVE_DECL_IPPROTO_TCP && HAVE_DECL_TCP_CONNECTION_INFO
// https://stackoverflow.com/a/40478874/648162
#		define ENABLE_TCP_NICE
#	endif
#endif

#ifdef ENABLE_TCP_NICE
// --------------------------------------------------------------------------
//
// Class
//		Name:    TcpNice
//		Purpose: Calculator for adaptive TCP window sizing.
//		Created: 11/02/2012
//
// --------------------------------------------------------------------------

class TcpNice
{
public:
	TcpNice();
	int GetNextWindowSize(int bytesChange, box_time_t timeElapsed,
		int rttEstimateMicros);
	
private:
	/**
	 * The previous (last recommended) window size is one of the parameters
	 * used to calculate the next window size.
	 */
	int mLastWindowSize;
	
	/**
	 * Controls the speed of adaptation and the variance (random variation)
	 * of the stable state in response to noise. The paper suggests using
	 * 1.0 (100%).
	 */
	int mGammaPercent;
	
	/**
	 * Controls the extent to which background flows are allowed to affect
	 * foreground flows. Its detailed meaning is not explained in the paper,
	 * but its units are bytes, and I think it controls how aggressive we
	 * are at increasing window size, potentially at the expense of other
	 * competing flows.
	 */
	int mAlphaStar;
	
	/**
	 * Controls the speed of adaptation of the exponential weighted moving
	 * average (EWMA) estimate of the bandwidth available to this flow.
	 * The paper uses 10%.
	 */
	int mDeltaPercent;
	
	/**
	 * The stochastic algorithm in the paper uses the rate estimate for the
	 * last-but-one period (rbHat[n-2]) to calculate the next window size.
	 * So we keep both the last (in rateEstimateMovingAverage[1]) and the
	 * last-but-one (in rateEstimateMovingAverage[0]) values.
	 */
	int mRateEstimateMovingAverage[2];	
};

// --------------------------------------------------------------------------
//
// Class
//		Name:    NiceSocketStream
//		Purpose: Wrapper around a SocketStream to limit sending rate to
//               avoid interference with higher-priority flows.
//		Created: 11/02/2012
//
// --------------------------------------------------------------------------

class NiceSocketStream : public SocketStream
{
private:
	std::auto_ptr<SocketStream> mapSocket;
	TcpNice mTcpNice;
	std::auto_ptr<Timer> mapTimer;
	int mBytesWrittenThisPeriod;
	box_time_t mPeriodStartTime;
	
	/**
	 * The control interval T from the paper, in milliseconds. The available
	 * bandwidth is estimated over this period, and the window size is
	 * recalculated at the end of each period. It should be long enough for
	 * TCP to adapt to a change in window size; perhaps 10-100 RTTs. One
	 * second (1000) is probably a good first approximation in many cases.
	 */
	int mTimeIntervalMillis;

	/**
	 * Because our data use is bursty, and tcp nice works on the assumption
	 * that we've always got data to send, we should only enable nice mode
	 * when we're doing a bulk upload, and disable it afterwards.
	 */
	bool mEnabled;
	
	void StartTimer()
	{
		mapTimer.reset(new Timer(mTimeIntervalMillis, "NiceSocketStream"));
	}

	void StopTimer()
	{
		mapTimer.reset();
	}

public:
	NiceSocketStream(std::auto_ptr<SocketStream> apSocket);
	virtual ~NiceSocketStream()
	{
		// Be nice about closing the socket
		mapSocket->Shutdown();
		mapSocket->Close();
	}

	// This is the only magic
	virtual void Write(const void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite);
	using IOStream::Write;

	// Everything else is delegated to the sink
	virtual int Read(void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite)
	{
		return mapSocket->Read(pBuffer, NBytes, Timeout);
	}
	virtual pos_type BytesLeftToRead()
	{
		return mapSocket->BytesLeftToRead();
	}
	virtual pos_type GetPosition() const
	{
		return mapSocket->GetPosition();
	}
	virtual void Seek(IOStream::pos_type Offset, int SeekType)
	{
		mapSocket->Seek(Offset, SeekType);
	}
	virtual void Flush(int Timeout = IOStream::TimeOutInfinite)
	{
		mapSocket->Flush(Timeout);
	}
	virtual void Close()
	{
		mapSocket->Close();
	}
	virtual bool StreamDataLeft()
	{
		return mapSocket->StreamDataLeft();
	}
	virtual bool StreamClosed()
	{
		return mapSocket->StreamClosed();
	}
	virtual void SetEnabled(bool enabled);

	off_t GetBytesRead() const { return mapSocket->GetBytesRead(); }
	off_t GetBytesWritten() const { return mapSocket->GetBytesWritten(); }
	void ResetCounters() { mapSocket->ResetCounters(); }

private:
	NiceSocketStream(const NiceSocketStream &rToCopy) 
	{ /* do not call */ }
};
#endif // ENABLE_TCP_NICE

#endif // TCPNICE__H
