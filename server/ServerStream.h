// --------------------------------------------------------------------------
//
// File
//		Name:    ServerStream.h
//		Purpose: Stream based server daemons
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#ifndef SERVERSTREAM__H
#define SERVERSTREAM__H

#include <stdlib.h>
#include <errno.h>

#ifndef WIN32
	#include <sys/wait.h>
#endif

#include "Daemon.h"
#include "SocketListen.h"
#include "Utils.h"
#include "Configuration.h"
#include "WaitForEvent.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    ServerStream
//		Purpose: Stream based server daemon
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
template<typename StreamType, int Port, int ListenBacklog = 128, bool ForkToHandleRequests = true>
class ServerStream : public Daemon
{
public:
	ServerStream()
	{
	}
	~ServerStream()
	{
		DeleteSockets();
	}
private:
	ServerStream(const ServerStream &rToCopy)
	{
	}
public:

	virtual const char *DaemonName() const
	{
		return "generic-stream-server";
	}

	virtual void OnIdle() { }

	virtual void Run()
	{
		// Set process title as appropriate
		SetProcessTitle(ForkToHandleRequests?"server":"idle");
	
		// Handle exceptions and child task quitting gracefully.
		bool childExit = false;
		try
		{
			Run2(childExit);
		}
		catch(BoxException &e)
		{
			if(childExit)
			{
				BOX_ERROR("Error in child process, "
					"terminating connection: exception " <<
					e.what() << "(" << e.GetType() <<
					"/" << e.GetSubType() << ")");
				_exit(1);
			}
			else throw;
		}
		catch(std::exception &e)
		{
			if(childExit)
			{
				BOX_ERROR("Error in child process, "
					"terminating connection: exception " <<
					e.what());
				_exit(1);
			}
			else throw;
		}
		catch(...)
		{
			if(childExit)
			{
				BOX_ERROR("Error in child process, "
					"terminating connection: "
					"unknown exception");
				_exit(1);
			}
			else throw;
		}

		// if it's a child fork, exit the process now
		if(childExit)
		{
			// Child task, dump leaks to trace, which we make sure is on
			#ifdef BOX_MEMORY_LEAK_TESTING
				#ifndef NDEBUG
					TRACE_TO_SYSLOG(true);
					TRACE_TO_STDOUT(true);
				#endif
				memleakfinder_traceblocksinsection();
			#endif

			// If this is a child quitting, exit now to stop bad things happening
			_exit(0);
		}
	}
	
