//project headers: 
#include "StringManipulation.h"

#include "FastMath.h"

//3rd party headers:
#include "swiftdtoa/SwiftDtoa.h"

//system headers:
#include <algorithm>

std::string StringManipulation::NumberToString(double value)
{
	//first check for unusual values
	if(FastIsNaN(value))
		return ".nan";
	if(value == std::numeric_limits<double>::infinity())
		return ".infinity";
	if(value == -std::numeric_limits<double>::infinity())
		return "-.infinity";

	char char_buffer[128];
	size_t num_chars_written = swift_dtoa_optimal_double(value, &char_buffer[0], sizeof(char_buffer));
	return std::string(&char_buffer[0], num_chars_written);
}

std::string StringManipulation::NumberToString(size_t value)
{
	//do this our own way because regular string manipulation libraries are slow and measurably impact performance
	constexpr size_t max_num_digits = std::numeric_limits<size_t>::digits / 3; //max of binary digits per character
	constexpr size_t buffer_size = max_num_digits + 2;
	char buffer[buffer_size];
	char *p = &buffer[0];

	if(value == 0) //check for zero because it's a very common case for integers
		*p++ = '0';
	else //convert each character
	{
		//peel off digits and put them in the next position for the string (reverse when done)
		char *buffer_start = &buffer[0];
		while(value != 0)
		{
			//pull off the least significant digit and convert it to a number character
			*p++ = ('0' + (value % 10));
			value /= 10;
		}

		//put back in original order
		std::reverse(buffer_start, p);
	}
	*p = '\0';	//terminate string
	return std::string(&buffer[0]);
}

std::string StringManipulation::RemoveFirstWord(std::string &str)
{
	std::string first_token;
	size_t spacepos = str.find(' ');
	if(spacepos == std::string::npos)
	{
		first_token = str;
		str = "";
	}
	else
	{
		first_token = str.substr(0, spacepos);
		str = str.substr(spacepos + 1);
	}
	return first_token;
}

std::string StringManipulation::BinaryStringToBase16(std::string &binary_string)
{
	std::string base16_string;
	base16_string.resize(2 * binary_string.size());
	for(size_t i = 0; i < binary_string.size(); i++)
	{
		uint8_t value = binary_string[i];
		base16_string[2 * i] = base16Chars[value >> 4];
		base16_string[2 * i + 1] = base16Chars[value & 15];
	}

	return base16_string;
}

std::string StringManipulation::Base16ToBinaryString(std::string &base16_string)
{
	std::string binary_string;
	binary_string.resize(base16_string.size() / 2);
	for(size_t i = 0; i < base16_string.size(); i += 2)
	{
		uint8_t value = (Base16CharToVal(base16_string[i]) << 4);
		value += Base16CharToVal(base16_string[i + 1]);
		binary_string[i / 2] = value;
	}

	return binary_string;
}

std::string StringManipulation::BinaryStringToBase64(std::string &binary_string)
{
	size_t binary_len = binary_string.size();
	size_t full_triples = binary_len / 3;

	std::string base64_string;
	//resize triples to quads
	base64_string.reserve((full_triples + 2) * 4);

	//encode all groups of 3
	for(size_t i = 0; i + 3 <= binary_len; i += 3)
	{
		auto encoded_quad = Base64ThreeBytesToFourChars(binary_string[i],
								binary_string[i + 1], binary_string[i + 2]);
		base64_string.append(begin(encoded_quad), end(encoded_quad));
	}

	//clean up any characters that aren't divisible by 3,
	// zero fill the remaining bytes, and pad with '=' characters per standard
	size_t chars_beyond_triplets = binary_len - full_triples * 3;
	if(chars_beyond_triplets == 2)
	{
		auto encoded_quad = Base64ThreeBytesToFourChars(binary_string[binary_len - 2],
														binary_string[binary_len - 1], 0);

		base64_string.push_back(encoded_quad[0]);
		base64_string.push_back(encoded_quad[1]);
		base64_string.push_back(encoded_quad[2]);
		base64_string.push_back('=');
	}
	else if(chars_beyond_triplets == 1)
	{
		auto encoded_quad = Base64ThreeBytesToFourChars(binary_string[binary_len - 1], 0, 0);

		base64_string.push_back(encoded_quad[0]);
		base64_string.push_back(encoded_quad[1]);
		base64_string.push_back('=');
		base64_string.push_back('=');
	}

	return base64_string;
}

std::string StringManipulation::Base64ToBinaryString(std::string &base64_string)
{
	size_t base64_len = base64_string.size();

	if(base64_len == 0)
		return std::string();

	//if the length isn't divisible by 4, then resize down
	if((base64_len % 4) != 0)
	{
		base64_len = (base64_len * 4) / 4;
		base64_string.resize(base64_len);
	}

	//exclude last quad, because don't know if it is full
	// in case it has any padding via '=' character and will need special logic
	size_t known_full_quads = (base64_len / 4) - 1;

	std::string binary_string;
	//resize quads to triples
	binary_string.reserve( ((known_full_quads + 2) * 3) / 4);

	//iterate over quads, but don't use <= because don't want to include last quad,
	// same reasoning as known_full_quads
	for(size_t i = 0; i + 4 < base64_len; i += 4)
	{
		auto triplet = Base64FourCharsToThreeBytes(base64_string[i],
						base64_string[i + 1], base64_string[i + 2], base64_string[i + 3]);
		binary_string.append(begin(triplet), end(triplet));
	}

	size_t last_quad_start = known_full_quads * 4;

	if(base64_string[last_quad_start + 2] == '=')
	{
		auto triplet = Base64FourCharsToThreeBytes(base64_string[last_quad_start],
						base64_string[last_quad_start + 1], 'A', 'A');
		binary_string.push_back(triplet[0]);
	}
	else if(base64_string[last_quad_start + 3] == '=')
	{
		auto triplet = Base64FourCharsToThreeBytes(base64_string[last_quad_start],
			base64_string[last_quad_start + 1], base64_string[last_quad_start + 2], 'A');
		binary_string.push_back(triplet[0]);
		binary_string.push_back(triplet[1]);
	}
	else //last quad is full
	{
		auto triplet = Base64FourCharsToThreeBytes(base64_string[last_quad_start],
			base64_string[last_quad_start + 1], base64_string[last_quad_start + 2],
			base64_string[last_quad_start + 3]);
		binary_string.append(begin(triplet), end(triplet));
	}

	return binary_string;
}
