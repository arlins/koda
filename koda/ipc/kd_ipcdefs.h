/** *************************************************
Created by: Arlin (arlins.dps@gmail.com).
This file defines the common parts related to IPC.
*****************************************************/

#pragma once
#include <iostream>
#include <string>
#include <functional>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <algorithm>
#include <chrono>
#include "koda/kd_global.h"
#include "koda/base/kd_memory.h"
#include "koda/base/kd_utils.h"
#include "koda/async/kd_fastmutex.h"
#include "koda/async/kd_operation.h"

//====================
// Defines
//====================
#ifdef KD_DEBUG
//#define KD_IPC_ENABLE_DEBUG
#endif

#ifdef KD_IPC_ENABLE_DEBUG
#define kd_ipc_debug(fmt, ...) \
do  { \
	printf("[IPC] [%llu] [%s] [%s] " fmt "\n", kd::now_time(), kd::this_thread_id(false, 5).c_str(), \
		name().c_str(), ##__VA_ARGS__); \
} while (0);

#else
#define kd_ipc_debug(fmt, ...) do {} while(0);
#endif // KD_IPC_ENABLE_DEBUG


// ===============================
// IPCDelegate & IPCConnectionImpl
// ===============================
__NAMESPACE_KD_BEGIN

#if defined(KD_OS_WIN)
using IPCMutex = kd::FastMutex;
#else
using IPCMutex = std::mutex;
#endif

// IPCDelegate
class IPCDelegate {
public:
	virtual ~IPCDelegate() {}
	virtual bool isQuit() = 0;
};

// IPCConnectionImpl
class IPCConnectionImpl {
public:
	IPCConnectionImpl(IPCDelegate* delegate, const std::string& channel, bool isServer,
		unsigned int readBufferSize, unsigned int writeBufferSize)
		: m_delegate(delegate)
		, m_channel(channel)
		, m_isServer(isServer)
		, m_readBufferSize(readBufferSize)
		, m_writeBufferSize(writeBufferSize) {
	}

	virtual ~IPCConnectionImpl() {}

	// Open connection
	virtual bool open(const std::function<void()>& aboutToConnect,
		const std::function<void(bool)>& didConnected) = 0;

	virtual void close() = 0; // Close connection
	virtual bool ready() = 0; // Check connection is ready to write data or not
	virtual void interrupt() = 0; // Interrupt connection
	virtual bool read(void* data, uint32_t size) = 0; // Read data from peer
	virtual bool write(const void* data, uint32_t size) = 0; // Write data to peer

	// Identify the connection for debugging etc.
	virtual std::string name() {
		return (m_isServer ? "Server" : "Client");
	}

protected:
	friend class IPCConnection;
	IPCDelegate* m_delegate;
	IPCMutex m_connMtx;
	std::string m_channel;
	bool m_isServer{ true };
	unsigned int m_readBufferSize{ 1024 * 1024 }; // 1MB
	unsigned int m_writeBufferSize{ 1024 * 1024 }; // 1MB
};

__NAMESPACE_KD_END
