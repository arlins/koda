/** ***************************************************************************
Created by: Arlin (arlins.dps@gmail.com).
This file implements RLDispatcher on Windows.

 Regarding implementations of handle waiting
 IOCP only supports device handles with asynchronous I/O properties (such as 
 Socket, Overlapped File, Named Pipe, etc.), and does not support state-triggered 
 objects (Event, Semaphore, Mutex, Thread, Process). 
 The WaitForSingleObject/WaitForMultipleObjects APIs limit the maximum number 
 of waiting handles to 64. So we use RegisterWaitForSingleObject to to implement
 handle waiting for any number and type of handles.
*******************************************************************************/


#pragma once
#include <map>
#include <set>
#include "koda/runloop/kd_runloopdefs.h"


#if defined(KD_OS_WIN)
#include <windows.h>

__NAMESPACE_KD_BEGIN

// =================================
// RLNativeLooperWin32
// =================================
class RLNativeLooperWin32 : public RLLooper {
	static constexpr char RL_MSGWND_NAME[] = "_RLNativeLooperWin32MsgWindow_";
	static constexpr UINT RL_WM_EXIT_LOOP = WM_USER + 100;
	
	std::atomic<bool> m_exit{ false };
	std::atomic<int> m_exitCode{ 0 };
	HWND m_msgHwnd{ NULL };
	DWORD m_winThreadId{ 0 };

public:
	void construct(std::weak_ptr<RLContext> ctx) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		RLLooper::construct(ctx);

		m_winThreadId = GetCurrentThreadId();
		_ensureMsgHwnd();
	}

	void destroy() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (m_msgHwnd && ::IsWindow(m_msgHwnd)) {
			::DestroyWindow(m_msgHwnd);
			m_msgHwnd = NULL;
		}

		RLLooper::destroy();
	}

	int exec() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		auto ctx = m_ctx.lock();
		if (ctx == nullptr || ctx->getDispatcher()->destroyed()) {
			return -1;
		}

		_ensureMsgHwnd();

		while (!m_exit && !ctx->getDispatcher()->destroyed()) {
			// Wait for messages
			WaitMessage();

			// Peek all messages
			MSG msg;
			while (!m_exit && PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
				if (msg.message == WM_QUIT) {
					m_exitCode = static_cast<int>(msg.wParam);
					m_exit = true;
					PostQuitMessage(m_exitCode); // Post quit to next looper
					break;
				}

				if (msg.hwnd == m_msgHwnd && msg.message == RL_WM_EXIT_LOOP) {
					m_exitCode = static_cast<int>(msg.wParam);
					m_exit = true;
					break;
				}

				// Dispatch the message
				::TranslateMessage(&msg);
				::DispatchMessageA(&msg);
			}
		} // while

		return m_exitCode;
	}

	void exit(int exitCode) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (IsWindow(m_msgHwnd)) {
			PostMessageA(m_msgHwnd, RL_WM_EXIT_LOOP, (WPARAM)exitCode, 0);
		}
	}

private:
	static LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
		RLNativeLooperWin32* pThis = reinterpret_cast<RLNativeLooperWin32*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
		if (uMsg == RL_WM_EXIT_LOOP) {
			pThis->m_exit = true;
			pThis->m_exitCode = static_cast<int>(wParam);
			return 0;
		}

		return DefWindowProcA(hwnd, uMsg, wParam, lParam);
	}

	void _ensureMsgHwnd() {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);

		static std::once_flag once_flag_;
		std::call_once(once_flag_, [] {
			WNDCLASSEXA wc = { 0 };
			wc.cbSize = sizeof(wc);
			wc.lpfnWndProc = &RLNativeLooperWin32::MsgWndProc;
			wc.hInstance = GetModuleHandleA(NULL);
			wc.lpszClassName = RL_MSGWND_NAME;
			RegisterClassExA(&wc);
		});

		if (m_msgHwnd == NULL) {
			m_msgHwnd = CreateWindowExA(0, RL_MSGWND_NAME, "", 0,
				0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
			KD_ASSERT(m_msgHwnd != NULL);
			::SetWindowLongPtr(m_msgHwnd, GWLP_USERDATA, reinterpret_cast<LPARAM>(this));
		}
	}
};

