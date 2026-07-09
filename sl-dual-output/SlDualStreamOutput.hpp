#pragma once

// Module-internal. RTMP output + encoders bound to the dual canvas video.

#include "SlDualConfig.hpp"

#include <obs.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

enum class SlDualStreamState
{
	Idle,
	Starting,
	Live,
	Reconnecting,
	Stopping,
};

class SlDualStreamOutput
{
public:
	// Invoked from OBS output threads; the receiver marshals to the UI thread.
	using StateCallback = std::function<void(SlDualStreamState, const std::string&)>;

	SlDualStreamOutput() = default;
	~SlDualStreamOutput();

	SlDualStreamOutput(const SlDualStreamOutput&) = delete;
	SlDualStreamOutput& operator=(const SlDualStreamOutput&) = delete;

	void setStateCallback(StateCallback callback);

	bool start(const SlDualConfig& config, video_t* canvasVideo);
	void requestStop();

	// Synchronous teardown: no state callback, force-stops if live.
	void hardStop();

	bool active() const;
	SlDualStreamState state() const { return m_state.load(); }

private:
	static void onStartSignal(void* data, calldata_t* cd);
	static void onStopSignal(void* data, calldata_t* cd);
	static void onReconnectSignal(void* data, calldata_t* cd);
	static void onReconnectSuccessSignal(void* data, calldata_t* cd);

	void connectSignals();
	void disconnectSignals();
	void releaseAll();
	void setState(SlDualStreamState state, const char* msg = nullptr);

	obs_output_t* m_output = nullptr;
	obs_service_t* m_service = nullptr;
	obs_encoder_t* m_videoEncoder = nullptr;
	obs_encoder_t* m_audioEncoder = nullptr;
	bool m_signalsConnected = false;

	std::atomic<SlDualStreamState> m_state{SlDualStreamState::Idle};
	std::mutex m_callbackMutex;
	StateCallback m_callback;
};
