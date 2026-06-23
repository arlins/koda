/* **************************************************************
 Created by: Arlin (arlins.dps@gmail.com).
 This file implements a RAII ScopeGuard
************************************************************** */

#pragma once
#include <functional>
#include "koda/kd_global.h"


__NAMESPACE_KD_BEGIN

class ScopeGuard {
	std::function<void()> m_on_exit;
	bool m_cancel;

	KD_DISABLE_COPY(ScopeGuard);
	KD_DISABLE_MOVE(ScopeGuard);

public:
	ScopeGuard(std::function<void()> on_exit)
		: m_on_exit(std::move(on_exit))
		, m_cancel(false) {
	}

	ScopeGuard(std::function<void()> on_created,
		std::function<void()> on_exit)
		: m_on_exit(std::move(on_exit))
		, m_cancel(false) {
		if (on_created) {
			on_created();
		}
	}

	~ScopeGuard() {
		if (m_on_exit && !m_cancel) {
			m_on_exit();
		}
	}

	void cancel() {
		m_cancel = true;
	}
};

__NAMESPACE_KD_END