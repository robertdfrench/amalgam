//project headers:
#include "PlatformSpecific.h"

//system headers:
#include <algorithm>
#include <cctype>
#include <codecvt>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#ifdef OS_WINDOWS

	//disable std::wstring_convert deprecation warning: no replacement in C++17 so
	// will require rework. 
	#pragma warning(disable: 4996)

	#define NOMINMAX
	#include <Windows.h>

	class WindowsUtf8WStringConversion
	{
	public:
		//convert UTF-8 string to wstring
		inline std::wstring utf8_to_wstring(const std::string& str)
		{
			return conversion.from_bytes(str);
		}

		//convert wstring to UTF-8 string
		inline std::string wstring_to_utf8(const std::wstring& str)
		{
			return conversion.to_bytes(str);
		}

	protected:
		std::wstring_convert<std::codecvt_utf8<wchar_t>> conversion;
	};

#else
	#include <cstdlib>
	#include <dirent.h>
	#include <sys/types.h>
	#include <sys/stat.h>
	#include <string>
	#include <unistd.h>
#endif

std::vector<std::string> Platform_SplitArgString(const std::string &arg_string)
{
	std::vector<std::string> args;

	size_t cur_pos = 0;
	while(cur_pos < arg_string.size())
	{
		//skip over any leading spaces
		if(std::isspace(static_cast<unsigned char>(arg_string[cur_pos])))
		{
			cur_pos++;
			continue;
		}

		std::string cur_arg;

		//quotation, so go to the end of quotation
		if(arg_string[cur_pos] == '"')
		{
			cur_pos++;
			while(cur_pos < arg_string.size())
			{
				if(arg_string[cur_pos] == '"')
				{
					cur_pos++;
					break;
				}

				cur_arg.push_back(arg_string[cur_pos++]);
			}
		}
		else //not quotation, go until next whitespace
		{
			while(cur_pos < arg_string.size())
			{
				if(std::isspace(static_cast<unsigned char>(arg_string[cur_pos])))
				{
					cur_pos++;
					break;
				}

				cur_arg.push_back(arg_string[cur_pos++]);
			}
		}

		args.push_back(cur_arg);
	}

	return args;
}

void Platform_SeparatePathFileExtension(const std::string &combined, std::string &path, std::string &base_filename, std::string &extension)
{
	if(combined.size() == 0)
		return;

	//get path
	path = combined;
	size_t first_forward_slash = path.rfind('/');
	size_t first_backslash = path.rfind('\\');
	size_t first_slash;

	if(first_forward_slash == std::string::npos && first_backslash == std::string::npos)
		first_slash = 0;
	else if(first_forward_slash != std::string::npos && first_backslash == std::string::npos)
		first_slash = first_forward_slash;
	else if(first_forward_slash == std::string::npos && first_backslash != std::string::npos)
		first_slash = first_backslash;
	else //grab whichever one is closer to the end of the string
		first_slash = std::max(first_forward_slash, first_backslash);

	if(first_slash == 0)
		path = std::string("./");
	else
	{
		first_slash++;  //keep the slash in the path
		path = combined.substr(0, first_slash);
	}
	
	//get extension
	std::string filename = combined.substr(first_slash, combined.size() - first_slash);
	size_t extension_position = filename.rfind('.');
	if(extension_position != std::string::npos)
	{
		base_filename = filename.substr(0, extension_position);
		if(filename.size() > extension_position)
			extension = filename.substr(extension_position + 1, filename.size() - (extension_position + 1)); //get rid of .
	}
	else
	{
		base_filename = filename;
		extension = "";
	}
}

