/*
 * Copyright 2013-2022 Arx Libertatis Team (see the AUTHORS file)
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

#include "platform/OS.h"

#include "Configure.h"
#include "platform/Architecture.h"
#include "platform/Platform.h"

#include <cstring>
#include <algorithm>
#include <sstream>
#include <string_view>
#include <vector>

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/predicate.hpp>

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
#include <windows.h>
#if ARX_COMPILER_MSVC && (ARX_ARCH == ARX_ARCH_X86 || ARX_ARCH == ARX_ARCH_X86_64)
#include <intrin.h>
#endif
#endif

#if ARX_HAVE_UNAME
#include <sys/utsname.h>
#endif

#if ARX_HAVE_GET_CPUID && !defined(ARX_INCLUDED_CPUID_H)
#define ARX_INCLUDED_CPUID_H <cpuid.h>
#include ARX_INCLUDED_CPUID_H
#endif

#if ARX_HAVE_SYSCONF || ARX_HAVE_CONFSTR
#include <unistd.h>
#endif

#include "io/fs/FilePath.h"
#include "io/fs/Filesystem.h"

#include "platform/Process.h"
#include "platform/WindowsUtils.h"

#include "util/String.h"

namespace platform {

// Windows-specific functions
#if ARX_PLATFORM == ARX_PLATFORM_WIN32

//! Get a string describing the Windows version
static std::string getWindowsVersionName() {
	
	bool osviValid = false;
	OSVERSIONINFOEXW osvi;
	SYSTEM_INFO si;
	
	ZeroMemory(&si, sizeof(si));
	ZeroMemory(&osvi, sizeof(osvi));
	osvi.dwOSVersionInfoSize = sizeof(osvi);
	
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
	
	typedef LONG (WINAPI * RtlGetVersionPtr)(POSVERSIONINFOW);
	RtlGetVersionPtr RtlGetVersion = nullptr;
	if(ntdll) {
		RtlGetVersion = getProcAddress<RtlGetVersionPtr>(ntdll, "RtlGetVersion");
	}
	if(RtlGetVersion && RtlGetVersion(reinterpret_cast<POSVERSIONINFOW>(&osvi)) == 0) {
		osviValid = true;
	}
	
	#if ARX_COMPILER_MSVC
	#pragma warning(push)
	#pragma warning(disable:4996) // VC12+ deprecates GetVersionEx
	#endif
	if(!osviValid && GetVersionExW(reinterpret_cast<POSVERSIONINFOW>(&osvi))) {
		osviValid = true;
	}
	#if ARX_COMPILER_MSVC
	#pragma warning(pop)
	#endif
	
	// Call GetNativeSystemInfo if supported or GetSystemInfo otherwise.
	typedef void (WINAPI * GetNativeSystemInfoPtr)(LPSYSTEM_INFO);
	GetNativeSystemInfoPtr GetNativeSystemInfo = nullptr;
	if(kernel32) {
		GetNativeSystemInfo = getProcAddress<GetNativeSystemInfoPtr>(kernel32, "GetNativeSystemInfo");
	}
	if(GetNativeSystemInfo) {
		GetNativeSystemInfo(&si);
	} else {
		GetSystemInfo(&si);
	}
	
	std::stringstream os;
	os << "Microsoft Windows"; // Test for the specific product
	
	if(VER_PLATFORM_WIN32_NT != osvi.dwPlatformId || osvi.dwMajorVersion <= 4) {
		osviValid = false;
	}
	
	bool isServer = (osvi.wProductType != VER_NT_WORKSTATION);
	
	#define ARX_WINVER(x, y) ((u64(x) << 32) | u64(y))
	if(osviValid) {
		switch(ARX_WINVER(osvi.dwMajorVersion, osvi.dwMinorVersion)) {
			case ARX_WINVER(6, 2): {
				os << (isServer ? " Server 2012" : " 8");
				break;
			}
			case ARX_WINVER(6, 1): {
				os << (isServer ? " Server 2008 R2" : " 7");
				break;
			}
			case ARX_WINVER(6, 0): {
				os << (isServer ? " Server 2008" : "Windows Vista");
				break;
			}
			case ARX_WINVER(5, 2): {
				if(GetSystemMetrics(SM_SERVERR2)) {
					os << " Server 2003 R2";
				} else if(!isServer && si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
					os << " XP Professional x64 Edition";
				} else {
					os << " Server 2003";
				}
				break;
			}
			case ARX_WINVER(5, 1): {
				os << " XP";
				break;
			}
			case ARX_WINVER(5, 0): {
				os << (isServer ? " 2000 Server" : " 2000");
				break;
			}
			default: {
				os << "  version " << osvi.dwMajorVersion << "." << osvi.dwMinorVersion;
			}
		}
	}
	#undef ARX_WINVER
	
	// Include service pack (if any) and build number
	if(osviValid && osvi.szCSDVersion[0] != L'\0') {
		os << " " << platform::WideString::toUTF8(osvi.szCSDVersion);
	}
	
	if(osviValid) {
		os << " (build " << osvi.dwBuildNumber << ")";
	}
	if(osviValid && osvi.dwMajorVersion >= 6) {
		if(si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
			os << ", 64-bit";
		} else if(si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) {
			os << ", 32-bit";
		}
	}
	
	typedef const char * (CDECL * wine_get_version_ptr)();
	wine_get_version_ptr wine_get_version = nullptr;
	if(ntdll) {
		wine_get_version = getProcAddress<wine_get_version_ptr>(ntdll, "wine_get_version");
	}
	if(wine_get_version) {
		os << " (Wine " << wine_get_version() << ")";
	}
	
	return os.str();
}

#endif // ARX_PLATFORM == ARX_PLATFORM_WIN32


std::string getOSName() {
	
	#if ARX_HAVE_UNAME
	struct utsname uname_buf;
	if(uname(&uname_buf) == 0) {
		return std::string(uname_buf.sysname) + ' ' + uname_buf.release;
	}
	#endif
	
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	// Get operating system friendly name from registry.
	return getWindowsVersionName();
	#elif ARX_PLATFORM == ARX_PLATFORM_LINUX
	return "Linux";
	#elif ARX_PLATFORM == ARX_PLATFORM_MACOS
	return "macOS";
	#elif ARX_PLATFORM == ARX_PLATFORM_BSD
	return "BSD";
	#elif ARX_PLATFORM == ARX_PLATFORM_HAIKU
	return "Haiku";
	#elif ARX_PLATFORM == ARX_PLATFORM_UNIX
	return "UNIX";
	#else
	return std::string();
	#endif
}


std::string getOSArchitecture() {
	
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	
	HANDLE process = GetCurrentProcess();
	
	// IsWow64Process2 is only available starting with Windows 10, version 1511
	if(HMODULE handle = GetModuleHandleW(L"kernel32")) {
		typedef BOOL (WINAPI * IsWow64Process2_t)(HANDLE, USHORT *, USHORT *);
		IsWow64Process2_t IsWow64Process2_p = getProcAddress<IsWow64Process2_t>(handle, "IsWow64Process2");
		USHORT processArch;
		USHORT systemArch;
		if(IsWow64Process2_p && IsWow64Process2_p(process, &processArch, &systemArch)) {
			switch(systemArch) {
				case 0x014c: return ARX_ARCH_NAME_X86;
				case 0x01c0: return ARX_ARCH_NAME_ARM;
				case 0x01c2: return ARX_ARCH_NAME_ARM; // Thumb
				case 0x01c4: return ARX_ARCH_NAME_ARM; // Thumb-2
				case 0x0200: return ARX_ARCH_NAME_IA64;
				case 0x8664: return ARX_ARCH_NAME_X86_64;
				case 0xAA64: return ARX_ARCH_NAME_ARM64;
			}
		}
	}
	
	#if ARX_ARCH == ARX_ARCH_X86 || ARX_ARCH == ARX_ARCH_ARM
	if(platform::isWoW64Process(process)) {
		#if ARX_ARCH == ARX_ARCH_X86
		// Could actually be running on ARM64 using emulation built into Windows
		// But that should be caught with IsWow64Process2
		return ARX_ARCH_NAME_X86_64;
		#else
		return ARX_ARCH_NAME_ARM64;
		#endif
	}
	#endif
	
	return ARX_ARCH_NAME;
	
	#else
	
	#if ARX_HAVE_UNAME
	struct utsname uname_buf;
	if(uname(&uname_buf) == 0) {
		return uname_buf.machine;
	}
	#endif
	
	return std::string();
	
	#endif
	
}


#if ARX_PLATFORM == ARX_PLATFORM_LINUX


/*!
 * Parse key-value pairs from /etc/os-release or `lsb_release -a` to form a
 * pretty distribution name.
 *
 * \param data      The text to parse.
 * \param separator Character used to separate keys and values.
 * \param keys      Keys that should be used in the final name.
 *                  Values are added to the name in order of the keys listed here
 *                  unless the name so far already contains the value.
 *                  Prepend a a '(' character to surround in parentheses before adding it
 *                  to the name (unless the name so far is empty).
 * \param keyCount  Number of entries in the @c keys array.
 */
