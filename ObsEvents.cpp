#include "ObsEvents.h"
#include "GrpcPlugin.h"

#include <obs.hpp>

#include <util/config-file.h>

#include <QApplication>
#include <QMainWindow>
#include <QPalette>
#include <QTimer>

#include <chrono>
#include <cstring>
#include <ctime>
#include <vector>

using namespace json11;

namespace
{
constexpr long long kTransformIntervalMs = 100;
constexpr long long kSourceUpdateIntervalMs = 250;

// A coalescing entry nobody has touched for this long is forgotten
constexpr long long kCoalescedPruneMs = 10000;

// Bound on what a stalled browser process can make us hold
constexpr size_t kMaxQueued = 10000;

long long nowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string str(const char *s)
{
	return s ? s : "";
}

enum class Kind
{
	Ignore,
	Source,
	Scene,
};

// What obs_query_all_sources lists are Sources, scenes are Scenes, everything else is nobody's business
Kind classify(obs_source_t *source)
{
	if (!source)
		return Kind::Ignore;

	if (obs_source_is_group(source))
		return Kind::Source;

	switch (obs_source_get_type(source))
	{
	case OBS_SOURCE_TYPE_INPUT:
		return Kind::Source;
	case OBS_SOURCE_TYPE_SCENE:
		return Kind::Scene;
	default:
		return Kind::Ignore;
	}
}

struct OutputTransition
{
	obs_frontend_event event;
	const char *output;
	const char *state;
};

constexpr OutputTransition kOutputTransitions[] = {
	{OBS_FRONTEND_EVENT_STREAMING_STARTING, "streaming", "starting"},
	{OBS_FRONTEND_EVENT_STREAMING_STARTED, "streaming", "started"},
	{OBS_FRONTEND_EVENT_STREAMING_STOPPING, "streaming", "stopping"},
	{OBS_FRONTEND_EVENT_STREAMING_STOPPED, "streaming", "stopped"},
	{OBS_FRONTEND_EVENT_RECORDING_STARTING, "recording", "starting"},
	{OBS_FRONTEND_EVENT_RECORDING_STARTED, "recording", "started"},
	{OBS_FRONTEND_EVENT_RECORDING_PAUSED, "recording", "paused"},
	{OBS_FRONTEND_EVENT_RECORDING_UNPAUSED, "recording", "started"},
	{OBS_FRONTEND_EVENT_RECORDING_STOPPING, "recording", "stopping"},
	{OBS_FRONTEND_EVENT_RECORDING_STOPPED, "recording", "stopped"},
	{OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING, "replayBuffer", "starting"},
	{OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED, "replayBuffer", "started"},
	{OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPING, "replayBuffer", "stopping"},
	{OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED, "replayBuffer", "stopped"},
	{OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED, "virtualcam", "started"},
	{OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED, "virtualcam", "stopped"},
};

// ISO 8601 UTC, the form of the status times Streamlabs Desktop reports
std::string isoTimeNow()
{
	const auto now = std::chrono::system_clock::now();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
	const std::time_t seconds = std::chrono::system_clock::to_time_t(now);

	std::tm utc{};
	gmtime_s(&utc, &seconds);

	char buf[32];
	std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec, static_cast<int>(ms));
	return buf;
}

obs_source_t *sourceFrom(calldata_t *params)
{
	return static_cast<obs_source_t *>(calldata_ptr(params, "source"));
}

// pos, scale, rot and crop, the part of a transform an event reports
std::array<float, 9> readTransform(obs_sceneitem_t *item)
{
	vec2 pos, scale;
	obs_sceneitem_crop crop;
	obs_sceneitem_get_pos(item, &pos);
	obs_sceneitem_get_scale(item, &scale);
	obs_sceneitem_get_crop(item, &crop);

	return {pos.x, pos.y, scale.x, scale.y, obs_sceneitem_get_rot(item), static_cast<float>(crop.left), static_cast<float>(crop.top), static_cast<float>(crop.right), static_cast<float>(crop.bottom)};
}

// Scenes and groups both hold items and signal them on their own handler
bool isContainer(obs_source_t *source)
{
	return obs_source_is_scene(source) || obs_source_is_group(source);
}

