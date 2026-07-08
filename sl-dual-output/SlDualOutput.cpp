#include "SlDualOutput.hpp"
#include "SlDualOutputInternal.hpp"
#include "SlDualCanvas.hpp"
#include "SlDualStreamOutput.hpp"
#include "SlDualDock.hpp"

#include <util/platform.h>

#include <QApplication>
#include <QMetaObject>
#include <QThread>

#include <cstring>
#include <filesystem>

namespace {

const char *kSaveKey = "sl-dual-output";
const char *kDockId = "sl-dual-output-dock";

void frontendEventThunk(enum obs_frontend_event event, void *data)
{
	static_cast<SlDualOutput::Impl *>(data)->onFrontendEvent(event);
}

void saveThunk(obs_data_t *saveData, bool saving, void *data)
{
	static_cast<SlDualOutput::Impl *>(data)->onSaveLoad(saveData, saving);
}

} // namespace

// ---- Facade -----------------------------------------------------------------

SlDualOutput &SlDualOutput::instance()
{
	static SlDualOutput s_instance;
	return s_instance;
}

void SlDualOutput::initialize()
{
	if (!qApp)
		return;

	if (QThread::currentThread() != qApp->thread()) {
		QMetaObject::invokeMethod(
			qApp, []() { SlDualOutput::instance().initialize(); }, Qt::QueuedConnection);
		return;
	}

	if (m_impl)
		return;

	auto impl = std::make_unique<Impl>();
	if (!impl->init()) {
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "initialize failed");
		return; // impl cleans itself up; initialize() may be retried
	}

	m_impl = std::move(impl);
}

void SlDualOutput::shutdown()
{
	if (!m_impl)
		return;

	m_impl->shutdown();
	m_impl.reset();
}

// ---- Impl -------------------------------------------------------------------

SlDualOutput::Impl::~Impl()
{
	shutdown();
}

bool SlDualOutput::Impl::init()
{
	if (!obs_initialized())
		return false;

	blog(LOG_INFO, SL_DUAL_LOG_PREFIX "initializing (built against OBS %s)", SL_DUAL_OBS_VERSION_RAW);

	restoreFromCollectionFile();

	canvas = std::make_unique<SlDualCanvas>();

	// If the frontend hasn't loaded its scene collection yet (initialize()
	// from obs_module_post_load), defer the canvas: the frontend clears all
	// canvases when the collection loads. FINISHED_LOADING attaches it.
	obs_source_t *currentScene = obs_frontend_get_current_scene();
	bool frontendLoaded = currentScene != nullptr;
	if (currentScene)
		obs_source_release(currentScene);

	if (frontendLoaded)
		ensureCanvas();

	output = std::make_unique<SlDualStreamOutput>();
	output->setStateCallback([](SlDualStreamState state, const std::string &msg) {
		// Output signals arrive on OBS threads; hop to the UI thread.
		QMetaObject::invokeMethod(
			qApp,
			[state, msg]() {
				if (Impl *impl = SlDualOutput::instance().m_impl.get())
					impl->onOutputState(state, msg);
			},
			Qt::QueuedConnection);
	});

	createDock();

	obs_frontend_add_event_callback(frontendEventThunk, this);
	obs_frontend_add_save_callback(saveThunk, this);
	callbacksRegistered = true;

	blog(LOG_INFO, SL_DUAL_LOG_PREFIX "ready (canvas %ux%u%s)", config.canvasWidth, config.canvasHeight,
	     canvas->valid() ? "" : ", attach deferred until collection load");
	return true;
}

void SlDualOutput::Impl::ensureCanvas()
{
	if (!canvas)
		return;

	// Quiesce the preview while the canvas may be swapped out underneath.
	if (dock)
		dock->setPreviewActive(false);

	if (canvas->create(config.canvasWidth, config.canvasHeight)) {
		config.canvasWidth = canvas->width();
		config.canvasHeight = canvas->height();
		canvas->ensureScenes(config); // seeds or adopts scenes
		canvas->verifyChannelIntegrity();
		config.activeScene = canvas->activeSceneName();
		if (dock)
			dock->setPreviewActive(true);
	}
}

void SlDualOutput::Impl::shutdown()
{
	if (callbacksRegistered) {
		obs_frontend_remove_event_callback(frontendEventThunk, this);
		obs_frontend_remove_save_callback(saveThunk, this);
		callbacksRegistered = false;
	}

	removeDock();

	if (output) {
		output->hardStop();
		output.reset();
	}

	if (canvas) {
		canvas->destroy();
		canvas.reset();
	}
}

// ---- Actions ----------------------------------------------------------------

void SlDualOutput::Impl::startStream()
{
	if (!canvas || !canvas->valid() || !output)
		return;

	output->start(config, canvas->video());
}

void SlDualOutput::Impl::stopStream()
{
	if (output)
		output->requestStop();
}

