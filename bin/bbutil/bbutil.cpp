#include "bbutil.h"


int main(int argc, char *argv[])
{
	switch(argc) {
	case 1:
		cout << "bbutil" << endl
			  << endl
			  << "    est <path>" << endl
			  << endl
			  << "        Estimate disk usage." << endl
			  << endl
			  << "    u2w <KeysFile> <CertificateFile> <PrivateKeyFile>" << endl
			  << endl
			  << "        Imports Unix-style certificate and key files." << endl
			  << endl;
		break;
	case 3:
		if (!strcmp("est",argv[1]))
			do_est(argv[2]);
		break;
	case 5:
		if (!strcmp("u2w",argv[1]))
			do_u2w(argv[2],argv[3],argv[4]);
		break;
	}

	return 0;
}
