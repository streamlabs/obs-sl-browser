#pragma once

#include "SlDualConfig.hpp"

#include <memory>
#include <string>
#include <vector>

class SlDualController;

// Forward declared rather than pulled from obs.h: below the canvas API floor obs.h has no obs_canvas_t at all,
//	and this header still has to compile there for the stub build. Identical to obs_canvas_t where it does exist.
struct obs_canvas;

// Dual output subsystem.
// Compiled into sl-browser-plugin; this is the only header the rest of the plugin needs.
//
// Lifecycle is two calls:
//   SlDualOutput::instance().initialize();  // obs_module_post_load or later
//   SlDualOutput::instance().shutdown();    // obs_module_unload
//
// initialize() is idempotent, has no effect until called, may be called from any thread (marshals itself to the Qt main thread),
//	and may be called late in the session. shutdown() is safe to call whether or not initialize() ever ran.
//
// The rest is the control surface the JS API drives (PluginJsHandler). Every one of those is UI-thread only and
//	returns false / empty when available() is false, so callers never need to know whether the subsystem is up.
// Everything else lives in SlDualController, which stays module-internal.
class SlDualOutput
{
public:
	static SlDualOutput& instance();

	void initialize();
	void shutdown();

public: // control surface, UI thread only

	// False in a stub build, before initialize(), and while the canvas is detached (scene collection loading/switching).
	bool available() const;

	// Borrowed, null when unavailable. Enough on its own to resolve scenes in the canvas's namespace.
	struct obs_canvas* canvas() const;

	bool sceneCreate(const std::string& name);
	bool sceneRemove(const std::string& name);
	bool sceneSetActive(const std::string& name);
	std::string activeSceneName() const;
	std::vector<std::string> sceneNames() const;

	SlDualConfig config() const;
	bool applyConfig(const SlDualConfig& next);

	bool setEnabled(bool enabled);
	bool setOutputMode(SlDualOutputMode mode);

	bool startStream();
	bool stopStream();

	// "idle" | "starting" | "live" | "reconnecting" | "stopping"
	std::string streamState() const;

private:
	SlDualOutput();
	~SlDualOutput();
	SlDualOutput(const SlDualOutput&) = delete;
	SlDualOutput& operator=(const SlDualOutput&) = delete;

	// its output-state callback re-enters via instance()
	friend class SlDualController;

	std::unique_ptr<SlDualController> m_controller;
};