bool SlDualOutput::Impl::streamActive() const
{
	return output && output->active();
}

void SlDualOutput::Impl::applySettings(const SlDualConfig &next)
{
	// Scene state is owned by the dock/editor; preserve it.
	std::string activeScene = config.activeScene;

	config = next;
	config.activeScene = activeScene;

	if (canvas && !streamActive()) {
		canvas->resetVideo(config.canvasWidth, config.canvasHeight);
		config.canvasWidth = canvas->width();
		config.canvasHeight = canvas->height();
	}

	obs_frontend_save(); // persist promptly via the save callback
}

void SlDualOutput::Impl::sceneSetActive(const std::string &name)
{
	if (canvas && canvas->setActiveScene(name)) {
		config.activeScene = canvas->activeSceneName();
		if (dock)
			dock->resetEditorState(false);
	}
	if (dock)
		dock->refreshScenes();
}

bool SlDualOutput::Impl::sceneCreate(const std::string &name)
{
	bool ok = canvas && canvas->createScene(name);
	if (ok) {
		config.activeScene = canvas->activeSceneName();
		if (dock)
			dock->resetEditorState(false);
		obs_frontend_save();
	}
	if (dock)
		dock->refreshScenes();
	return ok;
}

void SlDualOutput::Impl::sceneRemoveActive()
{
	if (!canvas)
		return;

	// Quiesce the preview: the draw callback must not enumerate a scene
	// that is being destroyed.
	if (dock) {
		dock->setPreviewActive(false);
		dock->resetEditorState(false);
	}

	if (canvas->removeActiveScene()) {
		config.activeScene = canvas->activeSceneName();
		obs_frontend_save();
	}

	if (dock) {
		dock->setPreviewActive(true);
		dock->refreshScenes();
	}
}

bool SlDualOutput::Impl::sceneRenameActive(const std::string &name)
{
	bool ok = canvas && canvas->renameActiveScene(name);
	if (ok) {
		config.activeScene = canvas->activeSceneName();
		obs_frontend_save();
	}
	if (dock)
		dock->refreshScenes();
	return ok;
}

// ---- Events -----------------------------------------------------------------

void SlDualOutput::Impl::onFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		if (config.autoStart && !streamActive() && !config.server.empty())
			startStream();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
		if (config.autoStart && streamActive())
			stopStream();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		if (canvas)
			canvas->onProgramSceneChanged(); // retarget program-mirror items
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		onCollectionChanging();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		onCollectionChanged();
		break;
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		ensureCanvas();
		if (dock)
			dock->refreshScenes();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		onExit();
		break;
	default:
		break;
	}
}

void SlDualOutput::Impl::onCollectionChanging()
{
	// The frontend is about to destroy its canvases and this collection's
	// scenes. Stop the output hard, quiesce the preview, drop canvas refs.
	restartOutputAfterCollectionChange = streamActive();

	if (output && restartOutputAfterCollectionChange) {
		output->hardStop();
		onOutputState(SlDualStreamState::Idle, "Paused for scene collection change");
	}

	if (dock) {
		dock->setPreviewActive(false);
		dock->resetEditorState(true); // scenes are about to be destroyed
	}

	if (canvas)
		canvas->detach();

	if (dock)
		dock->refreshScenes(); // unbinds the source list from dying scenes
}

void SlDualOutput::Impl::onCollectionChanged()
{
	// If the new collection carried our settings, the save callback has
	// already applied them to `config` during load.
	ensureCanvas();

	if (dock)
		dock->refreshScenes();

	if (restartOutputAfterCollectionChange) {
		restartOutputAfterCollectionChange = false;
		startStream();
	}
}

void SlDualOutput::Impl::onExit()
{
	if (exitCleanupDone)
		return;
	exitCleanupDone = true;

	if (output)
		output->hardStop();

	// While the frontend still exists; the rest is torn down in shutdown().
	removeDock();
}

void SlDualOutput::Impl::onOutputState(SlDualStreamState state, const std::string &msg)
{
	if (dock)
		dock->setStreamState(state, msg);
}

// ---- Persistence ------------------------------------------------------------

void SlDualOutput::Impl::onSaveLoad(obs_data_t *saveData, bool saving)
{
	if (saving) {
		obs_data_t *data = buildSaveData();
		obs_data_set_obj(saveData, kSaveKey, data);
		obs_data_release(data);
	} else {
		obs_data_t *data = obs_data_get_obj(saveData, kSaveKey);
		if (data) {
			applyLoadedData(data);
			obs_data_release(data);
		}
	}
}