// =========================================
// RLWin32ObjectsWaiter
// =========================================
static const uint64_t kRLWin32ObjectsWaiterMinSeqKey = 0xFFFF;
class RLWin32ObjectsWaiter {
private:
	struct WaitRecord {
		HANDLE eventHandle{ NULL };
		HANDLE waitHandle{ NULL };
		std::function<void(uint64_t, HANDLE)> callback;
	};

	std::mutex m_mutex;
	std::map<uint64_t, WaitRecord> m_registry;
	std::atomic<uint64_t> m_nextSequenceId{ kRLWin32ObjectsWaiterMinSeqKey + 1 };

private:
	RLWin32ObjectsWaiter() = default;

public:
	static RLWin32ObjectsWaiter& instance() {
		static RLWin32ObjectsWaiter* inst = new RLWin32ObjectsWaiter();
		KD_LEAKY_SINGLETON_DEFINE(inst);
		return *inst;
	}

	RLWin32ObjectsWaiter(const RLWin32ObjectsWaiter&) = delete;
	RLWin32ObjectsWaiter& operator=(const RLWin32ObjectsWaiter&) = delete;

	// Registers handle
	uint64_t registerHandle(HANDLE eventHandle, std::function<void(uint64_t, HANDLE)> callback) {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		std::lock_guard<std::mutex> lock(m_mutex);
		uint64_t seqKey = m_nextSequenceId.fetch_add(1, std::memory_order_relaxed);

		// Setup the record
		WaitRecord record;
		record.eventHandle = eventHandle;
		record.callback = std::move(callback);

		// Use WT_EXECUTEONLYONCE to implement an edge-triggered one-shot lifecycle.
		PVOID context = reinterpret_cast<PVOID>(static_cast<ULONG_PTR>(seqKey));
		BOOL success = ::RegisterWaitForSingleObject( &record.waitHandle, eventHandle,
			&RLWin32ObjectsWaiter::_objectsSignaledCallback, context, INFINITE, WT_EXECUTEONLYONCE );

		if (success) {
			m_registry[seqKey] = std::move(record);
			return seqKey;
		}

		kd_runloop_debug("Failed to RegisterWaitForSingleObject for handle <%p>", eventHandle);
		return 0;
	}

	// Unregisters handle
	void unregisterHandle(uint64_t seqKey) {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		if (seqKey == 0) {
			return;
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_registry.find(seqKey);
		if (it != m_registry.end()) {
			if (it->second.waitHandle) {
				::UnregisterWait(it->second.waitHandle);
			}
			m_registry.erase(it);
		}
	}

private:
	static void CALLBACK _objectsSignaledCallback(PVOID lpParameter, BOOLEAN TimerOrWaitSignaled) {
		uint64_t firedSeqKey = static_cast<uint64_t>(reinterpret_cast<ULONG_PTR>(lpParameter));
		//kd_runloop_debug("RLWin32ObjectsWaiter: Object Signaled with seqKey <%lld>", firedSeqKey);

		RLWin32ObjectsWaiter& waiter = RLWin32ObjectsWaiter::instance();
		std::unique_lock<decltype(waiter.m_mutex)> lock(waiter.m_mutex);

		auto it = waiter.m_registry.find(firedSeqKey);
		if (it != waiter.m_registry.end()) {
			HANDLE eventHandle = it->second.eventHandle;
			HANDLE waitHandle = it->second.waitHandle;
			auto callback = std::move(it->second.callback);

			// Erase record from registries
			waiter.m_registry.erase(it);
			lock.unlock();

			// Remove handle from waiting queue
			if (waitHandle) {
				::UnregisterWait(waitHandle);
			}

			// Call callback
			if (callback) {
				callback(firedSeqKey, eventHandle);
			}
		}
	}
};

// =================================
// RLNativeWin32EventWatcher
// =================================
class RLNativeWin32EventWatcher : public std::enable_shared_from_this<RLNativeWin32EventWatcher> {
private:
	static constexpr ULONG_PTR kCompletionKeyWakeUp = 1;
	static constexpr ULONG_PTR kCompletionKeyStop = 2;

