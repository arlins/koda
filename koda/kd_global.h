/** *************************************************************
Created by: Arlin (arlins.dps@gmail.com).
This file contains global macro definitions for the koda library.
*****************************************************************/

#pragma once

#define __KD_MIN_CXX_VER__ 201402L // C++ 14
#if __cplusplus < __KD_MIN_CXX_VER__
#error "Koda require C++14 or later. Please compile with -std=c++14 or higher."
#endif

// ===============================
// Internal defines
// ===============================

#define __NAMESPACE_KD_BEGIN namespace kd {
#define __NAMESPACE_KD_END };


// ===============================
// DEBUG
// ===============================

#if !defined(NDEBUG) || defined(_DEBUG) || defined(DEBUG)
#define KD_DEBUG 1
#endif


// ===============================
// CXX
// ===============================

#define KD_CXX __cplusplus

#define KD_CXX20 202002L
#define KD_CXX17 201703L
#define KD_CXX14 201402L
#define KD_CXX11 201103L
#define KD_CXX98 199711L


// ==================================
// OS
// ==================================

// Windows
#if defined(_WIN32) || defined(__WIN32__) || defined(_WIN64) 
#define KD_OS_WIN 1 // Win32
#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY != 100 /*WINAPI_FAMILY_DESKTOP_APP*/)
#define KD_OS_WINRT 1 // WinRT
#endif
// Apple: iOS / macOS / WatchOS / tvOS
#elif defined(__APPLE__) 

#include <TargetConditionals.h> // For TARGET_OS_X
#define KD_OS_DARWIN 1 // Apple

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#define KD_OS_iOS 1 // iOS
#elif defined(TARGET_OS_WATCH) && TARGET_OS_WATCH
#define KD_OS_WATCHOS 1 // WatchOS
#elif defined(TARGET_OS_TV) && TARGET_OS_TV
#define KD_OS_TVOS 1 // tvOS
#elif defined(TARGET_OS_OSX) && TARGET_OS_OSX
#define KD_OS_MACOS 1 // macOS
#endif

#if defined(TARGET_OS_MAC) && TARGET_OS_MAC
#define KD_OS_DARWIN_NATIVE
#endif

// HarmonyOS / OpenHarmony
#elif defined(__OHOS__) || defined(__OpenHarmony__) 
#define KD_OS_OHOS 1
// Android
#elif defined(__ANDROID__) || defined(ANDROID) 
#define KD_OS_ANDROID 1 // Android
#define KD_OS_LINUX 1 // Linux
// Linux
#elif defined(__linux__) || defined(__linux) 
#define KD_OS_LINUX 1 // Linux
// Unix-like
#elif defined(__FreeBSD__) || defined(__DragonFly__) || defined(__FreeBSD_kernel__)
#define KD_OS_FREEBSD 1
#define KD_OS_BSD 1
#elif defined(__QNXNTO__)
#define KD_OS_QNX 1
#elif defined(__HAIKU__)
#define KD_OS_HAIKU 1
#endif

// UNIX / POSIX
#if !defined(KD_OS_WIN)
#define KD_OS_UNIX 1
#include <unistd.h> // For _POSIX_VERSION
#if defined(_POSIX_VERSION)
#define KD_OS_POSIX 1
#endif
#endif


// ==================================
// 32-64 bits
// ==================================

#if defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(__LP64__)
#define KD_BIT_64 1
#elif defined(_M_IX86) || defined(__i386__) || defined(__arm__)
#define KD_BIT_32 1
#endif

// ==================================
// Arch
// ==================================

// x86
#if defined(_M_IX86) || defined(__i386__)
#define KD_ARCH_X86 1
#define KD_ARCH_X86_32 1
#elif defined(_M_X64) || defined(__x86_64__)
#define KD_ARCH_X86 1
#define KD_ARCH_X86_64 1
#endif

// ARM
#if defined(_M_ARM) || defined(__arm__)
#define KD_ARCH_ARM 1
#define KD_ARCH_ARM_32 1
#elif defined(_M_ARM64) || defined(__aarch64__)
#define KD_ARCH_ARM 1 //  Apple Silicon (ARM64)
#define KD_ARCH_ARM_64 1
#endif

// MIPS
#if defined(__mips__)
#define KD_ARCH_MIPS 1
#endif

// PowerPC
#if defined(__powerpc__) || defined(__ppc__)
#define KD_ARCH_PPC 1
#endif

// RISC-V
#if defined(__riscv)
#define KD_ARCH_RISCV 1
#endif

// ======================================
// Compiler
// ======================================

#if defined(_MSC_VER)
#define KD_COMPILER_MSVC 1
#define KD_COMPILER_VER _MSC_VER
#elif defined(__clang__)
#define KD_COMPILER_CLANG 1
#define KD_COMPILER_VER (__clang_major__ * 100 + __clang_minor__)
#elif defined(__GNUC__)
#define KD_COMPILER_GCC 1
#define KD_COMPILER_VER (__GNUC__ * 100 + __GNUC_MINOR__)
#elif defined(__MINGW32__)
#define KD_COMPILER_MINGW
#define KD_COMPILER_VER (__GNUC__ * 100 + __GNUC_MINOR__)  
#elif defined(__INTEL_COMPILER)
#define KD_COMPILER_INTEL 1
#define KD_COMPILER_VER __INTEL_COMPILER
#endif

// ======================================
// Disable warning
// ======================================

