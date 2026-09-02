#include "SlDualOutput.hpp"

#ifndef SL_DUAL_ENABLED
#define SL_DUAL_ENABLED 1
#endif

#if !SL_DUAL_ENABLED
// Stub build (obs.ver below the canvas API floor): the public surface exists and does nothing, so the plugin's callers stay put.

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
	blog(LOG_INFO, SL_DUAL_LOG_PREFIX "built against OBS %s, below the canvas API floor; dual output is disabled", SL_DUAL_OBS_VERSION_RAW);
}

void SlDualOutput::shutdown() {}

bool SlDualOutput::available() const { return false; }
struct obs_canvas* SlDualOutput::canvas() const { return nullptr; }
bool SlDualOutput::sceneCreate(const std::string&) { return false; }
bool SlDualOutput::sceneRemove(const std::string&) { return false; }
bool SlDualOutput::sceneSetActive(const std::string&) { return false; }
std::string SlDualOutput::activeSceneName() const { return std::string(); }
std::vector<std::string> SlDualOutput::sceneNames() const { return std::vector<std::string>(); }
SlDualConfig SlDualOutput::config() const { return SlDualConfig(); }
bool SlDualOutput::applyConfig(const SlDualConfig&) { return false; }
bool SlDualOutput::setEnabled(bool) { return false; }
bool SlDualOutput::setOutputMode(SlDualOutputMode) { return false; }
bool SlDualOutput::startStream() { return false; }
bool SlDualOutput::stopStream() { return false; }
std::string SlDualOutput::streamState() const { return "idle"; }

#else // SL_DUAL_ENABLED

#include "SlDualController.hpp"
#include "SlDualCanvas.hpp"
#include "SlDualStreamOutput.hpp"

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

/**
* Control surface
*
* Guarded on the canvas rather than just the controller: between SCENE_COLLECTION_CHANGING and the next
*	attach the controller is alive but its canvas is detached, and callers must see that as unavailable.
*/

bool SlDualOutput::available() const
{
	return m_controller && m_controller->canvas && m_controller->canvas->valid();
}

struct obs_canvas* SlDualOutput::canvas() const
{
	return available() ? m_controller->canvas->canvasHandle() : nullptr;
}

bool SlDualOutput::sceneCreate(const std::string& name)
{
	return available() && m_controller->sceneCreate(name);
}

bool SlDualOutput::sceneRemove(const std::string& name)
{
	return available() && m_controller->sceneRemove(name);
}

bool SlDualOutput::sceneSetActive(const std::string& name)
{
	if (!available())
		return false;

	m_controller->sceneSetActive(name);
	return m_controller->canvas->activeSceneName() == name;
}

std::string SlDualOutput::activeSceneName() const
{
	return available() ? m_controller->canvas->activeSceneName() : std::string();
}

std::vector<std::string> SlDualOutput::sceneNames() const
{
	return available() ? m_controller->canvas->sceneNames() : std::vector<std::string>();
}

SlDualConfig SlDualOutput::config() const
{
	// Readable without a canvas: the frontend needs the persisted settings before the collection finishes loading.
	return m_controller ? m_controller->config : SlDualConfig();
}

bool SlDualOutput::applyConfig(const SlDualConfig& next)
{
	if (!m_controller)
		return false;

	m_controller->applySettings(next);
	return true;
}

bool SlDualOutput::setEnabled(bool enabled)
{
	if (!m_controller)
		return false;

	m_controller->setEnabled(enabled);
	return true;
}

bool SlDualOutput::setOutputMode(SlDualOutputMode mode)
{
	if (!m_controller)
		return false;

	return m_controller->setOutputMode(mode);
}

bool SlDualOutput::startStream()
{
	if (!available())
		return false;

	// What the start itself said, not a state sampled after it. streamBusy() was wrong both ways:
	// true for a Stopping output whose start had just been rejected, and it says nothing about
	// whether this call was the one accepted.
	return m_controller->startStream();
}

bool SlDualOutput::stopStream()
{
	if (!m_controller)
		return false;

	m_controller->stopStream();
	return true;
}

std::string SlDualOutput::streamState() const
{
	if (!m_controller || !m_controller->output)
		return "idle";

	switch (m_controller->output->state())
	{
	case SlDualStreamState::Starting: return "starting";
	case SlDualStreamState::Live: return "live";
	case SlDualStreamState::Reconnecting: return "reconnecting";
	case SlDualStreamState::Stopping: return "stopping";
	case SlDualStreamState::Idle:
	default: return "idle";
	}
}

#endif // SL_DUAL_ENABLED
