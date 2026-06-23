#pragma once
#include <mutex>
#include "koda/base/kd_str.h"
#include "koda/base/kd_utils.h"
#include "koda/base/kd_trackpointer.h"
#include "koda/async/kd_operation.h"
#include "koda/sigslot/kd_sigslot.h"
#include "koda/async/kd_delayscheduler.h"


#define kd_sig_debug1(fmt, ...) printf("[SIG_TEST] " fmt "\n", ##__VA_ARGS__);

#define kd_sig_debug2(fmt, ...) \
{   static std::mutex test_debug_mtx; \
    std::lock_guard<std::mutex> lock(test_debug_mtx); \
	kd_sig_debug1(fmt, ##__VA_ARGS__); }

// kd_sigslot_test
namespace kd_sigslot_test
{
//
class Button {
public:
	kd::sigslot::Signal<> clicked;
	kd::sigslot::Signal<int, const std::string&> clicked2;

	~Button() {
		kd_sig_debug2("~Button");
	}

	void press(int idx) {
		if (idx == 2) {
			clicked2.emitSignal(1, "222");
		} else {
			clicked.emitSignal();
		}
	}
};

//
class SignalCenter {
	std::thread worker;
	int m_index = 0;

public:

	template <int index, bool startWork = true>
	static SignalCenter& getInstance() {
		static SignalCenter signal_center(index, startWork);
		return signal_center;
	}

	SignalCenter(int index, bool startWork) {
		m_index = index;
#ifdef SIGSLOT_ENABLE_LOG
		clicked.setDebugName(kd::format_str("SignalCenter%d_clicked", m_index));
		clicked2.setDebugName(kd::format_str("SignalCenter%d_clicked2", m_index));
#endif // SIGSLOT_ENABLE_LOG

		if (startWork) {
			worker = std::thread(std::bind(&SignalCenter::doWork, this));
		}
	}

	~SignalCenter() {
		if (worker.joinable()) {
			worker.join();
		}
	}

	void doWork() {
		// Emit signal from main-thread
		kd::thread_sleep(500);
		kd::OperationMainQueue::queue().addOperation([] {
			//clicked.emit(1, kd::format_str("SignalCenter%d_emit_clicked_on_main_thread", m_index));
		});

		// Emit signal from work thread
		// Can not be used like this because a lock is acquired during the emit process. 
		// If the main thread also emits at this time, it will block the main thread due to 
		// the lock waiting.
		kd::thread_sleep(500);
		//clicked2.emit(2, kd::format_str("SignalCenter%d_emit_clicked2_on_work_thread", m_index));
		kd::OperationMainQueue::queue().addOperation([this] {
			clicked2.emitSignal(2, kd::format_str("SignalCenter%d_emit_clicked2_on_work_thread", m_index));
		});
	}

	kd::sigslot::Signal<int, const std::string&> clicked;
	kd::sigslot::Signal<int, const std::string&> clicked2;
};

//
class SlotFrame : public kd::sigslot::SlotObject {
	KD_TRACKABLE_OBJECT(SlotFrame)

public:
	SlotFrame(const std::string& name = "SlotFrame") {
		data = new int(100);
		enableSlotObjectLocker();
#ifdef SIGSLOT_ENABLE_LOG
		m_debugName = name;
#endif // SIGSLOT_ENABLE_LOG
	}

	~SlotFrame() {
		kd_sig_debug2("~SlotFrame start");

		// Must call disconnect in destructor
		disconnectAllWhenDestructing();

		delete data;
		data = nullptr;
		kd_sig_debug2("~SlotFrame end");
	}

	void on_click1() {
		kd_sig_debug2("on_slot, clicked 1");
	}

	void on_click2(int a, const std::string& b) {
		kd_sig_debug2("on_slot, clicked 2, a = %d, b = %s", a, b.c_str());
	}

	void on_click21(int a, const std::string& b) {
		kd_sig_debug2("on_slot, clicked 21, a = %d, b = %s", a, b.c_str());
	}

	void on_click22(int a, const std::string& b) {
		kd::TrackPointer<SlotFrame> this_ptr(this);

		if (kd::OperationMainQueue::isMainThread()) {
			kd_sig_debug2("on_slot, clicked 22 on main thread, a = %d, b = %s", a, b.c_str());
		} else {
			kd_sig_debug2("on_slot, clicked 22 begin, a = %d, b = %s", a, b.c_str());
			kd::thread_sleep(1000);
			kd_sig_debug2("on_slot, clicked 22 end, a = %d, b = %s", a, b.c_str());

			kd::OperationMainQueue::queue().addOperation([this_ptr, a, b] {
				if (this_ptr) {
					kd_sig_debug2("on_slot, clicked 22 on main thread after sleep, a = %d, b = %s, data = %d",
                        a, b.c_str(), (*this_ptr->data));
				} else {
					kd_sig_debug2("on_slot, clicked 22 on main thread after sleep, a = %d, b = %s, this_ptr is null",
                        a, b.c_str());
				}
			});
		}
	}

	int* data;
};

static void test_sig1() {
	kd_sig_debug1("Start testing sig1");
	Button* button = new Button();
	SlotFrame* frame = new SlotFrame();

	button->clicked.connect(frame, &SlotFrame::on_click1);
	button->clicked2.connect(frame, &SlotFrame::on_click2);

	button->press(1);
	button->press(2);

	delete frame;

	/*button->clicked.disconnect_all();
	button->clicked2.disconnect_all();*/
	button->press(1);
	button->press(2);
	delete button;
}

static void test_sig2() {
	kd_sig_debug1("Start testing sig2");
	SlotFrame* frame = new SlotFrame("SlotFrame1");
	SlotFrame* frame2 = new SlotFrame("SlotFrame2");

	SignalCenter::getInstance<0>().clicked.connect(frame, &SlotFrame::on_click21);
	SignalCenter::getInstance<0>().clicked2.connect(frame, &SlotFrame::on_click22);
	SignalCenter::getInstance<1, false>().clicked.connect(frame2, &SlotFrame::on_click21);
	SignalCenter::getInstance<1, false>().clicked2.connect(frame2, &SlotFrame::on_click22);

	//sigslot::connect(SignalCenter::getInstance().clicked, frame, &SlotFrame::on_click21);
	//sigslot::connect(SignalCenter::getInstance().clicked2, frame, &SlotFrame::on_click22);

	//  [CASE]. 
	// Test delete slot during emitting signal:  ok
	// 
	// 500ms：emit clicked on main-thread
	// 1000ms：emit clicked2 on work-thread
	// 1000ms-1500ms：emit clicked2 and calling slots
	// 1200ms，release slot obj

	// 1100ms：emit clicked on main, slot Called
	// 1300ms：emit clicked on main, slot Not Called, slot obj Released

	kd::MainDelayScheduler::instance().post(1200, [frame] {
		kd_sig_debug2("About to delete slot object");
		delete frame;
	});


	// 1. emit clicked When emitting clicked2，and slot obj Not Released
	// Result: slot Called
	kd::MainDelayScheduler::instance().post(1100, [] {
		// Blocking while emitting signal 
		SignalCenter::getInstance<0>().clicked(333, "333bbbb");
		//SignalCenter::getInstance<1, false>().clicked(333, "333bbbb");
	});


	// 2. emit clicked When emitting clicked2，and slot obj Released
	// Result: The slot will not be triggered. Slot has already been destructed.
	/*kd::MainDelayScheduler::instance().post(1300, [] {
		SignalCenter::getInstance<0>().clicked(555, "555bbbb");
	});*/
}
};
