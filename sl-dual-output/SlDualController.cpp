#include "SlDualController.hpp"
#include "SlDualOutput.hpp"
#include "SlDualCanvas.hpp"
#include "SlDualStreamOutput.hpp"
#include "SlDualTransitions.hpp"
#include "SlDualDock.hpp"
#include "SlDualScenesDock.hpp"
#include "SlDualSourcesDock.hpp"
#include "SlDualTransitionsDock.hpp"

#include <util/platform.h>

#include <QApplication>
#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <cstring>
#include <filesystem>

static const char* kSaveKey = "sl-dual-output";
static const char* kPreviewDockId = "sl-dual-output-dock";
static const char* kScenesDockId = "sl-dual-output-scenes-dock";
static const char* kSourcesDockId = "sl-dual-output-sources-dock";
static const char* kTransitionsDockId = "sl-dual-output-transitions-dock";

static void frontendEventThunk(enum obs_frontend_event event, void* data)
{
	static_cast<SlDualController*>(data)->onFrontendEvent(event);
}

static void saveThunk(obs_data_t* saveData, bool saving, void* data)
{
	static_cast<SlDualController*>(data)->onSaveLoad(saveData, saving);
}

SlDualController::SlDualController() = default;

SlDualController::~SlDualController()
{
	shutdown();
}

bool SlDualController::init()
{
	if (!obs_initialized())
		return false;

	blog(LOG_INFO, SL_DUAL_LOG_PREFIX "initializing (built against OBS %s)", SL_DUAL_OBS_VERSION_RAW);

	transitions = std::make_unique<SlDualTransitions>();
	restoreFromCollectionFile();
	transitions->rebuild(config);

	canvas = std::make_unique<SlDualCanvas>();

	// If the frontend hasn't loaded its scene collection yet (initialize() from obs_module_post_load),
	//	defer the canvas: the frontend clears all canvases when the collection loads.
	// FINISHED_LOADING attaches it.
	obs_source_t* currentScene = obs_frontend_get_current_scene();
	bool frontendLoaded = currentScene != nullptr;

	if (currentScene)
		obs_source_release(currentScene);

	if (frontendLoaded)
		ensureCanvas();

	output = std::make_unique<SlDualStreamOutput>();
	output->setStateCallback([](SlDualStreamState state, const std::string& msg)
	{
		// Output signals arrive on OBS threads; hop to the UI thread.
		QMetaObject::invokeMethod(
			qApp,
			[state, msg]()
			{
				if (SlDualController* controller = SlDualOutput::instance().m_controller.get())
					controller->onOutputState(state, msg);
			},
			Qt::QueuedConnection);
	});

	createDocks();

	obs_frontend_add_event_callback(frontendEventThunk, this);
	obs_frontend_add_save_callback(saveThunk, this);
	m_callbacksRegistered = true;

	blog(LOG_INFO, SL_DUAL_LOG_PREFIX "ready (canvas %ux%u%s)", config.canvasWidth, config.canvasHeight, canvas->valid() ? "" : ", attach deferred until collection load");
	return true;
}

void SlDualController::ensureCanvas()
{
	if (!canvas)
		return;

	// Quiesce the preview while the canvas may be swapped out underneath.
	if (dock)
		dock->setPreviewActive(false);

	if (canvas->create(config.canvasWidth, config.canvasHeight))
	{
		config.canvasWidth = canvas->width();
		config.canvasHeight = canvas->height();

		bool firstSeed = !config.seeded;

		// seeds once or adopts scenes
		canvas->ensureScenes(config);
		applyTransition();
		canvas->verifyChannelIntegrity();
		config.activeScene = canvas->activeSceneName();

		if (firstSeed)
		{
			config.seeded = true;

			// persist the seed marker promptly
			obs_frontend_save();
		}

		if (dock)
			dock->setPreviewActive(true);
	}
}

void SlDualController::shutdown()
{
	if (m_callbacksRegistered)
	{
		obs_frontend_remove_event_callback(frontendEventThunk, this);
		obs_frontend_remove_save_callback(saveThunk, this);
		m_callbacksRegistered = false;
	}

	removeDocks();

	if (output)
	{
		output->hardStop();
		output.reset();
	}

	if (canvas)
	{
		canvas->destroy();
		canvas.reset();
	}

	// After the canvas: its channel held the selected instance.
	if (transitions)
		transitions.reset();
}

