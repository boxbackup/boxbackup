#include "bbutil.h"
#include "zlib.h"


static uint64_t rawTotal = 0;
static uint64_t cmpTotal = 0;


static void recurse(std::string &path)
{
	uint64_t c = 0;
	struct dirent *de;
	DIR *dir = opendir(path.c_str());

	while(NULL != (de = readdir(dir))) {
		if ('.' == de->d_name[0] && ('\0' == de->d_name[1] || ('.' == de->d_name[1] && '\0' == de->d_name[2])))
			continue;

		std::string str(path);
		str.append("\\");
		str.append(de->d_name);

		if (de->d_type & DT_DIR) {
			cout << str.c_str() << ": " << rawTotal << " => " << cmpTotal << endl;
			recurse(str);
		} else {
			char		inBuf[4096];
			Bytef		outBuf[4096];
			z_stream	z = {	reinterpret_cast<Bytef*>(inBuf),
								0,
								0,
								outBuf,
								sizeof(outBuf),
								0,
								NULL,
								NULL,
								Z_NULL,
								Z_NULL,
								Z_NULL,
								Z_BINARY,
								0,
								0};
			ifstream	fs(str, ios_base::binary);

			if (!fs.bad()) {
				if (Z_OK == deflateInit(&z,Z_DEFAULT_COMPRESSION)) {
					do {
						fs.read(inBuf,sizeof(inBuf));

						z.next_in = reinterpret_cast<Bytef*>(inBuf);
						z.avail_in = fs.gcount();
						do {
							z.next_out = outBuf;
							z.avail_out = sizeof(outBuf);
							deflate(&z,Z_NO_FLUSH);
						} while(z.avail_in > 0);
					} while(fs.good());
					do {
						z.next_out = outBuf;
						z.avail_out = sizeof(outBuf);
					} while (Z_OK == deflate(&z,Z_FINISH));
					deflateEnd(&z);

					rawTotal += z.total_in;
					cmpTotal += z.total_out;
				} else {
					cerr << "deflateInit failed" << endl;
				}
			} else {
				cerr << "Failed to open '" << str << "'" << endl;
			}
			fs.close();
		}
	}
	closedir(dir);
}


void do_est(const char *path)
{
	std::string rpath(path);

	recurse(rpath);

	cout << "Total data read:  " << rawTotal << endl;
	cout << "Total compressed: " << cmpTotal << endl;
}
