/* **********************************************************************
Created by: Arlin (arlins.dps@gmail.com).

[!] The KODA library
KODA is a header-only cross-platform C++ library based on the STL. It requires C++14 
or later and supports Windows/Darwin(iOS/macOS...)/Linux/Android/HarmonyOS,
and Other Unix-like systems.

[!] Modules:
1. Base: Includes basic C++ extension libraries
2. Async: Includes asynchronous programming libs such as mutex/event/thread-pools
3. IPC: Includes shared memory and IPC implementations based on pipes/UDS, etc.
4. RunLoop: A cross-platform RunLoop implementation.
5. SigSlot: A Qt-like sigslot implementation.

[!] Notes:
1. Setup before using KODA
	Before using KODA, It must be initialized by calling kd::startup() at the app entry point
	and clean up by calling kd::shutdown() when the app exits.

2. #define KD_DEBUGGING_TOOLS
	Define KD_DEBUGGING_TOOLS to enable KODA debugging tools
	to support checking for issues such as Self-DeadLock in debug mode.

3. Libs dependencies
	- Windows: kernel32.lib | user32.lib | advapi32.lib | winmm.lib
	- Linux: libstdc++.a | libpthread.a | librt.a
	- Darwin(macOS/iOS...): librt.tbd | libpthread.tbd | libc++.tbd | CoreFoundation.framework
	- Android: libc++_static.a | libpthread.a | librt.a
	- HarmonyOS: libc++.a | libpthread.a

	All dependent libraries can be either static or dynamic; there are no restrictions.

*********************************************************************** */

#pragma once
#include "kd_global.h"

// base
#include "base/kd_random.h"
#include "base/kd_scopeguard.h"
#include "base/kd_utils.h"
#include "base/kd_str.h"
#include "base/kd_typecast.h"
#include "base/kd_typetraits.h"
#include "base/kd_typename.h"
#include "base/kd_memory.h"
#include "base/kd_anyfunction.h"
#include "base/kd_memberaccessor.h"
#include "base/kd_trackpointer.h"

// async
#include "async/kd_spinlock.h"
#include "async/kd_fastmutex.h"
#include "async/kd_namedmutex.h"
#include "async/kd_namedevent.h"
#include "async/kd_operation.h"
#include "async/kd_delayscheduler.h"

// ipc
#include "ipc/kd_ipc.h"
#include "ipc/kd_sharedmemory.h"

// runloop
#include "runloop/kd_runloop.h"

// sigslot
#include "sigslot/kd_sigslot.h"


//
// Initialize and clean up KODA. 
// It must be initialized by calling kd::startup() at the app entry point 
// and shut down by calling kd::shutdown() when the app exits.
//
//	About inProcessDir
//	inProcessDir specifies the writable directory within in-process to avoid file
//	permission restrictions imposed by the sandbox system. It is used by 
// IPCConnection, etc. inProcessDir must be specified on Android/HarmonyOS.
//
//	About crossProcessDir
//	crossProcessDir specifies the writable directory within across-process to avoid file
//	permission restrictions imposed by the sandbox system. It is used by NamedMutex, etc.
//	crossProcessDir must be specified on Android/HarmonyOS/Darwin. On Darwin, you 
// must specify the crossProcessDir that includes the AppGroup path to works correctly 
// within across-process.
//
//	You can specify inProcessDir/crossProcessDir to change the default directory on the platforms
//	which KODA has automatically specified inside.
//

__NAMESPACE_KD_BEGIN
// Initialize KODA
inline void startup(const std::string& inProcessDir = "", const std::string& crossProcessDir = "") {
#if defined(KD_OS_ANDROID) || defined(KD_OS_OHOS)
	KD_ASSERT_M(!inProcessDir.empty(),
		"crossProcessDir must be specified within a writable directory "
		"on Android/HarmonyOS");
#endif

#if defined(KD_OS_ANDROID) || defined(KD_OS_OHOS) || defined(KD_OS_DARWIN)
	KD_ASSERT_M(!crossProcessDir.empty(),
		"crossProcessDir must be specified within a writable directory"
		"on Android/HarmonyOS/Darwin");
#endif

	inter_detail::getInProcessBaseSharedDir() = inProcessDir;
	inter_detail::getCrossProcessBaseSharedDir() = crossProcessDir;
	OperationMainQueue::startup();
}

// Clean up the KODA
inline void shutdown() {
	OperationMainQueue::shutdown();
}
__NAMESPACE_KD_END