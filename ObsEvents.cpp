#include "ObsEvents.h"
#include "GrpcPlugin.h"

#include <obs.hpp>

#include <chrono>
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

obs_source_t *sourceFrom(calldata_t *params)
{
	return static_cast<obs_source_t *>(calldata_ptr(params, "source"));
}

// pos, scale and rot, the part of a transform an event reports
std::array<float, 5> readTransform(obs_sceneitem_t *item)
{
	vec2 pos, scale;
	obs_sceneitem_get_pos(item, &pos);
	obs_sceneitem_get_scale(item, &scale);

	return {pos.x, pos.y, scale.x, scale.y, obs_sceneitem_get_rot(item)};
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
					m_queue.push_back(std::move(c.payload));
					c.pending = false;
					c.lastSentMs = now;
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

void ObsEvents::push(std::string json)
{
	{
		std::lock_guard<std::mutex> grd(m_mtx);

		if (m_queue.size() >= kMaxQueued)
		{
			if (!m_overflowed)
				blog(LOG_WARNING, "ObsEvents: queue is full, dropping events until the browser catches up");

			m_overflowed = true;
			return;
		}

		m_queue.push_back(std::move(json));
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

		if (!c.pending && now - c.lastSentMs >= intervalMs)
		{
			c.lastSentMs = now;
			m_queue.push_back(std::move(json));
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

void ObsEvents::seedTransform(const std::string &key, const std::array<float, 5> &transform)
{
	std::lock_guard<std::mutex> grd(m_mtx);
	m_transforms[key] = transform;
}

bool ObsEvents::transformChanged(const std::string &key, const std::array<float, 5> &transform)
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

void ObsEvents::connectScenes()
{
	auto connect = [](void *data, obs_source_t *source) {
		if (!obs_source_is_scene(source))
			return true;

		static_cast<ObsEvents *>(data)->connectScene(source);

		// Items already there are what a later transform signal is compared with
		auto seedItem = [](obs_scene_t *scene, obs_sceneitem_t *item, void *data) {
			const std::string sceneUuid = str(obs_source_get_uuid(obs_scene_get_source(scene)));
			static_cast<ObsEvents *>(data)->seedTransform(sceneUuid + ":" + std::to_string(obs_sceneitem_get_id(item)), readTransform(item));
			return true;
		};

		obs_scene_enum_items(obs_scene_from_source(source), seedItem, data);
		return true;
	};

	obs_enum_scenes(connect, this);
	obs_enum_sources(connect, this);
}

void ObsEvents::disconnectScenes()
{
	auto disconnect = [](void *data, obs_source_t *source) {
		if (obs_source_is_scene(source))
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
	if (obs_source_is_scene(source))
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
	const std::array<float, 5> transform = readTransform(item);

	const std::string key = itemKey(params);

	// Obs signals a transform for more than a move, such as the source's size becoming known
	if (!self->transformChanged(key, transform) || !self->enabled())
		return;

	Json::object event = itemEvent("sceneItemUpdated", params);
	event["reason"] = "transform";
	event["pos"] = Json::object({{"x", transform[0]}, {"y", transform[1]}});
	event["scale"] = Json::object({{"x", transform[2]}, {"y", transform[3]}});
	event["rot"] = transform[4];

	self->pushCoalesced("t:" + key, kTransformIntervalMs, Json(std::move(event)).dump());
}

/*static*/
void ObsEvents::onFrontendEvent(enum obs_frontend_event event, void *)
{
	ObsEvents &self = instance();

	if (!self.m_running)
		return;

	switch (event)
	{
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
