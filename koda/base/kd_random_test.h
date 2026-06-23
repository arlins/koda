#include <chrono>
#include "kd_random.h"

namespace kd_random_test {
static void test_random_main() {
	int count = 1000000;

	// UInt
	{
		auto now = std::chrono::high_resolution_clock::now();
		auto min_ = std::numeric_limits<uint32_t>::min() + 1;
		auto max_ = std::numeric_limits<uint32_t>::max() - 1;
		for (int i = 0; i < count; i++) {
			auto s = kd::random::generateUInt(min_, max_);
		}

		auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::high_resolution_clock::now() - now).count();
		printf("Generate UInt, count = %d, cost = %llu ms\n", count, cost);
	}

	// UInt64
	{
		auto now = std::chrono::high_resolution_clock::now();
		auto min_ = std::numeric_limits<uint32_t>::min() + 1;
		auto max_ = std::numeric_limits<uint32_t>::max() - 1;
		for (int i = 0; i < count; i++) {
			auto s = kd::random::generateUInt64(min_, max_);
		}

		auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::high_resolution_clock::now() - now).count();
		printf("Generate UInt64, count = %d, cost = %llu ms\n", count, cost);
	}

	// uuid
	{
		auto now = std::chrono::high_resolution_clock::now();
		for (int i = 0; i < count; i++) {
			auto s = kd::random::generateUUID();
		}

		auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::high_resolution_clock::now() - now).count();
		printf("Generate uuid, count = %d, cost = %llu ms\n", count, cost);
	}

	// random string
	{
		auto now = std::chrono::high_resolution_clock::now();
		for (int i = 0; i < count; i++) {
			auto s = kd::random::generateRandomString(100);
		}

		auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::high_resolution_clock::now() - now).count();
		printf("Generate random string, count = %d, cost = %llu ms\n", count, cost);
	}
}
}