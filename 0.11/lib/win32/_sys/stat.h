#ifndef EMU_SYS_STAT_H
#	define EMU_SYS_STAT_H

#	ifdef WIN32
#		pragma once

#		define S_IRWXG 1
#		define S_IRWXO 2
#		define S_ISUID 4
#		define S_ISGID 8
#		define S_ISVTX 16

#		define EMU_STRUCT_STAT struct emu_stat_
#		define EMU_STAT  emu_stat
#		define EMU_FSTAT emu_fstat
#		define EMU_LSTAT emu_stat
#		define EMU_MKDIR emu_mkdir

#		ifndef __MINGW32__
			//not sure if these are correct
			//S_IWRITE -   writing permitted
			//_S_IREAD -   reading permitted
			//_S_IREAD | _S_IWRITE - 
#			define S_IRUSR S_IWRITE
#			define S_IWUSR S_IREAD
#			define S_IRWXU (S_IREAD|S_IWRITE|S_IEXEC)	

#			define S_ISREG(x) (S_IFREG & x)
#			define S_ISDIR(x) (S_IFDIR & x)
#		endif



		//this shouldn't be needed.
		struct statfs
		{
			TCHAR f_mntonname[MAX_PATH];
		};

		struct emu_stat_ {
			int st_dev;
			uint64_t st_ino;
			short st_mode;
			short st_nlink;
			short st_uid;
			short st_gid;
			//_dev_t st_rdev;
			uint64_t st_size;
			time_t st_atime;
			time_t st_mtime;
			time_t st_ctime;
		};


		extern int emu_stat(const char * pName, struct emu_stat_ * st) throw();
		extern int emu_fstat(HANDLE hdir, struct emu_stat_ * st) throw();

		extern int emu_mkdir(const char* pPathName, mode_t mode) throw();
#	else
#		define EMU_STRUCT_STAT struct stat
#		define EMU_STAT  ::stat
#		define EMU_FSTAT ::fstat
#		define EMU_LSTAT ::lstat
#		define EMU_MKDIR ::mkdir
#	endif

#endif
