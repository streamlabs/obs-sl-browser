#pragma once

// Module-internal.
// Interactive editing on the dual canvas preview, ported from obs-studio's OBSBasicPreview (frontend/widgets/OBSBasicPreview.cpp,
//	GPL-2.0-or-later) and adapted to operate on the dual canvas's active scene instead of the main program scene.
// Multi-select (click / Ctrl-click / rubber-band), move with OBS-config snapping, 8-handle stretch, Alt-crop, rotation handle, hover cursors,
//	spacing helpers, and a local undo stack.
//
// Mouse/key input arrives on the UI thread in widget-local logical pixels; drawOverlay runs on the graphics thread inside the display draw callback.

#include "SlDualController.hpp"
#include "SlDualUndo.hpp"

#include <obs.h>
#include <graphics/matrix4.h>

#include <QPoint>
#include <QPointF>
#include <QSizeF>
#include <Qt>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class QMenu;
class QWidget;

#define SL_ITEM_LEFT (1 << 0)
#define SL_ITEM_RIGHT (1 << 1)
#define SL_ITEM_TOP (1 << 2)
#define SL_ITEM_BOTTOM (1 << 3)
#define SL_ITEM_ROT (1 << 4)

enum class SlItemHandle : uint32_t
{
	None = 0,
	TopLeft = SL_ITEM_TOP | SL_ITEM_LEFT,
	TopCenter = SL_ITEM_TOP,
	TopRight = SL_ITEM_TOP | SL_ITEM_RIGHT,
	CenterLeft = SL_ITEM_LEFT,
	CenterRight = SL_ITEM_RIGHT,
	BottomLeft = SL_ITEM_BOTTOM | SL_ITEM_LEFT,
	BottomCenter = SL_ITEM_BOTTOM,
	BottomRight = SL_ITEM_BOTTOM | SL_ITEM_RIGHT,
	Rot = SL_ITEM_ROT,
};

class SlDualEditor
{
public:
	SlDualEditor(SlDualController& controller, QWidget* widget);
	~SlDualEditor();

	SlDualEditor(const SlDualEditor&) = delete;
	SlDualEditor& operator=(const SlDualEditor&) = delete;

	void setViewSize(const QSizeF& sizeLogical, qreal dpr);

	void mousePress(const QPointF& pos, Qt::MouseButton button, Qt::KeyboardModifiers mods);
	void mouseMove(const QPointF& pos, bool buttonDown, Qt::KeyboardModifiers mods);
	void mouseRelease(const QPointF& pos, Qt::MouseButton button, Qt::KeyboardModifiers mods);
	void mouseDoubleClick(const QPointF& pos);
	void mouseLeave();
	bool keyPress(int key, Qt::KeyboardModifiers mods);
	void contextMenu(const QPointF& pos, QWidget* parent);

	// Drops drag/hover interaction state; optionally the undo stack too (scene collection changes).
	// Transform undo entries target scenes by UUID, so plain scene switches keep history, like the main preview.
	void reset(bool clearUndo);

	// Shared with the source list so both UIs produce identical, undoable operations.
	void showItemMenu(obs_sceneitem_t* item, const QPoint& globalPos, QWidget* parent);

	// "+" in the sources dock: the scene menu's Add Source contents as a standalone menu.
	void showAddSourceMenu(const QPoint& globalPos, QWidget* parent);
	void setItemVisibleUndoable(obs_sceneitem_t* item, bool visible);
	void applyOrderUndoable(const std::vector<int64_t>& newOrderBottomToTop);
	void removeSelectedItemsPublic(QWidget* parent) { removeSelectedItems(parent); }

	// Graphics thread.
	void drawOverlay(uint32_t cx, uint32_t cy);

private:
	struct ViewMap
	{
		bool valid = false;

		// physical px per canvas unit
		float scale = 1.0f;

		// physical px
		float offX = 0.0f;
		float offY = 0.0f;
		float cxDisp = 0.0f;
		float cyDisp = 0.0f;
		uint32_t canvasW = 0;
		uint32_t canvasH = 0;
	};

	ViewMap viewMap() const;
	ViewMap viewMapFor(uint32_t cx, uint32_t cy) const;
	bool widgetToCanvas(const QPointF& p, struct vec2& out) const;
	float pixelRatio() const { return (float)m_dpr; }

	// borrowed active scene
	obs_scene_t* scene() const;
	vec2 canvasSize() const;

	// Selection / hit testing (ported)
	obs_sceneitem_t* getItemAtPos(const struct vec2& pos, bool selectBelow) const;
	bool selectedAtPos(const struct vec2& pos) const;
	void doSelect(const struct vec2& pos);
	void doCtrlSelect(const struct vec2& pos);
	void processClick(const struct vec2& pos, Qt::KeyboardModifiers mods);
	void getStretchHandleData(const struct vec2& pos, bool ignoreGroup);
	void clearStretch();
	void updateCursor(uint32_t flags);