	std::weak_ptr<RLContext> m_ctxWeakPtr;
	std::atomic<bool> m_running{ false };

	HANDLE m_iocpHandle{ NULL };
	std::map<uint64_t, HANDLE> m_keyHandleMap;
	int64_t m_absoluteDeadlineMs{ -1 };

	std::deque<std::function<void(RLNativeWin32EventWatcher*)>> m_pendingCommands;
	std::atomic<bool> m_hasPendingCommands{ false };
	RLMutex m_pendingMutex;

	std::thread::id m_runLoopThreadId;
	std::thread::id m_watchThreadId;

private:
	RLNativeWin32EventWatcher(std::weak_ptr<RLContext> ctx)
		: m_ctxWeakPtr(ctx), m_runLoopThreadId(std::this_thread::get_id()) {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		m_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
		KD_ASSERT(m_iocpHandle != NULL);
	}

public:
	static std::shared_ptr<RLNativeWin32EventWatcher> createShared(std::weak_ptr<RLContext> ctx) {
		return std::shared_ptr<RLNativeWin32EventWatcher>( new RLNativeWin32EventWatcher(ctx) );
	}

	// The RLNativeWin32EventWatcher is release on watch thread
	// In addition to the iocp handle, please perform other cleanup 
	// in the stop function. 
	~RLNativeWin32EventWatcher() {
		KD_RUNLOOP_CHECK_ANY_THREADS();

		// Close iocp
		if (m_iocpHandle) {
			CloseHandle(m_iocpHandle);
			m_iocpHandle = NULL;
		}
		kd_runloop_debug("<%s> ::~RLNativeWin32EventWatcher destroyed", this_thread_id().c_str());
	}

	void start() {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (m_running.exchange(true)) {
			return;
		}

		std::thread watcherThread([watcherSp = shared_from_this()]() {
			watcherSp->m_watchThreadId = std::this_thread::get_id();
			watcherSp->_watchLoop();
		});
		watcherThread.detach();
	}

	void stop() {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (!m_running.exchange(false)) {
			return;
		}

		HANDLE iocpHandle = m_iocpHandle;
		if (iocpHandle) {
			PostQueuedCompletionStatus(iocpHandle, 0, kCompletionKeyStop, NULL);
		}
	}

	void postCommand(std::function<void(RLNativeWin32EventWatcher*)> cmd) {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		{
			std::lock_guard<RLMutex> lock(m_pendingMutex);
			m_pendingCommands.push_back(std::move(cmd));
		}
		m_hasPendingCommands.store(true, std::memory_order_release);
		wakeUp(); // Wake up watcher to process commands
	}

	void wakeUp() {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		HANDLE iocpHandle = m_iocpHandle;
		if (iocpHandle) {
			PostQueuedCompletionStatus(iocpHandle, 0, kCompletionKeyWakeUp, NULL);
		}
	}