// Names an item among all scenes: scene uuid, then item id
std::string itemKey(calldata_t *params)
{
	auto *scene = static_cast<obs_scene_t *>(calldata_ptr(params, "scene"));
	auto *item = static_cast<obs_sceneitem_t *>(calldata_ptr(params, "item"));

	return str(obs_source_get_uuid(obs_scene_get_source(scene))) + ":" + std::to_string(obs_sceneitem_get_id(item));
}

Json::object itemEvent(const char *event, calldata_t *params)
{
	auto *scene = static_cast<obs_scene_t *>(calldata_ptr(params, "scene"));
	auto *item = static_cast<obs_sceneitem_t *>(calldata_ptr(params, "item"));
	obs_source_t *sceneSource = obs_scene_get_source(scene);
	obs_source_t *source = obs_sceneitem_get_source(item);
	const int itemId = static_cast<int>(obs_sceneitem_get_id(item));

	return Json::object({
		{"event", event},
		{"scene", str(obs_source_get_name(sceneSource))},
		{"sceneUuid", str(obs_source_get_uuid(sceneSource))},
		{"itemId", itemId},
		{"source", str(obs_source_get_name(source))},
		{"sourceUuid", str(obs_source_get_uuid(source))},
	});
}
} // namespace

struct ObsEvents::PendingStart
{
	std::atomic<bool> accepted = false;
	OBSOutputAutoRelease output;
	OBSSignal signal;
};

void ObsEvents::start()
{
	if (m_running.exchange(true))
		return;

	m_thread = std::thread(&ObsEvents::senderThread, this);

	signal_handler_t *global = obs_get_signal_handler();
	signal_handler_connect(global, "source_create", onSourceCreate, this);
	signal_handler_connect(global, "source_remove", onSourceRemove, this);
	signal_handler_connect(global, "source_rename", onSourceRename, this);
	signal_handler_connect(global, "source_update", onSourceUpdate, this);
}

void ObsEvents::stop()
{
	if (!m_running.exchange(false))
		return;

	unregisterAllAppHotkeys();

	signal_handler_t *global = obs_get_signal_handler();
	signal_handler_disconnect(global, "source_create", onSourceCreate, this);
	signal_handler_disconnect(global, "source_remove", onSourceRemove, this);
	signal_handler_disconnect(global, "source_rename", onSourceRename, this);
	signal_handler_disconnect(global, "source_update", onSourceUpdate, this);
	disconnectScenes();

	m_cv.notify_all();

	if (m_thread.joinable())
		m_thread.join();

	std::lock_guard<std::mutex> grd(m_mtx);
	m_queue.clear();
	m_coalesced.clear();
	m_transforms.clear();
	m_pendingStarts.clear();
	m_outputStates.clear();
}

void ObsEvents::senderThread()
{
	std::unique_lock<std::mutex> lock(m_mtx);

	while (m_running)
	{
		const long long now = nowMs();
		long long nextDueMs = -1;

		for (auto itr = m_coalesced.begin(); itr != m_coalesced.end();)
		{
			Coalesced &c = itr->second;

			if (c.pending)
			{
				const long long dueMs = c.lastSentMs + c.intervalMs;

				if (now >= dueMs)
				{
					// A full queue leaves the newest payload pending
					if (enqueueLocked(c.payload))
					{
						c.pending = false;
						c.lastSentMs = now;
					}
				}
				else if (nextDueMs < 0 || dueMs < nextDueMs)
				{
					nextDueMs = dueMs;
				}

				++itr;
			}
			else if (now - c.lastSentMs > kCoalescedPruneMs)
			{
				itr = m_coalesced.erase(itr);
			}
			else
			{
				++itr;
			}
		}

		if (m_queue.empty())
		{
			const long long waitMs = nextDueMs >= 0 ? nextDueMs - now : 1000;
			m_cv.wait_for(lock, std::chrono::milliseconds(waitMs));
			continue;
		}

		std::string json = std::move(m_queue.front());
		m_queue.pop_front();
		m_overflowed = false;

		lock.unlock();

		// Not connected yet means nobody is listening
		if (auto *client = GrpcPlugin::instance().getClient())
			client->send_obsEvent(json);

		lock.lock();
	}
}

