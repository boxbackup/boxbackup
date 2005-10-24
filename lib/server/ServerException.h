// --------------------------------------------------------------------------
//
// File
//		Name:    ServerException.h
//		Purpose: Exception
//		Created: 2003/07/08
//
// --------------------------------------------------------------------------

#ifndef SERVEREXCEPTION__H
#define SERVEREXCEPTION__H

// Compatibility header
#include "autogen_ServerException.h"
#include "autogen_ConnectionException.h"

// Rename old connection exception names to new names without Conn_ prefix
// This is all because ConnectionException used to be derived from ServerException
// with some funky magic with subtypes. Perhaps a little unreliable, and the
// usefulness of it never really was used.
#define Conn_SocketWriteError			SocketWriteError	
#define Conn_SocketReadError			SocketReadError	
#define Conn_SocketNameLookupError		SocketNameLookupError
#define Conn_SocketShutdownError		SocketShutdownError
#define Conn_SocketConnectError			SocketConnectError	
#define Conn_TLSHandshakeFailed			TLSHandshakeFailed	
#define Conn_TLSShutdownFailed			TLSShutdownFailed	
#define Conn_TLSWriteFailed				TLSWriteFailed		
#define Conn_TLSReadFailed				TLSReadFailed		
#define Conn_TLSNoPeerCertificate		TLSNoPeerCertificate
#define Conn_TLSPeerCertificateInvalid	TLSPeerCertificateInvalid
#define Conn_TLSClosedWhenWriting		TLSClosedWhenWriting
#define Conn_TLSHandshakeTimedOut		TLSHandshakeTimedOut
#define Conn_Protocol_Timeout			Protocol_Timeout	
#define Conn_Protocol_ObjTooBig			Protocol_ObjTooBig	
#define Conn_Protocol_BadCommandRecieved			Protocol_BadCommandRecieved	
#define Conn_Protocol_UnknownCommandRecieved		Protocol_UnknownCommandRecieved
#define Conn_Protocol_TriedToExecuteReplyCommand	Protocol_TriedToExecuteReplyCommand
#define Conn_Protocol_UnexpectedReply				Protocol_UnexpectedReply		
#define Conn_Protocol_HandshakeFailed				Protocol_HandshakeFailed		
#define Conn_Protocol_StreamWhenObjExpected			Protocol_StreamWhenObjExpected	
#define Conn_Protocol_ObjWhenStreamExpected			Protocol_ObjWhenStreamExpected	
#define Conn_Protocol_TimeOutWhenSendingStream		Protocol_TimeOutWhenSendingStream

#endif // SERVEREXCEPTION__H