	void addEventNotifier(const RLEvent& eventHandle) {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		
		postCommand([eventHandle](RLNativeWin32EventWatcher* watcher) {
			for (auto& pair : watcher->m_keyHandleMap) {
				if (pair.second == eventHandle) {
					return; // Already exist
				}
			}

			// Define event callback
			std::weak_ptr<RLContext> ctxWp = watcher->m_ctxWeakPtr;
			std::weak_ptr<RLNativeWin32EventWatcher> watcherWp(watcher->shared_from_this());

			auto eventCallback = [watcherWp, ctxWp](uint64_t firedSeqKey, HANDLE activeHandle) {
				auto watcherSp = watcherWp.lock();
				if (watcherSp == nullptr) {
					return;
				}

				// Removing the event prevents duplicate triggering caused by the watcher 
				// re-entering the wait before the post task to the dispatcher is executed and 
				// before processEventNotifiers execute.
				watcherSp->postCommand([firedSeqKey] (RLNativeWin32EventWatcher* watcher) {
					auto it = watcher->m_keyHandleMap.find(firedSeqKey);
					if (it != watcher->m_keyHandleMap.end()) {
						watcher->m_keyHandleMap.erase(it);
					}
				});

				// Post processing event notifiers task to dispatcher
				watcherSp->_postTaskToDispatcher(ctxWp, [ctxWp, watcherWp, activeHandle] {
					auto ctxSp = ctxWp.lock();
					if (ctxSp) {
						// Process event notifiers
						std::map<RLEvent, int> activatedEvents = { {activeHandle, RLEventNotifier::NONE} };
						ctxSp->getDispatcher()->processEventNotifiers(activatedEvents);

						// Restore the removed event on dispatcher thread
						if (!ctxSp->getDispatcher()->destroyed() && ctxSp->getDispatcher()->isEventNotifierActive(activeHandle)) {
							auto watcherSp = watcherWp.lock();
							if (watcherSp) {
								watcherSp->addEventNotifier(activeHandle);
							}
						}
					}
				});
			};

			// Register event eventHandle
			uint64_t seqKey = RLWin32ObjectsWaiter::instance().registerHandle(eventHandle, std::move(eventCallback));
			if (seqKey != 0) {
				watcher->m_keyHandleMap[seqKey] = eventHandle;
			}
		});
	}

	void removeEventNotifier(RLEvent ev) {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		postCommand([eventHandle = ev](RLNativeWin32EventWatcher* watcher) {
			for (auto it = watcher->m_keyHandleMap.begin(); it != watcher->m_keyHandleMap.end(); ++it) {
				if (it->second == eventHandle) {
					RLWin32ObjectsWaiter::instance().unregisterHandle(it->first);
					watcher->m_keyHandleMap.erase(it);
					break;
				}
			}
		});
	}

	void updateAbsoluteDeadline(int64_t absoluteDeadlineMs) {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		postCommand([absoluteDeadlineMs](RLNativeWin32EventWatcher* watcher) {
			watcher->m_absoluteDeadlineMs = absoluteDeadlineMs;
		});
	}

private:
	void _processPendingCommands() {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_watchThreadId);
		if (!m_hasPendingCommands.load(std::memory_order_acquire)) {
			return;
		}

		decltype(m_pendingCommands) cmds;
		{
			std::lock_guard<RLMutex> lock(m_pendingMutex);
			cmds.swap(m_pendingCommands);
		}
		m_hasPendingCommands.store(false, std::memory_order_relaxed);

		for (auto& cmd : cmds) {
			if (cmd) {
				cmd(this);
			}
		}
	}