/**
* Actions
*/

void SlDualController::startStream()
{
	if (!canvas || !canvas->valid() || !output)
		return;

	output->start(config, canvas->video());
}

void SlDualController::stopStream()
{
	if (output)
		output->requestStop();
}

bool SlDualController::streamActive() const
{
	return output && output->active();
}

void SlDualController::applySettings(const SlDualConfig& next)
{
	// Scene state is owned by the dock/editor; preserve it.
	std::string activeScene = config.activeScene;

	config = next;
	config.activeScene = activeScene;

	if (canvas && !streamActive())
	{
		canvas->resetVideo(config.canvasWidth, config.canvasHeight);
		config.canvasWidth = canvas->width();
		config.canvasHeight = canvas->height();
	}

	// persist promptly via the save callback
	obs_frontend_save();
}

void SlDualController::sceneSetActive(const std::string& name)
{
	if (canvas && canvas->setActiveScene(name))
	{
		config.activeScene = canvas->activeSceneName();

		if (dock)
			dock->resetEditorState(false);
	}

	refreshSceneUi();
}

bool SlDualController::sceneCreate(const std::string& name)
{
	bool ok = canvas && canvas->createScene(name);

	if (ok)
	{
		config.activeScene = canvas->activeSceneName();

		if (std::find(config.sceneOrder.begin(), config.sceneOrder.end(), name) == config.sceneOrder.end())
			config.sceneOrder.push_back(name);

		if (dock)
			dock->resetEditorState(false);
		obs_frontend_save();
	}

	refreshSceneUi();
	return ok;
}

void SlDualController::sceneRemoveActive()
{
	if (!canvas)
		return;

	// Quiesce the preview: the draw callback must not enumerate a scene that is being destroyed.
	if (dock)
	{
		dock->setPreviewActive(false);
		dock->resetEditorState(false);
	}

	std::string removed = canvas->activeSceneName();

	if (canvas->removeActiveScene())
	{
		config.activeScene = canvas->activeSceneName();
		config.sceneOrder.erase(std::remove(config.sceneOrder.begin(), config.sceneOrder.end(), removed), config.sceneOrder.end());
		obs_frontend_save();
	}

	if (dock)
		dock->setPreviewActive(true);

	refreshSceneUi();
}

bool SlDualController::sceneRenameActive(const std::string& name)
{
	std::string oldName = canvas ? canvas->activeSceneName() : std::string();
	bool ok = canvas && canvas->renameActiveScene(name);

	if (ok)
	{
		config.activeScene = canvas->activeSceneName();
		std::replace(config.sceneOrder.begin(), config.sceneOrder.end(), oldName, name);
		obs_frontend_save();
	}

	refreshSceneUi();
	return ok;
}

void SlDualController::transitionSelect(const std::string& name)
{
	config.transitionName = name;
	applyTransition();
	obs_frontend_save();
	refreshTransitionUi();
}

void SlDualController::transitionSetDuration(int ms)
{
	config.transitionDurationMs = ms;

	if (canvas)
		canvas->setTransitionDuration(ms);
	obs_frontend_save();
}

bool SlDualController::transitionAdd(const std::string& typeId, const std::string& name)
{
	if (!transitions || !transitions->add(typeId, name))
		return false;

	config.transitionName = name;
	applyTransition();
	obs_frontend_save();
	refreshTransitionUi();
	return true;
}

void SlDualController::transitionRemoveSelected()
{
	if (!transitions || !transitions->remove(config.transitionName))
		return;

	// Fall back the same way selection resolution does (Fade, else the first instance).
	config.transitionName = transitions->selectedName(config);
	applyTransition();
	obs_frontend_save();
	refreshTransitionUi();
}

void SlDualController::applyTransition()
{
	if (!canvas || !transitions)
		return;

	canvas->setTransitionDuration(config.transitionDurationMs);
	canvas->setTransition(transitions->selected(config));
}

/**
* Events
*/