bool ObsEvents::enqueueLocked(std::string &json)
{
	if (m_queue.size() >= kMaxQueued)
	{
		if (!m_overflowed)
			blog(LOG_WARNING, "ObsEvents: queue is full, dropping events until the browser catches up");

		m_overflowed = true;
		return false;
	}

	m_queue.push_back(std::move(json));
	return true;
}

void ObsEvents::push(std::string json)
{
	{
		std::lock_guard<std::mutex> grd(m_mtx);
		enqueueLocked(json);
	}

	m_cv.notify_one();
}

void ObsEvents::pushCoalesced(const std::string &key, long long intervalMs, std::string json)
{
	{
		std::lock_guard<std::mutex> grd(m_mtx);
		Coalesced &c = m_coalesced[key];
		const long long now = nowMs();
		c.intervalMs = intervalMs;

		if (!c.pending && now - c.lastSentMs >= intervalMs && enqueueLocked(json))
		{
			c.lastSentMs = now;
		}
		else
		{
			// The sender thread sends the newest of these once the interval is up
			c.pending = true;
			c.payload = std::move(json);
		}
	}

	m_cv.notify_one();
}

void ObsEvents::dropCoalesced(const std::string &key)
{
	std::lock_guard<std::mutex> grd(m_mtx);
	m_coalesced.erase(key);
}

void ObsEvents::dropAllPending()
{
	std::lock_guard<std::mutex> grd(m_mtx);
	m_coalesced.clear();
}

void ObsEvents::seedTransform(const std::string &key, const std::array<float, 9> &transform)
{
	std::lock_guard<std::mutex> grd(m_mtx);
	m_transforms[key] = transform;
}

bool ObsEvents::transformChanged(const std::string &key, const std::array<float, 9> &transform)
{
	std::lock_guard<std::mutex> grd(m_mtx);
	// The first sighting is a baseline: obs can draw a new item before it signals item_add
	auto [itr, inserted] = m_transforms.try_emplace(key, transform);

	if (inserted || itr->second == transform)
		return false;

	itr->second = transform;
	return true;
}

void ObsEvents::dropTransform(const std::string &key)
{
	std::lock_guard<std::mutex> grd(m_mtx);
	m_transforms.erase(key);
}

void ObsEvents::dropSceneTransforms(const std::string &sceneUuid)
{
	const std::string keyPrefix = sceneUuid + ":";

	std::lock_guard<std::mutex> grd(m_mtx);
	auto first = m_transforms.lower_bound(keyPrefix);
	auto last = first;

	while (last != m_transforms.end() && last->first.compare(0, keyPrefix.size(), keyPrefix) == 0)
		++last;

	m_transforms.erase(first, last);
}

void ObsEvents::setOutputState(const char *output, const char *state)
{
	const std::string time = isoTimeNow();

	{
		std::lock_guard<std::mutex> grd(m_mtx);
		m_outputStates[output] = {state, time};
		m_pendingStarts.erase(output);
	}

	push(Json(Json::object({{"event", "outputStateChanged"}, {"output", output}, {"state", state}, {"time", time}})).dump());
}

/*static*/
void ObsEvents::onOutputStarting(void *data, calldata_t *)
{
	static_cast<PendingStart *>(data)->accepted = true;
}

// The output's "starting" signal means obs_output_start accepted the request.
// Check after the frontend callback returns so failed starts from OBS controls
// get the same terminal event as starts made through the browser API.
void ObsEvents::watchFailedStart(const char *output)
{
	auto pending = std::make_shared<PendingStart>();
	if (std::strcmp(output, "streaming") == 0)
		pending->output = obs_frontend_get_streaming_output();
	else if (std::strcmp(output, "recording") == 0)
		pending->output = obs_frontend_get_recording_output();
	else
		pending->output = obs_frontend_get_replay_buffer_output();

	if (pending->output)
		pending->signal.Connect(obs_output_get_signal_handler(pending->output), "starting", onOutputStarting, pending.get());

	{
		std::lock_guard<std::mutex> grd(m_mtx);
		m_pendingStarts[output] = pending;
	}

	const std::string name = output;
	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	QTimer::singleShot(0, mainWindow, [this, name, pending]() { checkFailedStart(name, pending); });
}

