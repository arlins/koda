/* **************************************************************
 Created by: Arlin (arlins.dps@gmail.com).
 This file implements random number engine using Xorshift
************************************************************** */

#pragma once
#include <iostream>
#include <string>
#include <random>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include "koda/kd_global.h"

__NAMESPACE_KD_BEGIN

namespace random {
// ======================
// FastRandom 32bits
// Random number engine using Xorshift
// ======================
class FastRandom {
private:
	uint32_t state;
	uint32_t next() {
		uint32_t x = state;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		state = x;
		return x;
	}

public:
	FastRandom() {
		std::random_device rd;
		state = rd();
		uint32_t timeSeed = static_cast<uint32_t>(
			std::chrono::high_resolution_clock::now().time_since_epoch().count()
			);
		state ^= timeSeed;

		// The Xorshift state cannot be 0; if it happens to be 0, 
		// an initial value must be forcibly assigned.
		if (state == 0) {
			state = 0xACE1;
		}
	}

	// Generate integers in the range [min, max]
	uint32_t generate(uint32_t min, uint32_t max) {
		if (max < min) {
			std::swap(min, max);
		}

		uint32_t range = max - min + 1;
		if (range == 0) {
			// If range == 0, it means an overflow has occurred.
			return next();
		}

		return min + (next() % range);
	}

	static FastRandom& getInstance() {
		static thread_local FastRandom* inst = new FastRandom();
		KD_LEAKY_SINGLETON_DEFINE(inst);
		return *inst;
	}
};

// ======================
// FastRandom 64bits
// ======================
class FastRandom64 {
private:
	uint64_t state;

	uint64_t next64() {
		uint64_t x = state;
		x ^= x << 13;
		x ^= x >> 7;
		x ^= x << 17;
		state = x;
		return x;
	}

public:
	FastRandom64() {
		std::random_device rd;
		uint64_t s = rd();
		s = (s << 32) | rd();
		uint64_t timeSeed = static_cast<uint64_t>(
			std::chrono::high_resolution_clock::now().time_since_epoch().count()
			);
		state = s ^ timeSeed;

		// The Xorshift state cannot be 0; if it happens to be 0, 
		// an initial value must be forcibly assigned.
		if (state == 0) {
			state = 0xACE1ACE1ACE1ACE1ULL;
		}
	}

	// Generate uint64_t in the range [min, max]
	uint64_t generate(uint64_t min, uint64_t max) {
		if (max < min) {
			std::swap(min, max);
		}

		uint64_t range = max - min + 1;
		if (range == 0) {
			// If range == 0, it means an overflow has occurred.
			return next64();
		}

		return min + (next64() % range);
	}

	static FastRandom64& getInstance() {
		static thread_local FastRandom64* inst = new FastRandom64;
		KD_LEAKY_SINGLETON_DEFINE(inst);
		return *inst;
	}
};

// Generate integer in the range [min, max]
inline uint32_t generateUInt(uint32_t min, uint32_t max) {
	return FastRandom::getInstance().generate(min, max);
}

// Generate 64-bits integer in the range [min, max]
inline uint64_t generateUInt64(uint64_t min, uint64_t max) {
	return FastRandom64::getInstance().generate(min, max);
}

// Generate standard UUID v4
inline std::string generateUUID() {
	const char* hex_chars = "0123456789abcdef";
	auto& rng = FastRandom::getInstance();
	std::string uuid = "00000000-0000-4000-8000-000000000000";

	// Fill the first segment (8 bits).
	for (int i = 0; i < 8; ++i) {
		uuid[i] = hex_chars[rng.generate(0, 15)];
	}

	// Fill the second segment (4 bits).
	for (int i = 9; i < 13; ++i) {
		uuid[i] = hex_chars[rng.generate(0, 15)];
	}

	// Fill the 3rd segment (4 bits).
	// The first digit must be 4
	for (int i = 15; i < 18; ++i) {
		uuid[i] = hex_chars[rng.generate(0, 15)];
	}

	// Fill the 4th segment (4 digits): 
	// The first digit y must be 8, 9, a, or b.
	const char* y_chars = "89ab";
	uuid[19] = y_chars[rng.generate(0, 3)];
	for (int i = 20; i < 23; ++i) {
		uuid[i] = hex_chars[rng.generate(0, 15)];
	}

	// Fill in the 5th segment (12 bits).
	for (int i = 24; i < 36; ++i) {
		uuid[i] = hex_chars[rng.generate(0, 15)];
	}

	return uuid;
}

// Generate a random N-bit string
inline std::string generateRandomString(int n) {
	const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
	auto& rng = FastRandom::getInstance();

	std::string result;
	result.reserve(n);
	for (int i = 0; i < n; ++i) {
		result += charset[rng.generate(0, 35)];
	}
	return result;
}

}; // namespace random

__NAMESPACE_KD_END