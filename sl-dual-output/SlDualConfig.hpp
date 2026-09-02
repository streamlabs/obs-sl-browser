#pragma once

#include <cstdint>
#include <string>
#include <vector>

#define SL_DUAL_LOG_PREFIX "[sl-dual-output] "

#ifndef SL_DUAL_OBS_VERSION_RAW
#define SL_DUAL_OBS_VERSION_RAW "unknown"
#endif

// A user-added configurable transition instance, recreated from the collection at load.
struct SlDualTransitionInfo
{
	std::string id;
	std::string name;
	std::string settingsJson;
};

// Who streams the dual canvas. Both at once would encode and send it twice.
enum class SlDualOutputMode
{
	// Our own RTMP output (SlDualStreamOutput).
	Rtmp,

	// OBS's multitrack path picks the canvas up as its Additional Canvas; our output stays idle.
	EnhancedBroadcasting,
};

inline const char* slDualOutputModeToString(SlDualOutputMode mode)
{
	return mode == SlDualOutputMode::EnhancedBroadcasting ? "enhanced_broadcasting" : "rtmp";
}

inline SlDualOutputMode slDualOutputModeFromString(const std::string& text)
{
	return text == "enhanced_broadcasting" ? SlDualOutputMode::EnhancedBroadcasting : SlDualOutputMode::Rtmp;
}

struct SlDualConfig
{
	uint32_t canvasWidth = 1080;
	uint32_t canvasHeight = 1920;

	// active canvas scene
	std::string activeScene;

	// scenes dock display order
	std::vector<std::string> sceneOrder;

	// Scene transition for the dual canvas (name resolved against SlDualTransitions).
	std::string transitionName = "Fade";
	int transitionDurationMs = 300;
	std::vector<SlDualTransitionInfo> customTransitions;

	// first scene was seeded once; never seed again
	bool seeded = false;

	std::string server;
	std::string key;

	// RTMP username/password (SE parity)
	bool useAuth = false;
	std::string authUsername;
	std::string authPassword;
	std::string encoderId = "obs_x264";
	int videoBitrateKbps = 6000;
	int audioBitrateKbps = 160;

	// 1-based
	int audioTrack = 1;

	// start/stop with the main stream
	bool autoStart = false;

	SlDualOutputMode outputMode = SlDualOutputMode::Rtmp;

	// Docks and streaming. The canvas stays registered either way, so scenes survive a round trip through disabled.
	bool enabled = true;
};
