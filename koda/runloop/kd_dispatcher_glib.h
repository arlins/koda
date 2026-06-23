/** ********************************************
Created by: Arlin (arlins.dps@gmail.com).
This file implements RLDispatcher for GLib
************************************************/

#pragma once
#include "koda/runloop/kd_runloopdefs.h"
#include "koda/runloop/kd_dispatcher_dummy.h"


#if defined(KD_HAS_GLIB)
#include <glib.h>

__NAMESPACE_KD_BEGIN

class RLGMainContext {
	GMainContext* m_mainContext{ nullptr };
	bool m_isContextOwned{ false };

public:
	void init() {
		m_mainContext = ::g_main_context_get_thread_default();

		if (m_mainContext != nullptr) {
			::g_main_context_ref(m_mainContext);
			m_isContextOwned = false;
		} else {
			m_mainContext = ::g_main_context_new();
			KD_ASSERT(m_mainContext != nullptr);
			if (m_mainContext) {
				m_isContextOwned = true;
				::g_main_context_push_thread_default(m_mainContext);
			}
		}
	}

	void destroy() {
		if (m_mainContext) {
			if (m_isContextOwned) {
				::g_main_context_pop_thread_default(m_mainContext);
			}

			::g_main_context_unref(m_mainContext);
			m_mainContext = nullptr;
		}
	}

	GMainContext* context() {
		return m_mainContext;
	}
};

// ============================
// RLNativeLooperGlib
// ============================
class RLNativeLooperGlib : public RLLooper {
private:
	std::atomic<bool> m_exit{ false };
	std::atomic<int> m_exitCode{ 0 };
	RLGMainContext m_mainContext;

public:
	void construct(std::weak_ptr<RLContext> ctx) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		RLLooper::construct(ctx);
		m_mainContext.init();
	}

	void destroy() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		m_mainContext.destroy();
		RLLooper::destroy();
	}

	int exec() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		auto ctx = m_ctx.lock();
		if (ctx == nullptr || ctx->getDispatcher()->destroyed()) {
			return -1;
		}
		if (m_mainContext.context() == nullptr) {
			return -1;
		}

		m_exit = false;
		m_exitCode = 0;

		// Exec the loop
		while (!m_exit && !ctx->getDispatcher()->destroyed()) {
			::g_main_context_iteration(m_mainContext.context(), TRUE);
		}

		return m_exitCode.load();
	}

	void exit(int exitCode) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		m_exitCode = exitCode;
		m_exit = true;

		if (m_mainContext.context()) {
			::g_main_context_wakeup(m_mainContext.context());
		}
	}
};

// =================================
// RLNativeDispatcherGLib
// =================================
class RLNativeDispatcherGLib : public RLDispatcher {
	KD_DEBUG_DISPATCHER_DEFINE(RLNativeDispatcherGLib);

private:
	RLGMainContext m_mainContext;
	GMainContext* m_glibCtx{ nullptr };

	GSource* m_timerSource{ nullptr };
	std::map<RLEvent, GSource*> m_fdSourceMap;
	int64_t m_lastAbsoluteFireTimeMs{ -1 };

public:
	~RLNativeDispatcherGLib() {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		// Clear glib context
		if (m_glibCtx != nullptr) {
			::g_main_context_unref(m_glibCtx);
			m_glibCtx = nullptr;
		}

		m_mainContext.destroy();
	}