	void _watchLoop() {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_watchThreadId);
		while (m_running.load(std::memory_order_relaxed)) {
			_processPendingCommands();

			if (!m_running.load(std::memory_order_relaxed)) {
				break;
			}

			DWORD bytesTransferred = 0;
			ULONG_PTR completionKey = 0;
			LPOVERLAPPED overlapped = nullptr;

			//	Calculate waiting time
			DWORD waitTimeMs = INFINITE;
			if (m_absoluteDeadlineMs >= 0) {
				int64_t nowMs = kd::now_time();
				if (nowMs >= m_absoluteDeadlineMs) {
					waitTimeMs = 0;
				} else {
					waitTimeMs = static_cast<DWORD>(m_absoluteDeadlineMs - nowMs);
				}
			}

			// Wait for events
			BOOL bSuccess = GetQueuedCompletionStatus(m_iocpHandle, &bytesTransferred, &completionKey, &overlapped, waitTimeMs);
			if (!m_running.load(std::memory_order_relaxed)) {
				break; // Stopped
			}

			// Timeout or Error occurred
			if (!bSuccess && overlapped == nullptr) {
				if (GetLastError() == WAIT_TIMEOUT) { // Timeout
					//kd_runloop_debug("RLNativeWin32EventWatcher WAIT_TIMEOUT");
					_processTimeout();
				}
				continue;
			}

			// Process completion events
			if (completionKey == kCompletionKeyWakeUp) {
				_processPendingCommands();
				continue; // Wake up
			} else if (completionKey == kCompletionKeyStop) {
				break; // Stop
			} else {
				kd_runloop_debug("[WARNING] Unknown completion with key = %lld", long long(completionKey));
				continue; // Unknown
			}
		} // while

		// Exit the thread
		_processPendingCommands();

		// Unregister all handles
		for (auto& pair : m_keyHandleMap) {
			RLWin32ObjectsWaiter::instance().unregisterHandle(pair.first);
		}
		m_keyHandleMap.clear();
	}

	void _processTimeout() {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_watchThreadId);

		// Process pending commands
		_processPendingCommands();

		std::weak_ptr<RLContext> ctxWp = m_ctxWeakPtr;
		std::weak_ptr<RLNativeWin32EventWatcher> watcherWp = shared_from_this();

		// Restore the absolute dead line before posting task
		m_absoluteDeadlineMs = -1;

		_postTaskToDispatcher(ctxWp, [ctxWp, watcherWp] {
			auto ctxSp = ctxWp.lock();
			if (ctxSp) {
				// Process timer tasks
				ctxSp->getDispatcher()->processEvents(false);

				// Update the absolute dead line after processing timer tasks
				if (!ctxSp->getDispatcher()->destroyed()) {
					auto watcherSp = watcherWp.lock();
					if (watcherSp) {
						int64_t timeMs = ctxSp->getDispatcher()->getNextAbsoluteFireTimeMs();
						watcherSp->updateAbsoluteDeadline(timeMs);
					}
				}
			}
		});
	}

	void _postTaskToDispatcher(std::weak_ptr<RLContext> ctxWp,  std::function<void()> task) {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		if (task == nullptr) {
			return;
		}

		auto ctxSp = ctxWp.lock();
		if (ctxSp) {
			ctxSp->getDispatcher()->postTask([task_ = std::move(task)]() {
				task_();
			});
		}
	}
};

// =================================
// RLNativeDispatcherWin32
// =================================
class RLNativeDispatcherWin32 : public RLDispatcher  {
	KD_DEBUG_DISPATCHER_DEFINE(RLNativeDispatcherWin32);

	static constexpr char RL_MSGWND_NAME[] = "_RLNativeDispatcherWin32MsgWindow_";
	static constexpr UINT RL_WM_WAKEUP = WM_USER + 100;

