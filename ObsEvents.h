#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <obs.h>
#include <obs-frontend-api.h>

#include <json11/json11.hpp>

// Turns obs signals and frontend events into json events for the main browser page (events_registerReceiver).
// The obs callbacks only build a string and queue it; a thread of our own sends them, so no obs thread
// ever waits on the browser process.
class ObsEvents
{
public:
	static ObsEvents &instance()
	{
		static ObsEvents a;
		return a;
	}

	void start();
	void stop();

	static void onFrontendEvent(enum obs_frontend_event event, void *data);

	// The functions below touch frontend state and are called on the Qt UI thread

	json11::Json outputState();
	void settleFailedStart(const char *output, bool active);
	static json11::Json theme();

	// Each answers with the json the api function replies. A hotkey is a frontend hotkey, so it shows in Settings > Hotkeys
	std::string registerAppHotkey(const std::string &appKey, const std::string &name, const std::string &description, const json11::Json &bindings);
	std::string unregisterAppHotkeys(const std::string &appKey, const std::string *name);

private:
	ObsEvents() = default;
	~ObsEvents() = default;

	struct Coalesced
	{
		long long lastSentMs = 0;
		long long intervalMs = 0;
		bool pending = false;
		std::string payload;
	};

	bool enabled() const { return m_running && m_loaded && !m_switching; }

	void senderThread();
	void push(std::string json);
	bool enqueueLocked(std::string &json);
	void pushCoalesced(const std::string &key, long long intervalMs, std::string json);
	void dropCoalesced(const std::string &key);
	void dropAllPending();
	void seedTransform(const std::string &key, const std::array<float, 9> &transform);
	bool transformChanged(const std::string &key, const std::array<float, 9> &transform);
	void dropTransform(const std::string &key);
	void dropSceneTransforms(const std::string &sceneUuid);
	void setOutputState(const char *output, const char *state);
	void unregisterAllAppHotkeys();

	void connectScenes();
	void connectScene(obs_source_t *scene);
	void disconnectScene(obs_source_t *scene);
	void disconnectScenes();

	static void onSourceCreate(void *data, calldata_t *params);
	static void onSourceRemove(void *data, calldata_t *params);
	static void onSourceRename(void *data, calldata_t *params);
	static void onSourceUpdate(void *data, calldata_t *params);
	static void onSceneDestroy(void *data, calldata_t *params);
	static void onItemAdd(void *data, calldata_t *params);
	static void onItemRemove(void *data, calldata_t *params);
	static void onItemVisible(void *data, calldata_t *params);
	static void onItemLocked(void *data, calldata_t *params);
	static void onItemTransform(void *data, calldata_t *params);

	struct AppHotkey
	{
		obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
		std::string appKey;
		std::string name;
	};

	static void onAppHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);

	struct OutputState
	{
		std::string state;
		std::string time;
	};

	std::atomic<bool> m_running = false;
	std::atomic<bool> m_loaded = false;
	std::atomic<bool> m_switching = false;

	std::mutex m_mtx;
	std::condition_variable m_cv;
	std::deque<std::string> m_queue;
	std::map<std::string, Coalesced> m_coalesced;

	// What each item's transform last was, so a signal that moved nothing is not an event
	std::map<std::string, std::array<float, 9>> m_transforms;
	bool m_overflowed = false;
	std::thread m_thread;

	// The last transition seen per output, under m_mtx
	std::map<std::string, OutputState> m_outputStates;

	// appKey, then hotkey name. The AppHotkey is the callback's data, so it lives until obs_hotkey_unregister has returned
	std::mutex m_hotkeyMtx;
	std::map<std::string, std::map<std::string, std::unique_ptr<AppHotkey>>> m_appHotkeys;
};
