/* **************************************************************
 Created by: Arlin (arlins.dps@gmail.com).
 This file implements String-related APIs
************************************************************** */

#pragma once
#include <iostream>
#include <string>
#include <cstdio>
#include <cwchar>
#include <vector>
#include <utility> // std::forward
#include <sstream>
#include <cctype>
#include <codecvt>
#include <locale>
#include <iomanip>
#include <cstdint>
#include "koda/kd_global.h"
#include "koda/base/kd_typetraits.h"

__NAMESPACE_KD_BEGIN

// Character formatting
// std::string str = format_str("s=%s", "a");
template<typename ... Args>
std::string format_str(const std::string& format, Args&&... args) {
	KD_DISABLE_WARNING_PUSH
#ifndef KD_COMPILER_MSVC
    KD_DISABLE_WARNING("-Wformat-security")
    KD_DISABLE_WARNING("-Wformat-nonliteral")
#endif

    int size = std::snprintf(nullptr, 0, format.c_str(), std::forward<Args>(args) ...); // Calc size
	if (size < 0) {
		return std::string(); // Failed
	}

	std::vector<char> buf(size + 1); // Extra space for '\0'
	std::snprintf(buf.data(), buf.size(), format.c_str(), std::forward<Args>(args) ...);

	KD_DISABLE_WARNING_POP

    return std::string(buf.data(), size); // We don't want the '\0' inside
}

// Character formatting
// std::wstring str = format_str(L"s=%ls", L"a");
template<typename ... Args>
std::wstring format_str(const std::wstring& format, Args&& ... args) {
	int size = std::swprintf(nullptr, 0, format.c_str(), std::forward<Args>(args) ...); // Calc size
	if (size < 0) {
		return std::wstring(); // Failed
	}

	std::vector<wchar_t> buf(size + 1); // Extra space for '\0'
	std::swprintf(buf.data(), buf.size(), format.c_str(), std::forward<Args>(args) ...);
	return std::wstring(buf.data(), size); // We don't want the '\0' inside
}

// String replacement
inline std::string str_replace(const std::string& _str, const std::string& from, const std::string& to) {
	size_t pos = 0;
	std::string str = _str;

	while ((pos = str.find(from, pos)) != std::string::npos) {
		str.replace(pos, from.length(), to);
		pos += to.length();
	}

	return str;
}

// Any to string
template <typename T>
std::string to_string(T&& value) {
	std::ostringstream oss;
	oss << std::forward<T>(value);
	return oss.str();
}

template <typename T>
std::string to_string(const T& value) {
	std::ostringstream oss;
	oss << value;
	return oss.str();
}

// Format value to hex
template <typename T>
std::string hex_str(T&& val) {
	std::ostringstream oss;
	oss << std::hex << std::forward<T>(val);

	return oss.str();
}

// String hashing(16)
// Based on the FNV-1a Hash algorithm, and return size is 16.
inline std::string str_hash(const std::string& s) {
	const uint64_t FNV_offset_basis = 0xcbf29ce484222325;
	const uint64_t FNV_prime = 0x100000001b3;
	uint64_t hash = FNV_offset_basis;

	for (char c : s) {
		hash ^= static_cast<uint8_t>(c);
		hash *= FNV_prime;
	}

	std::stringstream ss;
	ss << std::hex << std::setw(16) << std::setfill('0') << hash;
	return ss.str();
}

// String to wstring
inline std::wstring str2wstr(const std::string& str) {
	try {
		std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
		return converter.from_bytes(str);
	} catch (...) {
		return L""; // Failed
	}
}

// WString to string
inline std::string wstr2str(const std::wstring& wstr) {
	try {
		std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
		std::string utf8_str = converter.to_bytes(wstr);
		return utf8_str;
	} catch (...) {
		return ""; // Failed
	}
}

// Format args to string
namespace str_detail {
template<typename T>
void append_to_stream(std::ostringstream& oss, T&& value) {
	oss << std::forward<T>(value);
}

// join_args_impl: end
inline void join_args_impl(std::ostringstream&, const std::string&) {}

// join_args_impl
template<typename T, typename... Args>
void join_args_impl(std::ostringstream& oss, const std::string& delimiter,
	T&& first, Args&&... args) {
	append_to_stream(oss, std::forward<T>(first));
	if (sizeof...(args) > 0) { oss << delimiter; }
	join_args_impl(oss, delimiter, std::forward<Args>(args)...);
}
}

// Join args to string with delimiter
template<typename... Args>
std::string join_args(const std::string& delimiter, Args&&... args) {
	KD_STATIC_ASSERT_M(is_all_printable<typename std::decay<Args>::type...>::value,
		"All arguments must be printable (support operator<<)");

	std::ostringstream oss;
	str_detail::join_args_impl(oss, delimiter, std::forward<Args>(args)...);
	return oss.str();
}

// Format args to string 
namespace str_detail {
// collect_formatted_args
inline void collect_formatted_args(std::vector<std::string>&) {}

template <typename T, typename... Args>
void collect_formatted_args(std::vector<std::string>& parts, T&& first, Args&&... args) {
	parts.push_back(to_string(std::forward<T>(first)));
	collect_formatted_args(parts, std::forward<Args>(args)...);
}
}

// Format args to string
// format_args("a=%1, b=%2, c = %3", 42, "hello", obj);
template <typename... Args>
std::pair<bool, std::string> format_args(const std::string& fmt, Args&&... args) {
	KD_STATIC_ASSERT_M(is_all_printable<typename std::decay<Args>::type...>::value,
		"All arguments must be printable (support operator<<)");

	std::vector<std::string> parts;
	str_detail::collect_formatted_args(parts, std::forward<Args>(args)...);

	std::string result;
	size_t len = fmt.size();
	size_t pos = 0;

	while (pos < len) {
		if (fmt[pos] == '%') {
			if (pos + 1 >= len) {
				// Failed: Incomplete format specifier at end of string
				return std::make_pair<bool, std::string>(false, "");
			}

			if (fmt[pos + 1] == '%') {  // %%
				result += '%';
				pos += 2;
				continue;
			}

			// %n
			size_t end = pos + 1;
			while (end < len && std::isdigit(fmt[end])) {
				end++;
			}

			if (end == pos + 1) {
				// Failed: Invalid format specifier - missing index
				return std::make_pair<bool, std::string>(false, "");
			}

			try {
				int index = std::stoi(fmt.substr(pos + 1, end - pos - 1)) - 1;
				if (index < 0 || static_cast<size_t>(index) >= parts.size()) {
					// Failed: Argument index out of range
					return std::make_pair<bool, std::string>(false, "");
				}

				result += parts[index];
				pos = end;
			} catch (const std::invalid_argument&) {
				// Failed: Invalid number in format specifier
				return std::make_pair<bool, std::string>(false, "");
			} catch (const std::out_of_range&) {
				// Failed: Number in format specifier out of range
				return std::make_pair<bool, std::string>(false, "");
			}

		} else {
			result += fmt[pos++];
		}
	}

	return std::make_pair<bool, std::string>(true, std::move(result));
}

__NAMESPACE_KD_END