static std::string parseDistributionName(std::string_view data, const char separator,
                                         std::string_view keys[], size_t keyCount) {
	
	std::vector<std::string_view> values;
	values.resize(keyCount);
	
	for(std::string_view line : util::splitIgnoreEmpty(data, '\n')) {
		
		// Ignore comments
		arx_assert(!line.empty());
		if(line[0] == '#') {
			continue;
		}
		
		// Split key and value
		size_t pos = line.find(separator);
		if(pos == std::string::npos || pos == 0) {
			// Ignore bad syntax
			continue;
		}
		std::string_view key = util::trim(line.substr(0, pos));
		std::string_view value = util::trim(line.substr(pos + 1));
		if(!value.empty() && value.front() == '"') {
			value.remove_prefix(1);
			if(!value.empty() && value.back() == '"') {
				value.remove_suffix(1);
			}
		}
		if(!value.empty() && value.back() == '\\') {
			value.remove_suffix(1);
		}
		
		// Ignore empty values
		if(key.empty() || value.empty() || value == "n/a") {
			continue;
		}
		
		for(size_t i = 0; i < keyCount; i++) {
			
			// Only use the first value for each key type
			if(!values[i].empty()) {
				continue;
			}
			
			std::string_view kkey = keys[i];
			if(!kkey.empty() && kkey[0] == '(') {
				kkey.remove_prefix(1);
			}
			if(key == kkey) {
				values[i] = value;
				break;
			}
		}
		
	}
	
	std::string name;
	for(size_t i = 0; i < keyCount; i++) {
		
		// Skip missing keys
		if(values[i].empty()) {
			continue;
		}
		
		// Skip values that are already part of the name
		if(boost::icontains(name, values[i])) {
			continue;
		}
		
		// Add the new value to the name
		if(name.empty()) {
			name = values[i];
		} else if(!keys[i].empty() && keys[i][0] == '(') {
			name.push_back(' ');
			name.push_back('(');
			name += values[i];
			name.push_back(')');
		} else {
			name.push_back(' ');
			name += values[i];
		}
		
	}
	return name;
}