	virtual void Run2(bool &rChildExit)
	{
		try
		{
			// Wait object with a timeout of 10 seconds, which is a reasonable time to wait before
			// cleaning up finished child processes.
			WaitForEvent connectionWait(10000);
			
			// BLOCK
			{
				// Get the address we need to bind to
				// this-> in next line required to build under some gcc versions
				const Configuration &config(this->GetConfiguration());
				const Configuration &server(config.GetSubConfiguration("Server"));
				std::string addrs = server.GetKeyValue("ListenAddresses");
	
				// split up the list of addresses
				std::vector<std::string> addrlist;
				SplitString(addrs, ',', addrlist);
	
				for(unsigned int a = 0; a < addrlist.size(); ++a)
				{
					// split the address up into components
					std::vector<std::string> c;
					SplitString(addrlist[a], ':', c);
	
					// listen!
					SocketListen<StreamType, ListenBacklog> *psocket = new SocketListen<StreamType, ListenBacklog>;
					try
					{
						if(c[0] == "inet")
						{
							// Check arguments
							if(c.size() != 2 && c.size() != 3)
							{
								THROW_EXCEPTION(ServerException, ServerStreamBadListenAddrs)
							}
							
							// Which port?
							int port = Port;
							
							if(c.size() == 3)
							{
								// Convert to number
								port = ::atol(c[2].c_str());
								if(port <= 0 || port > ((64*1024)-1))
								{
									THROW_EXCEPTION(ServerException, ServerStreamBadListenAddrs)
								}
							}
							
							// Listen
							psocket->Listen(Socket::TypeINET, c[1].c_str(), port);
						}
						else if(c[0] == "unix")
						{
							#ifdef WIN32
								BOX_WARNING("Ignoring request to listen on a Unix socket on Windows: " << addrlist[a]);
								delete psocket;
								psocket = NULL;
							#else
								// Check arguments size
								if(c.size() != 2)
								{
									THROW_EXCEPTION(ServerException, ServerStreamBadListenAddrs)
								}

								// unlink anything there
								::unlink(c[1].c_str());
								
								psocket->Listen(Socket::TypeUNIX, c[1].c_str());
							#endif // WIN32
						}
						else
						{
							delete psocket;
							THROW_EXCEPTION(ServerException, ServerStreamBadListenAddrs)
						}
						
						if (psocket != NULL)
						{
							// Add to list of sockets
							mSockets.push_back(psocket);
						}
					}
					catch(...)
					{
						delete psocket;
						throw;
					}

					if (psocket != NULL)
					{
						// Add to the list of things to wait on
						connectionWait.Add(psocket);
					}
				}
			}
	
			while(!StopRun())
			{
				// Wait for a connection, or timeout
				SocketListen<StreamType, ListenBacklog> *psocket
					= (SocketListen<StreamType, ListenBacklog> *)connectionWait.Wait();

				if(psocket)
				{
					// Get the incoming connection
					// (with zero wait time)
					std::string logMessage;
					std::auto_ptr<StreamType> connection(psocket->Accept(0, &logMessage));

					// Was there one (there should be...)
					if(connection.get())
					{
						// Since this is a template parameter, the if() will be optimised out by the compiler
						#ifndef WIN32 // no fork on Win32
						if(ForkToHandleRequests && !IsSingleProcess())
						{
							pid_t pid = ::fork();
							switch(pid)
							{
							case -1:
								// Error!
								THROW_EXCEPTION(ServerException, ServerForkError)
								break;
								
							case 0:
								// Child process
								rChildExit = true;
								// Close listening sockets
								DeleteSockets();
								
								// Set up daemon
								EnterChild();
								SetProcessTitle("transaction");
								
								// Memory leak test the forked process
								#ifdef BOX_MEMORY_LEAK_TESTING
									memleakfinder_startsectionmonitor();
								#endif
								
								// The derived class does some server magic with the connection
								HandleConnection(*connection);
								// Since rChildExit == true, the forked process will call _exit() on return from this fn
								return;
			
							default:
								// parent daemon process
								break;
							}
							
							// Log it
							BOX_NOTICE("Message from child process " << pid << ": " << logMessage);
						}
						else
						{
						#endif // !WIN32
							// Just handle in this process
							SetProcessTitle("handling");
							HandleConnection(*connection);
							SetProcessTitle("idle");										
						#ifndef WIN32
						}
						#endif // !WIN32
					}
				}

				OnIdle();

				#ifndef WIN32
				// Clean up child processes (if forking daemon)
				if(ForkToHandleRequests && !IsSingleProcess())
				{
					int status = 0;
					int p = 0;
					do
					{
						if((p = ::waitpid(0 /* any child in process group */, &status, WNOHANG)) == -1
							&& errno != ECHILD && errno != EINTR)
						{
							THROW_EXCEPTION(ServerException, ServerWaitOnChildError)
						}
					} while(p > 0);
				}
				#endif // !WIN32
			}
		}
		catch(...)
		{
			DeleteSockets();
			throw;
		}
		
		// Delete the sockets
		DeleteSockets();
	}

	virtual void HandleConnection(StreamType &rStream)
	{
		Connection(rStream);
	}

	virtual void Connection(StreamType &rStream) = 0;
	
protected:
	// For checking code in derived classes -- use if you have an algorithm which
	// depends on the forking model in case someone changes it later.
	bool WillForkToHandleRequests()
	{
		#ifdef WIN32
		return false;
		#else
		return ForkToHandleRequests && !IsSingleProcess();
		#endif // WIN32
	}

private:
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    ServerStream::DeleteSockets()
	//		Purpose: Delete sockets
	//		Created: 9/3/04
	//
	// --------------------------------------------------------------------------
	void DeleteSockets()
	{
		for(unsigned int l = 0; l < mSockets.size(); ++l)
		{
			if(mSockets[l])
			{
				mSockets[l]->Close();
				delete mSockets[l];
			}
			mSockets[l] = 0;
		}
		mSockets.clear();
	}

private:
	std::vector<SocketListen<StreamType, ListenBacklog> *> mSockets;
};

#define SERVERSTREAM_VERIFY_SERVER_KEYS(DEFAULT_ADDRESSES) \
											{"ListenAddresses", DEFAULT_ADDRESSES, 0, 0}, \
											DAEMON_VERIFY_SERVER_KEYS 

#include "MemLeakFindOff.h"

#endif // SERVERSTREAM__H