obs_data_t *SlDualOutput::Impl::buildSaveData() const
{
	obs_data_t *d = obs_data_create();
	obs_data_set_int(d, "version", 2);
	obs_data_set_int(d, "canvas_width", config.canvasWidth);
	obs_data_set_int(d, "canvas_height", config.canvasHeight);
	obs_data_set_string(d, "active_scene", config.activeScene.c_str());
	obs_data_set_bool(d, "follow_program", config.followProgram);
	obs_data_set_string(d, "fixed_scene", config.fixedScene.c_str());
	obs_data_set_string(d, "server", config.server.c_str());
	obs_data_set_string(d, "key", config.key.c_str());
	obs_data_set_string(d, "encoder_id", config.encoderId.c_str());
	obs_data_set_int(d, "video_bitrate", config.videoBitrateKbps);
	obs_data_set_int(d, "audio_bitrate", config.audioBitrateKbps);
	obs_data_set_int(d, "audio_track", config.audioTrack);
	obs_data_set_bool(d, "auto_start", config.autoStart);
	return d;
}

void SlDualOutput::Impl::applyLoadedData(obs_data_t *d)
{
	// Absent keys fall back to the current values.
	obs_data_set_default_int(d, "canvas_width", config.canvasWidth);
	obs_data_set_default_int(d, "canvas_height", config.canvasHeight);
	obs_data_set_default_string(d, "active_scene", config.activeScene.c_str());
	obs_data_set_default_bool(d, "follow_program", config.followProgram);
	obs_data_set_default_string(d, "fixed_scene", config.fixedScene.c_str());
	obs_data_set_default_string(d, "server", config.server.c_str());
	obs_data_set_default_string(d, "key", config.key.c_str());
	obs_data_set_default_string(d, "encoder_id", config.encoderId.c_str());
	obs_data_set_default_int(d, "video_bitrate", config.videoBitrateKbps);
	obs_data_set_default_int(d, "audio_bitrate", config.audioBitrateKbps);
	obs_data_set_default_int(d, "audio_track", config.audioTrack);
	obs_data_set_default_bool(d, "auto_start", config.autoStart);

	config.canvasWidth = (uint32_t)obs_data_get_int(d, "canvas_width");
	config.canvasHeight = (uint32_t)obs_data_get_int(d, "canvas_height");
	config.activeScene = obs_data_get_string(d, "active_scene");
	config.followProgram = obs_data_get_bool(d, "follow_program");
	config.fixedScene = obs_data_get_string(d, "fixed_scene");
	config.server = obs_data_get_string(d, "server");
	config.key = obs_data_get_string(d, "key");
	config.encoderId = obs_data_get_string(d, "encoder_id");
	config.videoBitrateKbps = (int)obs_data_get_int(d, "video_bitrate");
	config.audioBitrateKbps = (int)obs_data_get_int(d, "audio_bitrate");
	config.audioTrack = (int)obs_data_get_int(d, "audio_track");
	config.autoStart = obs_data_get_bool(d, "auto_start");
}

void SlDualOutput::Impl::restoreFromCollectionFile()
{
	// The save callback's load side only fires for collections loaded while
	// registered. initialize() may run long after the current collection
	// loaded, so read our key straight from the collection file once.
	char *collectionName = obs_frontend_get_current_scene_collection();
	if (!collectionName)
		return;

	char scenesDir[512];
	if (os_get_config_path(scenesDir, sizeof(scenesDir), "obs-studio/basic/scenes") <= 0) {
		bfree(collectionName);
		return;
	}

	try {
		std::filesystem::path dir = std::filesystem::u8path(scenesDir);
		for (const auto &entry : std::filesystem::directory_iterator(dir)) {
			if (!entry.is_regular_file() || entry.path().extension() != L".json")
				continue;

			obs_data_t *root = obs_data_create_from_json_file(entry.path().u8string().c_str());
			if (!root)
				continue;

			const char *name = obs_data_get_string(root, "name");
			bool match = name && strcmp(name, collectionName) == 0;
			if (match) {
				obs_data_t *ours = obs_data_get_obj(root, kSaveKey);
				if (ours) {
					applyLoadedData(ours);
					obs_data_release(ours);
					blog(LOG_INFO, SL_DUAL_LOG_PREFIX "restored settings from collection '%s'",
					     name);
				}
			}

			obs_data_release(root);
			if (match)
				break;
		}
	} catch (const std::exception &e) {
		blog(LOG_WARNING, SL_DUAL_LOG_PREFIX "scene collection scan failed: %s", e.what());
	}

	bfree(collectionName);
}

// ---- Dock -------------------------------------------------------------------

void SlDualOutput::Impl::createDock()
{
	dock = new SlDualDock(*this);
	if (!obs_frontend_add_dock_by_id(kDockId, "Dual Output", dock)) {
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "failed to add dock");
		delete dock;
		dock = nullptr;
	}
}

void SlDualOutput::Impl::removeDock()
{
	if (!dock)
		return;

	obs_frontend_remove_dock(kDockId); // deletes the widget
	dock = nullptr;
}
