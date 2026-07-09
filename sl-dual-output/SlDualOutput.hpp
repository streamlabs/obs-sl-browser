#pragma once

#include <memory>

class SlDualController;

// Dual output subsystem. Compiled into sl-browser-plugin; this is the only
// header the rest of the plugin needs.
//
// Public surface is exactly two calls:
//   SlDualOutput::instance().initialize();  // obs_module_post_load or later
//   SlDualOutput::instance().shutdown();    // obs_module_unload
//
// initialize() is idempotent, has no effect until called, may be called from
// any thread (marshals itself to the Qt main thread), and may be called late
// in the session. shutdown() is safe to call whether or not initialize() ever
// ran. Everything else lives in SlDualController.
class SlDualOutput
{
public:
	static SlDualOutput& instance();

	void initialize();
	void shutdown();

private:
	SlDualOutput();
	~SlDualOutput();
	SlDualOutput(const SlDualOutput&) = delete;
	SlDualOutput& operator=(const SlDualOutput&) = delete;

	friend class SlDualController; // its output-state callback re-enters via instance()

	std::unique_ptr<SlDualController> m_controller;
};
