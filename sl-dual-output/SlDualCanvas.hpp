#pragma once

// Module-internal.
// A frontend-registered canvas with its own editable scenes.
// Scenes live in the canvas's namespace (invisible to the main OBS scene list) and are saved/restored with the scene collection by the frontend itself via canvas_uuid.

#include "SlDualConfig.hpp"

#include <obs.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class SlDualCanvas
{
public:
	SlDualCanvas() = default;
	~SlDualCanvas();

	SlDualCanvas(const SlDualCanvas &) = delete;
	SlDualCanvas &operator=(const SlDualCanvas &) = delete;

	bool create(uint32_t width, uint32_t height);
	void destroy();

	// The frontend destroys its canvases on scene collection switches; detach before the old collection unloads, create() again after.
	void detach();

	// Seeds a first scene (using legacy config for content) or adopts frontend-restored scenes; makes cfg.activeScene (or the first) active.
	void ensureScenes(const SlDualConfig &config);

	bool resetVideo(uint32_t width, uint32_t height);

	bool valid() const { return m_canvas != nullptr; }
	video_t *video() const;

	struct Size
	{
		uint32_t width;
		uint32_t height;
	};

	// The only way to read the dimensions, deliberately. Separate width() and height() accessors
	// existed and every caller used them as a pair, which meant two loads and a window in which a
	// resize could land between them - the packed field below cannot help if the pair is split.
	Size size() const
	{
		const uint64_t packed = m_size.load(std::memory_order_acquire);
		return {(uint32_t)(packed >> 32), (uint32_t)(packed & 0xffffffffu)};
	}

	obs_canvas_t *canvasHandle() const { return m_canvas; }

	// Scenes. UI thread.
	std::vector<std::string> sceneNames() const;
	std::string activeSceneName() const;

	// Borrowed, UI thread only: the pointer is only valid while nothing swaps the active scene,
	// which is the caller's own thread.
	obs_scene_t *activeScene() const;

	// A strong reference the caller must release, or null. The graphics thread enumerates the
	// active scene while the UI thread may be replacing and releasing it, so the draw callback
	// needs the scene held for the frame rather than a pointer that can go away mid-enumeration.
	obs_scene_t *activeSceneRef() const;
	bool setActiveScene(const std::string &name);

	// becomes active
	bool createScene(const std::string &name);

	// refuses to remove the last scene
	bool removeActiveScene();

	// Removes any scene by name. A background scene goes without disturbing the active one, so deleting
	// it never puts it on air; the active scene still goes through removeActiveScene().
	bool removeScene(const std::string &name);
	bool renameActiveScene(const std::string &newName);

	// Channel 0 holds the transition (like the main output's channel 0); scene switches obs_transition_start through it.
	// The reference drops on detach; the controller re-applies it after every (re)attach.
	void setTransition(obs_source_t *transition);
	void setTransitionDuration(int ms) { m_transitionDurationMs = ms; }

	// Scale-to-fill, centered (the editor's Fill Canvas preset).
	void applyFillTransform(obs_sceneitem_t *item) const;

	// Logs and repairs any divergence between the rendered channel and the scene the editor/list operate on.
	// UI thread.
	void verifyChannelIntegrity();

	// Graphics thread.
	// Only invoked while attached; the facade removes display draw callbacks around detach()/destroy().
	void renderPreview(uint32_t cx, uint32_t cy);

private:
	bool attach();
	bool buildVideoInfo(struct obs_video_info &ovi, uint32_t width, uint32_t height) const;
	obs_canvas_t *findExistingByName() const;

	// new reference
	obs_scene_t *findSceneByName(const std::string &name) const;
	void setChannelToActive();

	// Animated switch to the active scene; previous is the scene source shown before the switch (nullable).
	void transitionToActive(obs_source_t *previous);
	void deselectAllInActive();

	obs_canvas_t *m_canvas = nullptr;

	// owned reference, instance belongs to SlDualTransitions
	obs_source_t *m_transition = nullptr;
	int m_transitionDurationMs = 300;

	// strong reference, guarded by m_activeSceneMutex - swapped on the UI thread, referenced from
	// the graphics thread through activeSceneRef()
	obs_scene_t *m_activeScene = nullptr;
	mutable std::mutex m_activeSceneMutex;

	void swapActiveScene(obs_scene_t *next);

	// (width << 32) | height. resetVideo() writes this from the UI thread while renderPreview() and
	// SlDualEditor read it from the graphics thread's draw callback, so it is one atomic rather than
	// two plain fields - both to make the pair consistent and because the unsynchronised read was
	// undefined behaviour regardless of how benign it looked.
	std::atomic<uint64_t> m_size{0};

	void setSize(uint32_t width, uint32_t height) { m_size.store(((uint64_t)width << 32) | height, std::memory_order_release); }
};
