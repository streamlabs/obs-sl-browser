#include "SlDualCanvas.hpp"

#include <obs-frontend-api.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <cstring>

static const char* kCanvasName = "Streamlabs Vertical";
static const char* kLegacyCanvasName = "Streamlabs Dual Output";
static const char* kDefaultSceneName = "Scene";

static uint32_t alignedWidth(uint32_t w)
{
	return std::clamp(w, 32u, 8192u) & ~uint32_t(3);
}

static uint32_t alignedHeight(uint32_t h)
{
	return std::clamp(h, 32u, 8192u) & ~uint32_t(1);
}

static bool collectSceneProc(void* param, obs_source_t* source)
{
	// borrowed
	static_cast<std::vector<obs_source_t*>*>(param)->push_back(source);
	return true;
}

// Canvas holds strong refs to its scenes (SCENE_REF), so borrowed pointers collected here stay valid for the duration of the calling UI-thread scope.
static std::vector<obs_source_t*> collectScenes(obs_canvas_t* canvas)
{
	std::vector<obs_source_t*> scenes;

	if (canvas)
		obs_canvas_enum_scenes(canvas, collectSceneProc, &scenes);
	return scenes;
}

SlDualCanvas::~SlDualCanvas()
{
	destroy();
}

bool SlDualCanvas::buildVideoInfo(struct obs_video_info& ovi, uint32_t width, uint32_t height) const
{
	if (!obs_get_video_info(&ovi))
		return false;

	ovi.base_width = width;
	ovi.base_height = height;
	ovi.output_width = width;
	ovi.output_height = height;
	return true;
}

obs_canvas_t* SlDualCanvas::findExistingByName() const
{
	obs_frontend_canvas_list list = {};
	obs_frontend_get_canvases(&list);

	obs_canvas_t* found = nullptr;
	obs_canvas_t* legacy = nullptr;

	for (size_t i = 0; i < list.canvases.num; i++)
	{
		obs_canvas_t* c = list.canvases.array[i];
		const char* name = obs_canvas_get_name(c);

		if (!name)
			continue;

		if (!found && strcmp(name, kCanvasName) == 0)
			found = obs_canvas_get_ref(c);

		if (!legacy && strcmp(name, kLegacyCanvasName) == 0)
			legacy = obs_canvas_get_ref(c);
	}

	obs_frontend_canvas_list_free(&list);

	if (!found && legacy)
	{
		// Persisted by a build that used the old name; adopt and rename once (UUID stays stable).
		obs_canvas_set_name(legacy, kCanvasName);
		blog(LOG_INFO, SL_DUAL_LOG_PREFIX "renamed legacy canvas '%s' to '%s'", kLegacyCanvasName, kCanvasName);
		return legacy;
	}

	if (legacy)
		obs_canvas_release(legacy);
	return found;
}

bool SlDualCanvas::create(uint32_t width, uint32_t height)
{
	// The frontend may have destroyed the canvas under us (it clears its canvas list when (re)loading a scene collection).
	if (m_canvas && obs_canvas_removed(m_canvas))
		detach();

	if (m_canvas)
		return resetVideo(width, height);

	setSize(alignedWidth(width), alignedHeight(height));
	return attach();
}

bool SlDualCanvas::attach()
{
	if (m_canvas)
		return true;

	obs_video_info ovi;

	const Size current = size();

	if (!buildVideoInfo(ovi, current.width, current.height))
	{
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "main video not initialized, cannot create canvas");
		return false;
	}

	// Adopt the canvas if the frontend restored it from the scene collection (keeps UUID stable for Enhanced Broadcasting,
	//	and brings back its scenes, which the frontend saves/loads natively via canvas_uuid).
	m_canvas = findExistingByName();

	// SCENE_REF is mandatory: without it the canvas holds no strong refs to its scenes,
	//	and obs_load_sources() destroys restored scenes the moment it releases its own references (the main canvas survives because libobs creates it with PROGRAM = ACTIVATE|MIX_AUDIO|SCENE_REF).
	// Replace any canvas persisted by builds that lacked the flag.
	if (m_canvas && !(obs_canvas_get_flags(m_canvas) & obs_canvas_flags::SCENE_REF))
	{
		blog(LOG_INFO, SL_DUAL_LOG_PREFIX "replacing restored canvas that lacks SCENE_REF");
		obs_frontend_remove_canvas(m_canvas);
		obs_canvas_release(m_canvas);
		m_canvas = nullptr;
	}

	if (m_canvas)
	{
		if (!obs_canvas_reset_video(m_canvas, &ovi))
		{
			blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "reset video failed on adopted canvas");
			obs_canvas_release(m_canvas);
			m_canvas = nullptr;
			return false;
		}

		blog(LOG_INFO, SL_DUAL_LOG_PREFIX "adopted canvas '%s' (%ux%u)", kCanvasName, current.width, current.height);
	}
	else
	{
		int flags = obs_canvas_flags::ACTIVATE | obs_canvas_flags::MIX_AUDIO | obs_canvas_flags::SCENE_REF;
		m_canvas = obs_frontend_add_canvas(kCanvasName, &ovi, flags);

		if (!m_canvas)
		{
			blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "obs_frontend_add_canvas failed");
			return false;
		}

		blog(LOG_INFO, SL_DUAL_LOG_PREFIX "created canvas '%s' (%ux%u)", kCanvasName, current.width, current.height);
	}

	return true;
}

