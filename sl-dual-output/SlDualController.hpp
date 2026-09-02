#pragma once

// Module-internal.
// Owns the dual-output state and logic: canvas, stream output, docks, persistence, frontend event handling.
// The SlDualOutput facade creates one of these in initialize() and destroys it in shutdown().

#include "SlDualConfig.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <memory>
#include <string>

class SlDualCanvas;
class SlDualStreamOutput;
class SlDualTransitions;
class SlDualDock;
class SlDualScenesDock;
class SlDualSourcesDock;
class SlDualTransitionsDock;
enum class SlDualStreamState;

class SlDualController
{
public: // lifecycle (called by the facade)

	// defined out of line: members are incomplete types here
	SlDualController();
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
	bool sceneRemove(const std::string& name);
	bool sceneRenameActive(const std::string& name);

	// Gates the docks and streaming; the canvas stays registered so its scenes persist.
	void setEnabled(bool enabled);

	void setOutputMode(SlDualOutputMode mode);

	void transitionSelect(const std::string& name);
	void transitionSetDuration(int ms);
	bool transitionAdd(const std::string& typeId, const std::string& name);
	void transitionRemoveSelected();

public: // state shared with the docks, editor and source list
	SlDualConfig config;
	std::unique_ptr<SlDualCanvas> canvas;
	std::unique_ptr<SlDualStreamOutput> output;
	std::unique_ptr<SlDualTransitions> transitions;

	// owned by the frontend once added
	SlDualDock* dock = nullptr;
	SlDualScenesDock* scenesDock = nullptr;
	SlDualSourcesDock* sourcesDock = nullptr;
	SlDualTransitionsDock* transitionsDock = nullptr;

public: // frontend events (invoked by the registered callbacks)
	void onFrontendEvent(enum obs_frontend_event event);
	void onSaveLoad(obs_data_t* saveData, bool saving);
	void onOutputState(SlDualStreamState state, const std::string& msg);

private: // event details
	void onCollectionChanging();
	void onCollectionChanged();
	void onExit();

	// (Re)creates/adopts the canvas and applies current config to it.
	// Safe to call repeatedly; used at init, FINISHED_LOADING and collection changes, since the frontend owns canvas lifetime per collection.
	void ensureCanvas();

private: // persistence (scene collection, key "sl-dual-output")
	obs_data_t* buildSaveData() const;
	void applyLoadedData(obs_data_t* data);
	void restoreFromCollectionFile();

private: // docks
	void createDocks();
	void removeDocks();

	// Refreshes the scenes dock and rebinds the sources dock after any scene set/canvas change.
	void refreshSceneUi();
	void refreshTransitionUi();

	// Applies the selected transition and duration to the canvas channel.
	void applyTransition();

	// Hands the canvas UUID to OBS's multitrack path, or clears it. Read live at stream start, so no restart is needed.
	void applyOutputModeSetting();

private:
	bool m_callbacksRegistered = false;
	bool m_exitCleanupDone = false;
	bool m_restartOutputAfterCollectionChange = false;
};
