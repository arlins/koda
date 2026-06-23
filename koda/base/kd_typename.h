/* **************************************************************
 Created by: Arlin (arlins.dps@gmail.com).
 This file implements the function of retrieving a string based on its type.
************************************************************** */

#pragma once

#include <string>
#include <typeinfo>
#include <sstream>
#include <cstdlib>

#if defined(KD_COMPILER_MSVC) // MSVC
#elif defined(KD_COMPILER_GCC) || defined(KD_COMPILER_CLANG) // GCC/Clang
#include <cxxabi.h> // type_name
#elif defined(KD_COMPILER_MINGW) //MinGW
#endif

#include "koda/kd_global.h"
#include "koda/base/kd_str.h"

// =========================
// Pretty type name
// =========================


__NAMESPACE_KD_BEGIN

// type_name2 
// Output type string, used for debugging purposes
template<typename... Types>
std::string type_name2() {
	std::string str;

	// __PRETTY_FUNCTION__: std::string kd::util::dump_type_name() [Types = <int>]
	const char* msvc_del_str0 = "class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> >";
	const char* msvc_del_str1 = "__cdecl kd::util::";
	const char* gcc_clang_del_str0 = "std::string kd::util::dump_type_name() ";
	const char* gcc_clang_del_str1 = "[Types = <";
	const char* gcc_clang_del_str2 = ">]";

#if defined(KD_COMPILER_MSVC) // msvc
	str = std::string(__FUNCSIG__);
	str = str_replace(str, msvc_del_str0, "");
	str = str_replace(str, msvc_del_str1, "");
#elif defined(KD_COMPILER_GCC) && !defined(KD_COMPILER_CLANG) // gcc
	str = std::string(__PRETTY_FUNCTION__);
	str = str_replace(str, gcc_clang_del_str0, "");
	str = str_replace(str, gcc_clang_del_str1, "");
	str = str_replace(str, gcc_clang_del_str2, "");
#elif defined(KD_COMPILER_CLANG) // clang
	str = std::string(__PRETTY_FUNCTION__);
	str = str_replace(str, gcc_clang_del_str0, "");
	str = str_replace(str, gcc_clang_del_str1, "");
	str = str_replace(str, gcc_clang_del_str2, "");
#elif defined(KD_COMPILER_MINGW) // mingw
	str = std::string(__PRETTY_FUNCTION__);
	str = str_replace(str, gcc_clang_del_str0, "");
	str = str_replace(str, gcc_clang_del_str1, "");
	str = str_replace(str, gcc_clang_del_str2, "");
#else
	str = std::string("dump_type_name(): Unsupported compiler");
#endif

	// type_name2<int>(void)
	str = str_replace(str, "type_name2", "");
	str = str_replace(str, "(void)", "");

	return str;
};

// Trigger a compilation error to obtain the variable type 
// by instantiating an undefined template
// dump_type_error<decltype(x)> t;
template<typename... T>
class dump_type_error;


// Output type information
// type_name_internal
template <typename T>
std::string type_name_internal() {

#if defined(KD_COMPILER_MSVC) // MSVC
	return std::string(typeid(T).name()); // MSVC will remove the ref and cv of type
#elif defined(KD_COMPILER_GCC) || defined(KD_COMPILER_CLANG) // GCC/Clang
	int status;
	const char* tname = typeid(T).name();
	char* demangled = abi::__cxa_demangle(tname, nullptr, nullptr, &status);
	std::string name = (status == 0) ? demangled : tname;
	free(demangled);
	return name;
#elif defined(KD_COMPILER_MINGW) //MinGW
	return std::string(typeid(T).name());
#else
	return std::string(typeid(T).name());
#endif

}

// type_name_impl
// forward declaration
template<typename... Types>
struct type_name_impl;

// recursion termination
template<>
struct type_name_impl<> {
	static std::string str() { return ""; }
};

// recursion expansion
template<typename T, typename... Types>
struct type_name_impl<T, Types...> {
	static std::string str() {
		std::ostringstream oss;
		oss << type_name_internal<T>();
		if (sizeof...(Types) > 0) {
			oss << ", " << type_name_impl<Types...>::str();
		}

		return oss.str();
	}
};

// type_name 
// Obtain the type string
// type_name<int, double>(), type_name<Types...>()
template<typename... Types>
std::string type_name() { return type_name_impl<Types...>::str(); }


__NAMESPACE_KD_END