void ObsEvents::checkFailedStart(const std::string &output, const std::shared_ptr<PendingStart> &pending)
{
	if (!m_running)
		return;

	{
		std::lock_guard<std::mutex> grd(m_mtx);
		auto itr = m_pendingStarts.find(output);
		if (itr == m_pendingStarts.end() || itr->second != pending)
			return;
	}

	bool active;
	if (output == "streaming")
		active = obs_frontend_streaming_active();
	else if (output == "recording")
		active = obs_frontend_recording_active();
	else
		active = obs_frontend_replay_buffer_active();

	const bool accepted = pending->accepted || active || (pending->output && obs_output_active(pending->output));
	if (!accepted && QApplication::activeModalWidget()) {
		// OBS can enter a nested event loop while the start call is still on the stack.
		auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		QTimer::singleShot(100, mainWindow, [this, output, pending]() { checkFailedStart(output, pending); });
		return;
	}

	{
		std::lock_guard<std::mutex> grd(m_mtx);
		auto itr = m_pendingStarts.find(output);
		if (itr == m_pendingStarts.end() || itr->second != pending)
			return;
		m_pendingStarts.erase(itr);
	}

	if (!accepted)
		settleFailedStart(output.c_str(), false);
}

// OBS emits *_STARTING before a start that can fail without any *_STOPPED
void ObsEvents::settleFailedStart(const char *output, bool active)
{
	{
		std::lock_guard<std::mutex> grd(m_mtx);
		auto itr = m_outputStates.find(output);

		if (active || itr == m_outputStates.end() || itr->second.state != "starting")
			return;
	}

	setOutputState(output, "stopped");
}

Json ObsEvents::outputState()
{
	// Before the first transition seen, an output is as active as obs says
	auto describe = [this](const char *output, bool active) {
		std::lock_guard<std::mutex> grd(m_mtx);
		auto itr = m_outputStates.find(output);

		if (itr == m_outputStates.end())
			return Json::object({{"state", active ? "started" : "stopped"}, {"time", ""}});

		return Json::object({{"state", itr->second.state}, {"time", itr->second.time}});
	};

	Json::object streaming = describe("streaming", obs_frontend_streaming_active());
	OBSOutputAutoRelease streamingOutput = obs_frontend_get_streaming_output();
	streaming["reconnecting"] = streamingOutput && obs_output_reconnecting(streamingOutput);

	Json::object recording = describe("recording", obs_frontend_recording_active());
	recording["paused"] = obs_frontend_recording_paused();

	return Json::object({
		{"streaming", streaming},
		{"recording", recording},
		{"replayBuffer", describe("replayBuffer", obs_frontend_replay_buffer_active())},
		{"virtualcam", Json::object({{"active", obs_frontend_virtualcam_active()}})},
	});
}

Json ObsEvents::theme()
{
	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	const char *id = config_get_string(obs_frontend_get_user_config(), "Appearance", "Theme");

	return Json::object({{"isDark", mainWindow->palette().color(QPalette::Window).lightness() < 128}, {"id", str(id)}});
}

/*static*/
void ObsEvents::onAppHotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	ObsEvents &self = instance();
	auto *hotkey = static_cast<AppHotkey *>(data);

	if (!self.m_running)
		return;

	self.push(Json(Json::object({{"event", "hotkey"}, {"appKey", hotkey->appKey}, {"name", hotkey->name}, {"pressed", pressed}})).dump());
}

