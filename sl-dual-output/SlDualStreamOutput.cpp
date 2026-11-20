#include "SlDualStreamOutput.hpp"

#include <algorithm>
#include <cstring>

static const char *stopCodeText(int code)
{
	switch (code)
	{
	case OBS_OUTPUT_SUCCESS: {
		return "Stopped";
	}
	case OBS_OUTPUT_BAD_PATH: {
		return "Invalid stream path or URL";
	}
	case OBS_OUTPUT_CONNECT_FAILED: {
		return "Failed to connect to server";
	}
	case OBS_OUTPUT_INVALID_STREAM: {
		return "Invalid stream key or channel";
	}
	case OBS_OUTPUT_DISCONNECTED: {
		return "Disconnected from server";
	}
	case OBS_OUTPUT_UNSUPPORTED: {
		return "Output format unsupported";
	}
	case OBS_OUTPUT_NO_SPACE: {
		return "Out of disk space";
	}
	case OBS_OUTPUT_ENCODE_ERROR: {
		return "Encoder error";
	}
	case OBS_OUTPUT_ERROR:
	default: {
		return "Output error";
	}
	}
}

SlDualStreamOutput::~SlDualStreamOutput()
{
	hardStop();
}

void SlDualStreamOutput::setStateCallback(StateCallback callback)
{
	std::lock_guard<std::mutex> lock(m_callbackMutex);
	m_callback = std::move(callback);
}

bool SlDualStreamOutput::active() const
{
	return m_output && obs_output_active(m_output);
}

bool SlDualStreamOutput::start(const SlDualConfig &config, video_t *canvasVideo)
{
	const SlDualStreamState state = m_state.load();

	// let the previous stop finish
	if (state == SlDualStreamState::Stopping)
		return false;

	// Already in use, so a repeated start is a no-op rather than a restart. The tracked state, not
	// obs_output_active(): that is false while an attempt is still connecting, and falling through
	// would reach releaseAll() below and tear down the output already in flight.
	if (state != SlDualStreamState::Idle || active())
		return true;

	if (!canvasVideo)
	{
		setState(SlDualStreamState::Idle, "Canvas video unavailable");
		return false;
	}

	if (config.server.empty())
	{
		setState(SlDualStreamState::Idle, "No stream server configured");
		return false;
	}

	releaseAll();

	obs_data_t *serviceSettings = obs_data_create();
	obs_data_set_string(serviceSettings, "server", config.server.c_str());
	obs_data_set_string(serviceSettings, "key", config.key.c_str());
	obs_data_set_bool(serviceSettings, "use_auth", config.useAuth);

	if (config.useAuth)
	{
		obs_data_set_string(serviceSettings, "username", config.authUsername.c_str());
		obs_data_set_string(serviceSettings, "password", config.authPassword.c_str());
	}

	m_service = obs_service_create("rtmp_custom", "sl-dual-service", serviceSettings, nullptr);
	obs_data_release(serviceSettings);

	obs_data_t *videoSettings = obs_data_create();
	obs_data_set_int(videoSettings, "bitrate", config.videoBitrateKbps);
	obs_data_set_string(videoSettings, "rate_control", "CBR");
	obs_data_set_int(videoSettings, "keyint_sec", 2);

	const char *encoderId = config.encoderId.empty() ? "obs_x264" : config.encoderId.c_str();
	m_videoEncoder = obs_video_encoder_create(encoderId, "sl-dual-video-encoder", videoSettings, nullptr);

	if (!m_videoEncoder && strcmp(encoderId, "obs_x264") != 0)
	{
		blog(LOG_WARNING, SL_DUAL_LOG_PREFIX "encoder '%s' unavailable, falling back to obs_x264", encoderId);
		m_videoEncoder = obs_video_encoder_create("obs_x264", "sl-dual-video-encoder", videoSettings, nullptr);
	}

	obs_data_release(videoSettings);

	obs_data_t *audioSettings = obs_data_create();
	obs_data_set_int(audioSettings, "bitrate", config.audioBitrateKbps);
	size_t track = (size_t)std::clamp(config.audioTrack, 1, (int)MAX_AUDIO_MIXES) - 1;
	m_audioEncoder = obs_audio_encoder_create("ffmpeg_aac", "sl-dual-audio-encoder", audioSettings, track, nullptr);
	obs_data_release(audioSettings);

	if (!m_service || !m_videoEncoder || !m_audioEncoder)
	{
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "failed to create service/encoders");
		releaseAll();
		setState(SlDualStreamState::Idle, "Failed to create output components");
		return false;
	}

	obs_encoder_set_video(m_videoEncoder, canvasVideo);
	obs_encoder_set_audio(m_audioEncoder, obs_get_audio());

	const char *outputType = obs_service_get_preferred_output_type(m_service);

	if (!outputType)
		outputType = "rtmp_output";

	m_output = obs_output_create(outputType, "sl-dual-stream", nullptr, nullptr);

	if (!m_output)
	{
		releaseAll();
		setState(SlDualStreamState::Idle, "Failed to create RTMP output");
		return false;
	}

	obs_output_set_video_encoder(m_output, m_videoEncoder);
	obs_output_set_audio_encoder(m_output, m_audioEncoder, 0);
	obs_output_set_service(m_output, m_service);
	obs_output_set_reconnect_settings(m_output, 20, 2);

	connectSignals();
	setState(SlDualStreamState::Starting, "Connecting");

	if (!obs_output_start(m_output))
	{
		const char *err = obs_output_get_last_error(m_output);
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "output start failed: %s", err ? err : "(none)");
		disconnectSignals();
		std::string msg = (err && *err) ? err : "Failed to start output";
		releaseAll();
		setState(SlDualStreamState::Idle, msg.c_str());
		return false;
	}

	blog(LOG_INFO, SL_DUAL_LOG_PREFIX "dual output started (%s, encoder %s, %d kbps, track %d)", config.server.c_str(), encoderId, config.videoBitrateKbps, config.audioTrack);
	return true;
}

