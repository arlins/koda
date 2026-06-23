#include "koda/ipc/kd_ipc.h"
#include "koda/base/kd_random.h"

//test_ipc_debug
#define test_ipc_debug(fmt, ...) \
printf("=== [IPC_TEST] [%llu] [%s] " fmt "\n", kd::now_time(), kd::this_thread_id(false, 5).c_str(), ##__VA_ARGS__);

//============================
// debug
//============================

#define KD_IPC_TEST_MSG_TIME_COST
#define KD_MSG_TIME_MAGIC "##msg_time##"
#define KD_IPC_TEST_BIG_DATA_SIZE 1024*1024*30

namespace kd_ipc_test {
using TimeType = long long;
static void addNowTimeToData(std::vector<char>& data) {
	int timeSize = sizeof(TimeType);
	auto nowTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::high_resolution_clock::now().time_since_epoch()).count();

	// [magic][time][data]
	std::string magic = KD_MSG_TIME_MAGIC;
	std::vector<char> dataWithTime(magic.length() + timeSize + data.size());
	memcpy(dataWithTime.data(), magic.data(), magic.length());
	memcpy(dataWithTime.data() + magic.length(), &nowTime, timeSize);
	memcpy(dataWithTime.data() + magic.length() + timeSize, data.data(), data.size());

	data = dataWithTime;
}

static bool parseTimeFromData(std::shared_ptr<std::vector<char>>& data, TimeType& costNs) {
	int timeSize = sizeof(TimeType);
	std::string magic = KD_MSG_TIME_MAGIC;
	if (data->size() <= magic.length() + timeSize) {
		return false;
	}

	std::string testMagic(data->data(), magic.length());
	if (testMagic != magic) {
		return false;
	}

	TimeType time = 0;
	memcpy(&time, data->data() + magic.length(), timeSize);
	costNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::high_resolution_clock::now().time_since_epoch()).count() - time;

	auto realData = std::make_shared<std::vector<char>>(data->size() - magic.length() - timeSize);
	memcpy(realData->data(), data->data() + magic.length() + timeSize, realData->size());
	data = realData;

	return true;
}
};

//============================
// Testing of kd::IPCConnection
//============================
namespace kd_ipc_test {
static int ipc_msg_count = 0;
static char ipc_channel[] = "TestChannel";

// IPCConnectionTestHandler
class IPCConnectionTestHandler : public kd::IPCConnectionHandler {
	std::string m_name;
	bool m_pingPong;
	bool m_stateLog;

public:
	IPCConnectionTestHandler(bool pingPong = true, bool stateLog = false) {
		init(pingPong, stateLog);
	}

	void init(bool pingPong, bool stateLog) {
		m_pingPong = pingPong;
		m_stateLog = stateLog;
	}

	void printfRecvInfo(const std::string& name, size_t rawSize,
		const std::string& str, TimeType costUs = 0) {
		std::string log = kd::format_str("[%s] Recv: ", name.c_str());
		if (rawSize > 1024) { // Big data
			size_t mb = (rawSize / (1024 * 1024));
			log += std::to_string(mb) + " MB";
			if (costUs > 0) {
				log += kd::format_str(" (%llu ms)", costUs / 1000);
			}
		} else {
			log += str;
			if (costUs > 0) {
				log += kd::format_str(" (%llu us)", costUs);
			}
		}

		test_ipc_debug("%s", log.c_str());
	}

	virtual void onIPCConnectionRecvData(std::shared_ptr<kd::IPCConnection> conn, std::shared_ptr<std::vector<char>> data) override {
		std::string str(data->begin(), data->end());

#ifdef KD_IPC_TEST_MSG_TIME_COST
		TimeType costNs = 0;
		auto done = parseTimeFromData(data, costNs);
		if (done) {
			TimeType costUs = costNs / 1000.0; // microseconds
			TimeType costMs = costNs / 1000000.0;
			str = std::string(data->begin(), data->end());
			printfRecvInfo(conn->name(), data->size(), str, costUs);
		} else {
			printfRecvInfo(conn->name(), data->size(), str);
		}
#else 
		printfRecvInfo(conn->name(), data->size(), str);
#endif // KD_IPC_TEST_MSG_TIME_COST

		kd::IPCPerformOnMainThread([=] {
			if (m_pingPong && str == "Ping") {
				conn->send("Pong");
			}
		});
	};