	// Transforms (ported)
	struct vec3 getSnapOffset(const struct vec3& tl, const struct vec3& br) const;
	void snapItemMovement(struct vec2& offset) const;
	void moveItems(const struct vec2& pos, Qt::KeyboardModifiers mods);
	void boxItems(const struct vec2& startPos, const struct vec2& pos);
	void snapStretchingToScreen(struct vec3& tl, struct vec3& br) const;
	void clampAspect(struct vec3& tl, struct vec3& br, struct vec2& size, const struct vec2& baseSize) const;
	struct vec3 calculateStretchPos(const struct vec3& tl, const struct vec3& br) const;
	void cropItem(const struct vec2& pos);
	void stretchItem(const struct vec2& pos, Qt::KeyboardModifiers mods);
	void rotateItem(const struct vec2& pos, Qt::KeyboardModifiers mods);

	// Context menu / item operations
	void buildItemMenu(QMenu& menu, obs_sceneitem_t* item, QWidget* parent);
	void buildSceneMenu(QMenu& menu, QWidget* parent);
	void buildAddSourceMenu(QMenu& menu);
	void flagUndoable(obs_sceneitem_t* item, bool isVisibility, bool value);
	void addNewSource(const std::string& typeId);
	void addExistingSource(const std::string& name);
	void addProgramMirror();
	void placeNewItem(obs_sceneitem_t* item);
	void removeSelectedItems(QWidget* parent);
	void nudgeSelected(float dx, float dy);

	// Undo helpers
	std::string snapshot() const;
	void beginUndoSnapshot();
	void finishUndoSnapshot(const char* name);
	void transformAction(const char* name, const std::function<void()>& fn);
	void recordItemAdd(obs_sceneitem_t* item, const char* name);
	void recordItemRemoveAndRemove(obs_sceneitem_t* item);
	bool undoOnce();
	bool redoOnce();

	// Drawing (ported; graphics thread)
	void ensureGraphics();
	void drawOverflow(const ViewMap& map);
	void drawSceneEditing(const ViewMap& map);
	void drawSpacingHelpers(const ViewMap& map);
	static bool drawSelectedItemProc(obs_scene_t* scene, obs_sceneitem_t* item, void* param);
	static bool drawSelectedOverflowProc(obs_scene_t* scene, obs_sceneitem_t* item, void* param);
	void drawSelectionBox(float x1, float y1, float x2, float y2);
	void drawStripedLine(float x1, float y1, float x2, float y2, float thickness, struct vec2 scale);
	void renderSpacingHelper(int index, struct vec3& start, struct vec3& end, struct vec3& viewport, float pixelRatio, uint32_t baseW, uint32_t baseH);

	SlDualController& m_controller;
	QWidget* m_widget = nullptr;

	QSizeF m_viewSize;
	qreal m_dpr = 1.0;

	// Interaction state (mirrors OBSBasicPreview member-for-member)
	obs_sceneitem_crop m_startCrop = {};
	struct vec2 m_startItemPos = {};
	struct vec2 m_cropSize = {};

	// addref'd
	obs_sceneitem_t* m_stretchGroup = nullptr;

	// addref'd
	obs_sceneitem_t* m_stretchItem = nullptr;
	SlItemHandle m_stretchHandle = SlItemHandle::None;
	float m_rotateAngle = 0.0f;
	struct vec2 m_rotatePoint = {};
	struct vec2 m_offsetPoint = {};
	struct vec2 m_stretchItemSize = {};
	struct matrix4 m_screenToItem = {};
	struct matrix4 m_itemToScreen = {};
	struct matrix4 m_invGroupTransform = {};

	struct vec2 m_startPos = {};
	struct vec2 m_mousePos = {};
	struct vec2 m_lastMoveOffset = {};
	bool m_mouseDown = false;
	bool m_mouseMoved = false;
	bool m_mouseOverItems = false;
	bool m_cropping = false;
	bool m_selectionBox = false;
	float m_groupRot = 0.0f;

	mutable std::mutex m_selectMutex;

	// borrowed
	std::vector<obs_sceneitem_t*> m_hoveredPreviewItems;

	// borrowed
	std::vector<obs_sceneitem_t*> m_selectedItems;

	// Undo
	SlDualUndo m_undo;
	std::string m_dragSnapshot;
	bool m_changed = false;

	// Graphics resources (graphics thread)
	gs_vertbuffer_t* m_squareFill = nullptr;
	gs_vertbuffer_t* m_circleFill = nullptr;
	gs_effect_t* m_stripedLineEffect = nullptr;
	gs_texture_t* m_overflowTexture = nullptr;
	bool m_stripedLineTried = false;
	bool m_overflowTried = false;

	// Spacing helper labels (private text sources)
	obs_source_t* m_spacerLabel[4] = {};
	int m_spacerPx[4] = {};

	friend struct SlDrawCtx;
};