void SlDualController::onFrontendEvent(enum obs_frontend_event event)
{
	switch (event)
	{
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
	{
		if (config.autoStart && !streamActive() && !config.server.empty())
			startStream();
		break;
	}
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
	{
		if (config.autoStart && streamActive())
			stopStream();
		break;
	}
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
	{
		onCollectionChanging();
		break;
	}
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	{
		onCollectionChanged();
		break;
	}
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	{
		ensureCanvas();
		refreshSceneUi();
		break;
	}
	case OBS_FRONTEND_EVENT_EXIT:
	{
		onExit();
		break;
	}
	default:
	{
		break;
	}
	}
}

void SlDualController::onCollectionChanging()
{
	// The frontend is about to destroy its canvases and this collection's scenes.
	// Stop the output hard, quiesce the preview, drop canvas refs.
	m_restartOutputAfterCollectionChange = streamActive();

	if (output && m_restartOutputAfterCollectionChange)
	{
		output->hardStop();
		onOutputState(SlDualStreamState::Idle, "Paused for scene collection change");
	}

	if (dock)
	{
		dock->setPreviewActive(false);

		// scenes are about to be destroyed
		dock->resetEditorState(true);
	}

	if (canvas)
		canvas->detach();

	// unbinds the source list from dying scenes
	refreshSceneUi();
}

void SlDualController::onCollectionChanged()
{
	// If the new collection carried our settings, the save callback has already applied them to `config` during load.
	ensureCanvas();
	refreshSceneUi();

	if (m_restartOutputAfterCollectionChange)
	{
		m_restartOutputAfterCollectionChange = false;
		startStream();
	}
}

void SlDualController::onExit()
{
	if (m_exitCleanupDone)
		return;
	m_exitCleanupDone = true;

	// Full teardown while the frontend is still alive (the collection was already saved: SaveProjectNow runs before OBS_FRONTEND_EVENT_EXIT).
	// Holding canvas/scene refs into obs_module_unload extends libobs object lifetimes past the frontend's own teardown; release now,
	//	like the frontend does. shutdown() remains a safe no-op fallback.
	if (output)
	{
		output->hardStop();
		output.reset();
	}

	removeDocks();

	if (canvas)
	{
		canvas->destroy();
		canvas.reset();
	}

	if (transitions)
		transitions.reset();
}

void SlDualController::onOutputState(SlDualStreamState state, const std::string& msg)
{
	if (dock)
		dock->setStreamState(state, msg);
}

/**
* Persistence
*/

void SlDualController::onSaveLoad(obs_data_t* saveData, bool saving)
{
	if (saving)
	{
		obs_data_t* data = buildSaveData();
		obs_data_set_obj(saveData, kSaveKey, data);
		obs_data_release(data);
	}
	else
	{
		obs_data_t* data = obs_data_get_obj(saveData, kSaveKey);

		if (data)
		{
			applyLoadedData(data);
			obs_data_release(data);
		}
	}
}

obs_data_t* SlDualController::buildSaveData() const
{
	obs_data_t* d = obs_data_create();
	obs_data_set_int(d, "version", 4);
	obs_data_set_int(d, "canvas_width", config.canvasWidth);
	obs_data_set_int(d, "canvas_height", config.canvasHeight);
	obs_data_set_string(d, "active_scene", config.activeScene.c_str());

	obs_data_array_t* order = obs_data_array_create();

	for (const std::string& name : config.sceneOrder)
	{
		obs_data_t* entry = obs_data_create();
		obs_data_set_string(entry, "name", name.c_str());
		obs_data_array_push_back(order, entry);
		obs_data_release(entry);
	}

	obs_data_set_array(d, "scene_order", order);
	obs_data_array_release(order);

	obs_data_set_string(d, "transition", config.transitionName.c_str());
	obs_data_set_int(d, "transition_duration", config.transitionDurationMs);

	obs_data_array_t* customs = obs_data_array_create();
	std::vector<SlDualTransitionInfo> infos = transitions ? transitions->customInfos() : config.customTransitions;

	for (const SlDualTransitionInfo& info : infos)
	{
		obs_data_t* entry = obs_data_create();
		obs_data_set_string(entry, "id", info.id.c_str());
		obs_data_set_string(entry, "name", info.name.c_str());
		obs_data_set_string(entry, "settings", info.settingsJson.c_str());
		obs_data_array_push_back(customs, entry);
		obs_data_release(entry);
	}

	obs_data_set_array(d, "custom_transitions", customs);
	obs_data_array_release(customs);
	obs_data_set_bool(d, "seeded", config.seeded);
	obs_data_set_string(d, "server", config.server.c_str());
	obs_data_set_string(d, "key", config.key.c_str());
	obs_data_set_bool(d, "use_auth", config.useAuth);
	obs_data_set_string(d, "auth_username", config.authUsername.c_str());
	obs_data_set_string(d, "auth_password", config.authPassword.c_str());
	obs_data_set_string(d, "encoder_id", config.encoderId.c_str());
	obs_data_set_int(d, "video_bitrate", config.videoBitrateKbps);
	obs_data_set_int(d, "audio_bitrate", config.audioBitrateKbps);
	obs_data_set_int(d, "audio_track", config.audioTrack);
	obs_data_set_bool(d, "auto_start", config.autoStart);
	return d;
}

