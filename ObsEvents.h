#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
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
	void pushCoalesced(const std::string &key, long long intervalMs, std::string json);
	void dropCoalesced(const std::string &key);
	void dropAllPending();
	void seedTransform(const std::string &key, const std::array<float, 5> &transform);
	bool transformChanged(const std::string &key, const std::array<float, 5> &transform);
	void dropTransform(const std::string &key);
	void dropSceneTransforms(const std::string &sceneUuid);

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

	std::atomic<bool> m_running = false;
	std::atomic<bool> m_loaded = false;
	std::atomic<bool> m_switching = false;

	std::mutex m_mtx;
	std::condition_variable m_cv;
	std::deque<std::string> m_queue;
	std::map<std::string, Coalesced> m_coalesced;

	// What each item's transform last was, so a signal that moved nothing is not an event
	std::map<std::string, std::array<float, 5>> m_transforms;
	bool m_overflowed = false;
	std::thread m_thread;
};
