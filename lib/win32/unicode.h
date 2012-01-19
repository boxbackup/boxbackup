#ifndef EMU_UNICODE_H
#	define EMU_UNICODE_H

#	ifdef WIN32
#		pragma once

		extern bool ConvertEncoding (const std::string& rSource, int sourceCodePage, std::string& rDest, int destCodePage) throw();
		extern bool ConvertToUtf8   (const std::string& rSource, std::string& rDest, int sourceCodePage) throw();
		extern bool ConvertFromUtf8 (const std::string& rSource, std::string& rDest, int destCodePage) throw();
		extern bool ConvertUtf8ToConsole(const std::string& rSource, std::string& rDest) throw();
		extern bool ConvertConsoleToUtf8(const std::string& rSource, std::string& rDest) throw();
#	endif

#endif