void SlDualStreamOutput::requestStop()
{
	const SlDualStreamState state = m_state.load();

	if (!m_output || state == SlDualStreamState::Idle)
	{
		setState(SlDualStreamState::Idle, "Stopped");
		return;
	}

	if (state == SlDualStreamState::Stopping)
		return;

	setState(SlDualStreamState::Stopping, "Stopping");

	if (obs_output_active(m_output))
	{
		// the stop signal carries us to Idle
		obs_output_stop(m_output);
		return;
	}

	// Connecting, but not active yet, so there is no running output for a stop signal to come from.
	// Reporting Idle and walking away would leave the attempt to go live after we said it stopped.
	obs_output_force_stop(m_output);
	disconnectSignals();
	releaseAll();
	setState(SlDualStreamState::Idle, "Stopped");
}

void SlDualStreamOutput::hardStop()
{
	disconnectSignals();

	// Every output that exists, not just an active one: this runs when the canvas is about to be
	// detached, and an attempt still connecting must be cancelled before its components go away.
	if (m_output)
		obs_output_force_stop(m_output);

	releaseAll();
	m_state.store(SlDualStreamState::Idle);
}

void SlDualStreamOutput::connectSignals()
{
	if (!m_output || m_signalsConnected)
		return;

	signal_handler_t *sh = obs_output_get_signal_handler(m_output);
	signal_handler_connect(sh, "start", onStartSignal, this);
	signal_handler_connect(sh, "stop", onStopSignal, this);
	signal_handler_connect(sh, "reconnect", onReconnectSignal, this);
	signal_handler_connect(sh, "reconnect_success", onReconnectSuccessSignal, this);
	m_signalsConnected = true;
}

void SlDualStreamOutput::disconnectSignals()
{
	if (!m_output || !m_signalsConnected)
		return;

	signal_handler_t *sh = obs_output_get_signal_handler(m_output);
	signal_handler_disconnect(sh, "start", onStartSignal, this);
	signal_handler_disconnect(sh, "stop", onStopSignal, this);
	signal_handler_disconnect(sh, "reconnect", onReconnectSignal, this);
	signal_handler_disconnect(sh, "reconnect_success", onReconnectSuccessSignal, this);
	m_signalsConnected = false;
}

void SlDualStreamOutput::releaseAll()
{
	if (m_output)
	{
		disconnectSignals();
		obs_output_release(m_output);
		m_output = nullptr;
	}

	if (m_videoEncoder)
	{
		obs_encoder_release(m_videoEncoder);
		m_videoEncoder = nullptr;
	}

	if (m_audioEncoder)
	{
		obs_encoder_release(m_audioEncoder);
		m_audioEncoder = nullptr;
	}

	if (m_service)
	{
		obs_service_release(m_service);
		m_service = nullptr;
	}
}

void SlDualStreamOutput::setState(SlDualStreamState state, const char *msg)
{
	m_state.store(state);

	StateCallback callback;
	{
		std::lock_guard<std::mutex> lock(m_callbackMutex);
		callback = m_callback;
	}

	if (callback)
		callback(state, msg ? std::string(msg) : std::string());
}

void SlDualStreamOutput::onStartSignal(void *data, calldata_t *)
{
	auto *self = static_cast<SlDualStreamOutput *>(data);
	self->setState(SlDualStreamState::Live, "Live");
}

void SlDualStreamOutput::onStopSignal(void *data, calldata_t *cd)
{
	auto *self = static_cast<SlDualStreamOutput *>(data);
	int code = (int)calldata_int(cd, "code");
	const char *lastError = calldata_string(cd, "last_error");

	const char *msg;

	if (code == OBS_OUTPUT_SUCCESS)
		msg = "Stopped";
	else if (lastError && *lastError)
		msg = lastError;
	else
		msg = stopCodeText(code);

	if (code != OBS_OUTPUT_SUCCESS)
		blog(LOG_WARNING, SL_DUAL_LOG_PREFIX "output stopped, code %d: %s", code, msg);

	self->setState(SlDualStreamState::Idle, msg);
}

void SlDualStreamOutput::onReconnectSignal(void *data, calldata_t *)
{
	auto *self = static_cast<SlDualStreamOutput *>(data);
	self->setState(SlDualStreamState::Reconnecting, "Reconnecting");
}

void SlDualStreamOutput::onReconnectSuccessSignal(void *data, calldata_t *)
{
	auto *self = static_cast<SlDualStreamOutput *>(data);
	self->setState(SlDualStreamState::Live, "Live");
}
