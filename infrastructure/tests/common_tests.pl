
# perl fragment, not directly runnable

{
	# test for a C++ compiler
	do_test('Name' => 'Compiler 1',
		'AbortOnFailure' => 1,
		'FailureText' => <<__E,

================

You do not appear to have the g++ compiler installed. Please fix and try again.
(Tested for C++ compilation and use of standard STL C++ library.)

Some distributions rename the g++ compiler to something including the version number,
and fail to create a symlink to the expected name. Investigate this if you believe
you have the C++ compiler installed.

================

__E
		'Code' => <<__E);
#include <string>
int main(int argc, char *argv[])
{
	std::string str;
	str = "Test";
	str += "_test";
}
__E

	# test for a properly working C++ compiler
	do_test('Name' => 'Compiler 2',
		'AbortOnFailure' => 1,
		'RunCode' => 1,
		'FailureText' => <<__E,

================

The C++ compiler fails basic C++ tests. It is impossible to compile and deploy this
software on this platform.

Some less common platforms do not have a working C++ implementation, especially
regarding modern language features such as exceptions. A basic test failed. It is
unlikely that you will be able to use this software without fixing the compiler.

You could try a later version of the compiler, if available.

================

__E
		'Code' => <<__E);
#include <string>
class test_class
{
public:
	test_class(const char *str) : mString(str) {}
	~test_class() {}
private:
	std::string mString;
};
int main(int argc, char *argv[])
{
	try
	{
		throw test_class("Test exception");
	}
	catch(test_class &e)
	{
		return 0;
	}
	return 1;
}
__E

	# test for any version of OpenSSL
	do_test('Name' => 'OpenSSL 1',
		'SuccessFlags' => ['OLD_OPENSSL_OK'],
		'TestCompileFlags' => '-lcrypto ',
		'Code' => <<__E);
#include <openssl/evp.h>
int main(int argc, char *argv[])
{
	EVP_CipherInit(0, 0, 0, 0, 0);
	return 0;
}
__E

	# test for new version of OpenSSL
	do_test('Name' => 'OpenSSL 2',
		'SuccessFlags' => ['OPENSSL_OK'],
		'TestCompileFlags' => '-lcrypto ',
		'Code' => <<__E);
#include <openssl/evp.h>
int main(int argc, char *argv[])
{
	EVP_CipherInit_ex(0, 0, 0, 0, 0, 0);
	return 0;
}
__E

	# Linux is always more fun
	if($build_os eq 'Linux')
	{
		# see if curses is available
		sub curses_test
		{
			my $c = $_[0];
			do_test('Name' => $c,
				'SuccessFlags' => [$c.'_PRESENT'],
				'TestCompileFlags' => '-l'.$c.' ',
				'Code' => <<__E);
#include <curses.h>
int main(int argc, char *argv[])
{
	initscr(); cbreak(); noecho();
	nonl();
	intrflush(stdscr, 0);
	keypad(stdscr, 0);
	return 0;
}
__E
		}
		curses_test('curses');
		curses_test('ncurses');

		# see if LFS support is available
		do_test('Name' => 'Linux LFS support',
			'RunCode' => 1,
			'TestCompileFlags' => '-D_FILE_OFFSET_BITS=64 ',
			'SuccessCompileFlags' => '-D_FILE_OFFSET_BITS=64',
			'Code' => <<__E);
#include <sys/types.h>
#include <sys/stat.h>
int main(int argc, char *argv[])
{
	struct stat st;
	if(sizeof(st.st_size) == 8 && sizeof(off_t) == 8)
	{
		return 0;
	}
	return 1;
}
__E

	}

	# USE_MALLOC doesn't work on some < gcc3 platforms
	if(!$gcc_v3)
	{
		do_test('Name' => 'USE_MALLOC',
			'FailureCompileFlags' => '-DPLATFORM_STL_USE_MALLOC_BROKEN',
			'FailureText' => <<__E,

================
WARNING: The implementation of the C++ STL on this platform may have a flaw
which causes it to apparently leak memory, and this flaw cannot be worked
around.

When running the daemons, check their memory usage does not constantly
increase. The STL flaw can cause excessive memory use.
================

__E
			'Code' => <<__E);
#define __USE_MALLOC
#include <string>
int main(int argc, char *argv[])
{
        std::string s;
        s = "test";
}
__E
	}
}


1;
