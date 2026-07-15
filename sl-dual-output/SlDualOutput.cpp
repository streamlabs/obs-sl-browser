#include "SlDualOutput.hpp"

#ifndef SL_DUAL_ENABLED
#define SL_DUAL_ENABLED 1
#endif

#if !SL_DUAL_ENABLED
// Stub build (obs.ver below the 32.1 functional floor): the public surface exists and does nothing, so the plugin's two lifecycle calls stay put.

#include "SlDualConfig.hpp"

#include <obs.h>

// never instantiated
class SlDualController
{
};

SlDualOutput::SlDualOutput() = default;
SlDualOutput::~SlDualOutput() = default;

SlDualOutput& SlDualOutput::instance()
{
	static SlDualOutput s_instance;
	return s_instance;
}

void SlDualOutput::initialize()
{
	blog(LOG_INFO, SL_DUAL_LOG_PREFIX "built against OBS %s, below the 32.1 functional floor; dual output is disabled", SL_DUAL_OBS_VERSION_RAW);
}

void SlDualOutput::shutdown() {}

#else // SL_DUAL_ENABLED

#include "SlDualController.hpp"

#include <QApplication>
#include <QMetaObject>
#include <QThread>

SlDualOutput::SlDualOutput() = default;
SlDualOutput::~SlDualOutput() = default;

SlDualOutput& SlDualOutput::instance()
{
	static SlDualOutput s_instance;
	return s_instance;
}

void SlDualOutput::initialize()
{
	if (!qApp)
		return;

	if (QThread::currentThread() != qApp->thread())
	{
		QMetaObject::invokeMethod(qApp, []() { SlDualOutput::instance().initialize(); }, Qt::QueuedConnection);
		return;
	}

	if (m_controller)
		return;

	auto controller = std::make_unique<SlDualController>();

	if (!controller->init())
	{
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "initialize failed");

		// the controller cleans itself up; initialize() may be retried
		return;
	}

	m_controller = std::move(controller);
}

void SlDualOutput::shutdown()
{
	if (!m_controller)
		return;

	m_controller->shutdown();
	m_controller.reset();
}

#endif // SL_DUAL_ENABLED
