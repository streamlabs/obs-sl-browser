#include "SlDualCanvas.hpp"

#include <obs-frontend-api.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <cstring>

namespace {

const char *kCanvasName = "Streamlabs Dual Output";
const char *kMirrorFlag = "sl_dual_program_mirror";
const char *kDefaultSceneName = "Vertical";

uint32_t alignedWidth(uint32_t w)
{
	return std::clamp(w, 32u, 8192u) & ~uint32_t(3);
}

uint32_t alignedHeight(uint32_t h)
{
	return std::clamp(h, 32u, 8192u) & ~uint32_t(1);
}

bool collectSceneProc(void *param, obs_source_t *source)
{
	static_cast<std::vector<obs_source_t *> *>(param)->push_back(source); // borrowed
	return true;
}

// Canvas holds strong refs to its scenes (SCENE_REF), so borrowed pointers
// collected here stay valid for the duration of the calling UI-thread scope.
std::vector<obs_source_t *> collectScenes(obs_canvas_t *canvas)
{
	std::vector<obs_source_t *> scenes;
	if (canvas)
		obs_canvas_enum_scenes(canvas, collectSceneProc, &scenes);
	return scenes;
}

} // namespace

SlDualCanvas::~SlDualCanvas()
{
	destroy();
}

bool SlDualCanvas::buildVideoInfo(struct obs_video_info &ovi, uint32_t width, uint32_t height) const
{
	if (!obs_get_video_info(&ovi))
		return false;

	ovi.base_width = width;
	ovi.base_height = height;
	ovi.output_width = width;
	ovi.output_height = height;
	return true;
}

obs_canvas_t *SlDualCanvas::findExistingByName() const
{
	obs_frontend_canvas_list list = {};
	obs_frontend_get_canvases(&list);

	obs_canvas_t *found = nullptr;
	for (size_t i = 0; i < list.canvases.num; i++) {
		obs_canvas_t *c = list.canvases.array[i];
		const char *name = obs_canvas_get_name(c);
		if (!found && name && strcmp(name, kCanvasName) == 0)
			found = obs_canvas_get_ref(c);
	}

	obs_frontend_canvas_list_free(&list);
	return found;
}

bool SlDualCanvas::create(uint32_t width, uint32_t height)
{
	// The frontend may have destroyed the canvas under us (it clears its
	// canvas list when (re)loading a scene collection).
	if (m_canvas && obs_canvas_removed(m_canvas))
		detach();

	if (m_canvas)
		return resetVideo(width, height);

	m_width = alignedWidth(width);
	m_height = alignedHeight(height);
	return attach();
}

bool SlDualCanvas::attach()
{
	if (m_canvas)
		return true;

	obs_video_info ovi;
	if (!buildVideoInfo(ovi, m_width, m_height)) {
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "main video not initialized, cannot create canvas");
		return false;
	}

	// Adopt the canvas if the frontend restored it from the scene collection
	// (keeps UUID stable for Enhanced Broadcasting, and brings back its
	// scenes, which the frontend saves/loads natively via canvas_uuid).
	m_canvas = findExistingByName();
	if (m_canvas) {
		if (!obs_canvas_reset_video(m_canvas, &ovi)) {
			blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "reset video failed on adopted canvas");
			obs_canvas_release(m_canvas);
			m_canvas = nullptr;
			return false;
		}
		blog(LOG_INFO, SL_DUAL_LOG_PREFIX "adopted canvas '%s' (%ux%u)", kCanvasName, m_width, m_height);
	} else {
		int flags = obs_canvas_flags::ACTIVATE | obs_canvas_flags::MIX_AUDIO;
		m_canvas = obs_frontend_add_canvas(kCanvasName, &ovi, flags);
		if (!m_canvas) {
			blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "obs_frontend_add_canvas failed");
			return false;
		}
		blog(LOG_INFO, SL_DUAL_LOG_PREFIX "created canvas '%s' (%ux%u)", kCanvasName, m_width, m_height);
	}

	return true;
}

void SlDualCanvas::ensureScenes(const SlDualConfig &config)
{
	if (!m_canvas)
		return;

	std::vector<std::string> names = sceneNames();

	if (names.empty()) {
		if (!createScene(kDefaultSceneName))
			return;

		// Seed content from the legacy (pre-editor) config.
		if (config.followProgram) {
			addProgramMirrorItem();
		} else if (!config.fixedScene.empty()) {
			obs_source_t *src = obs_get_source_by_name(config.fixedScene.c_str());
			if (src && obs_source_is_scene(src)) {
				obs_sceneitem_t *item = obs_scene_add(m_activeScene, src);
				if (item)
					applyFillTransform(item);
			}
			if (src)
				obs_source_release(src);
		}
		return;
	}

	const std::string &want = config.activeScene.empty() ? names.front() : config.activeScene;
	if (!setActiveScene(want))
		setActiveScene(names.front());
}