std::string ObsEvents::registerAppHotkey(const std::string &appKey, const std::string &name, const std::string &description, const Json &bindings)
{
	auto error = [](const std::string &message) { return Json(Json::object({{"error", message}})).dump(); };

	if (!m_running)
		return error("Events are not running");

	if (appKey.empty() || name.empty())
		return error("Invalid params");

	if (!bindings.is_array())
		return error("Invalid bindings");

	// Nothing is registered unless every binding is good
	OBSDataArrayAutoRelease array = obs_data_array_create();

	for (const Json &binding : bindings.array_items())
	{
		if (!binding.is_object() || !binding["key"].is_string())
			return error("Invalid binding");

		const std::string &key = binding["key"].string_value();

		if (obs_key_from_name(key.c_str()) == OBS_KEY_NONE)
			return error("Unknown key " + key);

		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "key", key.c_str());
		obs_data_set_bool(item, "shift", binding["shift"].bool_value());
		obs_data_set_bool(item, "control", binding["control"].bool_value());
		obs_data_set_bool(item, "alt", binding["alt"].bool_value());
		obs_data_set_bool(item, "command", binding["command"].bool_value());
		obs_data_array_push_back(array, item);
	}

	unregisterAppHotkeys(appKey, &name);

	auto hotkey = std::make_unique<AppHotkey>();
	hotkey->appKey = appKey;
	hotkey->name = name;
	hotkey->id = obs_hotkey_register_frontend(("SlbApp." + appKey + "." + name).c_str(), (description.empty() ? name : description).c_str(), onAppHotkey, hotkey.get());

	if (hotkey->id == OBS_INVALID_HOTKEY_ID)
		return error("obs refused the hotkey");

	obs_hotkey_load(hotkey->id, array);

	const int id = static_cast<int>(hotkey->id);

	std::lock_guard<std::mutex> grd(m_hotkeyMtx);
	m_appHotkeys[appKey][name] = std::move(hotkey);

	return Json(Json::object({{"id", id}})).dump();
}

std::string ObsEvents::unregisterAppHotkeys(const std::string &appKey, const std::string *name)
{
	int removed = 0;

	std::lock_guard<std::mutex> grd(m_hotkeyMtx);
	auto app = m_appHotkeys.find(appKey);

	if (app != m_appHotkeys.end())
	{
		auto &hotkeys = app->second;

		for (auto itr = hotkeys.begin(); itr != hotkeys.end();)
		{
			if (name && itr->first != *name)
			{
				++itr;
				continue;
			}

			obs_hotkey_unregister(itr->second->id);
			itr = hotkeys.erase(itr);
			++removed;
		}

		if (hotkeys.empty())
			m_appHotkeys.erase(app);
	}

	return Json(Json::object({{"removed", removed}})).dump();
}

void ObsEvents::unregisterAllAppHotkeys()
{
	std::lock_guard<std::mutex> grd(m_hotkeyMtx);

	for (auto &app : m_appHotkeys)
		for (auto &hotkey : app.second)
			obs_hotkey_unregister(hotkey.second->id);

	m_appHotkeys.clear();
}

void ObsEvents::connectScenes()
{
	auto connect = [](void *data, obs_source_t *source) {
		if (!isContainer(source))
			return true;

		static_cast<ObsEvents *>(data)->connectScene(source);

		// Items already there are what a later transform signal is compared with
		auto seedItem = [](obs_scene_t *scene, obs_sceneitem_t *item, void *data) {
			const std::string sceneUuid = str(obs_source_get_uuid(obs_scene_get_source(scene)));
			static_cast<ObsEvents *>(data)->seedTransform(sceneUuid + ":" + std::to_string(obs_sceneitem_get_id(item)), readTransform(item));
			return true;
		};

		obs_scene_enum_items(obs_group_or_scene_from_source(source), seedItem, data);
		return true;
	};

	obs_enum_scenes(connect, this);
	obs_enum_sources(connect, this);
}

void ObsEvents::disconnectScenes()
{
	auto disconnect = [](void *data, obs_source_t *source) {
		if (isContainer(source))
			static_cast<ObsEvents *>(data)->disconnectScene(source);

		return true;
	};

	obs_enum_scenes(disconnect, this);
	obs_enum_sources(disconnect, this);
}

// Connecting twice would deliver twice, so every connect starts from none
void ObsEvents::connectScene(obs_source_t *scene)
{
	disconnectScene(scene);

	signal_handler_t *handler = obs_source_get_signal_handler(scene);
	signal_handler_connect(handler, "item_add", onItemAdd, this);
	signal_handler_connect(handler, "item_remove", onItemRemove, this);
	signal_handler_connect(handler, "item_visible", onItemVisible, this);
	signal_handler_connect(handler, "item_locked", onItemLocked, this);
	signal_handler_connect(handler, "item_transform", onItemTransform, this);
	signal_handler_connect(handler, "destroy", onSceneDestroy, this);
}

