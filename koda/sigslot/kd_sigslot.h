/* ********************************************************************************
Created by: Arlin (arlins.dps@gmail.com).

The sigslot library
The sigslot is a lightweight, thread-safe event receiving and responding C++ library
similar to Qt sigslot. It can be used safely in multiple threads. You can connect,
disconnect sigslot and emit signal in different threads.

 It is forbidden to perform time-consuming operations in the slot
 when using it, otherwise it will block the sigslot operation related to the slot object
 associated with the slot. In addition, all slots will be called in the thread where the
 signal is emitted.

[!] #define switches:
	- KD_SIGSLOT_SINGAL_THREAD
		By default, sigslot supports multi-threading. You can define this macro
		to use sigslot in a single thread to improve performance.

[!] How does sigslot achieve multi-threaded safety?
1. Use a lock to protect the connection center of all connections of sigslot.
	When a signal connects to a slot and a sigslot disconnects,
	lock the connection center of sigslot to ensure thread safety.

2. Prevent the slot object from being released while a signal is being emitted

	Troublesome Problem:
		When a signal is being emitted in one thread
		and the receiver of signal is destroyed in another thread
		at the same time, it will cause a crash.

		In the non-uniform memory management model of C++,
		This problem seems to have no solution, except for
		forcing the receiver to use smart pointers to manage memory,
		as Boost.signal2 does.

	Our solution in sigslot lib:
		When calling a slot while emitting a signal, the slot object is locked.
		Waiting for this lock during the slot object's destruction prevents it
		from being released during the call to the slot.

		This has a side effect: when calling a slot, any subsequent slot calls to the slot object
		associated with that slot must wait until all previous slots have completed
		and released the slot object's lock before continuing.

		In other words, when using a slot, absolutely avoid performing time-consuming operations within it;
		otherwise, the normal operation of sigslot on that slot object will be significantly affected,
		and the consequences are generally unacceptable.

[!] Usage:
	struct Button {
		sigslot::Signal<> clicked;
	}

	struct Frame : public sigslot::SlotObject {
		Frame() {
			// If you need to receive signals from different threads in slot object,
			// call the following function
			// enableSlotObjectLocker();
		}

		~Frame() {
			// Must call disconnect all when destructing in first line
			disconnectAllWhenDestructing();
			// Other cleanup work
		}

		void on_clicked() {};
	}

	//
	Button button;
	Frame frame;

	button.clicked.connect(&frame, &Frame::on_click);  // Connect
	button.clicked();  // Emit the signal
******************************************************************************** */

#pragma once
#include <iostream>
#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <deque>
#include <vector>
#include <unordered_map>

#include "koda/kd_global.h"
#include "koda/base/kd_anyfunction.h"
#include "koda/async/kd_fastmutex.h"

//#define KD_SIGSLOT_SINGAL_THREAD

#ifdef KD_DEBUG
// #define KD_SIG_ENABLE_DEBUG
#endif

// Log
#ifdef KD_SIG_ENABLE_DEBUG
#include "koda/base/kd_str.h"
#define kd_sigslot_debug(fmt, ...) printf("[SIGSLOT] " fmt "\n", ##__VA_ARGS__);

#define SIGSLOT_DEFINE_DEBUG_NAME \
public: \
std::string m_debugName;  \
std::string _debugName() { \
	return kd::format_str("%s 0x%p", m_debugName.c_str(), this); \
}

#else 
#define kd_sigslot_debug(fmt, ...) do {} while(false);
#define SIGSLOT_DEFINE_DEBUG_NAME 
#endif // KD_SIG_ENABLE_DEBUG


//
__NAMESPACE_KD_BEGIN

