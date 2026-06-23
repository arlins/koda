/** *****************************************************************
Created by: Arlin (arlins.dps@gmail.com).
Implementation of IPCConnectionImpl of Unknown OS
This file is only intended to trick the compilation system into 
compiling successfully.
******************************************************************* **/

#pragma once
#include "koda/ipc/kd_ipcdefs.h"

__NAMESPACE_KD_BEGIN

// IPCConnectionImpl
class IPCConnectionImplDummy : public IPCConnectionImpl {
public:
	IPCConnectionImplDummy(IPCDelegate* delegate, const std::string& channel, bool isServer,
		unsigned int readBufferSize, unsigned int writeBufferSize)
		: IPCConnectionImpl(delegate, channel, isServer, readBufferSize, writeBufferSize) {
		KD_ASSERT_M(false, "IPCConnectionImpl unsupported on the OS");
	}
	virtual ~IPCConnectionImplDummy() {
		KD_ASSERT_M(false, "IPCConnectionImpl unsupported on the OS");
	}

	virtual bool open(const std::function<void()>& aboutToConnect,
		const std::function<void(bool)>& didConnected) override {
		KD_ASSERT_M(false, "IPCConnectionImpl unsupported on the OS");
		return false;
	};

	virtual void close() override {
		KD_ASSERT_M(false, "IPCConnectionImpl unsupported on the OS");
	};

	virtual bool ready() override {
		KD_ASSERT_M(false, "IPCConnectionImpl unsupported on the OS");
		return false;
	};

	virtual void interrupt() override {
		KD_ASSERT_M(false, "IPCConnectionImpl unsupported on the OS");
	};

	virtual bool read(void* data, uint32_t size) override {
		KD_ASSERT_M(false, "IPCConnectionImpl unsupported on the OS");
		return false;
	};

	virtual bool write(const void* data, uint32_t size) override {
		KD_ASSERT_M(false, "IPCConnectionImpl unsupported on the OS");
		return false;
	};
};

__NAMESPACE_KD_END