void SlDualCanvas::ensureScenes(const SlDualConfig& config)
{
	if (!m_canvas)
		return;

	std::vector<std::string> names = sceneNames();

	if (names.empty())
	{
		if (!createScene(kDefaultSceneName))
			return;

		// First run creates an empty default scene; finding no scenes on a later load means a restore went missing.
		if (!config.seeded)
			blog(LOG_INFO, SL_DUAL_LOG_PREFIX "seeded scene '%s'", kDefaultSceneName);
		else
			blog(LOG_WARNING, SL_DUAL_LOG_PREFIX "no canvas scenes restored from the collection; created empty scene '%s'", kDefaultSceneName);

		return;
	}

	std::string joined;

	for (const std::string& n : names)
		joined += (joined.empty() ? "" : ", ") + n;

	blog(LOG_INFO, SL_DUAL_LOG_PREFIX "adopted %zu canvas scene(s): %s", names.size(), joined.c_str());

	const std::string& want = config.activeScene.empty() ? names.front() : config.activeScene;

	if (!setActiveScene(want))
		setActiveScene(names.front());
}

void SlDualCanvas::detach()
{
	swapActiveScene(nullptr);

	if (m_transition)
	{
		obs_source_release(m_transition);
		m_transition = nullptr;
	}

	if (!m_canvas)
		return;

	obs_canvas_set_channel(m_canvas, 0, nullptr);
	obs_canvas_release(m_canvas);
	m_canvas = nullptr;
}

void SlDualCanvas::destroy()
{
	detach();
}

bool SlDualCanvas::resetVideo(uint32_t width, uint32_t height)
{
	uint32_t w = alignedWidth(width);
	uint32_t h = alignedHeight(height);

	if (!m_canvas)
	{
		setSize(w, h);

		// applied on next attach
		return true;
	}

	const Size current = size();

	if (w == current.width && h == current.height)
		return true;

	obs_video_info ovi;

	if (!buildVideoInfo(ovi, w, h))
		return false;

	if (!obs_canvas_reset_video(m_canvas, &ovi))
	{
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "obs_canvas_reset_video %ux%u failed", w, h);
		return false;
	}

	setSize(w, h);

	if (m_transition)
		obs_transition_set_size(m_transition, w, h);

	return true;
}

video_t* SlDualCanvas::video() const
{
	return m_canvas ? obs_canvas_get_video(m_canvas) : nullptr;
}

/**
* Scenes
*/

std::vector<std::string> SlDualCanvas::sceneNames() const
{
	std::vector<std::string> names;

	for (obs_source_t* src : collectScenes(m_canvas))
	{
		const char* name = obs_source_get_name(src);

		if (name)
			names.emplace_back(name);
	}

	return names;
}

std::string SlDualCanvas::activeSceneName() const
{
	if (!m_activeScene)
		return std::string();
	const char* name = obs_source_get_name(obs_scene_get_source(m_activeScene));
	return name ? name : std::string();
}

obs_scene_t* SlDualCanvas::findSceneByName(const std::string& name) const
{
	for (obs_source_t* src : collectScenes(m_canvas))
	{
		const char* n = obs_source_get_name(src);

		if (n && name == n)
		{
			obs_scene_t* scene = obs_scene_from_source(src);
			return scene ? obs_scene_get_ref(scene) : nullptr;
		}
	}

	return nullptr;
}

void SlDualCanvas::setChannelToActive()
{
	if (!m_canvas)
		return;

	obs_source_t* active = m_activeScene ? obs_scene_get_source(m_activeScene) : nullptr;

	if (m_transition)
	{
		// Hard cut; animated switches go through transitionToActive().
		obs_transition_set(m_transition, active);
		obs_canvas_set_channel(m_canvas, 0, m_transition);
	}
	else
		obs_canvas_set_channel(m_canvas, 0, active);
}

void SlDualCanvas::transitionToActive(obs_source_t* previous)
{
	if (!m_canvas)
		return;

	obs_source_t* active = m_activeScene ? obs_scene_get_source(m_activeScene) : nullptr;

	if (!m_transition || !active)
	{
		setChannelToActive();
		return;
	}

	// Keep the A side coherent when the channel was last set outside a transition (adopt, integrity repair).
	obs_source_t* sourceA = obs_transition_get_source(m_transition, OBS_TRANSITION_SOURCE_A);

	if (previous && sourceA != previous)
		obs_transition_set(m_transition, previous);

	if (sourceA)
		obs_source_release(sourceA);

	obs_transition_start(m_transition, OBS_TRANSITION_MODE_AUTO, m_transitionDurationMs, active);
}