namespace sigslot {

#ifdef KD_SIGSLOT_SINGAL_THREAD
struct dummy_mutex {
	void lock() {};
	void unlock() {};
};

using ConnMutex = dummy_mutex;
using SlotMutex = dummy_mutex;
#else
using ConnMutex = kd::FastMutex;
using SlotMutex = std::recursive_mutex;
#endif // KD_SIGSLOT_SINGAL_THREAD

// =========================
// Connection entry
// =========================
class SignalBase;
class SlotObject;

template <typename... Signatures>
class Signal;

struct Connection {
	void* sender; // Unused
	SignalBase* signal;
	SlotObject* receiver;
	any_function slot;

	Connection clone() const {
		Connection conn = *this;
		return conn;
	}
};

// =========================
// Global connection manager (thread-safe)
// =========================
class ConnectionCenter {
public:
	static ConnectionCenter& defaultCenter() {
		static ConnectionCenter* inst = new ConnectionCenter;
		KD_LEAKY_SINGLETON_DEFINE(inst);
		return *inst;
	}

	// Add a new connection
	void add(Connection conn) {
		std::lock_guard<ConnMutex> lock(m_mutex);
		add_without_locked(conn);
	}

	// Remove all connections matching criteria
	void remove(void* sender, SignalBase* signal, void* receiver) {
		std::lock_guard<ConnMutex> lock(m_mutex);
		remove_without_locked(sender, signal, receiver);
	}

	// Get all connections matching criteria
	std::vector<Connection> get(void* sender, SignalBase* signal) {
		std::lock_guard<ConnMutex> lock(m_mutex);
		return get_without_locked(sender, signal);
	}

private:
	void add_without_locked(Connection conn) {
		m_connections.push_back(conn);
	}

	void remove_without_locked(void* sender, SignalBase* signal, void* receiver) {
		auto it = m_connections.begin();
		while (it != m_connections.end()) {
			if ((!sender || it->sender == sender)
				&& (!signal || it->signal == signal)
				&& (!receiver || it->receiver == receiver)) {
				it = m_connections.erase(it);
			} else {
				++it;
			}
		}
	}

	std::vector<Connection> get_without_locked(void* sender, SignalBase* signal) {
		std::vector<Connection> result;
		for (const auto& conn : m_connections) {
			if ((!sender || conn.sender == sender)
				&& (!signal || conn.signal == signal)) {
				result.push_back(conn.clone());
			}
		}
		return result;
	}

	void lock() {
		m_mutex.lock();
	}

	void unlock() {
		m_mutex.unlock();
	}

private:
	std::deque<Connection> m_connections;
	ConnMutex m_mutex;

	friend class SignalBase;
	template <typename... Signatures> friend class Signal;
	friend class SlotObject;
};

// =========================
// SlotObject
// =========================
class SlotObject {
	SIGSLOT_DEFINE_DEBUG_NAME

public:
	SlotObject() = default;
	virtual ~SlotObject() {
		disconnectAll();
	}

	// Disconnect all slot of object when destructing.
	// All derived classes of SlotObject must call this function 
	// when destructing.
	void disconnectAllWhenDestructing() {
		m_isDestructing.store(true, std::memory_order_release);
		ConnectionCenter::defaultCenter().lock();
		lockSlotObject(); // Prevent slot object from being released during emitting signal
		ConnectionCenter::defaultCenter().remove_without_locked(nullptr, nullptr, this);
		unlockSlotObject();
		ConnectionCenter::defaultCenter().unlock();
	}

	// Disconnect all slot of object Not during destruction.
	void disconnectAll() {
		ConnectionCenter::defaultCenter().remove(nullptr, nullptr, this);
	}

	// Enable slot locker 
	// When slot object needs to receive signals from multiple threads,
	// the slot locker must be enabled. This locker can prevent slot object 
	// from being released during emitting signal
	void enableSlotObjectLocker() {
		if (m_mutex == nullptr) {
			m_mutex = std::make_unique<SlotMutex>();
		}
	}

	bool isDestructing() const {
		return m_isDestructing.load(std::memory_order_acquire);
	}

private:
	void lockSlotObject() {
		if (m_mutex != nullptr) {
			m_mutex->lock();
		}
	}