	void construct() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);

		m_mainContext.init();
		m_glibCtx = m_mainContext.context();
		if (m_glibCtx) {
			::g_main_context_ref(m_glibCtx);
		}
	}

	void destroy() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		
		// Clear timer source
		_clearTimer();
		m_lastAbsoluteFireTimeMs = -1;

		// Destroy all fd sources
		for (auto& pair : m_fdSourceMap) {
			::g_source_destroy(pair.second);
			::g_source_unref(pair.second);
		}
		m_fdSourceMap.clear();
	};

	void addEventNotifier(const RLEventNotifier& eventNotifier) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed() || m_glibCtx == nullptr) {
			return;
		}

		RLEvent nativeFd = eventNotifier.ev;
		removeEventNotifier(nativeFd);

		// Setup types
		GIOCondition condition = static_cast<GIOCondition>(0);
		if (eventNotifier.type & RLEventNotifier::READ) {
			condition = static_cast<GIOCondition>(condition | G_IO_IN | G_IO_HUP | G_IO_ERR);
		}
		if (eventNotifier.type & RLEventNotifier::WRITE) {
			condition = static_cast<GIOCondition>(condition | G_IO_OUT);
		}

		// Create poll handle
		GSource* fdSource = ::g_unix_fd_source_new(nativeFd, condition);
		if (fdSource == nullptr) {
			return;
		}

		::g_source_set_callback(fdSource, G_SOURCE_FUNC(&RLNativeDispatcherGLib::_fdEventCallback), this, nullptr);
		::g_source_set_can_recurse(fdSource, TRUE);
		::g_source_attach(fdSource, m_glibCtx);

		m_fdSourceMap[nativeFd] = fdSource;
	}

	void removeEventNotifier(RLEvent ev) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (m_glibCtx == nullptr) {
			return;
		}

		auto it = m_fdSourceMap.find(ev);
		if (it != m_fdSourceMap.end()) {
			::g_source_destroy(it->second);
			::g_source_unref(it->second);
			m_fdSourceMap.erase(it);
		}
	}

	void updateTimer() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		if (destroyed() || m_glibCtx == nullptr) {
			return;
		}

		int64_t nextAbsoluteFireTimeMs = getNextAbsoluteFireTimeMs();
		if (nextAbsoluteFireTimeMs == m_lastAbsoluteFireTimeMs) {
			return;
		}
		m_lastAbsoluteFireTimeMs = nextAbsoluteFireTimeMs;

		_clearTimer();
		if (nextAbsoluteFireTimeMs < 0) {
			return;
		}

		int64_t delayMs = nextAbsoluteFireTimeMs - now_time();
		delayMs = delayMs > 0 ? delayMs : 0;

		// Reset timer
		m_timerSource = ::g_timeout_source_new(static_cast<guint>(delayMs));
		if (m_timerSource) {
			::g_source_set_callback(m_timerSource, G_SOURCE_FUNC(&RLNativeDispatcherGLib::_timerCallback), this, nullptr);
			::g_source_set_can_recurse(m_timerSource, TRUE);
			::g_source_attach(m_timerSource, m_glibCtx);
		}
	}

	std::shared_ptr<RLLooper> createDefaultLooper() override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		return std::shared_ptr<RLLooper>(new RLNativeLooperGlib);
	};

	void wakeUp() override {
		KD_RUNLOOP_CHECK_ANY_THREADS();
		if (destroyed() || m_glibCtx == nullptr) {
			return;
		}

		// When wakeUp is called, dispatcher will still holds a reference to 
		// m_glibCtx, so using wakeUp in a multi-threaded environment is safe.
		::g_main_context_ref(m_glibCtx);

		// Invoke on target loop context
		::g_main_context_invoke_full(
			m_glibCtx,
			G_PRIORITY_DEFAULT,
			[](gpointer user_data) -> gboolean {
				auto* dispatcher = static_cast<RLNativeDispatcherGLib*>(user_data);
				KD_RUNLOOP_CHECK_RAW_POINTER(dispatcher);
				if (dispatcher && !dispatcher->destroyed()) {
					dispatcher->processEvents(false);
				}
				return G_SOURCE_REMOVE;
			},
			this,
			[](gpointer user_data) {
				auto* dispatcher = static_cast<RLNativeDispatcherGLib*>(user_data);
				KD_RUNLOOP_CHECK_RAW_POINTER(dispatcher);
				if (dispatcher && dispatcher->m_glibCtx) {
					::g_main_context_unref(dispatcher->m_glibCtx);
				}
			}
		);
	}

	bool processEvents(bool canWait) override {
		KD_RUNLOOP_CHECK_SAME_THREAD(m_runLoopThreadId);
		KD_ASSERT_M(!canWait, "canWait must be false in native mode");
		if (destroyed()) {
			return false;
		}

		// Trigger events
		processTasks();
		processTimerTasks();

		return true;
	}

private:
	void _clearTimer() {
		if (m_timerSource) {
			::g_source_destroy(m_timerSource);
			::g_source_unref(m_timerSource);
			m_timerSource = nullptr;
		}
	}

	static gboolean _timerCallback(gpointer user_data) {
		auto* dispatcher = static_cast<RLNativeDispatcherGLib*>(user_data);
		KD_RUNLOOP_CHECK_RAW_POINTER(dispatcher);
		if (dispatcher == nullptr || dispatcher->destroyed()) {
			return G_SOURCE_REMOVE;
		}
		KD_RUNLOOP_CHECK_SAME_THREAD(dispatcher->m_runLoopThreadId);

		dispatcher->processEvents(false);
		dispatcher->updateTimer();

		return G_SOURCE_REMOVE;
	}

	static gboolean _fdEventCallback(gint fd, GIOCondition condition, gpointer user_data) {
		auto* dispatcher = static_cast<RLNativeDispatcherGLib*>(user_data);
		KD_RUNLOOP_CHECK_RAW_POINTER(dispatcher);
		if (dispatcher == nullptr || dispatcher->destroyed()) {
			return G_SOURCE_REMOVE;
		}
		KD_RUNLOOP_CHECK_SAME_THREAD(dispatcher->m_runLoopThreadId);

		std::map<RLEvent, int> activatedEvents;
		if (condition & (G_IO_IN | G_IO_HUP | G_IO_ERR)) {
			activatedEvents[fd] |= RLEventNotifier::READ;
		}
		if (condition & G_IO_OUT) {
			activatedEvents[fd] |= RLEventNotifier::WRITE;
		}

		if (!activatedEvents.empty()) {
			dispatcher->processEventNotifiers(activatedEvents);
		}

		return dispatcher->isEventNotifierActive(fd) ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
	}
};

__NAMESPACE_KD_END
#endif // KD_HAS_GLIB