void SlDualCanvas::setTransition(obs_source_t* transition)
{
	if (transition == m_transition)
	{
		setChannelToActive();
		return;
	}

	obs_source_t* old = m_transition;
	m_transition = transition ? obs_source_get_ref(transition) : nullptr;

	if (m_transition)
	{
		const Size current = size();
		obs_transition_set_size(m_transition, current.width, current.height);

		// Same live swap the main dock's SetTransition does; plain set when nothing was showing yet.
		// Unguarded by design: swap_begin/swap_end is what hands a running transition over, and the frontend
		//	swaps this way too. obs_transition_is_active would let us skip it, but that is 32.1+ only.
		if (old && m_canvas)
		{
			obs_transition_swap_begin(m_transition, old);
			obs_canvas_set_channel(m_canvas, 0, m_transition);
			obs_transition_swap_end(m_transition, old);
		}
		else
			setChannelToActive();
	}
	else
		setChannelToActive();

	if (old)
		obs_source_release(old);
}

void SlDualCanvas::deselectAllInActive()
{
	if (!m_activeScene)
		return;

	obs_scene_enum_items(
		m_activeScene,
		[](obs_scene_t*, obs_sceneitem_t* item, void*)
		{
			obs_sceneitem_select(item, false);
			return true;
		},
		nullptr);
}

// Takes ownership of `next` (already referenced, or null) and releases whatever it replaces.
// Every swap goes through here so activeSceneRef() can never see a pointer mid-replacement.
void SlDualCanvas::swapActiveScene(obs_scene_t* next)
{
	obs_scene_t* previous = nullptr;

	{
		std::lock_guard<std::mutex> lock(m_activeSceneMutex);
		previous = m_activeScene;
		m_activeScene = next;
	}

	// Released outside the lock: the graphics thread may still hold its own reference, and dropping
	// the last one can run teardown we have no business doing with the lock held.
	if (previous)
		obs_scene_release(previous);
}

obs_scene_t* SlDualCanvas::activeScene() const
{
	std::lock_guard<std::mutex> lock(m_activeSceneMutex);
	return m_activeScene;
}

obs_scene_t* SlDualCanvas::activeSceneRef() const
{
	std::lock_guard<std::mutex> lock(m_activeSceneMutex);
	return m_activeScene ? obs_scene_get_ref(m_activeScene) : nullptr;
}

bool SlDualCanvas::setActiveScene(const std::string& name)
{
	if (!m_canvas)
		return false;

	obs_scene_t* scene = findSceneByName(name);

	if (!scene)
		return false;

	if (scene == m_activeScene)
	{
		obs_scene_release(scene);
		return true;
	}

	deselectAllInActive();

	obs_source_t* previous = m_activeScene ? obs_scene_get_source(m_activeScene) : nullptr;

	swapActiveScene(scene);
	transitionToActive(previous);
	return true;
}

bool SlDualCanvas::createScene(const std::string& name)
{
	if (!m_canvas || name.empty())
		return false;

	if (obs_scene_t* existing = findSceneByName(name))
	{
		obs_scene_release(existing);
		return false;
	}

	obs_scene_t* scene = obs_canvas_scene_create(m_canvas, name.c_str());

	if (!scene)
	{
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "obs_canvas_scene_create '%s' failed", name.c_str());
		return false;
	}

	deselectAllInActive();

	swapActiveScene(obs_scene_get_ref(scene));

	// creation ref; the canvas (SCENE_REF) keeps it alive
	obs_scene_release(scene);
	setChannelToActive();
	return true;
}

bool SlDualCanvas::removeActiveScene()
{
	if (!m_canvas || !m_activeScene)
		return false;

	// never remove the last scene
	if (sceneNames().size() <= 1)
		return false;

	// Same lock as every other swap; this one keeps the reference rather than releasing it, so it
	// takes the pointer out under the lock instead of going through swapActiveScene.
	obs_scene_t* dying = nullptr;

	{
		std::lock_guard<std::mutex> lock(m_activeSceneMutex);
		dying = m_activeScene;
		m_activeScene = nullptr;
	}

	// Without a transition the channel would render the dying scene; with one, the transition holds its own ref and fades it out.
	if (!m_transition)
		obs_canvas_set_channel(m_canvas, 0, nullptr);

	obs_canvas_scene_remove(dying);
	obs_scene_release(dying);

	std::vector<std::string> remaining = sceneNames();

	if (!remaining.empty())
		setActiveScene(remaining.front());
	return true;
}