void SlDualController::applyLoadedData(obs_data_t* d)
{

	// Absent keys fall back to the current values.
	obs_data_set_default_int(d, "canvas_width", config.canvasWidth);
	obs_data_set_default_int(d, "canvas_height", config.canvasHeight);
	obs_data_set_default_string(d, "active_scene", config.activeScene.c_str());
	obs_data_set_default_bool(d, "seeded", config.seeded);
	obs_data_set_default_string(d, "server", config.server.c_str());
	obs_data_set_default_string(d, "key", config.key.c_str());
	obs_data_set_default_bool(d, "use_auth", config.useAuth);
	obs_data_set_default_string(d, "auth_username", config.authUsername.c_str());
	obs_data_set_default_string(d, "auth_password", config.authPassword.c_str());
	obs_data_set_default_string(d, "encoder_id", config.encoderId.c_str());
	obs_data_set_default_int(d, "video_bitrate", config.videoBitrateKbps);
	obs_data_set_default_int(d, "audio_bitrate", config.audioBitrateKbps);
	obs_data_set_default_int(d, "audio_track", config.audioTrack);
	obs_data_set_default_bool(d, "auto_start", config.autoStart);

	config.canvasWidth = (uint32_t)obs_data_get_int(d, "canvas_width");
	config.canvasHeight = (uint32_t)obs_data_get_int(d, "canvas_height");
	config.activeScene = obs_data_get_string(d, "active_scene");

	// Absent (pre-v3 saves): keep whatever order is already known.
	obs_data_array_t* order = obs_data_get_array(d, "scene_order");

	if (order)
	{
		config.sceneOrder.clear();
		size_t n = obs_data_array_count(order);

		for (size_t i = 0; i < n; i++)
		{
			obs_data_t* entry = obs_data_array_item(order, i);
			const char* name = obs_data_get_string(entry, "name");

			if (name && *name)
				config.sceneOrder.push_back(name);
			obs_data_release(entry);
		}

		obs_data_array_release(order);
	}

	obs_data_set_default_string(d, "transition", config.transitionName.c_str());
	obs_data_set_default_int(d, "transition_duration", config.transitionDurationMs);
	config.transitionName = obs_data_get_string(d, "transition");
	config.transitionDurationMs = (int)obs_data_get_int(d, "transition_duration");

	// Cleared when absent: a save without the key means no custom instances existed.
	config.customTransitions.clear();
	obs_data_array_t* customs = obs_data_get_array(d, "custom_transitions");

	if (customs)
	{
		size_t count = obs_data_array_count(customs);

		for (size_t i = 0; i < count; i++)
		{
			obs_data_t* entry = obs_data_array_item(customs, i);
			SlDualTransitionInfo info;
			info.id = obs_data_get_string(entry, "id");
			info.name = obs_data_get_string(entry, "name");
			info.settingsJson = obs_data_get_string(entry, "settings");

			if (!info.id.empty() && !info.name.empty())
				config.customTransitions.push_back(info);
			obs_data_release(entry);
		}

		obs_data_array_release(customs);
	}

	if (transitions)
		transitions->rebuild(config);

	config.seeded = obs_data_get_bool(d, "seeded");
	config.server = obs_data_get_string(d, "server");
	config.key = obs_data_get_string(d, "key");
	config.useAuth = obs_data_get_bool(d, "use_auth");
	config.authUsername = obs_data_get_string(d, "auth_username");
	config.authPassword = obs_data_get_string(d, "auth_password");
	config.encoderId = obs_data_get_string(d, "encoder_id");
	config.videoBitrateKbps = (int)obs_data_get_int(d, "video_bitrate");
	config.audioBitrateKbps = (int)obs_data_get_int(d, "audio_bitrate");
	config.audioTrack = (int)obs_data_get_int(d, "audio_track");
	config.autoStart = obs_data_get_bool(d, "auto_start");
}

