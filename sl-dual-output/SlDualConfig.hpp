#pragma once

#include <cstdint>
#include <string>

#define SL_DUAL_LOG_PREFIX "[sl-dual-output] "

#ifndef SL_DUAL_OBS_VERSION_RAW
#define SL_DUAL_OBS_VERSION_RAW "unknown"
#endif

struct SlDualConfig
{
	uint32_t canvasWidth = 1080;
	uint32_t canvasHeight = 1920;

	std::string activeScene; // active canvas scene
	bool seeded = false;     // first scene was seeded once; never seed again

	// Legacy (pre-editor) fields, used only for the one-time seed.
	bool followProgram = true;
	std::string fixedScene;

	std::string server;
	std::string key;
	bool useAuth = false; // RTMP username/password (SE parity)
	std::string authUsername;
	std::string authPassword;
	std::string encoderId = "obs_x264";
	int videoBitrateKbps = 6000;
	int audioBitrateKbps = 160;
	int audioTrack = 1; // 1-based
	bool autoStart = false; // start/stop with the main stream
};