	virtual void onIPCConnectionStateChanged(std::shared_ptr<kd::IPCConnection> conn, kd::IPCState state) override {
		if (m_stateLog) {
			test_ipc_debug("[%s] Changing state = %s", conn->name().c_str(), kd::IPCStateToString(state).c_str());
		}

		kd::IPCPerformOnMainThread([=] {
			if (state == kd::IPCState::Connected) {
				ipc_msg_count = 0;
				if (conn->isServer() && m_pingPong) {
					conn->send("Ping");
				}
			}
		});
	};
};

//
static void test_ipc(bool is_server) {
	auto  ipcConnection = kd::IPCConnection::create(ipc_channel, is_server, true);
	auto handler = std::make_shared<IPCConnectionTestHandler>();
	ipcConnection->addHandler(handler);

	test_ipc_debug("[%s] Start ", ipcConnection->name().c_str());
	ipcConnection->start();

	// Testing stop
	/*Sleep(2000);
	ipcConnection->stop();
	return;*/

	// Testing send and stop
	int ipc_msg_count = 0;
	while (true) {
		std::string str = ipcConnection->name() + " Msg #" + std::to_string(++ipc_msg_count);
		ipcConnection->send(std::vector<char>(str.begin(), str.end()));
		kd::thread_sleep(2000);

		if ((is_server && ipc_msg_count == 5) || (!is_server && ipc_msg_count == 6)) {
			//ipcConnection->stop();
			//break;
		}
	}

	ipcConnection->removeHandler(handler);
}

// ipc2
static std::shared_ptr<kd::IPCConnection> ipcConnection = nullptr;
static std::shared_ptr <IPCConnectionTestHandler> ipcHandler = nullptr;
static void test_ipc2_start(bool is_server, bool big_data = false) {
	if (ipcConnection == nullptr || ipcConnection->isServer() != is_server) {
		if (ipcConnection) {
			if (ipcHandler) {
				ipcConnection->removeHandler(ipcHandler);
				ipcHandler = nullptr;
			}
			ipcConnection->stop();
			ipcConnection = nullptr;
		}

		if (big_data) {
			ipcConnection = kd::IPCConnection::create(ipc_channel, is_server, true,
				KD_IPC_TEST_BIG_DATA_SIZE, KD_IPC_TEST_BIG_DATA_SIZE, 30);
		} else {
			ipcConnection = kd::IPCConnection::create(ipc_channel, is_server, true);
		}

		ipcHandler = std::make_shared<IPCConnectionTestHandler>();
		ipcConnection->addHandler(ipcHandler);
	}

	test_ipc_debug("[%s] Start ", ipcConnection->name().c_str());
	ipcHandler->init(true, false);
	ipcConnection->start();
	ipcConnection->start(); // Test starting at the same time
}

//
static void test_ipc2_send_data(bool big_data = false) {
	if (ipcConnection == nullptr) {
		return;
	}

	if (big_data) {
		auto scale = kd::random::generateUInt(1, 5);
		std::vector<char> data(scale * 2 * 1024 * 1024); // MB
#ifdef KD_IPC_TEST_MSG_TIME_COST
		addNowTimeToData(data);
#endif // KD_IPC_TEST_MSG_TIME_COST
		ipcConnection->send(data);
	} else {
		std::string str = ipcConnection->name() + " Msg #" + std::to_string(++ipc_msg_count);
		std::vector<char> data = std::vector<char>(str.begin(), str.end());
#ifdef KD_IPC_TEST_MSG_TIME_COST
		addNowTimeToData(data);
#endif // KD_IPC_TEST_MSG_TIME_COST
		ipcConnection->send(data);
	}
}

//
static void test_ipc2_stop() {
	if (ipcConnection == nullptr) {
		return;
	}

	test_ipc_debug("[%s] Stop ", ipcConnection->name().c_str());
	ipcConnection->stop();
	ipcConnection->stop(); // Test stopped at the same time
	// ipcConnection->removeHandler(ipcHandler);
	// ipcConnection = nullptr;
	// ipcHandler = nullptr;
}

// Testing connection start/stop stress
static std::atomic<int> ipc_stress_msg_count{ 0 };
static void test_ipc_conn_stress(bool is_server, int thread_count) {
	ipc_stress_msg_count = 0;
	test_ipc_debug("Start testing stress, is_server = %d, thread_count = %d",
		is_server, thread_count);

	if (ipcConnection == nullptr) {
		ipcConnection = kd::IPCConnection::create(ipc_channel, is_server, true);
		ipcHandler = std::make_shared<IPCConnectionTestHandler>(true, false);
		ipcConnection->addHandler(ipcHandler);
	}

	auto startAction = [=](int idx) {
		test_ipc_debug("[%s] Start <%d> === ", ipcConnection->name().c_str(), idx);
		ipcConnection->start();
	};

	auto stopAction = [=](int idx) {
		test_ipc_debug("[%s] Stop <%d> === ", ipcConnection->name().c_str(), idx);
		ipcConnection->stop();
	};

	auto sendAction = [=](int idx) {
		std::string str = ipcConnection->name() + " Msg #" + std::to_string(++ipc_stress_msg_count);
		test_ipc_debug("[%s] Send: %s <%d> === ", ipcConnection->name().c_str(), str.c_str(), idx);
		ipcConnection->send(str);
	};

	//
	auto doRandomActions = [=]() {
		for (int i = 0; i < 300; i++) {
			auto ms = kd::random::generateUInt(5, 50);
			kd::thread_sleep(ms);

			auto a = kd::random::generateUInt(0, 100);
			if (a % 3 == 0) {
				startAction(i);
			} else if (a % 3 == 1) {
				stopAction(i);
			} else if (a % 3 == 2) {
				//sendAction(i);
			}
		}
	};

	// 
	std::thread([=] {
		std::vector<std::thread> workers;
		for (int i = 0; i < thread_count; i++) {
			std::thread worker(doRandomActions);
			workers.push_back(std::move(worker));
		}

		//
		for (auto& worker : workers) { worker.join(); }
		kd::thread_sleep(1000);
		printf("\n");
		test_ipc_debug("Finish testing stress, is_server = %d, thread_count = %d, state = %s", is_server,
			thread_count, kd::IPCStateToString(ipcConnection->connectionState()).c_str());
		printf("\n");

	}).detach();
}

// Testing send stress
static void test_ipc_send_stress(int thread_count, bool big_data = false) {
	if (ipcConnection == nullptr) {
		KD_ASSERT(false);
		return;
	}

	ipc_msg_count = 0;
	test_ipc_debug("Start testing send stress, name = %s, thread_count = %d, big_data = %d",
		ipcConnection->name().c_str(), thread_count, big_data);

	auto doRandomActions = [=]() {
		for (int i = 0; i < 500; i++) {
			uint32_t ms = 0;
			if (big_data) {
				ms = kd::random::generateUInt(50, 100);
			} else {
				ms = kd::random::generateUInt(5, 50);
			}

			kd::thread_sleep(ms);
			test_ipc2_send_data(big_data);
		}
	};

	// 
	std::thread([=] {
		std::vector<std::thread> workers;
		for (int i = 0; i < thread_count; i++) {
			std::thread worker(doRandomActions);
			workers.push_back(std::move(worker));
		}

		for (auto& worker : workers) { worker.join(); }
		kd::thread_sleep(1000);
		printf("\n");
		test_ipc_debug("Finish testing send stress,name = %s, thread_count = %d, big_data = %d",
			ipcConnection->name().c_str(), thread_count, big_data);
		printf("\n");

	}).detach();
}
};
