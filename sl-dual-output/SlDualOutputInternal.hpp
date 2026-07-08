#pragma once

// Module-internal. Do not include outside sl-dual-output/.

#include "SlDualOutput.hpp"
#include "SlDualConfig.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <memory>
#include <string>

class SlDualCanvas;
class SlDualStreamOutput;
class SlDualDock;
enum class SlDualStreamState;

struct SlDualOutput::Impl {
	Impl() = default;
	~Impl();

	Impl(const Impl &) = delete;
	Impl &operator=(const Impl &) = delete;

	bool init();
	void shutdown();

	// Actions. UI thread only.
	void startStream();
	void stopStream();
	bool streamActive() const;
	void applySettings(const SlDualConfig &next);

	// Canvas scene operations (driven by the dock/editor).
	void sceneSetActive(const std::string &name);
	bool sceneCreate(const std::string &name);
	void sceneRemoveActive();
	bool sceneRenameActive(const std::string &name);

	SlDualConfig config;
	std::unique_ptr<SlDualCanvas> canvas;
	std::unique_ptr<SlDualStreamOutput> output;
	SlDualDock *dock = nullptr; // owned by the frontend once added

	// (Re)creates/adopts the canvas and applies current config to it. Safe
	// to call repeatedly; used at init, FINISHED_LOADING and collection
	// changes, since the frontend owns canvas lifetime per collection.
	void ensureCanvas();

	// Events
	void onFrontendEvent(enum obs_frontend_event event);
	void onSaveLoad(obs_data_t *saveData, bool saving);
	void onCollectionChanging();
	void onCollectionChanged();
	void onExit();
	void onOutputState(SlDualStreamState state, const std::string &msg);

	// Persistence (scene collection, key "sl-dual-output")
	obs_data_t *buildSaveData() const;
	void applyLoadedData(obs_data_t *data);
	void restoreFromCollectionFile();

	void createDock();
	void removeDock();

	bool callbacksRegistered = false;
	bool exitCleanupDone = false;
	bool restartOutputAfterCollectionChange = false;
};
