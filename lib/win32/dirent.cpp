#include "Box.h"


struct dirent& dirent::operator =(WIN32_FIND_DATAW& pWFD)
{
	d_type	= attr_to_type(pWFD.dwFileAttributes);
	d_name_w	= pWFD.cFileName;
	Win32::wide2multi(d_name_w, d_name_m);
	d_namlen	= static_cast<uint16_t>(d_name_w.size());
	d_name	= d_name_m.c_str();
	return *this;
}

DIR_::DIR_(const char *dir_)
{
	if (!dir_)
		THROW_EXCEPTION(Win32Exception, Internal)
	if (!*dir_)
		THROW_EXCEPTION(Win32Exception, Internal)
	std::string tdir(dir_);
	if ('\\' != tdir[tdir.size()-1] || '/' != tdir[tdir.size()-1])
		tdir.push_back('\\');
	tdir.push_back('*');
	Win32::multi2wide(tdir, dir);
	if (INVALID_HANDLE_VALUE == (hFind = FindFirstFileW(dir.c_str(),&wfd)))
		THROW_EXCEPTION(Win32Exception, API_FindFirstFile);
}

DIR_::~DIR_()
{
	if (INVALID_HANDLE_VALUE != hFind)
		FindClose(hFind);
}

void DIR_::next()
{
	if (!FindNextFileW(hFind,&wfd))
	{
		DWORD gle = GetLastError();
		FindClose(hFind);
		hFind = INVALID_HANDLE_VALUE;
		if (ERROR_NO_MORE_FILES != gle)
		{
			SetLastError(gle);
			THROW_EXCEPTION(Win32Exception, API_FindNextFile);
		}
	}
}


DIR* opendir(const char* name) throw()
{
	DIR *dir = NULL;

	try
	{
		dir = new DIR(name);
	}
	catch(...)
	{
		dir = NULL; // just in case
	}
	return dir;
}

struct dirent* readdir(DIR *dir) throw()
{
	struct dirent *de = NULL;

	if (!dir)
	{
		errno = EINVAL;
	}
	else if (INVALID_HANDLE_VALUE == dir->hFind)
	{
		errno = EBADF;
	}
	else
	{
		try
		{
			dir->de = dir->wfd;
			dir->next();
			de = &dir->de;
		}
		EMU_EXCEPTION_HANDLING
	}
	return de;
}

int closedir(DIR *dir)
{
	int r = -1;

	if (!dir)
	{
		errno = EINVAL;
	}
	else
	{
		delete dir;
		r = 0;
	}
	return r;
}