void ObsEvents::disconnectScene(obs_source_t *scene)
{
	signal_handler_t *handler = obs_source_get_signal_handler(scene);
	signal_handler_disconnect(handler, "item_add", onItemAdd, this);
	signal_handler_disconnect(handler, "item_remove", onItemRemove, this);
	signal_handler_disconnect(handler, "item_visible", onItemVisible, this);
	signal_handler_disconnect(handler, "item_locked", onItemLocked, this);
	signal_handler_disconnect(handler, "item_transform", onItemTransform, this);
	signal_handler_disconnect(handler, "destroy", onSceneDestroy, this);
}

/*static*/
void ObsEvents::onSourceCreate(void *data, calldata_t *params)
{
	auto *self = static_cast<ObsEvents *>(data);
	obs_source_t *source = sourceFrom(params);
	const Kind kind = classify(source);

	if (kind == Kind::Ignore)
		return;

	// Connected even while events are held back: the scene's items are what come next
	if (isContainer(source))
		self->connectScene(source);

	if (!self->enabled())
		return;

	if (kind == Kind::Scene)
	{
		self->push(Json(Json::object({{"event", "sceneCreated"}, {"name", str(obs_source_get_name(source))}, {"uuid", str(obs_source_get_uuid(source))}})).dump());
		return;
	}

	self->push(Json(Json::object({{"event", "sourceCreated"}, {"name", str(obs_source_get_name(source))}, {"uuid", str(obs_source_get_uuid(source))}, {"id", str(obs_source_get_id(source))}, {"isGroup", obs_source_is_group(source)}})).dump());
}

/*static*/
void ObsEvents::onSourceRemove(void *data, calldata_t *params)
{
	auto *self = static_cast<ObsEvents *>(data);
	obs_source_t *source = sourceFrom(params);
	const Kind kind = classify(source);

	if (kind == Kind::Ignore)
		return;

	self->dropCoalesced("u:" + str(obs_source_get_uuid(source)));

	if (!self->enabled())
		return;

	if (kind == Kind::Scene)
	{
		self->push(Json(Json::object({{"event", "sceneRemoved"}, {"name", str(obs_source_get_name(source))}, {"uuid", str(obs_source_get_uuid(source))}})).dump());
		return;
	}

	self->push(Json(Json::object({{"event", "sourceRemoved"}, {"name", str(obs_source_get_name(source))}, {"uuid", str(obs_source_get_uuid(source))}, {"id", str(obs_source_get_id(source))}})).dump());
}

/*static*/
void ObsEvents::onSourceRename(void *data, calldata_t *params)
{
	auto *self = static_cast<ObsEvents *>(data);
	obs_source_t *source = sourceFrom(params);

	if (classify(source) == Kind::Ignore || !self->enabled())
		return;

	self->push(Json(Json::object({{"event", "sourceRenamed"}, {"uuid", str(obs_source_get_uuid(source))}, {"prevName", str(calldata_string(params, "prev_name"))}, {"newName", str(calldata_string(params, "new_name"))}, {"id", str(obs_source_get_id(source))}})).dump());
}

/*static*/
void ObsEvents::onSourceUpdate(void *data, calldata_t *params)
{
	auto *self = static_cast<ObsEvents *>(data);
	obs_source_t *source = sourceFrom(params);

	if (classify(source) != Kind::Source || !self->enabled())
		return;

	const std::string uuid = str(obs_source_get_uuid(source));

	self->pushCoalesced("u:" + uuid, kSourceUpdateIntervalMs, Json(Json::object({{"event", "sourceUpdated"}, {"name", str(obs_source_get_name(source))}, {"uuid", uuid}})).dump());
}

/*static*/
void ObsEvents::onSceneDestroy(void *data, calldata_t *params)
{
	auto *self = static_cast<ObsEvents *>(data);
	obs_source_t *scene = sourceFrom(params);

	self->dropSceneTransforms(str(obs_source_get_uuid(scene)));
	self->disconnectScene(scene);
}

/*static*/
void ObsEvents::onItemAdd(void *data, calldata_t *params)
{
	auto *self = static_cast<ObsEvents *>(data);

	// The transform obs reports once the new item is first drawn is not a change
	self->seedTransform(itemKey(params), readTransform(static_cast<obs_sceneitem_t *>(calldata_ptr(params, "item"))));

	if (!self->enabled())
		return;

	self->push(Json(itemEvent("sceneItemAdded", params)).dump());
}