	void unlockSlotObject() {
		if (m_mutex != nullptr) {
			m_mutex->unlock();
		}
	}

private:
	std::unique_ptr<SlotMutex> m_mutex;
	std::atomic<bool> m_isDestructing{ false };

	friend class SignalBase;
	template <typename... Signatures> friend class Signal;
};

// =========================
// SignalBase
// =========================
class SignalBase {
	SIGSLOT_DEFINE_DEBUG_NAME

public:
	virtual ~SignalBase() {
		ConnectionCenter::defaultCenter().remove(nullptr, this, nullptr);
	}
};

// =========================
// Signal
// =========================
template <typename... Signatures>
class Signal : public SignalBase {
public:
	virtual ~Signal() {
		disconnectAll();
	}

	// Emit the signal on emitting thread.
	template <typename... Args>
	void emitSignal(Args&&... args) {
		kd_sigslot_debug("Preparing to emit signal <%s>", _debugName().c_str());

		void* sender = this;
		SignalBase* signal = this;

		// Lock all slot objects based on their addresses to prevent slots 
		// from being released during emitting the signal.
		ConnectionCenter::defaultCenter().lock();
		auto connections = ConnectionCenter::defaultCenter().get_without_locked(sender, signal);

		std::map<SlotObject*, size_t> recv_counts;
		for (auto& conn : connections) {
			if (conn.receiver && !conn.receiver->isDestructing()) {
				recv_counts[conn.receiver]++;
			}
		}
		for (auto& pair : recv_counts) {
			pair.first->lockSlotObject();
		}

		ConnectionCenter::defaultCenter().unlock();
		kd_sigslot_debug("Start to emit signal <%s> [Connections unlocked, Slot locked]", _debugName().c_str());

		// Invoke all slots of signal and unlocked the slot
		for (auto& conn : connections) {
			if (conn.receiver == nullptr) {
				continue;
			}

			auto it = recv_counts.find(conn.receiver);
			if (it != recv_counts.end()) {
				if (conn.slot) {
					conn.slot.invoke_mfn<void(Signatures...)>(conn.receiver, std::forward<Args>(args)...);
				}

				// Unlocked the slot object ASAP
				if (--(it->second) == 0) {
					it->first->unlockSlotObject();
					recv_counts.erase(it);
				}
			}
		}

		kd_sigslot_debug("Stop emitting signal <%s> [Slot unlocked]", _debugName().c_str());
	}

	// operator()
	template <typename... Args>
	void operator() (Args&&... args) {
		emitSignal(std::forward<Args>(args)...);
	}

	// Connect a slot to this signal
	template <typename T>
	bool connect(SlotObject* receiver, void (T::* method)(Signatures...)) {
		if (receiver == nullptr) {
			return false;
		}

		Connection conn;
		conn.sender = this;
		conn.signal = this;
		conn.receiver = receiver;
		conn.slot = kd::any_function(method);
		ConnectionCenter::defaultCenter().add(conn);
		return true;
	}

	// Disconnect a specific receiver connected to this signal
	void disconnect(void* receiver) {
		ConnectionCenter::defaultCenter().remove(this, this, receiver);
	}

	// Disconnect all slots connected to this signal
	void disconnectAll() {
		ConnectionCenter::defaultCenter().remove(this, this, nullptr);
	}
};

// =========================
// Global API
// =========================

// Connect a signal to a slot
template <typename T, typename R, typename... SlotArgs, typename... SignalArgs>
bool connect(Signal<SignalArgs...>& signal, T* receiver, R(T::* slot)(SlotArgs...)) {
	return signal.connect(receiver, slot);
}

// Disconnect all slot of object Not during destruction
inline void disconnectAll(SlotObject* slotObj) {
	slotObj->disconnectAll();
}

// Disconnect all slot of object when destructing
inline void disconnectAllWhenDestructing(SlotObject* slotObj) {
	slotObj->disconnectAllWhenDestructing();
}

} // namespace sigslot

__NAMESPACE_KD_END