void Platform_GetFileNamesOfType(std::vector<std::string> &file_names, const std::string &path, const std::string &extension, bool get_directories)
{
#ifdef OS_WINDOWS
	std::string path_with_wildcard = path + "\\*." + extension;

	WindowsUtf8WStringConversion conv;
	auto wstring = conv.utf8_to_wstring(path_with_wildcard);

	//retreive file names
	WIN32_FIND_DATA find_data;
	HANDLE find = FindFirstFile(wstring.c_str(), &find_data);
	while(find != INVALID_HANDLE_VALUE)
	{
		//if looking for directories and it's a directory, or not looking for directories and not a directory, count it
		if((get_directories && (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			|| (!get_directories && !(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)))
		{
			auto utf8_string = conv.wstring_to_utf8(find_data.cFileName);
			file_names.push_back(utf8_string);
		}

		if(!FindNextFile(find, &find_data))
			break;
	}

	FindClose(find);

#else
	//see if using a wildcard for all extensions
	bool check_extensions = true;
	if(extension.size() >= 1 && extension[extension.size() - 1] == '*')
		check_extensions = false;

	DIR *dh = opendir(path.c_str());
	if(dh == nullptr)
		return;

	struct dirent *ent;
	while((ent = readdir(dh)))
	{
		if(check_extensions)
		{
			char *cur_ext = strstr(ent->d_name, extension.c_str());
			if(cur_ext == nullptr
					|| strlen(ent->d_name) != ((intptr_t)cur_ext - (intptr_t)ent->d_name) + extension.size())
				continue;
		}

		//make a string of the filename (including relative path)
		std::string full_path = path + '/' + ent->d_name;

		//check if it is a directory
		struct stat stat_buf;
		stat(full_path.c_str(), &stat_buf);
		bool is_dir = S_ISDIR(stat_buf.st_mode);
		if(get_directories == is_dir)
			file_names.push_back(std::string(ent->d_name));
	}

	closedir(dh);

#endif
}

std::string Platform_RunSystemCommand(std::string command, bool &successful_run, int &exit_code)
{
	FILE *p;
#ifdef OS_WINDOWS
	p = _popen(command.c_str(), "r");
#else
	p = popen(command.c_str(), "r");
#endif

	if(p == NULL)
	{
		exit_code = 0;
		successful_run = false;
		return "";
	}

	successful_run = true;

	std::string stdout_data;

	//not the fastest, but robust
	char ch;
	while((ch = fgetc(p)) != EOF)
		stdout_data.push_back(ch);

#ifdef OS_WINDOWS
	exit_code = _pclose(p);
#else
	exit_code = pclose(p);
#endif

	return stdout_data;
}

std::string Platform_GetHomeDirectory()
{
#if defined (WINDOWS) || defined (WIN32) || defined (_WIN32)
	static char buff[MAX_PATH];
	const DWORD ret = GetEnvironmentVariableA("USERPROFILE", &buff[0], MAX_PATH);
	if(ret == 0 || ret > MAX_PATH)
		return "";
	else
		return &buff[0];
#else
	return getenv("HOME");
#endif
}

bool Platform_IsResourcePathAccessible(const std::string &resource_path, bool must_exist, std::string &error)
{
	struct stat fileStatus;
	errno = 0;
	if(stat(resource_path.c_str(), &fileStatus) == -1) // == 0 ok; == -1 error
	{
		if(must_exist && errno == ENOENT)
		{
			error = "Resource path does not exist, or path is an empty string.";
			return false;
		}
		else if(errno == ENOTDIR)
		{
			error = "A component of the path is not a directory.";
			return false;
		}
		else if(errno == ELOOP)
		{
			error = "Too many symbolic links encountered while traversing the path.";
			return false;
		}
		else if(errno == EACCES)
		{
			error = "Permission denied.";
			return false;
		}
		else if(errno == ENAMETOOLONG)
		{
			error = "File cannot be read.";
			return false;
		}
	}

	return true;
}

void Platform_GenerateSecureRandomData(void *buffer, size_t length)
{
#ifdef OS_WINDOWS
	HCRYPTPROV hCryptProv;
	CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
	CryptGenRandom(hCryptProv, static_cast<DWORD>(length), static_cast<BYTE *>(buffer));
	CryptReleaseContext(hCryptProv, 0);
#else
	std::ifstream fp("/dev/random", std::ios::in | std::ios::binary);
	if(fp.good())
		fp.read(static_cast<char *>(buffer), sizeof(uint8_t) * length);
	fp.close();
#endif
}

void Platform_EnsurePreciseTiming()
{
	//need to link to extra .libs for this
#if defined(OS_WINDOWS) && defined(OS_WINDOWS_ACCURATE_SLEEP)
	static bool time_resolution_initialized = false;
	if(!time_resolution_initialized)
	{
		timeBeginPeriod(1);
		time_resolution_initialized = true;
	}
#endif
}

//performs localtime in a threadsafe manner
bool Platform_ThreadsafeLocaltime(std::time_t time_value, std::tm **localized_time)
{
#ifdef OS_WINDOWS
	return localtime_s(*localized_time, &time_value) == 0; //MS swaps the values and returns the wrong thing
#else // POSIX
	return ::localtime_r(&time_value, *localized_time) != nullptr;
#endif
}

bool Platform_IsDebuggerPresent()
{
#ifdef OS_WINDOWS
	return (IsDebuggerPresent() ? true : false);
#endif
	return false;
}

std::string Platform_GetOperatingSystemName()
{
#ifdef OS_WINDOWS
	return "Windows";
#endif

#ifdef OS_LINUX
	return "Linux";
#endif

#ifdef OS_MAC
	return "Darwin";
#endif

	return "Unknown";
}
