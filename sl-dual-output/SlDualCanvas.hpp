#pragma once

// Module-internal. A frontend-registered canvas with its own editable scenes.
// Scenes live in the canvas's namespace (invisible to the main OBS scene
// list) and are saved/restored with the scene collection by the frontend
// itself via canvas_uuid.

#include "SlDualConfig.hpp"

#include <obs.h>

#include <cstdint>
#include <string>
#include <vector>

class SlDualCanvas
{
public:
	SlDualCanvas() = default;
	~SlDualCanvas();

	SlDualCanvas(const SlDualCanvas&) = delete;
	SlDualCanvas& operator=(const SlDualCanvas&) = delete;

	bool create(uint32_t width, uint32_t height);
	void destroy();

	// The frontend destroys its canvases on scene collection switches;
	// detach before the old collection unloads, create() again after.
	void detach();

	// Seeds a first scene (using legacy config for content) or adopts
	// frontend-restored scenes; makes cfg.activeScene (or the first) active.
	void ensureScenes(const SlDualConfig& config);

	bool resetVideo(uint32_t width, uint32_t height);

	bool valid() const { return m_canvas != nullptr; }
	video_t *video() const;
	uint32_t width() const { return m_width; }
	uint32_t height() const { return m_height; }
	obs_canvas_t *canvasHandle() const { return m_canvas; }

	// Scenes. UI thread.
	std::vector<std::string> sceneNames() const;
	std::string activeSceneName() const;
	obs_scene_t *activeScene() const { return m_activeScene; } // borrowed
	bool setActiveScene(const std::string& name);
	bool createScene(const std::string& name); // becomes active
	bool removeActiveScene();                  // refuses to remove the last scene
	bool renameActiveScene(const std::string& newName);

	// Program mirror items: scene items tagged to always show the current
	// program scene. Tag lives in item private settings and persists.
	void onProgramSceneChanged();
	obs_sceneitem_t *addProgramMirrorItem(); // into the active scene, fill transform
	static bool isProgramMirrorItem(obs_sceneitem_t *item);
	static void markProgramMirrorItem(obs_sceneitem_t *item);
	void applyFillTransform(obs_sceneitem_t *item) const;
	bool activeSceneHasMirror() const;

	// Logs and repairs any divergence between the rendered channel and the
	// scene the editor/list operate on. UI thread.
	void verifyChannelIntegrity();

	// Graphics thread. Only invoked while attached; the facade removes
	// display draw callbacks around detach()/destroy().
	void renderPreview(uint32_t cx, uint32_t cy);

private:
	bool attach();
	bool buildVideoInfo(struct obs_video_info& ovi, uint32_t width, uint32_t height) const;
	obs_canvas_t *findExistingByName() const;
	obs_scene_t *findSceneByName(const std::string& name) const; // new reference
	void setChannelToActive();
	void deselectAllInActive();
	void refreshMirrorItemsInScene(obs_scene_t *scene, obs_source_t *program);
	void refillMirrorItems();

	obs_canvas_t *m_canvas = nullptr;
	obs_scene_t *m_activeScene = nullptr; // strong reference

	uint32_t m_width = 0;
	uint32_t m_height = 0;
};