	HWND m_msgHwnd{ NULL };
	std::shared_ptr<RLNativeWin32EventWatcher> m_eventWatcher{ nullptr };

public:
	void construct() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);

		::timeBeginPeriod(1);

		// Create msg hwnd
		static std::once_flag once_flag_;
		std::call_once(once_flag_, [] {
			WNDCLASSEXA wc = { 0 };
			wc.cbSize = sizeof(wc);
			wc.lpfnWndProc = &RLNativeDispatcherWin32::MsgWndProc;
			wc.hInstance = GetModuleHandleA(NULL);
			wc.lpszClassName = RL_MSGWND_NAME;
			RegisterClassExA(&wc);
		});
		m_msgHwnd = CreateWindowExA(0, RL_MSGWND_NAME, "", 0,
			0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
		KD_ASSERT(m_msgHwnd != NULL);
		::SetWindowLongPtr(m_msgHwnd, GWLP_USERDATA, reinterpret_cast<LPARAM>(this));

		m_eventWatcher = RLNativeWin32EventWatcher::createShared(m_ctx->weakContext());
		m_eventWatcher->start();
	}

	void destroy() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (m_eventWatcher) {
			m_eventWatcher->stop();
		}

		::timeEndPeriod(1);
	}

	void addEventNotifier(const RLEventNotifier& eventNotifier) override  {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (m_eventWatcher) {
			m_eventWatcher->addEventNotifier(eventNotifier.ev);
		}
	}

	void removeEventNotifier(RLEvent handle) override  {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (m_eventWatcher) {
			m_eventWatcher->removeEventNotifier(handle);
		}
	}

	void updateTimer() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);

		if (m_eventWatcher) {
			int64_t nextAbsoluteFireTimeMs = getNextAbsoluteFireTimeMs();
			m_eventWatcher->updateAbsoluteDeadline(nextAbsoluteFireTimeMs);
		}
	}

	std::shared_ptr<RLLooper> createDefaultLooper() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		return std::shared_ptr<RLLooper>(new RLNativeLooperWin32);
	}

	void wakeUp() override {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		if (destroyed()) {
			kd_op_debug("Can not wake up when dispatcher was destroyed");
			return;
		}

		// Wake up to process events
		HWND msgWnd = m_msgHwnd;
		if (msgWnd != NULL && IsWindow(msgWnd)) {
			PostMessageA(msgWnd, RL_WM_WAKEUP, 0, 0);
		}
	}

	bool processEvents(bool canWait) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		KD_ASSERT_M(!canWait, "canWait must be false in native mode");
		if (destroyed()) {
			return false;
		}

		// Process tasks and timers
		processTasks();
		processTimerTasks();

		return true;
	}

private:
	static LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
		RLNativeDispatcherWin32* pThis = reinterpret_cast<RLNativeDispatcherWin32*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
		if (uMsg == RL_WM_WAKEUP) {
			pThis->processEvents(false);
		}

		return DefWindowProcA(hwnd, uMsg, wParam, lParam);
	}
};

