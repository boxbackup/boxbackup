#ifndef EMU_DIRENT_H
#	define EMU_DIRENT_H

#	ifdef WIN32
#		pragma once

#		define DT_DIR 4
#		define DT_REG 8

		struct dirent {
			uint8_t	d_type;					/* file type, see below */
			uint16_t	d_namlen;				/* length of string in d_name */
			const char *d_name;
			std::string d_name_m;
			std::wstring d_name_w;

		private:
			uint8_t attr_to_type(DWORD attr) throw() {
				return (attr & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
			}

		public:
			struct dirent& operator =(WIN32_FIND_DATAW& pWFD);
		};

		typedef struct DIR_ {
			HANDLE				hFind;
			WIN32_FIND_DATAW	wfd;
			std::wstring		dir;
			struct dirent		de;

		public:
			DIR_(const char* dir_);
			~DIR_();
			void next();
		} DIR;


		extern DIR* opendir(const char* name);
		extern struct dirent* readdir(DIR *dir);
		extern int closedir(DIR *dir);
#	endif

#endif
