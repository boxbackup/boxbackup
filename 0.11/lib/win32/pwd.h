#pragma once

#ifdef WIN32
	struct passwd {
		char *pw_name;
		char *pw_passwd;
		int pw_uid;
		int pw_gid;
		time_t pw_change;
		char *pw_class;
		char *pw_gecos;
		char *pw_dir;
		char *pw_shell;
		time_t pw_expire;
	};

	extern struct passwd * getpwnam(const char * name) throw();
#endif
