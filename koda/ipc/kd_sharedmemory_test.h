#pragma once
#include <string>
#include <cstdint> // uint32_t
#include "koda/kd_global.h"
#include "koda/ipc/kd_sharedmemory.h"

#define kd_shm_debug(fmt, ...) printf("[SHM_TEST] " fmt, ##__VA_ARGS__);

//============================
// test
//============================
namespace kd_shm_test {

static void test_shared_memory() {
	uint32_t size = 512;
	static kd::SharedMemory* shm_ptr = new kd::SharedMemory(("MyAppMemory"));
	kd::SharedMemory& shm = *shm_ptr;

	kd::SharedMemory::Error err = shm.create(size);
	if (err == kd::SharedMemory::Error::NoError) {
		std::string msg = "Shared Data by Create";

		shm.lock();
		memcpy(shm.data(), msg.c_str(), msg.length());
		shm.setDataSize((uint32_t)msg.length());
		kd_shm_debug("Created SHM with size: %u content = %s\n", shm.capacity(), (char*)shm.data());
		shm.unlock();

	} else if (err == kd::SharedMemory::Error::AlreadyExists) {

		kd_shm_debug("SHM AlreadyExists\n");
		kd::SharedMemory::Error err2 = shm.attach();
		if (err2 == kd::SharedMemory::Error::NoError) {
			kd_shm_debug("Attached! Size is:: %u, content = %s\n", shm.capacity(), (char*)shm.data());
			std::string newContent = std::string("Shared Data by Attached ") + std::to_string(kd::now_time());
			kd_shm_debug("newContent = %s\n", newContent.c_str());

			shm.lock();
			memcpy(shm.data(), newContent.c_str(), newContent.length());
			shm.setDataSize((uint32_t)newContent.length());
			shm.unlock();
		} else {
			kd_shm_debug("Failed to Attached SHM, err = %d\n", err2);
		}

	} else {
		kd_shm_debug("Failed to Create SHM, err = %d\n", err);
	}
}

#define MyTestSharedMemory "MyTestSharedMemory989"
#define MyTestAutoReset false
static void test_run_writer() {
	kd::SharedMemory shm(MyTestSharedMemory, MyTestAutoReset);
	uint32_t dataSize = 1024;
	kd_shm_debug("[Writer] Creating shared memory...\n");

	auto err = shm.create(dataSize);
	if (err != kd::SharedMemory::Error::NoError) {
		if (err == kd::SharedMemory::Error::AlreadyExists) {
			kd_shm_debug("[Writer] Memory already exists, attaching instead...\n");
			shm.attach();
		} else {
			kd_shm_debug("[Writer] Create failed, error code: err = %d\n", static_cast<int>(err));
			return;
		}
	}

	char* buffer = (char*)shm.data();
	for (int i = 0; i < 5; ++i) {
		std::this_thread::sleep_for(std::chrono::seconds(3));

		shm.lock();
		std::string msg = "Hello from Writer, count: " + std::to_string(i);
		uint32_t len = (uint32_t)msg.length();
		memcpy(shm.data(), msg.c_str(), len);
		shm.setDataSize(len);
		kd_shm_debug("[Writer] Send data size: %u bytes, content: %s\n", len, msg.c_str());
		shm.unlock();

		shm.emitEvent();
		if (!shm.isAutoResetEvent()) {
			shm.resetEvent();
		}
	}
	kd_shm_debug("[Writer] Done.\n");
}

static void test_run_reader() {
	kd::SharedMemory shm(MyTestSharedMemory, MyTestAutoReset);
	kd_shm_debug("[Reader] Waiting for shared memory to be created...\n");

	while (shm.attach() != kd::SharedMemory::Error::NoError) {
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	kd_shm_debug("[Reader] Attached. Waiting for events...\n");

	for (int i = 0; i < 5; ++i) {
        auto result = shm.waitEvent(10000);
		if (result == kd::WaitEventResult::Success) {
			shm.lock();
			uint32_t realLen = shm.getDataSize();
			std::string str((char*)shm.data(), realLen);
			kd_shm_debug("[Reader] Receive data size: %u bytes, content: %s\n", realLen, str.c_str());
			shm.unlock();
		} else {
			kd_shm_debug("[Reader] Wait error: %d!\n", result);
		}
	}
	kd_shm_debug("[Reader] Done.\n");
}
};