void SlDualCanvas::detach()
{
	if (m_activeScene) {
		obs_scene_release(m_activeScene);
		m_activeScene = nullptr;
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

	if (!m_canvas) {
		m_width = w;
		m_height = h;
		return true; // applied on next attach
	}

	if (w == m_width && h == m_height)
		return true;

	obs_video_info ovi;
	if (!buildVideoInfo(ovi, w, h))
		return false;

	if (!obs_canvas_reset_video(m_canvas, &ovi)) {
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "obs_canvas_reset_video %ux%u failed", w, h);
		return false;
	}

	m_width = w;
	m_height = h;
	refillMirrorItems();
	return true;
}

video_t *SlDualCanvas::video() const
{
	return m_canvas ? obs_canvas_get_video(m_canvas) : nullptr;
}

// ---- Scenes -----------------------------------------------------------------

std::vector<std::string> SlDualCanvas::sceneNames() const
{
	std::vector<std::string> names;
	for (obs_source_t *src : collectScenes(m_canvas)) {
		const char *name = obs_source_get_name(src);
		if (name)
			names.emplace_back(name);
	}
	return names;
}

std::string SlDualCanvas::activeSceneName() const
{
	if (!m_activeScene)
		return std::string();
	const char *name = obs_source_get_name(obs_scene_get_source(m_activeScene));
	return name ? name : std::string();
}

obs_scene_t *SlDualCanvas::findSceneByName(const std::string &name) const
{
	for (obs_source_t *src : collectScenes(m_canvas)) {
		const char *n = obs_source_get_name(src);
		if (n && name == n) {
			obs_scene_t *scene = obs_scene_from_source(src);
			return scene ? obs_scene_get_ref(scene) : nullptr;
		}
	}
	return nullptr;
}

void SlDualCanvas::setChannelToActive()
{
	if (m_canvas)
		obs_canvas_set_channel(m_canvas, 0, m_activeScene ? obs_scene_get_source(m_activeScene) : nullptr);
}

void SlDualCanvas::deselectAllInActive()
{
	if (!m_activeScene)
		return;

	obs_scene_enum_items(
		m_activeScene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *) {
			obs_sceneitem_select(item, false);
			return true;
		},
		nullptr);
}

bool SlDualCanvas::setActiveScene(const std::string &name)
{
	if (!m_canvas)
		return false;

	obs_scene_t *scene = findSceneByName(name);
	if (!scene)
		return false;

	if (scene == m_activeScene) {
		obs_scene_release(scene);
		return true;
	}

	deselectAllInActive();
	if (m_activeScene)
		obs_scene_release(m_activeScene);
	m_activeScene = scene;
	setChannelToActive();
	return true;
}

bool SlDualCanvas::createScene(const std::string &name)
{
	if (!m_canvas || name.empty())
		return false;

	if (obs_scene_t *existing = findSceneByName(name)) {
		obs_scene_release(existing);
		return false;
	}

	obs_scene_t *scene = obs_canvas_scene_create(m_canvas, name.c_str());
	if (!scene) {
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "obs_canvas_scene_create '%s' failed", name.c_str());
		return false;
	}

	deselectAllInActive();
	if (m_activeScene)
		obs_scene_release(m_activeScene);
	m_activeScene = obs_scene_get_ref(scene);
	setChannelToActive();
	return true;
}

bool SlDualCanvas::removeActiveScene()
{
	if (!m_canvas || !m_activeScene)
		return false;

	if (sceneNames().size() <= 1)
		return false; // never remove the last scene

	obs_scene_t *dying = m_activeScene;
	m_activeScene = nullptr;
	obs_canvas_set_channel(m_canvas, 0, nullptr);

	obs_canvas_scene_remove(dying);
	obs_scene_release(dying);

	std::vector<std::string> remaining = sceneNames();
	if (!remaining.empty())
		setActiveScene(remaining.front());
	return true;
}

bool SlDualCanvas::renameActiveScene(const std::string &newName)
{
	if (!m_canvas || !m_activeScene || newName.empty())
		return false;

	if (obs_scene_t *existing = findSceneByName(newName)) {
		obs_scene_release(existing);
		return false;
	}

	obs_source_set_name(obs_scene_get_source(m_activeScene), newName.c_str());
	return true;
}

// ---- Program mirror items -----------------------------------------------------

void SlDualCanvas::markProgramMirrorItem(obs_sceneitem_t *item)
{
	obs_data_t *ps = obs_sceneitem_get_private_settings(item);
	obs_data_set_bool(ps, kMirrorFlag, true);
	obs_data_release(ps);
}

bool SlDualCanvas::isProgramMirrorItem(obs_sceneitem_t *item)
{
	obs_data_t *ps = obs_sceneitem_get_private_settings(item);
	bool mirror = obs_data_get_bool(ps, kMirrorFlag);
	obs_data_release(ps);
	return mirror;
}

