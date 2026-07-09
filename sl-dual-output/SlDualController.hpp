#pragma once

// Module-internal. Owns the dual-output state and logic: canvas, stream
// output, dock, persistence, frontend event handling. The SlDualOutput
// facade creates one of these in initialize() and destroys it in shutdown().

#include "SlDualConfig.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <memory>
#include <string>

class SlDualCanvas;
class SlDualStreamOutput;
class SlDualDock;
enum class SlDualStreamState;

class SlDualController
{
public: // lifecycle (called by the facade)
	SlDualController(); // defined out of line: members are incomplete types here
	~SlDualController();

	SlDualController(const SlDualController&) = delete;
	SlDualController& operator=(const SlDualController&) = delete;

	bool init();
	void shutdown();

public: // actions, UI thread (dock / editor / settings dialog)
	void startStream();
	void stopStream();
	bool streamActive() const;
	void applySettings(const SlDualConfig& next);

	void sceneSetActive(const std::string& name);
	bool sceneCreate(const std::string& name);
	void sceneRemoveActive();
	bool sceneRenameActive(const std::string& name);

public: // state shared with the dock, editor and source list
	SlDualConfig config;
	std::unique_ptr<SlDualCanvas> canvas;
	std::unique_ptr<SlDualStreamOutput> output;
	SlDualDock* dock = nullptr; // owned by the frontend once added

public: // frontend events (invoked by the registered callbacks)
	void onFrontendEvent(enum obs_frontend_event event);
	void onSaveLoad(obs_data_t* saveData, bool saving);
	void onOutputState(SlDualStreamState state, const std::string& msg);

private: // event details
	void onCollectionChanging();
	void onCollectionChanged();
	void onExit();

	// (Re)creates/adopts the canvas and applies current config to it. Safe
	// to call repeatedly; used at init, FINISHED_LOADING and collection
	// changes, since the frontend owns canvas lifetime per collection.
	void ensureCanvas();

private: // persistence (scene collection, key "sl-dual-output")
	obs_data_t* buildSaveData() const;
	void applyLoadedData(obs_data_t* data);
	void restoreFromCollectionFile();

private: // dock
	void createDock();
	void removeDock();

private:
	bool m_callbacksRegistered = false;
	bool m_exitCleanupDone = false;
	bool m_restartOutputAfterCollectionChange = false;
};