#define KD_MAKESTR(s) #s
#define KD_JOINSTR(x,y) KD_MAKESTR(x ## y)
#define KD_DOPRAGMA(x) _Pragma(#x)

#if defined(KD_COMPILER_GCC) // GCC
#define KD_DISABLE_WARNING_PUSH                KD_DOPRAGMA(GCC diagnostic push)
#define KD_DISABLE_WARNING_POP                  KD_DOPRAGMA(GCC diagnostic pop)
#define KD_DISABLE_WARNING(warningName)   KD_DOPRAGMA(GCC diagnostic ignored warningName)

#elif defined(KD_COMPILER_CLANG) // Clang
#define KD_DISABLE_WARNING_PUSH                KD_DOPRAGMA(clang diagnostic push)
#define KD_DISABLE_WARNING_POP                  KD_DOPRAGMA(clang diagnostic pop)
#define KD_DISABLE_WARNING(warningName)   KD_DOPRAGMA(clang diagnostic ignored warningName)

#elif defined(KD_COMPILER_MSVC) // MSVC
#define KD_DISABLE_WARNING_PUSH                 __pragma(warning(push))
#define KD_DISABLE_WARNING_POP                   __pragma(warning(pop))
#define KD_DISABLE_WARNING(warningNum)      __pragma(warning(disable: warningNum))
#else
#define KD_DISABLE_WARNING_PUSH          
#define KD_DISABLE_WARNING_POP            
#define KD_DISABLE_WARNING(warning)       
#endif


// ==================================
// Common Tools
// ==================================

// KD_UNUSED
#define KD_UNUSED(x) (void)x;

// The object accompanies the entire application lifecycle 
// and does not need to be released, thus avoiding the problem of uncontrollable 
// timing of static variable destruction when the program exits.
#define KD_LEAKY_SINGLETON_DEFINE(obj) do{ }while(0);

// KD_ASSERT
#ifdef KD_DEBUG
#include <assert.h>
#define KD_ASSERT(x) assert(x)
#define KD_ASSERT_M(cond, msg) \
do { \
    if (!(cond)) { \
        assert(false && msg); \
    } \
} while (false)
#else 
#define KD_ASSERT(x) do {} while (false);
#define KD_ASSERT_M(cond, msg) do {} while (false);
#endif // KD_DEBUG

#define KD_STATIC_ASSERT(cond) static_assert(bool(cond), #cond);
#define KD_STATIC_ASSERT_M(cond, msg) static_assert(bool(cond), msg);

// KD_SAFE_DELETE
#define KD_SAFE_DELETE(ptr) \
if (ptr !=  nullptr) { \
    delete ptr; \
    ptr = nullptr; \
}

// KD_PRETTY_FUNC: Pretty function name
#if defined(KD_COMPILER_GCC) || defined(KD_COMPILER_CLANG)
#define KD_PRETTY_FUNC __PRETTY_FUNCTION__
#elif defined(KD_COMPILER_MSVC)
#define KD_PRETTY_FUNC __FUNCSIG__
#else
#define KD_PRETTY_FUNC __func__  
#endif

// KD_DISABLE_COPY MOVE
#define KD_DISABLE_COPY(Class) \
    Class(const Class &) = delete;\
    Class &operator=(const Class &) = delete;

#define KD_DISABLE_MOVE(Class) \
    Class(Class &&) = delete; \
    Class &operator=(Class &&) = delete;

// ======================================
// Frameworks
// ======================================

// GLIB
#if !defined(KD_HAS_GLIB)
#if defined(__G_LIB_H__) || defined(GLIB_MAJOR_VERSION)
#define KD_HAS_GLIB 1
#endif
#endif

// GTK
#if !defined(KD_HAS_GTK)
#if defined(__GTK_H__) || defined(GTK_MAJOR_VERSION)
#define KD_HAS_GTK 1
#endif
#endif

// Qt
#ifdef QT_VERSION
#define KD_HAS_QT 1
#define KD_QT_VERSION QT_VERSION
#define KD_QT_MAJOR_VERSION ((QT_VERSION >> 16) & 0xFF)
#define KD_QT_MINOR_VERSION ((QT_VERSION >> 8) & 0xFF)
#define KD_QT_PATCH_VERSION (QT_VERSION & 0xFF)
#define KD_QT_VERSION_CHECK QT_VERSION_CHECK // QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)) Qt 5.12+
#endif

// KD_HAS_OBJC
#ifdef KD_OS_DARWIN
#if defined(__has_feature)
    #if __has_feature(blocks)
    #define KD_HAS_OBJC 1
    #endif
#elif defined(__OBJC__)
    #define KD_HAS_OBJC 1
#endif
#endif // KD_OS_DARWIN

// ==================================
// CPU Instructions
// ==================================

// KD_PAUSE_ASM
// KD_PAUSE_ASM is used to pause the CPU for dozens or hundreds 
// of CPU cycles of decoding instructions.
#if defined(KD_ARCH_X86)
#if defined(KD_COMPILER_MSVC)
#include <intrin.h>
#define KD_PAUSE_ASM() _mm_pause()
#else
#define KD_PAUSE_ASM() __builtin_ia32_pause()
#endif
#elif defined(KD_ARCH_ARM)
#if defined(KD_COMPILER_MSVC)
#define KD_PAUSE_ASM() __yield()
#else
#define KD_PAUSE_ASM() __asm__ __volatile__("yield")
#endif
#else
#define KD_PAUSE_ASM() ((void)0)
#endif