void SlDualCanvas::applyFillTransform(obs_sceneitem_t *item) const
{
	if (!item)
		return;

	// Scale-to-fill, centered; overflow is clipped by the canvas render target.
	struct obs_transform_info info;
	obs_sceneitem_get_info2(item, &info);

	info.rot = 0.0f;
	vec2_set(&info.pos, (float)m_width * 0.5f, (float)m_height * 0.5f);
	vec2_set(&info.scale, 1.0f, 1.0f);
	info.alignment = OBS_ALIGN_CENTER;
	info.bounds_type = OBS_BOUNDS_SCALE_OUTER;
	info.bounds_alignment = OBS_ALIGN_CENTER;
	vec2_set(&info.bounds, (float)m_width, (float)m_height);

	obs_sceneitem_set_info2(item, &info);
}

obs_sceneitem_t *SlDualCanvas::addProgramMirrorItem()
{
	if (!m_activeScene)
		return nullptr;

	obs_source_t *program = obs_frontend_get_current_scene();
	if (!program)
		return nullptr;

	obs_sceneitem_t *item = obs_scene_add(m_activeScene, program);
	obs_source_release(program);
	if (!item)
		return nullptr;

	markProgramMirrorItem(item);
	applyFillTransform(item);
	return item;
}

namespace {

struct MirrorCollect {
	std::vector<std::pair<obs_sceneitem_t *, int>> flagged; // addref'd + order index
	int index = 0;
};

bool collectMirrorProc(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *ctx = static_cast<MirrorCollect *>(param);
	if (SlDualCanvas::isProgramMirrorItem(item)) {
		obs_sceneitem_addref(item);
		ctx->flagged.emplace_back(item, ctx->index);
	}
	ctx->index++;
	return true;
}

} // namespace

void SlDualCanvas::refreshMirrorItemsInScene(obs_scene_t *scene, obs_source_t *program)
{
	MirrorCollect ctx;
	obs_scene_enum_items(scene, collectMirrorProc, &ctx);

	for (auto &entry : ctx.flagged) {
		obs_sceneitem_t *item = entry.first;

		if (obs_sceneitem_get_source(item) == program) {
			obs_sceneitem_release(item);
			continue;
		}

		struct obs_transform_info info;
		struct obs_sceneitem_crop crop;
		obs_sceneitem_get_info2(item, &info);
		obs_sceneitem_get_crop(item, &crop);
		bool visible = obs_sceneitem_visible(item);
		bool locked = obs_sceneitem_locked(item);
		bool selected = obs_sceneitem_selected(item);

		obs_sceneitem_remove(item);
		obs_sceneitem_release(item);

		obs_sceneitem_t *replacement = obs_scene_add(scene, program);
		if (!replacement)
			continue;

		obs_sceneitem_defer_update_begin(replacement);
		obs_sceneitem_set_info2(replacement, &info);
		obs_sceneitem_set_crop(replacement, &crop);
		obs_sceneitem_set_visible(replacement, visible);
		obs_sceneitem_set_locked(replacement, locked);
		obs_sceneitem_defer_update_end(replacement);
		markProgramMirrorItem(replacement);
		obs_sceneitem_set_order_position(replacement, entry.second);
		if (selected)
			obs_sceneitem_select(replacement, true);
	}
}

void SlDualCanvas::onProgramSceneChanged()
{
	if (!m_canvas)
		return;

	obs_source_t *program = obs_frontend_get_current_scene();
	if (!program)
		return;

	for (obs_source_t *src : collectScenes(m_canvas)) {
		if (obs_scene_t *scene = obs_scene_from_source(src))
			refreshMirrorItemsInScene(scene, program);
	}

	obs_source_release(program);
}

void SlDualCanvas::refillMirrorItems()
{
	for (obs_source_t *src : collectScenes(m_canvas)) {
		obs_scene_t *scene = obs_scene_from_source(src);
		if (!scene)
			continue;

		std::vector<obs_sceneitem_t *> flagged;
		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
				if (SlDualCanvas::isProgramMirrorItem(item))
					static_cast<std::vector<obs_sceneitem_t *> *>(param)->push_back(item);
				return true;
			},
			&flagged);

		for (obs_sceneitem_t *item : flagged)
			applyFillTransform(item);
	}
}

// ---- Preview ------------------------------------------------------------------

void SlDualCanvas::renderPreview(uint32_t cx, uint32_t cy)
{
	if (!m_canvas || !m_width || !m_height || !cx || !cy)
		return;

	float scale = std::min((float)cx / (float)m_width, (float)cy / (float)m_height);
	uint32_t vw = (uint32_t)((float)m_width * scale);
	uint32_t vh = (uint32_t)((float)m_height * scale);
	int x = ((int)cx - (int)vw) / 2;
	int y = ((int)cy - (int)vh) / 2;

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, (float)m_width, 0.0f, (float)m_height, -100.0f, 100.0f);
	gs_set_viewport(x, y, (int)vw, (int)vh);

	// Black backdrop so the canvas area reads against the grey letterbox.
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	struct vec4 black;
	vec4_set(&black, 0.0f, 0.0f, 0.0f, 1.0f);
	gs_effect_set_vec4(color, &black);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_quadf(NULL, 0, (float)m_width, (float)m_height);

	obs_canvas_render(m_canvas);

	gs_projection_pop();
	gs_viewport_pop();
}
