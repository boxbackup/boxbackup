// :mode=c++:
/*
encode.h - c++ wrapper for a base64 encoding algorithm

This is part of the libb64 project, and has been placed in the public domain.
For details, see http://sourceforge.net/projects/libb64
*/

#ifndef BASE64_ENCODE_H
#define BASE64_ENCODE_H

#include <iostream>

namespace base64
{
	
	extern "C" 
	{
		#include "cencode.h"
	}
	
	struct encoder
	{
		base64_encodestate _state;
		int _buffersize;
		
		encoder(int buffersize_in = 4096)
		: _buffersize(buffersize_in)
		{}
		int encode(char value_in)
		{
			return base64_encode_value(value_in);
		}
		int encode(const char* code_in, const int length_in, char* plaintext_out)
		{
			return base64_encode_block(code_in, length_in, plaintext_out, &_state);
		}
		int encode_end(char* plaintext_out)
		{
			return base64_encode_blockend(plaintext_out, &_state);
		}
		std::string encode(const std::string& input)
		{
			base64_init_encodestate(&_state);
			char* output = new char[2*input.size()];
			int outlength = encode(input.c_str(), input.size(),
				output);
			outlength += encode_end(output + outlength);
			std::string output_string(output, outlength);
			base64_init_encodestate(&_state);
			delete [] output;
			return output_string;
		}
		void encode(std::istream& istream_in, std::ostream& ostream_in)
		{
			base64_init_encodestate(&_state);
			//
			const int N = _buffersize;
			char* plaintext = new char[N];
			char* code = new char[2*N];
			int plainlength;
			int codelength;
			
			do
			{
				istream_in.read(plaintext, N);
				plainlength = istream_in.gcount();
				//
				codelength = encode(plaintext, plainlength, code);
				ostream_in.write(code, codelength);
			}
			while (istream_in.good() && plainlength > 0);
			
			codelength = encode_end(code);
			ostream_in.write(code, codelength);
			//
			base64_init_encodestate(&_state);
			
			delete [] code;
			delete [] plaintext;
		}
	};
	
} // namespace base64

#endif // BASE64_ENCODE_H

