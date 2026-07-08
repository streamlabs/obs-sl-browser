#pragma once

#include <cstdint>
#include <string>

#define SL_DUAL_LOG_PREFIX "[sl-dual-output] "

#ifndef SL_DUAL_OBS_VERSION_RAW
#define SL_DUAL_OBS_VERSION_RAW "unknown"
#endif

struct SlDualConfig {
	uint32_t canvasWidth = 1080;
	uint32_t canvasHeight = 1920;

	std::string activeScene; // active canvas scene

	// Legacy (pre-editor) fields, kept to seed the first canvas scene.
	bool followProgram = true;
	std::string fixedScene;

	std::string server;
	std::string key;
	std::string encoderId = "obs_x264";
	int videoBitrateKbps = 6000;
	int audioBitrateKbps = 160;
	int audioTrack = 1; // 1-based
	bool autoStart = false; // start/stop with the main stream
};
