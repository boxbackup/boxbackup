// --------------------------------------------------------------------------
//
// File
//		Name:    TLSContext.h
//		Purpose: TLS (SSL) context for connections
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------

#ifndef TLSCONTEXT__H
#define TLSCONTEXT__H

#ifndef TLS_CLASS_IMPLEMENTATION_CPP
	class SSL_CTX;
#endif

// --------------------------------------------------------------------------
//
// Class
//		Name:    TLSContext
//		Purpose: TLS (SSL) context for connections
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
class TLSContext
{
public:
	TLSContext();
	~TLSContext();
private:
	TLSContext(const TLSContext &);
public:
	void Initialise(bool AsServer, const char *CertificatesFile, const char *PrivateKeyFile, const char *TrustedCAsFile);
	SSL_CTX *GetRawContext() const;

private:
	SSL_CTX *mpContext;
};

#endif // TLSCONTEXT__H