// ========================================
// RLEventDispatcherWin32 
// ========================================
class RLEventDispatcherWin32 : public RLDispatcher {
	KD_DEBUG_DISPATCHER_DEFINE(RLEventDispatcherWin32);

private:
	static constexpr ULONG_PTR kCompletionKeyWakeUp = 1;
	HANDLE m_iocpHandle{ NULL };
	std::map<uint64_t, HANDLE> m_keyHandleMap;

public:
	void construct() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		m_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
		KD_ASSERT(m_iocpHandle != NULL);
	}

	void destroy() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);

		// Unregister all handles
		for (auto& pair : m_keyHandleMap) {
			RLWin32ObjectsWaiter::instance().unregisterHandle(pair.first);
		}
		m_keyHandleMap.clear();

		// Close iocp
		if (m_iocpHandle) {
			CloseHandle(m_iocpHandle);
			m_iocpHandle = NULL;
		}
	}

	void addEventNotifier(const RLEventNotifier& eventNotifier) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed() || m_iocpHandle == NULL) {
			return;
		}
		for (auto& pair : m_keyHandleMap) {
			if (pair.second == eventNotifier.ev) {
				return; // Exists
			}
		}

		// Register handle
		HANDLE handle = eventNotifier.ev;
		uint64_t seqKey = RLWin32ObjectsWaiter::instance().registerHandle(handle, [iocpHandle = m_iocpHandle](uint64_t seqKey, HANDLE handle) {
			if (iocpHandle) { 
				::PostQueuedCompletionStatus(iocpHandle, 0, static_cast<ULONG_PTR>(seqKey), NULL); 
			}
		});
		if (seqKey != 0) {
			m_keyHandleMap[seqKey] = handle;
		}
	}

	void removeEventNotifier(RLEvent handle) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);

		for (auto it = m_keyHandleMap.begin(); it != m_keyHandleMap.end(); ++it) {
			if (it->second == handle) {
				RLWin32ObjectsWaiter::instance().unregisterHandle(it->first);
				m_keyHandleMap.erase(it);
				break;
			}
		}
	}

	void updateTimer() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		wakeUp(); // Wake up to re-enter the blocked wait using the earliest time
	}

	void wakeUp() override {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		if (destroyed()) {
			return;
		}
		
		HANDLE iocpHandle = m_iocpHandle;
		if (iocpHandle) {
			PostQueuedCompletionStatus(iocpHandle, 0, kCompletionKeyWakeUp, NULL);
		}
	}

	std::shared_ptr<RLLooper> createDefaultLooper() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		return RLCommonEventLooper::creatShared();
	}

	bool processEvents(bool canWait) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed()) {
			return false;
		}

		// Process tasks and timers
		processTasks();
		processTimerTasks();

		// Calculate waiting time
		DWORD waitTimeMs = 0;
		if (canWait) {
			auto nextAbsoluteFireTimeMs = m_ctx->getDispatcher()->getNextAbsoluteFireTimeMs();
			if (nextAbsoluteFireTimeMs < 0) {
				waitTimeMs = INFINITE; // Wait forever
			} else {
				int64_t nextTimeoutMs = nextAbsoluteFireTimeMs - ::kd::now_time();
				waitTimeMs = (DWORD)(nextTimeoutMs > 0 ? nextTimeoutMs : 0);
			}
		}

		DWORD bytesTransferred = 0;
		ULONG_PTR completionKey = 0;
		LPOVERLAPPED overlapped = nullptr;

		// Wait for events
		BOOL status = GetQueuedCompletionStatus(m_iocpHandle, &bytesTransferred, &completionKey, &overlapped, waitTimeMs);
		
		// Timeout or Error occurred
		if (!status && overlapped == nullptr) {
			if (GetLastError() == WAIT_TIMEOUT) { // Timeout
				//kd_runloop_debug("RLEventDispatcherWin32 WAIT_TIMEOUT");
			}
			return true;
		}

		// Process internal events (wake-up...)
		if (completionKey < (ULONG_PTR)kRLWin32ObjectsWaiterMinSeqKey) {
			if (completionKey == kCompletionKeyWakeUp) { // Wake up
				m_ctx->processPendingCommands();
			} else {
				kd_runloop_debug("[WARNING] Unknown completion with key = %lld", long long(completionKey));
			}
			return true;
		}

		// Process event notifiers
		uint64_t seqKey = static_cast<uint64_t>(completionKey);
		auto itMap = m_keyHandleMap.find(seqKey);
		if (itMap != m_keyHandleMap.end()) {
			HANDLE activeHandle = itMap->second;
			m_keyHandleMap.erase(itMap); // Erase old record

			// Process event notifiers
			std::map<RLEvent, int> activatedEvents = { {activeHandle, RLEventNotifier::NONE} };
			processEventNotifiers(activatedEvents);
			
			// Re-register event notifiers
			if ( !destroyed() && isEventNotifierActive(activeHandle)) {
				uint64_t seqKey = RLWin32ObjectsWaiter::instance().registerHandle(activeHandle, [iocpHandle = m_iocpHandle](uint64_t seqKey, HANDLE handle) {
					if (iocpHandle) { 
						::PostQueuedCompletionStatus(iocpHandle, 0, static_cast<ULONG_PTR>(seqKey), NULL); 
					}
				});
				if (seqKey != 0) {
					m_keyHandleMap[seqKey] = activeHandle;
				}
			}
		} else {
			kd_runloop_debug("[WARNING] Unable to match completion with key = %lld", long long(completionKey));
		}

		return true;
	}
};

__NAMESPACE_KD_END

#endif // KD_OS_WIN