#endif // ARX_PLATFORM == ARX_PLATFORM_LINUX


std::string getOSDistribution() {
	
	#if ARX_PLATFORM == ARX_PLATFORM_LINUX
	
	// Get distribution information from SystemD's /etc/os-release
	// Spec: https://freedesktop.org/software/systemd/man/os-release.html
	{
		std::string_view keys[] = { "PRETTY_NAME", "NAME", "VERSION", "VERSION_ID" };
		std::string distro = parseDistributionName(fs::read("/etc/os-release"), '=', keys, std::size(keys));
		if(!distro.empty()) {
			return distro;
		}
	}
	
	// Get distribution information from `lsb_release -a` output.
	// Don't parse /etc/lsb-release ourselves unless there is no other way
	// because lsb_release may have distro-specific patches
	{
		const char * args[] = { "lsb_release", "-a", nullptr };
		std::string_view keys[] = { "Description", "Distributor ID", "Release", "(Codename" };
		std::string distro = parseDistributionName(getOutputOf(args), ':', keys, std::size(keys));
		if(!distro.empty()) {
			return distro;
		}
	}
	
	// Fallback for older / non-LSB-compliant distros.
	// Release file list taken from http://linuxmafia.com/faq/Admin/release-files.html
	
	const char * release_files[] = {
		"/etc/annvix-release",
		"/etc/arch-release",
		"/etc/arklinux-release",
		"/etc/aurox-release",
		"/etc/blackcat-release",
		"/etc/cobalt-release",
		"/etc/conectiva-release",
		"/etc/fedora-release",
		"/etc/gentoo-release",
		"/etc/immunix-release",
		"/etc/lfs-release",
		"/etc/linuxppc-release",
		"/etc/mandriva-release",
		"/etc/mandrake-release",
		"/etc/mandakelinux-release",
		"/etc/mklinux-release",
		"/etc/nld-release",
		"/etc/pld-release",
		"/etc/slackware-release",
		"/etc/e-smith-release",
		"/etc/release",
		"/etc/sun-release",
		"/etc/SuSE-release",
		"/etc/novell-release",
		"/etc/sles-release",
		"/etc/tinysofa-release",
		"/etc/turbolinux-release",
		"/etc/ultrapenguin-release",
		"/etc/UnitedLinux-release",
		"/etc/va-release",
		"/etc/yellowdog-release",
		"/etc/debian_release",
		"/etc/redhat-release",
		"/etc/frugalware-release",
		"/etc/altlinux-release",
		"/etc/meego-release",
		"/etc/mageia-release",
		"/etc/system-release",
	};
	for(size_t i = 0; i < std::size(release_files); i++) {
		std::string distro = fs::read(release_files[i]);
		boost::trim(distro);
		if(!distro.empty()) {
			return distro;
		}
	}
	
	const char * version_files[][2] = {
		{ "/etc/debian_version", "Debian " },
		{ "/etc/knoppix_version", "Knoppix " },
		{ "/etc/redhat_version", "RedHat " },
		{ "/etc/slackware-version", "Slackware " },
		{ "/etc/angstrom-version", "Ångström " },
	};
	for(size_t i = 0; i < std::size(version_files); i++) {
		if(fs::exists(version_files[i][0])) {
			std::string distro = version_files[i][1] + fs::read(release_files[i]);
			boost::trim(distro);
			return distro;
		}
	}
	
	// Fallback: parse /etc/lsb-release ourselves
	{
		std::string_view keys[] = { "DISTRIB_DESCRIPTION", "DISTRIB_ID", "DISTRIB_RELEASE", "(DISTRIB_CODENAME" };
		std::string distro = parseDistributionName(fs::read("/etc/lsb-release"), '=', keys, std::size(keys));
		if(!distro.empty()) {
			return distro;
		}
	}
	
	#endif // ARX_PLATFORM == ARX_PLATFORM_LINUX
	
	return std::string();
}

