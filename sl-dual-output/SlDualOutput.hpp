#pragma once

#include <memory>

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
// ran.
class SlDualOutput {
public:
	static SlDualOutput &instance();

	void initialize();
	void shutdown();

	struct Impl;

private:
	SlDualOutput() = default;
	~SlDualOutput() = default;
	SlDualOutput(const SlDualOutput &) = delete;
	SlDualOutput &operator=(const SlDualOutput &) = delete;

	std::unique_ptr<Impl> m_impl;
};