bool SlDualCanvas::removeScene(const std::string& name)
{
	if (!m_canvas || name.empty())
		return false;

	if (activeSceneName() == name)
		return removeActiveScene();

	// never remove the last scene
	if (sceneNames().size() <= 1)
		return false;

	obs_scene_t* scene = findSceneByName(name);

	if (!scene)
		return false;

	// Not on air and not in the channel, so the active scene and the running transition are untouched.
	obs_canvas_scene_remove(scene);
	obs_scene_release(scene);
	return true;
}

bool SlDualCanvas::renameActiveScene(const std::string& newName)
{
	if (!m_canvas || !m_activeScene || newName.empty())
		return false;

	if (obs_scene_t* existing = findSceneByName(newName))
	{
		obs_scene_release(existing);
		return false;
	}

	obs_source_set_name(obs_scene_get_source(m_activeScene), newName.c_str());
	return true;
}

/**
* Item transform
*/

void SlDualCanvas::applyFillTransform(obs_sceneitem_t* item) const
{
	if (!item)
		return;

	// Scale-to-fill, centered; overflow is clipped by the canvas render target.
	struct obs_transform_info info;
	obs_sceneitem_get_info2(item, &info);

	info.rot = 0.0f;
	const Size current = size();

	vec2_set(&info.pos, (float)current.width * 0.5f, (float)current.height * 0.5f);
	vec2_set(&info.scale, 1.0f, 1.0f);
	info.alignment = OBS_ALIGN_CENTER;
	info.bounds_type = OBS_BOUNDS_SCALE_OUTER;
	info.bounds_alignment = OBS_ALIGN_CENTER;
	vec2_set(&info.bounds, (float)current.width, (float)current.height);

	obs_sceneitem_set_info2(item, &info);
}

void SlDualCanvas::verifyChannelIntegrity()
{
	if (!m_canvas || !m_activeScene)
		return;

	obs_source_t* channel = obs_canvas_get_channel(m_canvas, 0);
	obs_source_t* expected = m_transition ? m_transition : obs_scene_get_source(m_activeScene);

	if (channel != expected)
	{
		blog(LOG_WARNING, SL_DUAL_LOG_PREFIX "channel divergence detected (channel '%s', expected '%s'), repairing", channel ? obs_source_get_name(channel) : "(none)", obs_source_get_name(expected));
		setChannelToActive();
	}

	if (channel)
		obs_source_release(channel);
}

/**
* Preview
*/

void SlDualCanvas::renderPreview(uint32_t cx, uint32_t cy)
{
	// Graphics thread. One snapshot, used for the whole frame - re-reading per use is what let a
	// resize land halfway through and skew the projection against the viewport.
	const Size canvasSize = size();
	const uint32_t w = canvasSize.width;
	const uint32_t h = canvasSize.height;

	if (!m_canvas || !w || !h || !cx || !cy)
		return;

	float scale = std::min((float)cx / (float)w, (float)cy / (float)h);
	uint32_t vw = (uint32_t)((float)w * scale);
	uint32_t vh = (uint32_t)((float)h * scale);
	int x = ((int)cx - (int)vw) / 2;
	int y = ((int)cy - (int)vh) / 2;

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);
	gs_set_viewport(x, y, (int)vw, (int)vh);

	// Black backdrop behind the canvas, like the main preview's DrawBackdrop.
	gs_effect_t* solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t* color = gs_effect_get_param_by_name(solid, "color");
	struct vec4 black;
	vec4_set(&black, 0.0f, 0.0f, 0.0f, 1.0f);
	gs_effect_set_vec4(color, &black);

	while (gs_effect_loop(solid, "Solid"))
		gs_draw_quadf(NULL, 0, (float)w, (float)h);

	obs_canvas_render(m_canvas);

	// 1px outline at the canvas bounds so they read on any theme.
	// One display px in canvas units.
	float t = scale > 0.0f ? 1.0f / scale : 1.0f;
	struct vec4 border;
	vec4_set(&border, 0.35f, 0.35f, 0.35f, 1.0f);
	gs_effect_set_vec4(color, &border);

	while (gs_effect_loop(solid, "Solid"))
	{
		gs_matrix_push();

		// top
		gs_draw_quadf(NULL, 0, (float)w, t);
		gs_matrix_translate3f(0.0f, (float)h - t, 0.0f);

		// bottom
		gs_draw_quadf(NULL, 0, (float)w, t);
		gs_matrix_pop();
		gs_matrix_push();

		// left
		gs_draw_quadf(NULL, 0, t, (float)h);
		gs_matrix_translate3f((float)w - t, 0.0f, 0.0f);

		// right
		gs_draw_quadf(NULL, 0, t, (float)h);
		gs_matrix_pop();
	}

	gs_projection_pop();
	gs_viewport_pop();
}