#if ARX_HAVE_CONFSTR && (defined(_CS_GNU_LIBC_VERSION) || defined(_CS_GNU_LIBPTHREAD_VERSION))
static std::string getCLibraryConfigString(int name) {
	
	size_t len = confstr(name, nullptr, 0);
	if(len == 0) {
		return std::string();
	}
	
	std::vector<char> buffer;
	buffer.resize(len);
	len = confstr(name, &buffer.front(), buffer.size());
	if(len == 0) {
		return std::string();
	}
	
	return std::string(&*buffer.begin(), &*--buffer.end());
	
}
#endif

std::string getCLibraryVersion() {
	
	#if ARX_HAVE_CONFSTR && defined(_CS_GNU_LIBC_VERSION)
	return getCLibraryConfigString(_CS_GNU_LIBC_VERSION);
	#elif defined(__GNU_LIBRARY__) || defined(__GLIBC__)
	return "glibc";
	#elif defined(__BIONIC__)
	return "Bionic";
	#elif defined(__UCLIBC__)
	return "uClibc";
	#else
	return std::string();
	#endif
	
}

std::string getThreadLibraryVersion() {
	
	#if ARX_HAVE_CONFSTR && defined(_CS_GNU_LIBPTHREAD_VERSION)
	return getCLibraryConfigString(_CS_GNU_LIBPTHREAD_VERSION);
	#else
	return std::string();
	#endif
	
}

