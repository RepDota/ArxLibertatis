/*
 * Copyright 2011-2022 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
 *
 * Arx Libertatis is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arx Libertatis is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Arx Libertatis.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "platform/Environment.h"

#include <cctype>
#include <algorithm>
#include <mutex>
#include <sstream>
#include <utility>

#include <stdlib.h> // needed for realpath and more

#include "Configure.h"

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
struct IUnknown; // Workaround for error C2187 in combaseapi.h when using /permissive-
#include <windows.h>
#include <shlobj.h>
#include <wchar.h>
#include <shellapi.h>
#include <objbase.h>
#endif

#if ARX_HAVE_READLINK
#include <unistd.h>
#endif

#if ARX_HAVE_FCNTL
#include <fcntl.h>
#include <errno.h>
#endif

#if ARX_PLATFORM == ARX_PLATFORM_MACOS
#include <mach-o/dyld.h>
#include <sys/param.h>
#endif

#if ARX_HAVE_SYSCTL
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#include <boost/algorithm/string/case_conv.hpp>

#include "io/fs/PathConstants.h"
#include "io/fs/FilePath.h"
#include "io/fs/Filesystem.h"

#include "platform/WindowsUtils.h"

#include "util/Number.h"
#include "util/String.h"


namespace platform {

std::string expandEnvironmentVariables(std::string_view in) {
	
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	
	platform::WideString win(in);
	
	platform::WideString out;
	out.allocate(out.capacity());
	
	DWORD length = ExpandEnvironmentStringsW(win, out.data(), out.size());
	if(length > out.size()) {
		out.allocate(length);
		length = ExpandEnvironmentStringsW(win, out.data(), out.size());
	}
	
	if(length == 0 || length > out.size()) {
		return std::string(in);
	}
	
	out.resize(length - 1);
	
	return out.toUTF8();
	
	#else
	
	std::ostringstream oss;
	
	size_t depth = 0;
	size_t skip = 0;
	
	for(size_t i = 0; i < in.size(); ) {
		
		if(in[i] == '\\') {
			i++;
			if(i < in.size()) {
				if(skip == 0) {
					oss << in[i];
				}
				i++;
			}
			continue;
		}
		
		if(in[i] == '$') {
			i++;
			
			bool nested = false;
			if(i < in.size() && in[i] == '{') {
				nested = true;
				i++;
			}
			
			size_t start = i;
			while(i < in.size() && (in[i] == '_' || (in[i] >= '0' && in[i] <= '9')
			                                     || (in[i] >= 'a' && in[i] <= 'z')
			                                     || (in[i] >= 'A' && in[i] <= 'Z'))) {
				i++;
			}
			
			if(skip) {
				if(nested) {
					depth++;
					skip++;
				}
				continue;
			}
			
			const char * value = std::getenv(std::string(in.substr(start, i - start)).c_str());
			if(!nested) {
				if(value) {
					oss << value;
				}
				continue;
			}
			
			bool empty = (value == nullptr);
			if(i < in.size() && in[i] == ':') {
				empty = empty || *value == '\0';
				i++;
			}
			
			depth++;
			
			if(i < in.size() && in[i] == '+') {
				if(empty) {
					skip++;
				}
				i++;
			} else {
				if(!empty) {
					oss << value;
				}
				if(i < in.size() && in[i] == '-') {
					if(!empty) {
						skip++;
					}
					i++;
				} else {
					skip++;
				}
			}
			
			continue;
		}
		
		if(depth > 0 && in[i] == '}') {
			if(skip > 0) {
				skip--;
			}
			depth--;
			i++;
			continue;
		}
		
		if(skip == 0) {
			oss << in[i];
		}
		i++;
	}
	
	return oss.str();
	
	#endif
}

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
static bool getRegistryValue(HKEY hkey, const platform::WideString & wname, std::string & result,
                             REGSAM flags = 0) {
	
	HKEY handle = 0;
	long ret = RegOpenKeyEx(hkey, L"Software\\ArxLibertatis\\", 0,
	                        KEY_QUERY_VALUE | flags, &handle);
	if(ret != ERROR_SUCCESS) {
		return false;
	}
	
	platform::WideString buffer;
	buffer.allocate(buffer.capacity());
	
	// find size of value
	DWORD type = 0;
	DWORD length = buffer.size() * sizeof(WCHAR);
	ret = RegQueryValueExW(handle, wname, nullptr, &type, LPBYTE(buffer.data()), &length);
	if(ret == ERROR_MORE_DATA && length > 0) {
		buffer.resize(length / sizeof(WCHAR) + 1);
		ret = RegQueryValueExW(handle, wname, nullptr, &type, LPBYTE(buffer.data()), &length);
	}
	
	RegCloseKey(handle);
	
	if(ret == ERROR_SUCCESS && type == REG_SZ) {
		buffer.resize(length / sizeof(WCHAR));
		buffer.compact();
		result = buffer.toUTF8();
		return true;
	} else {
		return false;
	}
}
#endif

bool getSystemConfiguration(std::string_view name, std::string & result) {
	
#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	
	#if defined(_WIN64)
	REGSAM foreign_registry = KEY_WOW64_32KEY;
	#else
	REGSAM foreign_registry = KEY_WOW64_64KEY;
	#endif
	
	platform::WideString wname(name);
	
	if(getRegistryValue(HKEY_CURRENT_USER, wname, result)) {
		return true;
	}
	if(getRegistryValue(HKEY_CURRENT_USER, wname, result, foreign_registry)) {
		return true;
	}
	
	if(getRegistryValue(HKEY_LOCAL_MACHINE, wname, result)) {
		return true;
	}
	if(getRegistryValue(HKEY_LOCAL_MACHINE, wname, result, foreign_registry)) {
		return true;
	}
	
#else
	ARX_UNUSED(name), ARX_UNUSED(result);
#endif
	
	return false;
}

#if ARX_PLATFORM == ARX_PLATFORM_WIN32

std::vector<fs::path> getSystemPaths(SystemPathId id) {
	
	std::vector<fs::path> result;
	
	if(id != UserDirPrefixes) {
		return result;
	}
	
	// Vista and up
	{
		// Don't hardlink with SHGetKnownFolderPath to allow the game to start on XP too!
		typedef HRESULT (WINAPI * PSHGetKnownFolderPath)(const GUID & rfid, DWORD dwFlags,
		                                                 HANDLE hToken, PWSTR * ppszPath);
		
		const int kfFlagCreate  = 0x00008000; // KF_FLAG_CREATE
		const int kfFlagNoAlias = 0x00001000; // KF_FLAG_NO_ALIAS
		const GUID folderIdSavedGames = {
			0x4C5C32FF, 0xBB9D, 0x43b0, { 0xB5, 0xB4, 0x2D, 0x72, 0xE5, 0x4E, 0xAA, 0xA4 }
		};
		
		CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		
		HMODULE dll = GetModuleHandleW(L"shell32.dll");
		if(dll) {
			
			PSHGetKnownFolderPath GetKnownFolderPath = getProcAddress<PSHGetKnownFolderPath>(dll, "SHGetKnownFolderPath");
			if(GetKnownFolderPath) {
				LPWSTR savedgames = nullptr;
				HRESULT hr = GetKnownFolderPath(folderIdSavedGames, kfFlagCreate | kfFlagNoAlias,
				                                nullptr, &savedgames);
				if(SUCCEEDED(hr)) {
					result.push_back(platform::WideString::toUTF8(savedgames));
				}
				CoTaskMemFree(savedgames);
			}
			
		}
		
		CoUninitialize();
		
	}
	
	// XP
	{
		WCHAR mydocuments[MAX_PATH];
		HRESULT hr = SHGetFolderPathW(nullptr, CSIDL_PERSONAL | CSIDL_FLAG_CREATE, nullptr,
		                              SHGFP_TYPE_CURRENT, mydocuments);
		if(SUCCEEDED(hr)) {
			result.push_back(fs::path(platform::WideString::toUTF8(mydocuments)) / "My Games");
		}
	}
	
	return result;
}

#else

std::vector<fs::path> getSystemPaths(SystemPathId id) {
	ARX_UNUSED(id);
	return std::vector<fs::path>();
}

#endif

static const char * executablePath = nullptr;

void initializeEnvironment(const char * argv0) {
	executablePath = argv0;
}

#if ARX_HAVE_READLINK && ARX_PLATFORM != ARX_PLATFORM_MACOS
static bool try_readlink(std::vector<char> & buffer, const char * path) {
	
	int ret = readlink(path, buffer.data(), buffer.size());
	while(ret >= 0 && std::size_t(ret) == buffer.size()) {
		buffer.resize(buffer.size() * 2);
		ret = readlink(path, buffer.data(), buffer.size());
	}
	
	if(ret < 0) {
		return false;
	}
	
	buffer.resize(ret);
	return true;
}
#endif

fs::path getExecutablePath() {
	
#if ARX_PLATFORM == ARX_PLATFORM_MACOS
	
	uint32_t bufsize = 0;
	
	// Obtain required size
	_NSGetExecutablePath(nullptr, &bufsize);
	
	std::vector<char> exepath(bufsize);
	
	if(_NSGetExecutablePath(exepath.data(), &bufsize) == 0) {
		char exerealpath[MAXPATHLEN];
		if(realpath(exepath.data(), exerealpath)) {
			return exerealpath;
		}
	}
	
#elif ARX_PLATFORM == ARX_PLATFORM_WIN32
	
	platform::WideString buffer;
	buffer.allocate(buffer.capacity());
	
	while(true) {
		DWORD size = GetModuleFileNameW(nullptr, buffer.data(), buffer.size());
		if(size < buffer.size()) {
			buffer.resize(size);
			return buffer.toUTF8();
		}
		buffer.allocate(buffer.size() * 2);
	}
	
#else
	
	// FreeBSD
	#if ARX_HAVE_SYSCTL && defined(CTL_KERN) && defined(KERN_PROC) \
	    && defined(KERN_PROC_PATHNAME) && ARX_PLATFORM == ARX_PLATFORM_BSD \
	    && defined(PATH_MAX)
	int mib[4];
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PATHNAME;
	mib[3] = -1;
	char pathname[PATH_MAX];
	size_t size = sizeof(pathname);
	int error = sysctl(mib, 4, pathname, &size, nullptr, 0);
	if(error != -1 && size > 0 && size < sizeof(pathname)) {
		return util::loadString(pathname, size);
	}
	#endif
	
	// Solaris
	#if ARX_HAVE_GETEXECNAME
	const char * execname = getexecname();
	if(execname != nullptr) {
		return execname;
	}
	#endif
	
	// Try to get the path from OS-specific procfs entries
	#if ARX_HAVE_READLINK
	std::vector<char> buffer(1024);
	// Linux
	if(try_readlink(buffer, "/proc/self/exe")) {
		return fs::path(std::string_view(buffer.data(), buffer.size()));
	}
	// FreeBSD, DragonFly BSD
	if(try_readlink(buffer, "/proc/curproc/file")) {
		return fs::path(std::string_view(buffer.data(), buffer.size()));
	}
	// NetBSD
	if(try_readlink(buffer, "/proc/curproc/exe")) {
		return fs::path(std::string_view(buffer.data(), buffer.size()));
	}
	// Solaris
	if(try_readlink(buffer, "/proc/self/path/a.out")) {
		return fs::path(std::string_view(buffer.data(), buffer.size()));
	}
	#endif
	
#endif
	
	// Fall back to argv[0] if possible
	if(executablePath != nullptr) {
		std::string path(executablePath);
		if(path.find('/') != std::string::npos) {
			return path;
		}
	}
	
	// Give up - we couldn't determine the exe path.
	return fs::path();
}

std::string getCommandName() {
	
	// Prefer the name passed on the command-line to the actual executable name
	fs::path path = executablePath ? fs::path(executablePath) : getExecutablePath();
	
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	if(path.has_ext(".exe")) {
		return std::string(path.basename());
	}
	#endif
	
	return std::string(path.filename());
}

fs::path getHelperExecutable(std::string_view name) {
	
	fs::path exe = getExecutablePath();
	if(!exe.empty()) {
		if(exe.is_relative()) {
			exe = fs::current_path() / exe;
		}
		exe = exe.parent();
		fs::path helper = exe / name;
		if(fs::is_regular_file(helper)) {
			return helper;
		}
		#if ARX_PLATFORM == ARX_PLATFORM_WIN32
		helper.append(".exe");
		if(fs::is_regular_file(helper)) {
			return helper;
		}
		#endif
	}
	
	if(fs::libexec_dir) {
		std::string decoded = expandEnvironmentVariables(fs::libexec_dir);
		for(fs::path libexec_dir : util::splitIgnoreEmpty(decoded, env_list_seperator)) {
			fs::path helper = libexec_dir / name;
			if(helper.is_relative()) {
				helper = exe / helper;
			}
			if(fs::is_regular_file(helper)) {
				return helper;
			}
			#if ARX_PLATFORM == ARX_PLATFORM_WIN32
			helper.append(".exe");
			if(fs::is_regular_file(helper)) {
				return helper;
			}
			#endif
		}
	}
	
	return fs::path(name);
}

bool isFileDescriptorDisabled(int fd) {
	
	ARX_UNUSED(fd);
	
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	
	DWORD names[] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
	if(fd < 0 || fd >= int(std::size(names))) {
		return false;
	}
	
	HANDLE h = GetStdHandle(names[fd]);
	if(h == INVALID_HANDLE_VALUE || h == nullptr) {
		return true; // Not a valid handle
	}
	
	// Redirected to NUL
	BY_HANDLE_FILE_INFORMATION fi;
	return (!GetFileInformationByHandle(h, &fi) && GetLastError() == ERROR_INVALID_FUNCTION);
	
	#else
	
	#if ARX_HAVE_FCNTL && defined(F_GETFD)
	if(fcntl(fd, F_GETFD) == -1 && errno == EBADF) {
		return false; // Not a valid file descriptor
	}
	#endif
	
	#if defined(MAXPATHLEN)
	char path[MAXPATHLEN];
	#else
	char path[64];
	#endif
	
	bool valid = false;
	#if ARX_HAVE_FCNTL && defined(F_GETPATH) && defined(MAXPATHLEN)
	// macOS
	valid = (fcntl(fd, F_GETPATH, path) != -1 && path[9] == '\0');
	#elif ARX_HAVE_READLINK
	// Linux
	const char * names[] = { "/proc/self/fd/0", "/proc/self/fd/1", "/proc/self/fd/2" };
	if(fd >= 0 && fd < int(std::size(names))) {
		valid = (readlink(names[fd], path, std::size(path)) == 9);
	}
	#endif
	
	// Redirected to /dev/null
	return (valid && !memcmp(path, "/dev/null", 9));
	
	#endif
	
}

static std::mutex g_environmentLock;

bool hasEnvironmentVariable(const char * name) {
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	return GetEnvironmentVariable(platform::WideString(name), nullptr, 0) != 0;
	#else
	return std::getenv(name) != nullptr;
	#endif
}

void setEnvironmentVariable(const char * name, const char * value) {
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	SetEnvironmentVariableW(platform::WideString(name), platform::WideString(value));
	#elif ARX_HAVE_SETENV
	setenv(name, value, 1);
	#endif
}

void unsetEnvironmentVariable(const char * name) {
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	SetEnvironmentVariableW(platform::WideString(name), nullptr);
	#elif ARX_HAVE_UNSETENV
	unsetenv(name);
	#endif
}

void EnvironmentLock::lock() {
	g_environmentLock.lock();
	for(size_t i = 0; i < m_count; i++) {
		if(m_overrides[i].name) {
			if(hasEnvironmentVariable(m_overrides[i].name)) {
				// Don't override variables already set by the user
				m_overrides[i].name = nullptr;
			} else if(m_overrides[i].value) {
				setEnvironmentVariable(m_overrides[i].name, m_overrides[i].value);
			} else {
				unsetEnvironmentVariable(m_overrides[i].name);
			}
		}
	}
}

void EnvironmentLock::unlock() {
	for(size_t i = 0; i < m_count; i++) {
		if(m_overrides[i].name) {
			unsetEnvironmentVariable(m_overrides[i].name);
		}
	}
	g_environmentLock.unlock();
}

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
#ifndef LOCALE_SNAME
#define LOCALE_SNAME 0x0000005c
#endif
#ifndef LOCALE_SPARENT
#define LOCALE_SPARENT 0x0000006d
#endif
#endif

std::vector<std::string> getPreferredLocales() {
	
	std::vector<std::string> result;
	
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	
	LCID installerLanguage = 0;
	{
		std::string value;
		if(getSystemConfiguration("InstallerLanguage", value)) {
			installerLanguage = static_cast<LCID>(util::toInt(value).value_or(0));
		}
	}
	
	WideString buffer;
	
	LCID locales[] = { installerLanguage, GetThreadLocale(), LOCALE_USER_DEFAULT, LOCALE_SYSTEM_DEFAULT };
	LCTYPE types[] = { LOCALE_SNAME, LOCALE_SPARENT, LOCALE_SISO639LANGNAME };
	for(LCID locale : locales) {
		if(!locale) {
			continue;
		}
		for(LCTYPE type : types) {
			buffer.allocate(LOCALE_NAME_MAX_LENGTH);
			if(GetLocaleInfoW(locale, type, buffer.data(), buffer.size())) {
				buffer.compact();
				std::string name = buffer.toUTF8();
				boost::to_lower(name);
				if(name.size() > 0 && std::find(result.begin(), result.end(), name) == result.end()) {
					result.push_back(name);
					for(size_t j = 0; j < name.size(); j++) {
						if(!std::isalnum(static_cast<unsigned char>(name[j]))) {
							std::string localename = name.substr(0, j);
							if(std::find(result.begin(), result.end(), localename) == result.end()) {
								result.push_back(localename);
							}
						}
					}
				}
			}
		}
	}
	
	#else
	
	// LANGUAGE is a colon-separated list of preferred languages and overwrites LC_* and LANG
	const char * languages = std::getenv("LANGUAGE");
	if(languages) {
		for(std::string_view locale : util::splitIgnoreEmpty(languages, env_list_seperator)) {
			result.emplace_back(locale);
			boost::to_lower(result.back());
			std::replace(result.back().begin(), result.back().end(), '_', '-');
		}
		size_t end = result.size();
		for(size_t i = 0; i < end; i++) {
			for(size_t j = 0; j < result[i].size(); j++) {
				if(!std::isalnum(static_cast<unsigned char>(result[i][j]))) {
					std::string_view locale = std::string_view(result[i]).substr(0, j);
					if(std::find(result.begin(), result.end(), locale) == result.end()) {
						result.emplace_back(locale);
					}
				}
			}
		}
	}
	
	const char * variables[] = { "LC_ALL", "LC_MESSAGES", "LANG" };
	for(const char * variable : variables) {
		const char * value = std::getenv(variable);
		if(value) {
			std::string buffer = value;
			boost::to_lower(buffer);
			size_t separator = std::string::npos;
			for(size_t i = 0; i < buffer.length(); i++) {
				if(std::isalnum(static_cast<unsigned char>(buffer[i]))) {
					// Normal character
				} else if(separator == std::string::npos && (buffer[i] == '_' || buffer[i] == '-')) {
					buffer[i] = '-';
					separator = i;
				} else {
					buffer.resize(i);
					break;
				}
			}
			if(buffer.empty() || buffer == "c" || buffer == "posix" || separator == 0) {
				continue;
			}
			if(separator != std::string::npos && separator + 1 == buffer.size()) {
				buffer.resize(separator);
				separator = std::string::npos;
			}
			if(std::find(result.begin(), result.end(), buffer) == result.end()) {
				result.push_back(buffer);
				if(separator != std::string::npos) {
					buffer.resize(separator);
					if(std::find(result.begin(), result.end(), buffer) == result.end()) {
						result.push_back(buffer);
					}
				}
			}
		}
	}
	
	#endif
	
	return result;
}

} // namespace platform