/*static*/
void ObsEvents::onItemRemove(void *data, calldata_t *params)
{
	auto *self = static_cast<ObsEvents *>(data);
	const std::string key = itemKey(params);

	// A transform still waiting its turn would describe an item that is gone
	self->dropCoalesced("t:" + key);
	self->dropTransform(key);

	if (!self->enabled())
		return;

	self->push(Json(itemEvent("sceneItemRemoved", params)).dump());
}

/*static*/
void ObsEvents::onItemVisible(void *data, calldata_t *params)
{
	auto *self = static_cast<ObsEvents *>(data);

	if (!self->enabled())
		return;

	Json::object event = itemEvent("sceneItemUpdated", params);
	event["reason"] = "visible";
	self->push(Json(std::move(event)).dump());
}

/*static*/
void ObsEvents::onItemLocked(void *data, calldata_t *params)
{
	auto *self = static_cast<ObsEvents *>(data);

	if (!self->enabled())
		return;

	Json::object event = itemEvent("sceneItemUpdated", params);
	event["reason"] = "locked";
	self->push(Json(std::move(event)).dump());
}

/*static*/
void ObsEvents::onItemTransform(void *data, calldata_t *params)
{
	auto *self = static_cast<ObsEvents *>(data);
	auto *item = static_cast<obs_sceneitem_t *>(calldata_ptr(params, "item"));
	const std::array<float, 9> transform = readTransform(item);

	const std::string key = itemKey(params);

	// Obs signals a transform for more than a move, such as the source's size becoming known
	if (!self->transformChanged(key, transform) || !self->enabled())
		return;

	Json::object event = itemEvent("sceneItemUpdated", params);
	event["reason"] = "transform";
	event["pos"] = Json::object({{"x", transform[0]}, {"y", transform[1]}});
	event["scale"] = Json::object({{"x", transform[2]}, {"y", transform[3]}});
	event["rot"] = transform[4];
	event["crop"] = Json::object({{"left", transform[5]}, {"top", transform[6]}, {"right", transform[7]}, {"bottom", transform[8]}});

	self->pushCoalesced("t:" + key, kTransformIntervalMs, Json(std::move(event)).dump());
}

/*static*/
void ObsEvents::onFrontendEvent(enum obs_frontend_event event, void *)
{
	ObsEvents &self = instance();

	if (!self.m_running)
		return;

	for (const OutputTransition &transition : kOutputTransitions)
	{
		if (transition.event == event)
		{
			self.setOutputState(transition.output, transition.state);
			if (std::strcmp(transition.state, "starting") == 0)
				self.watchFailedStart(transition.output);
			return;
		}
	}

	switch (event)
	{
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED: {
		char *path = obs_frontend_get_last_replay();
		self.push(Json(Json::object({{"event", "replayBufferSaved"}, {"path", str(path)}})).dump());
		bfree(path);
		break;
	}

	case OBS_FRONTEND_EVENT_THEME_CHANGED: {
		Json::object payload = theme().object_items();
		payload["event"] = "themeChanged";
		self.push(Json(std::move(payload)).dump());
		break;
	}

	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		self.connectScenes();
		self.m_loaded = true;
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		self.m_switching = true;
		self.dropAllPending();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED: {
		self.m_switching = false;

		if (!self.m_loaded)
			break;

		self.connectScenes();

		char *name = obs_frontend_get_current_scene_collection();
		self.push(Json(Json::object({{"event", "sceneCollectionChanged"}, {"name", str(name)}})).dump());
		bfree(name);
		break;
	}

	case OBS_FRONTEND_EVENT_SCENE_CHANGED: {
		if (!self.enabled())
			break;

		OBSSourceAutoRelease scene = obs_frontend_get_current_scene();

		if (scene)
			self.push(Json(Json::object({{"event", "sceneSwitched"}, {"name", str(obs_source_get_name(scene))}, {"uuid", str(obs_source_get_uuid(scene))}})).dump());

		break;
	}

	case OBS_FRONTEND_EVENT_EXIT:
		self.m_loaded = false;
		break;

	default:
		break;
	}
}