std::string getCPUName() {
	
	#if (ARX_ARCH == ARX_ARCH_X86 || ARX_ARCH == ARX_ARCH_X86_64) \
	    && (ARX_COMPILER_MSVC || ARX_HAVE_GET_CPUID)
	
	#if ARX_COMPILER_MSVC
	int cpuinfo[4] = { 0 };
	__cpuid(cpuinfo, 0x80000000);
	int max = cpuinfo[0];
	#elif ARX_HAVE_GET_CPUID_MAX
	unsigned cpuinfo[4] = { 0 };
	int max = __get_cpuid_max(0x80000000, nullptr);
	#else
	unsigned cpuinfo[4] = { 0 };
	__get_cpuid(0x80000000, &cpuinfo[0], &cpuinfo[1], &cpuinfo[2], &cpuinfo[3]);
	int max = cpuinfo[0];
	#endif
	
	const int first = 0x80000002;
	int count = std::min(std::max(max + 1, first) - first, 3);
	
	std::string name;
	name.resize(count * sizeof(cpuinfo));
	
	for(int i = 0; i < count; i++) {
		#if ARX_COMPILER_MSVC
		__cpuid(cpuinfo, first + i);
		#else
		__get_cpuid(first + i, &cpuinfo[0], &cpuinfo[1], &cpuinfo[2], &cpuinfo[3]);
		#endif
		std::memcpy(&*name.begin() + i * sizeof(cpuinfo), cpuinfo, sizeof(cpuinfo));
	}
	
	size_t p = name.find('\0');
	if(p != std::string::npos) {
		name.resize(p);
	}
	boost::trim(name);
	return name;
	
	#elif ARX_PLATFORM == ARX_PLATFORM_LINUX
	
	std::string cpuinfo = fs::read("/proc/cpuinfo");
	
	for(std::string_view line : util::splitIgnoreEmpty(cpuinfo, '\n')) {
		
		size_t sep = line.find(':');
		if(sep == std::string::npos) {
			continue;
		}
		
		std::string_view label = util::trim(line.substr(0, sep));
		if(label != "model name" && label != "Processor") {
			continue;
		}
		
		std::string_view name = util::trim(line.substr(sep + 1));
		if(!name.empty()) {
			return std::string(name);
		}
		
	}
	
	return { };
	
	#else
	return { };
	#endif
}

MemoryInfo getMemoryInfo() {
	
	MemoryInfo memory = { 0, 0 };
	
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	{
		MEMORYSTATUSEX status;
		status.dwLength = sizeof(status);
		if(GlobalMemoryStatusEx(&status)) {
			memory.total = status.ullTotalPhys;
			memory.available = status.ullAvailPhys;
			return memory;
		}
	}
	#endif
	
	#if ARX_PLATFORM == ARX_PLATFORM_LINUX
	{
		// sysinfo(2) does not report memory used for cache :/ - parse /proc/meminfo instead
		std::string meminfo = fs::read("/proc/meminfo");
		u64 total = u64(-1), free = u64(-1), buffers = u64(-1), cached = u64(-1);
		for(std::string_view line : util::splitIgnoreEmpty(meminfo, '\n')) {
			
			size_t sep = line.find(':');
			if(sep == std::string::npos) {
				continue;
			}
			
			size_t end = line.find("kB", sep + 1);
			if(end == std::string::npos) {
				continue;
			}
			
			std::string_view value = util::trim(line.substr(sep + 1, end - sep - 1));
			
			u64 number = 0;
			try {
				number = boost::lexical_cast<u64>(value) * u64(1024);
			} catch(...) {
				continue;
			}
			
			std::string_view label = util::trim(line.substr(0, sep));
			if(label == "MemTotal") {
				total = number;
			} else if(label == "MemFree") {
				free = number;
			} else if(label == "Buffers") {
				buffers = number;
			} else if(label == "Cached") {
				cached = number;
			} else {
				continue;
			}
			
			if(total != u64(-1) && free != u64(-1) && buffers != u64(-1) && cached != u64(-1)) {
				memory.total = total;
				memory.available = free + buffers + cached;
				return memory;
			}
			
		}
	}
	#endif
	
	#if ARX_HAVE_SYSCONF && defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
	{
		long pages = sysconf(_SC_PHYS_PAGES);
		long pagesize = sysconf(_SC_PAGESIZE);
		if(pages > 0 && pagesize > 0) {
			memory.total = u64(pages) * u64(pagesize);
		}
	}
	#endif
	
	return memory;
}

} // namespace platform
