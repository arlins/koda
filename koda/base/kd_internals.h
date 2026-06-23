#pragma once
#include <stdlib.h>
#include <string>
#include "koda/kd_global.h"
#include "koda/base/kd_utils.h"

#ifdef KD_OS_WIN
#include <windows.h>
#include <direct.h>  // _mkdir
#include <io.h> // _access
#else
#include <unistd.h>
#include <sched.h>
#include <sys/stat.h>
#include <errno.h>
#endif

__NAMESPACE_KD_BEGIN

namespace inter_detail {

inline std::string defaultSharedDir() {
	std::string baseDir;

#ifdef KD_OS_WIN
	char tempPath[MAX_PATH];
	DWORD pathLen = GetTempPathA(MAX_PATH, tempPath);
	if (pathLen > 0 && pathLen < MAX_PATH) {
		baseDir = tempPath;
	}
#else
	// Get default writable dir of OS
	// TMPDIR will return sanboxed temp dir in iOS/Mac
	const char* envTmp = std::getenv("TMPDIR");
	if (envTmp && access(envTmp, W_OK) == 0) {
		baseDir = envTmp;
	} else {
		if (access("/tmp", W_OK) == 0) {
			baseDir = "/tmp";
		}
	}
#endif

	return baseDir;
}

inline std::string& getInProcessBaseSharedDir() {
	static std::string s_inDir; 
	return s_inDir; 
}

inline std::string& getCrossProcessBaseSharedDir() {
	static std::string s_crossDir; 
	return s_crossDir; 
}

}; // namespace inter_detail

// Get in-process shared directory, sharedDir/subPath, without ending '/'
inline std::string inProcessSharedDir(const std::string& subPath = "") {
	std::string baseDir = inter_detail::getInProcessBaseSharedDir();

#if !defined(KD_OS_ANDROID) && !defined(KD_OS_OHOS)
	// It can be safely obtained on Windows/Darwin/Linux
	if (baseDir.empty()) {
		baseDir = inter_detail::defaultSharedDir();
	}
#endif

	KD_ASSERT_M(!baseDir.empty(),
		"No writable in-process shared directory found. "
		"Calling kd::startup() to fix this.");

	std::string fullPath;
	if (!baseDir.empty()) {
		fullPath = kd::join_path(baseDir, "koda/inprocess");
		fullPath = kd::join_path(fullPath, subPath);
		kd::create_directory(fullPath);
	}

	return fullPath;
}

// Get cross-process shared directory, sharedDir/subPath, without ending '/'
inline std::string crossProcessSharedDir(const std::string& subPath = "") {
	std::string baseDir = inter_detail::getCrossProcessBaseSharedDir();

#if !defined(KD_OS_DARWIN) && !defined(KD_OS_ANDROID) && !defined(KD_OS_OHOS)
	if (baseDir.empty()) {
		baseDir = inter_detail::defaultSharedDir();
	}
#endif

	KD_ASSERT_M(!baseDir.empty(),
		"No writable cross-process shared directory found. "
		"Calling kd::startup() to fix this.");

	std::string fullPath;
	if (!baseDir.empty()) {
		fullPath = kd::join_path(baseDir, "koda/crossprocess");
		fullPath = kd::join_path(fullPath, subPath);
		kd::create_directory(fullPath);
	}

	return fullPath;
}

__NAMESPACE_KD_END