void SlDualController::restoreFromCollectionFile()
{
	// The save callback's load side only fires for collections loaded while registered. initialize() may run long after the current collection loaded, so read our key straight from the collection file once.
	char* collectionName = obs_frontend_get_current_scene_collection();

	if (!collectionName)
		return;

	char scenesDir[512];

	if (os_get_config_path(scenesDir, sizeof(scenesDir), "obs-studio/basic/scenes") <= 0)
	{
		bfree(collectionName);
		return;
	}

	try
	{
		std::filesystem::path dir = std::filesystem::u8path(scenesDir);

		for (const auto& entry : std::filesystem::directory_iterator(dir))
		{
			if (!entry.is_regular_file() || entry.path().extension() != L".json")
				continue;

			obs_data_t* root = obs_data_create_from_json_file(entry.path().u8string().c_str());

			if (!root)
				continue;

			const char* name = obs_data_get_string(root, "name");
			bool match = name && strcmp(name, collectionName) == 0;

			if (match)
			{
				obs_data_t* ours = obs_data_get_obj(root, kSaveKey);

				if (ours)
				{
					applyLoadedData(ours);
					obs_data_release(ours);
					blog(LOG_INFO, SL_DUAL_LOG_PREFIX "restored settings from collection '%s'", name);
				}
			}

			obs_data_release(root);

			if (match)
				break;
		}
	}
	catch (const std::exception& e)
	{
		blog(LOG_WARNING, SL_DUAL_LOG_PREFIX "scene collection scan failed: %s", e.what());
	}

	bfree(collectionName);
}

/**
* Docks
*/

void SlDualController::createDocks()
{
	dock = new SlDualDock(*this);

	if (!obs_frontend_add_dock_by_id(kPreviewDockId, "Vertical", dock))
	{
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "failed to add preview dock");
		delete dock;
		dock = nullptr;
		return;
	}

	scenesDock = new SlDualScenesDock(*this);

	if (!obs_frontend_add_dock_by_id(kScenesDockId, "Vertical Scenes", scenesDock))
	{
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "failed to add scenes dock");
		delete scenesDock;
		scenesDock = nullptr;
	}

	sourcesDock = new SlDualSourcesDock(*this, dock->preview());

	if (!obs_frontend_add_dock_by_id(kSourcesDockId, "Vertical Sources", sourcesDock))
	{
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "failed to add sources dock");
		delete sourcesDock;
		sourcesDock = nullptr;
	}

	transitionsDock = new SlDualTransitionsDock(*this);

	if (!obs_frontend_add_dock_by_id(kTransitionsDockId, "Vertical Transitions", transitionsDock))
	{
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "failed to add transitions dock");
		delete transitionsDock;
		transitionsDock = nullptr;
	}
}

void SlDualController::removeDocks()
{
	if (transitionsDock)
	{
		obs_frontend_remove_dock(kTransitionsDockId);
		transitionsDock = nullptr;
	}

	// The frontend deletes each widget on remove; drop the sources dock first, it borrows the preview.
	if (sourcesDock)
	{
		obs_frontend_remove_dock(kSourcesDockId);
		sourcesDock = nullptr;
	}

	if (scenesDock)
	{
		obs_frontend_remove_dock(kScenesDockId);
		scenesDock = nullptr;
	}

	if (dock)
	{
		obs_frontend_remove_dock(kPreviewDockId);
		dock = nullptr;
	}
}

void SlDualController::refreshSceneUi()
{
	if (scenesDock)
		scenesDock->refresh();

	if (sourcesDock)
		sourcesDock->refreshBinding();

	refreshTransitionUi();
}

void SlDualController::refreshTransitionUi()
{
	if (transitionsDock)
		transitionsDock->refresh();
}

