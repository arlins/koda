
# KODA

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Standard](https://img.shields.io/badge/C%2B%2B-14%20%7C%2017%20%7C%2020-b30047.svg)](https://en.cppreference.com/w/cpp/14)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Darwin%20%7C%20Linux%20%7C%20Android%20%7C%20HarmonyOS-lightgrey.svg)]()

(Coming soon...)

KODA is a header-only cross-platform C++14 (or later) infrastructure library based on the STL. It provides lightweight yet powerful components for Async Tasks, RunLoop/Event Dispatching, IPC, and SigSlot. Seamlessly supports Windows, Darwin (iOS/macOS), Linux, Android, HarmonyOS, and other Unix-like systems with zero external dependencies

* **Developer:** Arlin (arlins.dps@gmail.com)
* **Requirements:** C++14 or later.

---

## 🚀 Modules & Components

KODA architecture is cleanly decoupled into 5 core modules. Below is the precise component breakdown and its exact underlying design purpose:

### 1. Base (Utilities)

| Component | Description |
| :--- | :--- |
| `base/kd_random.h` | Random number generator. |
| `base/kd_scopeguard.h` | RAII-based scope protection. |
| `base/kd_utils.h` | Basic utility wrappers. |
| `base/kd_str.h` | String operations. |
| `base/kd_typecast.h` | Support for implicit type conversion. |
| `base/kd_typetraits.h`| STL typetraits extensions. |
| `base/kd_typename.h` | Get type name as a string. |
| `base/kd_memory.h` | Memory operations. |
| `base/kd_anyfunction.h`| Store and call any type of function. |
| `base/kd_memberaccessor.h` | Access private members and methods of classes. |
| `base/kd_trackpointer.h` | Object lifecycle tracker (single-thread only). |

### 2. Async (Concurrency)

| Component | Description |
| :--- | :--- |
| `async/kd_spinlock.h` | Spinlock. |
| `async/kd_fastmutex.h` | High-performance mutex for single-process. |
| `async/kd_namedmutex.h`| Mutex for cross-process use. |
| `async/kd_namedevent.h`| Named event for cross-process thread synchronization. |
| `async/kd_operation.h` | Task queue similar to Darwin's NSOperationQueue. |
| `async/kd_delayscheduler.h` | Delayed task scheduler. |

### 3. IPC (Inter-Process Communication)

| Component | Description |
| :--- | :--- |
| `ipc/kd_ipc.h` | IPC connection and session management. |
| `ipc/kd_sharedmemory.h` | Shared memory implementation. |

### 4. RunLoop (Event Loop)

| Component | Description |
| :--- | :--- |
| `runloop/kd_runloop.h` | Cross-platform event loop (uses IOCP, epoll, kqueue). |

### 5. SigSlot (Messaging)

| Component | Description |
| :--- | :--- |
| `sigslot/kd_sigslot.h` | Qt-like type-safe signal and slot system. |

---

## 🏁 Quick Start & Initialization

### 1. Initialization & Sandbox Path Setup
Before calling any KODA components, you **must** initialize the framework via `kd::startup()` at your application's entry point, and release resources via `kd::shutdown()` before exiting.

To comply with strict **OS Sandbox / Permission restrictions**, you must provide dedicated writable directories on specific platforms to prevent security blocking:

```cpp
#include "koda/koda.h"

int main() {
    // Configuration Guidelines for OS Sandboxes:
    // - Android / HarmonyOS: BOTH paths must be specified within a writable directory to avoid permission restrictions.
    // - Darwin (iOS / macOS): crossProcessDir MUST be set to a directory including the AppGroup path to work across apps.
    // - You can also specify these paths to override the default directories automatically defined by KODA.
    std::string inProcessDir = "/path/to/writable/app/internal/dir";
    std::string crossProcessDir = "/path/to/writable/shared/group/dir";

    // 1. Initialize KODA engine
    kd::startup(inProcessDir, crossProcessDir);

    // ... Your Main Application Business Logic ...

    // 2. Clean up resources before exit
    kd::shutdown();
    return 0;
}

```

### 2. Advanced Diagnostic Tools (`KD_DEBUGGING_TOOLS`)

To inspect synchronization and concurrency issues early during development, define `KD_DEBUGGING_TOOLS` before including KODA headers. This enables built-in diagnostic instruments such as **Self-Deadlock detection** in debug configurations:

```cpp
// Define this macro to unlock KODA's internal debugging tools
#define KD_DEBUGGING_TOOLS
#include "koda/koda.h"

```

---

## 📦 Linker Dependencies

Since KODA is a **header-only** library, you do not need to build any source binaries. Simply include the `koda/koda.h` umbrella header and link against your target platform's native system libraries (both static and dynamic linking are fully supported without restriction):

### 💻 Windows

* `kernel32.lib`
* `user32.lib`
* `advapi32.lib`

### 🐧 Linux

* `libstdc++.a` / `.so`
* `libpthread.a` / `.so`
* `librt.a` / `.so`

### 🍏 Darwin (macOS / iOS / ...)

* `libc++.tbd`
* `libpthread.tbd`
* `librt.tbd`
* `CoreFoundation.framework`

### 🤖 Android

* `libc++_static.a` / `.so`
* `libpthread.a` / `.so`
* `librt.a` / `.so`

### 🔴 HarmonyOS (OpenHarmony)

* `libc++.a` / `.so`
* `libpthread.a` / `.so`

---

## 📜 License

This project is licensed under the MIT License
