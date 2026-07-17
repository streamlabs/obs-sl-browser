// Interaction and editing-overlay logic ported from obs-studio, frontend/widgets/OBSBasicPreview.cpp (GPL-2.0-or-later),
//	Copyright (C) 2023 by Lain Bailey <lain@obsproject.com> and contributors.
// Adapted to drive the sl-dual-output canvas's active scene.

#include "SlDualEditor.hpp"
#include "SlDualCanvas.hpp"

#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <graphics/math-defs.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>

#include <QCoreApplication>
#include <QCursor>
#include <QFile>
#include <QGuiApplication>
#include <QMenu>
#include <QPoint>
#include <QMessageBox>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>

#define HANDLE_RADIUS 4.0f
#define HANDLE_SEL_RADIUS (HANDLE_RADIUS * 1.5f)
#define HELPER_ROT_BREAKPOINT 45.0f
#define SPACER_LABEL_MARGIN 6.0f

/**
* Snap/display settings (same user-config keys as the main OBS preview)
*/

struct SnapConfig
{
	bool enabled = true;
	bool screen = true;
	bool center = false;
	bool sources = true;
	float distance = 10.0f;
};

static SnapConfig readSnapConfig()
{
	SnapConfig snap;
	config_t* cfg = obs_frontend_get_user_config();

	if (!cfg)
		return snap;

	snap.enabled = config_get_bool(cfg, "BasicWindow", "SnappingEnabled");
	snap.screen = config_get_bool(cfg, "BasicWindow", "ScreenSnapping");
	snap.center = config_get_bool(cfg, "BasicWindow", "CenterSnapping");
	snap.sources = config_get_bool(cfg, "BasicWindow", "SourceSnapping");
	snap.distance = (float)config_get_double(cfg, "BasicWindow", "SnapDistance");
	return snap;
}

static bool userConfigBool(const char* section, const char* key)
{
	config_t* cfg = obs_frontend_get_user_config();
	return cfg ? config_get_bool(cfg, section, key) : false;
}

static vec4 accessibilityColor(const char* overrideKey, float r, float g, float b)
{
	vec4 color;
	config_t* cfg = obs_frontend_get_user_config();

	if (cfg && config_get_bool(cfg, "Accessibility", "OverrideColors"))
	{
		uint32_t rgb = (uint32_t)config_get_int(cfg, "Accessibility", overrideKey);
		vec4_set(&color, (float)(rgb & 0xFF) / 255.0f, (float)((rgb >> 8) & 0xFF) / 255.0f, (float)((rgb >> 16) & 0xFF) / 255.0f, 1.0f);
	}
	else
	{
		vec4_set(&color, r, g, b, 1.0f);
	}

	return color;
}

static std::string frontendDataFile(const char* name)
{
	QString candidate = QCoreApplication::applicationDirPath() + "/../../data/obs-studio/" + name;

	if (QFile::exists(candidate))
		return candidate.toUtf8().constData();
	return std::string();
}

/**
* Ported helpers
*/

static void RotatePos(vec2* pos, float rot)
{
	float cosR = cos(rot);
	float sinR = sin(rot);

	vec2 newPos;
	newPos.x = cosR * pos->x - sinR * pos->y;
	newPos.y = sinR * pos->x + cosR * pos->y;
	vec2_copy(pos, &newPos);
}

static vec3 GetTransformedPos(float x, float y, const matrix4& mat)
{
	vec3 result;
	vec3_set(&result, x, y, 0.0f);
	vec3_transform(&result, &result, &mat);
	return result;
}

static bool SceneItemHasVideo(obs_sceneitem_t* item)
{
	obs_source_t* source = obs_sceneitem_get_source(item);
	uint32_t flags = obs_source_get_output_flags(source);
	return (flags & OBS_SOURCE_VIDEO) != 0;
}

static bool CloseFloat(float a, float b, float epsilon = 0.01f)
{
	return std::abs(a - b) <= epsilon;
}

struct SceneFindData
{
	const vec2& pos;
	obs_sceneitem_t* item = nullptr;
	bool selectBelow;
	obs_sceneitem_t* group = nullptr;

	SceneFindData(const vec2& pos_, bool selectBelow_) : pos(pos_), selectBelow(selectBelow_) {}
};

struct SceneFindBoxData
{
	const vec2& startPos;
	const vec2& pos;
	std::vector<obs_sceneitem_t*> sceneItems;

	SceneFindBoxData(const vec2& startPos_, const vec2& pos_) : startPos(startPos_), pos(pos_) {}
};

static bool FindItemAtPos(obs_scene_t*, obs_sceneitem_t* item, void* param)
{
	SceneFindData* data = static_cast<SceneFindData*>(param);
	matrix4 transform;
	matrix4 invTransform;
	vec3 transformedPos;
	vec3 pos3;
	vec3 pos3_;

	if (!SceneItemHasVideo(item))
		return true;

	if (obs_sceneitem_locked(item))
		return true;

	vec3_set(&pos3, data->pos.x, data->pos.y, 0.0f);

	obs_sceneitem_get_box_transform(item, &transform);

	matrix4_inv(&invTransform, &transform);
	vec3_transform(&transformedPos, &pos3, &invTransform);
	vec3_transform(&pos3_, &transformedPos, &transform);

	if (CloseFloat(pos3.x, pos3_.x) && CloseFloat(pos3.y, pos3_.y) && transformedPos.x >= 0.0f &&
	    transformedPos.x <= 1.0f && transformedPos.y >= 0.0f && transformedPos.y <= 1.0f)
	{
		if (data->selectBelow && obs_sceneitem_selected(item))
		{
			if (data->item)
				return false;
			else
				data->selectBelow = false;
		}

		data->item = item;
	}

	return true;
}

static bool CheckItemSelected(obs_scene_t*, obs_sceneitem_t* item, void* param)
{
	SceneFindData* data = static_cast<SceneFindData*>(param);
	matrix4 transform;
	vec3 transformedPos;
	vec3 pos3;

	if (!SceneItemHasVideo(item))
		return true;

	if (obs_sceneitem_is_group(item))
	{
		data->group = item;
		obs_sceneitem_group_enum_items(item, CheckItemSelected, param);
		data->group = nullptr;

		if (data->item)
			return false;
	}

	vec3_set(&pos3, data->pos.x, data->pos.y, 0.0f);

	obs_sceneitem_get_box_transform(item, &transform);

	if (data->group)
	{
		matrix4 parent_transform;
		obs_sceneitem_get_draw_transform(data->group, &parent_transform);
		matrix4_mul(&transform, &transform, &parent_transform);
	}

	matrix4_inv(&transform, &transform);
	vec3_transform(&transformedPos, &pos3, &transform);

	if (transformedPos.x >= 0.0f && transformedPos.x <= 1.0f && transformedPos.y >= 0.0f &&
	    transformedPos.y <= 1.0f)
	{
		if (obs_sceneitem_selected(item))
		{
			data->item = item;
			return false;
		}
	}

	return true;
}

struct HandleFindData
{
	const vec2& pos;
	const float radius;
	matrix4 parent_xform;

	obs_sceneitem_t* item = nullptr;
	SlItemHandle handle = SlItemHandle::None;
	float angle = 0.0f;
	vec2 rotatePoint = {};
	vec2 offsetPoint = {};
	float angleOffset = 0.0f;

	HandleFindData(const vec2& pos_, float scaleLogical) : pos(pos_), radius(HANDLE_SEL_RADIUS / scaleLogical)
	{
		matrix4_identity(&parent_xform);
	}

	HandleFindData(const HandleFindData& hfd, obs_sceneitem_t* parent)
		: pos(hfd.pos),
		  radius(hfd.radius),
		  item(hfd.item),
		  handle(hfd.handle),
		  angle(hfd.angle),
		  rotatePoint(hfd.rotatePoint),
		  offsetPoint(hfd.offsetPoint)
	{
		obs_sceneitem_get_draw_transform(parent, &parent_xform);
	}
};

static bool FindHandleAtPos(obs_scene_t*, obs_sceneitem_t* item, void* param)
{
	HandleFindData& data = *static_cast<HandleFindData*>(param);

	if (!obs_sceneitem_selected(item))
	{
		if (obs_sceneitem_is_group(item))
		{
			HandleFindData newData(data, item);
			newData.angleOffset = obs_sceneitem_get_rot(item);

			obs_sceneitem_group_enum_items(item, FindHandleAtPos, &newData);

			data.item = newData.item;
			data.handle = newData.handle;
			data.angle = newData.angle;
			data.rotatePoint = newData.rotatePoint;
			data.offsetPoint = newData.offsetPoint;
		}

		return true;
	}

	matrix4 transform;
	vec3 pos3;
	float closestHandle = data.radius;

	vec3_set(&pos3, data.pos.x, data.pos.y, 0.0f);

	obs_sceneitem_get_box_transform(item, &transform);

	auto TestHandle = [&](float x, float y, SlItemHandle handle)
	{
		vec3 handlePos = GetTransformedPos(x, y, transform);
		vec3_transform(&handlePos, &handlePos, &data.parent_xform);

		float dist = vec3_dist(&handlePos, &pos3);

		if (dist < data.radius)
		{
			if (dist < closestHandle)
			{
				closestHandle = dist;
				data.handle = handle;
				data.item = item;
			}
		}
	};

	TestHandle(0.0f, 0.0f, SlItemHandle::TopLeft);
	TestHandle(0.5f, 0.0f, SlItemHandle::TopCenter);
	TestHandle(1.0f, 0.0f, SlItemHandle::TopRight);
	TestHandle(0.0f, 0.5f, SlItemHandle::CenterLeft);
	TestHandle(1.0f, 0.5f, SlItemHandle::CenterRight);
	TestHandle(0.0f, 1.0f, SlItemHandle::BottomLeft);
	TestHandle(0.5f, 1.0f, SlItemHandle::BottomCenter);
	TestHandle(1.0f, 1.0f, SlItemHandle::BottomRight);

	vec2 scale;
	obs_sceneitem_get_scale(item, &scale);
	obs_bounds_type boundsType = obs_sceneitem_get_bounds_type(item);
	vec2 rotHandleOffset;
	vec2_set(&rotHandleOffset, 0.0f, HANDLE_RADIUS * data.radius * 1.5f - data.radius);
	bool invertx = scale.x < 0.0f && boundsType == OBS_BOUNDS_NONE;
	float angle =
		atan2(invertx ? transform.x.y * -1.0f : transform.x.y, invertx ? transform.x.x * -1.0f : transform.x.x);
	RotatePos(&rotHandleOffset, angle);
	RotatePos(&rotHandleOffset, RAD(data.angleOffset));

	bool inverty = scale.y < 0.0f && boundsType == OBS_BOUNDS_NONE;
	vec3 handlePos = GetTransformedPos(0.5f, inverty ? 1.0f : 0.0f, transform);
	vec3_transform(&handlePos, &handlePos, &data.parent_xform);
	handlePos.x -= rotHandleOffset.x;
	handlePos.y -= rotHandleOffset.y;

	float dist = vec3_dist(&handlePos, &pos3);

	if (dist < data.radius)
	{
		if (dist < closestHandle)
		{
			data.item = item;
			data.angle = obs_sceneitem_get_rot(item);
			data.handle = SlItemHandle::Rot;

			vec2_set(&data.rotatePoint, transform.t.x + transform.x.x / 2 + transform.y.x / 2, transform.t.y + transform.x.y / 2 + transform.y.y / 2);

			obs_sceneitem_get_pos(item, &data.offsetPoint);
			data.offsetPoint.x -= data.rotatePoint.x;
			data.offsetPoint.y -= data.rotatePoint.y;

			RotatePos(&data.offsetPoint, -RAD(obs_sceneitem_get_rot(item)));
		}
	}

	return true;
}

static vec2 GetItemSize(obs_sceneitem_t* item)
{
	obs_bounds_type boundsType = obs_sceneitem_get_bounds_type(item);
	vec2 size;

	if (boundsType != OBS_BOUNDS_NONE)
	{
		obs_sceneitem_get_bounds(item, &size);
	}
	else
	{
		obs_source_t* source = obs_sceneitem_get_source(item);
		obs_sceneitem_crop crop;
		vec2 scale;

		obs_sceneitem_get_scale(item, &scale);
		obs_sceneitem_get_crop(item, &crop);
		size.x = fmaxf(float((int)obs_source_get_width(source) - crop.left - crop.right), 0.0f);
		size.y = fmaxf(float((int)obs_source_get_height(source) - crop.top - crop.bottom), 0.0f);
		vec2_mul(&size, &size, &scale);
	}

	return size;
}

static bool select_one(obs_scene_t*, obs_sceneitem_t* item, void* param)
{
	obs_sceneitem_t* selectedItem = static_cast<obs_sceneitem_t*>(param);

	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, select_one, param);

	obs_sceneitem_select(item, (selectedItem == item));
	return true;
}

static bool FindSelected(obs_scene_t*, obs_sceneitem_t* item, void* param)
{
	SceneFindBoxData* data = static_cast<SceneFindBoxData*>(param);

	if (obs_sceneitem_selected(item))
		data->sceneItems.push_back(item);
	return true;
}

static bool CounterClockwise(float x1, float x2, float x3, float y1, float y2, float y3)
{
	return (y3 - y1) * (x2 - x1) > (y2 - y1) * (x3 - x1);
}

static bool IntersectLine(float x1, float x2, float x3, float x4, float y1, float y2, float y3, float y4)
{
	bool a = CounterClockwise(x1, x2, x3, y1, y2, y3);
	bool b = CounterClockwise(x1, x2, x4, y1, y2, y4);
	bool c = CounterClockwise(x3, x4, x1, y3, y4, y1);
	bool d = CounterClockwise(x3, x4, x2, y3, y4, y2);

	return (a != b) && (c != d);
}

static bool IntersectBox(matrix4 transform, float x1, float x2, float y1, float y2)
{
	float x3, x4, y3, y4;

	x3 = transform.t.x;
	y3 = transform.t.y;
	x4 = x3 + transform.x.x;
	y4 = y3 + transform.x.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	x4 = x3 + transform.y.x;
	y4 = y3 + transform.y.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	x3 = transform.t.x + transform.x.x;
	y3 = transform.t.y + transform.x.y;
	x4 = x3 + transform.y.x;
	y4 = y3 + transform.y.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	x3 = transform.t.x + transform.y.x;
	y3 = transform.t.y + transform.y.y;
	x4 = x3 + transform.x.x;
	y4 = y3 + transform.x.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	return false;
}

static bool FindItemsInBox(obs_scene_t*, obs_sceneitem_t* item, void* param)
{
	SceneFindBoxData* data = static_cast<SceneFindBoxData*>(param);
	matrix4 transform;
	matrix4 invTransform;
	vec3 transformedPos;
	vec3 pos3;
	vec3 pos3_;

	vec2 pos_min, pos_max;
	vec2_min(&pos_min, &data->startPos, &data->pos);
	vec2_max(&pos_max, &data->startPos, &data->pos);

	const float x1 = pos_min.x;
	const float x2 = pos_max.x;
	const float y1 = pos_min.y;
	const float y2 = pos_max.y;

	if (!SceneItemHasVideo(item))
		return true;

	if (obs_sceneitem_locked(item))
		return true;

	if (!obs_sceneitem_visible(item))
		return true;

	vec3_set(&pos3, data->pos.x, data->pos.y, 0.0f);

	obs_sceneitem_get_box_transform(item, &transform);

	matrix4_inv(&invTransform, &transform);
	vec3_transform(&transformedPos, &pos3, &invTransform);
	vec3_transform(&pos3_, &transformedPos, &transform);

	if (CloseFloat(pos3.x, pos3_.x) && CloseFloat(pos3.y, pos3_.y) && transformedPos.x >= 0.0f &&
	    transformedPos.x <= 1.0f && transformedPos.y >= 0.0f && transformedPos.y <= 1.0f)
	{
		data->sceneItems.push_back(item);
		return true;
	}

	if (transform.t.x > x1 && transform.t.x < x2 && transform.t.y > y1 && transform.t.y < y2)
	{
		data->sceneItems.push_back(item);
		return true;
	}

	if (transform.t.x + transform.x.x > x1 && transform.t.x + transform.x.x < x2 &&
	    transform.t.y + transform.x.y > y1 && transform.t.y + transform.x.y < y2)
	{
		data->sceneItems.push_back(item);
		return true;
	}

	if (transform.t.x + transform.y.x > x1 && transform.t.x + transform.y.x < x2 &&
	    transform.t.y + transform.y.y > y1 && transform.t.y + transform.y.y < y2)
	{
		data->sceneItems.push_back(item);
		return true;
	}

	if (transform.t.x + transform.x.x + transform.y.x > x1 && transform.t.x + transform.x.x + transform.y.x < x2 &&
	    transform.t.y + transform.x.y + transform.y.y > y1 && transform.t.y + transform.x.y + transform.y.y < y2)
	{
		data->sceneItems.push_back(item);
		return true;
	}

	if (transform.t.x + 0.5f * (transform.x.x + transform.y.x) > x1 &&
	    transform.t.x + 0.5f * (transform.x.x + transform.y.x) < x2 &&
	    transform.t.y + 0.5f * (transform.x.y + transform.y.y) > y1 &&
	    transform.t.y + 0.5f * (transform.x.y + transform.y.y) < y2)
	{
		data->sceneItems.push_back(item);
		return true;
	}

	if (IntersectBox(transform, x1, x2, y1, y2))
	{
		data->sceneItems.push_back(item);
		return true;
	}

	return true;
}

struct SelectedItemBounds
{
	bool first = true;
	vec3 tl = {}, br = {};
};

static bool AddItemBounds(obs_scene_t*, obs_sceneitem_t* item, void* param)
{
	SelectedItemBounds* data = static_cast<SelectedItemBounds*>(param);
	vec3 t[4];

	auto add_bounds = [data, &t]()
	{
		for (const vec3& v : t)
		{
			if (data->first)
			{
				vec3_copy(&data->tl, &v);
				vec3_copy(&data->br, &v);
				data->first = false;
			}
			else
			{
				vec3_min(&data->tl, &data->tl, &v);
				vec3_max(&data->br, &data->br, &v);
			}
		}
	};

	if (obs_sceneitem_is_group(item))
	{
		SelectedItemBounds sib;
		obs_sceneitem_group_enum_items(item, AddItemBounds, &sib);

		if (!sib.first)
		{
			matrix4 xform;
			obs_sceneitem_get_draw_transform(item, &xform);

			vec3_set(&t[0], sib.tl.x, sib.tl.y, 0.0f);
			vec3_set(&t[1], sib.tl.x, sib.br.y, 0.0f);
			vec3_set(&t[2], sib.br.x, sib.tl.y, 0.0f);
			vec3_set(&t[3], sib.br.x, sib.br.y, 0.0f);
			vec3_transform(&t[0], &t[0], &xform);
			vec3_transform(&t[1], &t[1], &xform);
			vec3_transform(&t[2], &t[2], &xform);
			vec3_transform(&t[3], &t[3], &xform);
			add_bounds();
		}
	}

	if (!obs_sceneitem_selected(item))
		return true;

	matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	t[0] = GetTransformedPos(0.0f, 0.0f, boxTransform);
	t[1] = GetTransformedPos(1.0f, 0.0f, boxTransform);
	t[2] = GetTransformedPos(0.0f, 1.0f, boxTransform);
	t[3] = GetTransformedPos(1.0f, 1.0f, boxTransform);
	add_bounds();

	return true;
}

struct OffsetData
{
	float clampDist;
	vec3 tl = {}, br = {}, offset = {};
};

static bool GetSourceSnapOffset(obs_scene_t*, obs_sceneitem_t* item, void* param)
{
	OffsetData* data = static_cast<OffsetData*>(param);

	if (obs_sceneitem_selected(item))
		return true;

	matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	vec3 t[4] = {GetTransformedPos(0.0f, 0.0f, boxTransform), GetTransformedPos(1.0f, 0.0f, boxTransform),
		     GetTransformedPos(0.0f, 1.0f, boxTransform), GetTransformedPos(1.0f, 1.0f, boxTransform)};

	bool first = true;
	vec3 tl, br;
	vec3_zero(&tl);
	vec3_zero(&br);

	for (const vec3& v : t)
	{
		if (first)
		{
			vec3_copy(&tl, &v);
			vec3_copy(&br, &v);
			first = false;
		}
		else
		{
			vec3_min(&tl, &tl, &v);
			vec3_max(&br, &br, &v);
		}
	}

	// Snap to other source edges
#define EDGE_SNAP(l, r, x, y)                                                                         \
	do                                                                                            \
	{                                                                                             \
		double dist = fabsf(l.x - data->r.x);                                                 \
		if (dist < data->clampDist && fabsf(data->offset.x) < EPSILON && data->tl.y < br.y && \
		    data->br.y > tl.y && (fabsf(data->offset.x) > dist || data->offset.x < EPSILON))  \
			data->offset.x = l.x - data->r.x;                                             \
	} while (false)

	EDGE_SNAP(tl, br, x, y);
	EDGE_SNAP(tl, br, y, x);
	EDGE_SNAP(br, tl, x, y);
	EDGE_SNAP(br, tl, y, x);
#undef EDGE_SNAP

	return true;
}

static bool move_items(obs_scene_t*, obs_sceneitem_t* item, void* param)
{
	if (obs_sceneitem_locked(item))
		return true;

	bool selected = obs_sceneitem_selected(item);
	vec2* offset = static_cast<vec2*>(param);

	if (obs_sceneitem_is_group(item) && !selected)
	{
		matrix4 transform;
		vec3 new_offset;
		vec3_set(&new_offset, offset->x, offset->y, 0.0f);

		obs_sceneitem_get_draw_transform(item, &transform);
		vec4_set(&transform.t, 0.0f, 0.0f, 0.0f, 1.0f);
		matrix4_inv(&transform, &transform);
		vec3_transform(&new_offset, &new_offset, &transform);
		obs_sceneitem_group_enum_items(item, move_items, &new_offset);
	}

	if (selected)
	{
		vec2 pos;
		obs_sceneitem_get_pos(item, &pos);
		vec2_add(&pos, &pos, offset);
		obs_sceneitem_set_pos(item, &pos);
	}

	return true;
}

static float maxfunc(float x, float y)
{
	return x > y ? x : y;
}

static float minfunc(float x, float y)
{
	return x < y ? x : y;
}

static QString uniqueSourceName(const QString& base)
{
	for (int i = 1; i < 1000; i++)
	{
		QString candidate = (i == 1) ? base : QString("%1 %2").arg(base).arg(i);
		obs_source_t* existing = obs_get_source_by_name(candidate.toUtf8().constData());

		if (!existing)
			return candidate;
		obs_source_release(existing);
	}

	return base;
}

/**
* Undo payload helpers
*/

// borrowed
static obs_scene_t* sceneByUuid(const std::string& uuid)
{
	obs_source_t* source = obs_get_source_by_uuid(uuid.c_str());

	if (!source)
		return nullptr;
	obs_scene_t* scene = obs_scene_from_source(source);

	// scene is kept alive by the canvas
	obs_source_release(source);
	return scene;
}

static int64_t itemOrderIndex(obs_sceneitem_t* item)
{
	struct Ctx
	{
		obs_sceneitem_t* item;
		int64_t index = -1;
		int64_t current = 0;
	} ctx{item};

	obs_scene_t* scene = obs_sceneitem_get_scene(item);
	obs_scene_enum_items(
		scene,
		[](obs_scene_t*, obs_sceneitem_t* it, void* param)
		{
			Ctx* c = static_cast<Ctx*>(param);

			if (it == c->item)
				c->index = c->current;
			c->current++;
			return true;
		},
		&ctx);
	return ctx.index;
}

static std::string buildItemPayload(obs_sceneitem_t* item)
{
	obs_source_t* source = obs_sceneitem_get_source(item);
	obs_scene_t* scene = obs_sceneitem_get_scene(item);

	obs_data_t* d = obs_data_create();
	obs_data_set_string(d, "scene_uuid", obs_source_get_uuid(obs_scene_get_source(scene)));
	obs_data_set_string(d, "source_uuid", obs_source_get_uuid(source));

	obs_data_t* sourceData = obs_save_source(source);

	if (sourceData)
	{
		obs_data_set_obj(d, "source_data", sourceData);
		obs_data_release(sourceData);
	}

	obs_transform_info info;
	obs_sceneitem_get_info2(item, &info);
	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);

	obs_data_set_vec2(d, "pos", &info.pos);
	obs_data_set_double(d, "rot", info.rot);
	obs_data_set_vec2(d, "scale", &info.scale);
	obs_data_set_int(d, "alignment", info.alignment);
	obs_data_set_int(d, "bounds_type", info.bounds_type);
	obs_data_set_int(d, "bounds_alignment", info.bounds_alignment);
	obs_data_set_vec2(d, "bounds", &info.bounds);
	obs_data_set_int(d, "crop_left", crop.left);
	obs_data_set_int(d, "crop_top", crop.top);
	obs_data_set_int(d, "crop_right", crop.right);
	obs_data_set_int(d, "crop_bottom", crop.bottom);
	obs_data_set_bool(d, "visible", obs_sceneitem_visible(item));
	obs_data_set_bool(d, "locked", obs_sceneitem_locked(item));
	obs_data_set_int(d, "order", itemOrderIndex(item));

	obs_data_t* priv = obs_sceneitem_get_private_settings(item);
	obs_data_set_obj(d, "private_settings", priv);
	obs_data_release(priv);

	std::string json = obs_data_get_json(d);
	obs_data_release(d);
	return json;
}

static int64_t restoreItemFromPayload(const std::string& payload)
{
	obs_data_t* d = obs_data_create_from_json(payload.c_str());

	if (!d)
		return -1;

	obs_scene_t* scene = sceneByUuid(obs_data_get_string(d, "scene_uuid"));

	if (!scene)
	{
		obs_data_release(d);
		return -1;
	}

	obs_source_t* source = obs_get_source_by_uuid(obs_data_get_string(d, "source_uuid"));

	if (!source)
	{
		obs_data_t* sourceData = obs_data_get_obj(d, "source_data");

		if (sourceData)
		{
			source = obs_load_source(sourceData);
			obs_data_release(sourceData);

			if (source)
				obs_source_load(source);
		}
	}

	if (!source)
	{
		obs_data_release(d);
		return -1;
	}

	obs_sceneitem_t* item = obs_scene_add(scene, source);
	int64_t id = -1;

	if (item)
	{
		obs_transform_info info;
		obs_sceneitem_get_info2(item, &info);
		obs_data_get_vec2(d, "pos", &info.pos);
		info.rot = (float)obs_data_get_double(d, "rot");
		obs_data_get_vec2(d, "scale", &info.scale);
		info.alignment = (uint32_t)obs_data_get_int(d, "alignment");
		info.bounds_type = (enum obs_bounds_type)obs_data_get_int(d, "bounds_type");
		info.bounds_alignment = (uint32_t)obs_data_get_int(d, "bounds_alignment");
		obs_data_get_vec2(d, "bounds", &info.bounds);

		obs_sceneitem_crop crop;
		crop.left = (int)obs_data_get_int(d, "crop_left");
		crop.top = (int)obs_data_get_int(d, "crop_top");
		crop.right = (int)obs_data_get_int(d, "crop_right");
		crop.bottom = (int)obs_data_get_int(d, "crop_bottom");

		obs_sceneitem_defer_update_begin(item);
		obs_sceneitem_set_info2(item, &info);
		obs_sceneitem_set_crop(item, &crop);
		obs_sceneitem_set_visible(item, obs_data_get_bool(d, "visible"));
		obs_sceneitem_set_locked(item, obs_data_get_bool(d, "locked"));
		obs_sceneitem_defer_update_end(item);

		obs_data_t* priv = obs_data_get_obj(d, "private_settings");

		if (priv)
		{
			obs_data_t* itemPriv = obs_sceneitem_get_private_settings(item);
			obs_data_apply(itemPriv, priv);
			obs_data_release(itemPriv);
			obs_data_release(priv);
		}

		int64_t order = obs_data_get_int(d, "order");

		if (order >= 0)
			obs_sceneitem_set_order_position(item, (int)order);

		id = obs_sceneitem_get_id(item);
	}

	obs_source_release(source);
	obs_data_release(d);
	return id;
}

static void removeItemInScene(const std::string& sceneUuid, int64_t itemId)
{
	obs_scene_t* scene = sceneByUuid(sceneUuid);

	if (!scene)
		return;
	obs_sceneitem_t* item = obs_scene_find_sceneitem_by_id(scene, itemId);

	if (item)
		obs_sceneitem_remove(item);
}

/**
* Lifecycle
*/

SlDualEditor::SlDualEditor(SlDualController& controller, QWidget* widget) : m_controller(controller), m_widget(widget) {}

SlDualEditor::~SlDualEditor()
{
	clearStretch();

	for (obs_source_t*& label : m_spacerLabel)
	{
		if (label)
		{
			obs_source_release(label);
			label = nullptr;
		}
	}

	if ((m_squareFill || m_circleFill || m_stripedLineEffect || m_overflowTexture) && obs_initialized())
	{
		obs_enter_graphics();

		if (m_squareFill)
			gs_vertexbuffer_destroy(m_squareFill);

		if (m_circleFill)
			gs_vertexbuffer_destroy(m_circleFill);

		if (m_stripedLineEffect)
			gs_effect_destroy(m_stripedLineEffect);

		if (m_overflowTexture)
			gs_texture_destroy(m_overflowTexture);
		obs_leave_graphics();
	}

	m_squareFill = nullptr;
	m_circleFill = nullptr;
	m_stripedLineEffect = nullptr;
	m_overflowTexture = nullptr;
}

void SlDualEditor::setViewSize(const QSizeF& sizeLogical, qreal dpr)
{
	m_viewSize = sizeLogical;
	m_dpr = dpr > 0.0 ? dpr : 1.0;
}

void SlDualEditor::reset(bool clearUndo)
{
	clearStretch();
	m_mouseDown = false;
	m_mouseMoved = false;
	m_cropping = false;
	m_selectionBox = false;
	m_dragSnapshot.clear();
	m_changed = false;

	std::lock_guard<std::mutex> lock(m_selectMutex);
	m_hoveredPreviewItems.clear();
	m_selectedItems.clear();

	if (clearUndo)
		m_undo.clear();
}

/**
* Mapping
*/

SlDualEditor::ViewMap SlDualEditor::viewMapFor(uint32_t cx, uint32_t cy) const
{
	ViewMap map;
	SlDualCanvas* canvas = m_controller.canvas.get();

	if (!canvas || !canvas->valid())
		return map;

	map.canvasW = canvas->width();
	map.canvasH = canvas->height();
	map.cxDisp = (float)cx;
	map.cyDisp = (float)cy;

	if (!map.canvasW || !map.canvasH || map.cxDisp <= 0.0f || map.cyDisp <= 0.0f)
		return map;

	map.scale = std::min(map.cxDisp / (float)map.canvasW, map.cyDisp / (float)map.canvasH);
	map.offX = (map.cxDisp - (float)map.canvasW * map.scale) * 0.5f;
	map.offY = (map.cyDisp - (float)map.canvasH * map.scale) * 0.5f;
	map.valid = map.scale > 0.0f;
	return map;
}

SlDualEditor::ViewMap SlDualEditor::viewMap() const
{
	return viewMapFor((uint32_t)(m_viewSize.width() * m_dpr), (uint32_t)(m_viewSize.height() * m_dpr));
}

bool SlDualEditor::widgetToCanvas(const QPointF& p, struct vec2& out) const
{
	ViewMap map = viewMap();

	if (!map.valid)
		return false;

	vec2_set(&out, ((float)(p.x() * m_dpr) - map.offX) / map.scale, ((float)(p.y() * m_dpr) - map.offY) / map.scale);
	return true;
}

obs_scene_t* SlDualEditor::scene() const
{
	SlDualCanvas* canvas = m_controller.canvas.get();
	return canvas ? canvas->activeScene() : nullptr;
}

vec2 SlDualEditor::canvasSize() const
{
	vec2 size;
	SlDualCanvas* canvas = m_controller.canvas.get();
	vec2_set(&size, canvas ? (float)canvas->width() : 0.0f, canvas ? (float)canvas->height() : 0.0f);
	return size;
}

/**
* Selection / hit testing
*/

obs_sceneitem_t* SlDualEditor::getItemAtPos(const struct vec2& pos, bool selectBelow) const
{
	obs_scene_t* s = scene();

	if (!s)
		return nullptr;

	SceneFindData data(pos, selectBelow);
	obs_scene_enum_items(s, FindItemAtPos, &data);
	return data.item;
}

bool SlDualEditor::selectedAtPos(const struct vec2& pos) const
{
	obs_scene_t* s = scene();

	if (!s)
		return false;

	SceneFindData data(pos, false);
	obs_scene_enum_items(s, CheckItemSelected, &data);
	return !!data.item;
}

void SlDualEditor::doSelect(const struct vec2& pos)
{
	obs_scene_t* s = scene();

	if (!s)
		return;

	obs_sceneitem_t* item = getItemAtPos(pos, true);
	obs_scene_enum_items(s, select_one, item);
}

void SlDualEditor::doCtrlSelect(const struct vec2& pos)
{
	obs_sceneitem_t* item = getItemAtPos(pos, false);

	if (!item)
		return;

	bool selected = obs_sceneitem_selected(item);
	obs_sceneitem_select(item, !selected);
}

void SlDualEditor::processClick(const struct vec2& pos, Qt::KeyboardModifiers mods)
{
	if (mods & Qt::ControlModifier)
		doCtrlSelect(pos);
	else
		doSelect(pos);
}

void SlDualEditor::clearStretch()
{
	if (m_stretchGroup)
	{
		obs_sceneitem_defer_group_resize_end(m_stretchGroup);
		obs_sceneitem_release(m_stretchGroup);
		m_stretchGroup = nullptr;
	}

	if (m_stretchItem)
	{
		obs_sceneitem_release(m_stretchItem);
		m_stretchItem = nullptr;
	}

	m_stretchHandle = SlItemHandle::None;
}

void SlDualEditor::getStretchHandleData(const struct vec2& pos, bool ignoreGroup)
{
	obs_scene_t* s = scene();

	if (!s)
		return;

	ViewMap map = viewMap();

	if (!map.valid)
		return;

	clearStretch();

	float scaleLogical = map.scale / pixelRatio();
	HandleFindData data(pos, scaleLogical);
	obs_scene_enum_items(s, FindHandleAtPos, &data);

	if (data.item)
	{
		m_stretchItem = data.item;
		obs_sceneitem_addref(m_stretchItem);
	}

	m_stretchHandle = data.handle;

	m_rotateAngle = data.angle;
	m_rotatePoint = data.rotatePoint;
	m_offsetPoint = data.offsetPoint;

	if (m_stretchHandle != SlItemHandle::None)
	{
		matrix4 boxTransform;
		vec3 itemUL;
		float itemRot;

		m_stretchItemSize = GetItemSize(m_stretchItem);

		obs_sceneitem_get_box_transform(m_stretchItem, &boxTransform);
		itemRot = obs_sceneitem_get_rot(m_stretchItem);
		vec3_from_vec4(&itemUL, &boxTransform.t);

		// build the item space conversion matrices
		matrix4_identity(&m_itemToScreen);
		matrix4_rotate_aa4f(&m_itemToScreen, &m_itemToScreen, 0.0f, 0.0f, 1.0f, RAD(itemRot));
		matrix4_translate3f(&m_itemToScreen, &m_itemToScreen, itemUL.x, itemUL.y, 0.0f);

		matrix4_identity(&m_screenToItem);
		matrix4_translate3f(&m_screenToItem, &m_screenToItem, -itemUL.x, -itemUL.y, 0.0f);
		matrix4_rotate_aa4f(&m_screenToItem, &m_screenToItem, 0.0f, 0.0f, 1.0f, RAD(-itemRot));

		obs_sceneitem_get_crop(m_stretchItem, &m_startCrop);
		obs_sceneitem_get_pos(m_stretchItem, &m_startItemPos);

		obs_source_t* source = obs_sceneitem_get_source(m_stretchItem);
		m_cropSize.x = float(obs_source_get_width(source) - m_startCrop.left - m_startCrop.right);
		m_cropSize.y = float(obs_source_get_height(source) - m_startCrop.top - m_startCrop.bottom);

		obs_sceneitem_t* group = obs_sceneitem_get_group(s, m_stretchItem);

		if (group && !ignoreGroup)
		{
			m_stretchGroup = group;
			obs_sceneitem_addref(m_stretchGroup);
			obs_sceneitem_get_draw_transform(m_stretchGroup, &m_invGroupTransform);
			matrix4_inv(&m_invGroupTransform, &m_invGroupTransform);
			obs_sceneitem_defer_group_resize_begin(m_stretchGroup);
		}
	}
}

void SlDualEditor::updateCursor(uint32_t flags)
{
	if (!m_stretchItem || obs_sceneitem_locked(m_stretchItem))
	{
		m_widget->unsetCursor();
		return;
	}

	if (!flags)
		m_widget->unsetCursor();

	if ((m_widget->cursor().shape() != Qt::ArrowCursor) || flags == 0)
		return;

	if (flags & SL_ITEM_ROT)
	{
		m_widget->setCursor(Qt::OpenHandCursor);
		return;
	}

	float rotation = obs_sceneitem_get_rot(m_stretchItem);
	vec2 scale;
	obs_sceneitem_get_scale(m_stretchItem, &scale);

	if (rotation < 0.0f)
		rotation = 360.0f + rotation;

	int octant = int(std::round(rotation / 45.0f));
	bool isCorner = (flags & (flags - 1)) != 0;

	if ((scale.x < 0.0f) && isCorner)
		flags ^= SL_ITEM_LEFT | SL_ITEM_RIGHT;

	if ((scale.y < 0.0f) && isCorner)
		flags ^= SL_ITEM_TOP | SL_ITEM_BOTTOM;

	if (octant % 4 >= 2)
	{
		if (isCorner)
		{
			flags ^= SL_ITEM_TOP | SL_ITEM_BOTTOM;
		}
		else
		{
			flags = (flags >> 2) | (flags << 2);
		}
	}

	if (octant % 2 == 1)
	{
		if (isCorner)
		{
			flags &= (flags % 3 == 0) ? ~SL_ITEM_TOP & ~SL_ITEM_BOTTOM : ~SL_ITEM_LEFT & ~SL_ITEM_RIGHT;
		}
		else
		{
			flags = (flags % 4 == 0) ? flags | flags >> ((flags / 2) - 1) : flags | ((flags >> 2) | (flags << 2));
		}
	}

	if ((flags & SL_ITEM_LEFT && flags & SL_ITEM_TOP) || (flags & SL_ITEM_RIGHT && flags & SL_ITEM_BOTTOM))
		m_widget->setCursor(Qt::SizeFDiagCursor);
	else if ((flags & SL_ITEM_LEFT && flags & SL_ITEM_BOTTOM) || (flags & SL_ITEM_RIGHT && flags & SL_ITEM_TOP))
		m_widget->setCursor(Qt::SizeBDiagCursor);
	else if (flags & SL_ITEM_LEFT || flags & SL_ITEM_RIGHT)
		m_widget->setCursor(Qt::SizeHorCursor);
	else if (flags & SL_ITEM_TOP || flags & SL_ITEM_BOTTOM)
		m_widget->setCursor(Qt::SizeVerCursor);
}

/**
* Snapping / movement
*/

struct vec3 SlDualEditor::getSnapOffset(const struct vec3& tl, const struct vec3& br) const
{
	vec2 screenSize = canvasSize();
	vec3 clampOffset;
	vec3_zero(&clampOffset);

	SnapConfig snap = readSnapConfig();

	if (!snap.enabled)
		return clampOffset;

	ViewMap map = viewMap();

	if (!map.valid)
		return clampOffset;

	const float clampDist = snap.distance / map.scale;
	const float centerX = br.x - (br.x - tl.x) / 2.0f;
	const float centerY = br.y - (br.y - tl.y) / 2.0f;

	// Left screen edge.
	if (snap.screen && fabsf(tl.x) < clampDist)
		clampOffset.x = -tl.x;

	// Right screen edge.
	if (snap.screen && fabsf(clampOffset.x) < EPSILON && fabsf(screenSize.x - br.x) < clampDist)
		clampOffset.x = screenSize.x - br.x;

	// Horizontal center.
	if (snap.center && fabsf(screenSize.x - (br.x - tl.x)) > clampDist &&
	    fabsf(screenSize.x / 2.0f - centerX) < clampDist)
		clampOffset.x = screenSize.x / 2.0f - centerX;

	// Top screen edge.
	if (snap.screen && fabsf(tl.y) < clampDist)
		clampOffset.y = -tl.y;

	// Bottom screen edge.
	if (snap.screen && fabsf(clampOffset.y) < EPSILON && fabsf(screenSize.y - br.y) < clampDist)
		clampOffset.y = screenSize.y - br.y;

	// Vertical center.
	if (snap.center && fabsf(screenSize.y - (br.y - tl.y)) > clampDist &&
	    fabsf(screenSize.y / 2.0f - centerY) < clampDist)
		clampOffset.y = screenSize.y / 2.0f - centerY;

	return clampOffset;
}

void SlDualEditor::snapItemMovement(struct vec2& offset) const
{
	obs_scene_t* s = scene();

	if (!s)
		return;

	SelectedItemBounds data;
	obs_scene_enum_items(s, AddItemBounds, &data);

	data.tl.x += offset.x;
	data.tl.y += offset.y;
	data.br.x += offset.x;
	data.br.y += offset.y;

	vec3 snapOffset = getSnapOffset(data.tl, data.br);

	SnapConfig snap = readSnapConfig();

	if (!snap.enabled)
		return;

	if (!snap.sources)
	{
		offset.x += snapOffset.x;
		offset.y += snapOffset.y;
		return;
	}

	ViewMap map = viewMap();
	const float clampDist = map.valid ? snap.distance / map.scale : snap.distance;

	OffsetData offsetData;
	offsetData.clampDist = clampDist;
	offsetData.tl = data.tl;
	offsetData.br = data.br;
	vec3_copy(&offsetData.offset, &snapOffset);

	obs_scene_enum_items(s, GetSourceSnapOffset, &offsetData);

	if (fabsf(offsetData.offset.x) > EPSILON || fabsf(offsetData.offset.y) > EPSILON)
	{
		offset.x += offsetData.offset.x;
		offset.y += offsetData.offset.y;
	}
	else
	{
		offset.x += snapOffset.x;
		offset.y += snapOffset.y;
	}
}

void SlDualEditor::moveItems(const struct vec2& pos, Qt::KeyboardModifiers mods)
{
	obs_scene_t* s = scene();

	if (!s)
		return;

	vec2 offset, moveOffset;
	vec2_sub(&offset, &pos, &m_startPos);
	vec2_sub(&moveOffset, &offset, &m_lastMoveOffset);

	if (!(mods & Qt::ControlModifier))
		snapItemMovement(moveOffset);

	vec2_add(&m_lastMoveOffset, &m_lastMoveOffset, &moveOffset);

	obs_scene_enum_items(s, move_items, &moveOffset);
}

void SlDualEditor::boxItems(const struct vec2& startPos, const struct vec2& pos)
{
	obs_scene_t* s = scene();

	if (!s)
		return;

	if (m_widget->cursor().shape() != Qt::CrossCursor)
		m_widget->setCursor(Qt::CrossCursor);

	SceneFindBoxData data(startPos, pos);
	obs_scene_enum_items(s, FindItemsInBox, &data);

	std::lock_guard<std::mutex> lock(m_selectMutex);
	m_hoveredPreviewItems = data.sceneItems;
}

/**
* Stretch / crop / rotate
*/

struct vec3 SlDualEditor::calculateStretchPos(const struct vec3& tl, const struct vec3& br) const
{
	uint32_t alignment = obs_sceneitem_get_alignment(m_stretchItem);
	vec3 pos;
	vec3_zero(&pos);

	if (alignment & OBS_ALIGN_LEFT)
		pos.x = tl.x;
	else if (alignment & OBS_ALIGN_RIGHT)
		pos.x = br.x;
	else
		pos.x = (br.x - tl.x) * 0.5f + tl.x;

	if (alignment & OBS_ALIGN_TOP)
		pos.y = tl.y;
	else if (alignment & OBS_ALIGN_BOTTOM)
		pos.y = br.y;
	else
		pos.y = (br.y - tl.y) * 0.5f + tl.y;

	return pos;
}

void SlDualEditor::clampAspect(struct vec3& tl, struct vec3& br, struct vec2& size, const struct vec2& baseSize) const
{
	float baseAspect = baseSize.x / baseSize.y;
	float aspect = size.x / size.y;
	uint32_t stretchFlags = (uint32_t)m_stretchHandle;

	if (m_stretchHandle == SlItemHandle::TopLeft || m_stretchHandle == SlItemHandle::TopRight ||
	    m_stretchHandle == SlItemHandle::BottomLeft || m_stretchHandle == SlItemHandle::BottomRight)
	{
		if (aspect < baseAspect)
		{
			if ((size.y >= 0.0f && size.x >= 0.0f) || (size.y <= 0.0f && size.x <= 0.0f))
				size.x = size.y * baseAspect;
			else
				size.x = size.y * baseAspect * -1.0f;
		}
		else
		{
			if ((size.y >= 0.0f && size.x >= 0.0f) || (size.y <= 0.0f && size.x <= 0.0f))
				size.y = size.x / baseAspect;
			else
				size.y = size.x / baseAspect * -1.0f;
		}

	}
	else if (m_stretchHandle == SlItemHandle::TopCenter || m_stretchHandle == SlItemHandle::BottomCenter)
	{
		if ((size.y >= 0.0f && size.x >= 0.0f) || (size.y <= 0.0f && size.x <= 0.0f))
			size.x = size.y * baseAspect;
		else
			size.x = size.y * baseAspect * -1.0f;

	}
	else if (m_stretchHandle == SlItemHandle::CenterLeft || m_stretchHandle == SlItemHandle::CenterRight)
	{
		if ((size.y >= 0.0f && size.x >= 0.0f) || (size.y <= 0.0f && size.x <= 0.0f))
			size.y = size.x / baseAspect;
		else
			size.y = size.x / baseAspect * -1.0f;
	}

	size.x = std::round(size.x);
	size.y = std::round(size.y);

	if (stretchFlags & SL_ITEM_LEFT)
		tl.x = br.x - size.x;
	else if (stretchFlags & SL_ITEM_RIGHT)
		br.x = tl.x + size.x;

	if (stretchFlags & SL_ITEM_TOP)
		tl.y = br.y - size.y;
	else if (stretchFlags & SL_ITEM_BOTTOM)
		br.y = tl.y + size.y;
}

void SlDualEditor::snapStretchingToScreen(struct vec3& tl, struct vec3& br) const
{
	uint32_t stretchFlags = (uint32_t)m_stretchHandle;
	vec3 newTL = GetTransformedPos(tl.x, tl.y, m_itemToScreen);
	vec3 newTR = GetTransformedPos(br.x, tl.y, m_itemToScreen);
	vec3 newBL = GetTransformedPos(tl.x, br.y, m_itemToScreen);
	vec3 newBR = GetTransformedPos(br.x, br.y, m_itemToScreen);
	vec3 boundingTL;
	vec3 boundingBR;

	vec3_copy(&boundingTL, &newTL);
	vec3_min(&boundingTL, &boundingTL, &newTR);
	vec3_min(&boundingTL, &boundingTL, &newBL);
	vec3_min(&boundingTL, &boundingTL, &newBR);

	vec3_copy(&boundingBR, &newTL);
	vec3_max(&boundingBR, &boundingBR, &newTR);
	vec3_max(&boundingBR, &boundingBR, &newBL);
	vec3_max(&boundingBR, &boundingBR, &newBR);

	vec3 offset = getSnapOffset(boundingTL, boundingBR);
	vec3_add(&offset, &offset, &newTL);
	vec3_transform(&offset, &offset, &m_screenToItem);
	vec3_sub(&offset, &offset, &tl);

	if (stretchFlags & SL_ITEM_LEFT)
		tl.x += offset.x;
	else if (stretchFlags & SL_ITEM_RIGHT)
		br.x += offset.x;

	if (stretchFlags & SL_ITEM_TOP)
		tl.y += offset.y;
	else if (stretchFlags & SL_ITEM_BOTTOM)
		br.y += offset.y;
}

void SlDualEditor::cropItem(const struct vec2& pos)
{
	obs_bounds_type boundsType = obs_sceneitem_get_bounds_type(m_stretchItem);
	uint32_t stretchFlags = (uint32_t)m_stretchHandle;
	uint32_t align = obs_sceneitem_get_alignment(m_stretchItem);
	vec3 tl, br, pos3;

	vec3_zero(&tl);
	vec3_set(&br, m_stretchItemSize.x, m_stretchItemSize.y, 0.0f);

	vec3_set(&pos3, pos.x, pos.y, 0.0f);
	vec3_transform(&pos3, &pos3, &m_screenToItem);

	obs_sceneitem_crop crop = m_startCrop;
	vec2 scale, rawscale;

	obs_sceneitem_get_scale(m_stretchItem, &rawscale);
	vec2_set(&scale, boundsType == OBS_BOUNDS_NONE ? rawscale.x : fabsf(rawscale.x), boundsType == OBS_BOUNDS_NONE ? rawscale.y : fabsf(rawscale.y));

	vec2 max_tl;
	vec2 max_br;

	vec2_set(&max_tl, float(-crop.left) * scale.x, float(-crop.top) * scale.y);
	vec2_set(&max_br, m_stretchItemSize.x + crop.right * scale.x, m_stretchItemSize.y + crop.bottom * scale.y);

	typedef std::function<float(float, float)> minmax_func_t;

	minmax_func_t min_x = scale.x < 0.0f && boundsType == OBS_BOUNDS_NONE ? maxfunc : minfunc;
	minmax_func_t min_y = scale.y < 0.0f && boundsType == OBS_BOUNDS_NONE ? maxfunc : minfunc;
	minmax_func_t max_x = scale.x < 0.0f && boundsType == OBS_BOUNDS_NONE ? minfunc : maxfunc;
	minmax_func_t max_y = scale.y < 0.0f && boundsType == OBS_BOUNDS_NONE ? minfunc : maxfunc;

	pos3.x = min_x(pos3.x, max_br.x);
	pos3.x = max_x(pos3.x, max_tl.x);
	pos3.y = min_y(pos3.y, max_br.y);
	pos3.y = max_y(pos3.y, max_tl.y);

	if (stretchFlags & SL_ITEM_LEFT)
	{
		float maxX = m_stretchItemSize.x - (2.0f * scale.x);
		pos3.x = tl.x = min_x(pos3.x, maxX);

	}
	else if (stretchFlags & SL_ITEM_RIGHT)
	{
		float minX = (2.0f * scale.x);
		pos3.x = br.x = max_x(pos3.x, minX);
	}

	if (stretchFlags & SL_ITEM_TOP)
	{
		float maxY = m_stretchItemSize.y - (2.0f * scale.y);
		pos3.y = tl.y = min_y(pos3.y, maxY);

	}
	else if (stretchFlags & SL_ITEM_BOTTOM)
	{
		float minY = (2.0f * scale.y);
		pos3.y = br.y = max_y(pos3.y, minY);
	}

#define SL_ALIGN_X (SL_ITEM_LEFT | SL_ITEM_RIGHT)
#define SL_ALIGN_Y (SL_ITEM_TOP | SL_ITEM_BOTTOM)
	vec3 newPos;
	vec3_zero(&newPos);

	uint32_t align_x = (align & SL_ALIGN_X);
	uint32_t align_y = (align & SL_ALIGN_Y);

	if (align_x == (stretchFlags & SL_ALIGN_X) && align_x != 0)
		newPos.x = pos3.x;
	else if (align & SL_ITEM_RIGHT)
		newPos.x = m_stretchItemSize.x;
	else if (!(align & SL_ITEM_LEFT))
		newPos.x = m_stretchItemSize.x * 0.5f;

	if (align_y == (stretchFlags & SL_ALIGN_Y) && align_y != 0)
		newPos.y = pos3.y;
	else if (align & SL_ITEM_BOTTOM)
		newPos.y = m_stretchItemSize.y;
	else if (!(align & SL_ITEM_TOP))
		newPos.y = m_stretchItemSize.y * 0.5f;
#undef SL_ALIGN_X
#undef SL_ALIGN_Y

	crop = m_startCrop;

	if (stretchFlags & SL_ITEM_LEFT)
		crop.left += int(std::round(tl.x / scale.x));
	else if (stretchFlags & SL_ITEM_RIGHT)
		crop.right += int(std::round((m_stretchItemSize.x - br.x) / scale.x));

	if (stretchFlags & SL_ITEM_TOP)
		crop.top += int(std::round(tl.y / scale.y));
	else if (stretchFlags & SL_ITEM_BOTTOM)
		crop.bottom += int(std::round((m_stretchItemSize.y - br.y) / scale.y));

	vec3_transform(&newPos, &newPos, &m_itemToScreen);
	newPos.x = std::round(newPos.x);
	newPos.y = std::round(newPos.y);

	obs_sceneitem_defer_update_begin(m_stretchItem);
	obs_sceneitem_set_crop(m_stretchItem, &crop);

	if (boundsType == OBS_BOUNDS_NONE)
		obs_sceneitem_set_pos(m_stretchItem, (vec2*)&newPos);
	obs_sceneitem_defer_update_end(m_stretchItem);
}

void SlDualEditor::stretchItem(const struct vec2& pos, Qt::KeyboardModifiers mods)
{
	obs_bounds_type boundsType = obs_sceneitem_get_bounds_type(m_stretchItem);
	uint32_t stretchFlags = (uint32_t)m_stretchHandle;
	bool shiftDown = (mods & Qt::ShiftModifier);
	vec3 tl, br, pos3;

	vec3_zero(&tl);
	vec3_set(&br, m_stretchItemSize.x, m_stretchItemSize.y, 0.0f);

	vec3_set(&pos3, pos.x, pos.y, 0.0f);
	vec3_transform(&pos3, &pos3, &m_screenToItem);

	if (stretchFlags & SL_ITEM_LEFT)
		tl.x = pos3.x;
	else if (stretchFlags & SL_ITEM_RIGHT)
		br.x = pos3.x;

	if (stretchFlags & SL_ITEM_TOP)
		tl.y = pos3.y;
	else if (stretchFlags & SL_ITEM_BOTTOM)
		br.y = pos3.y;

	if (!(mods & Qt::ControlModifier))
		snapStretchingToScreen(tl, br);

	obs_source_t* source = obs_sceneitem_get_source(m_stretchItem);

	uint32_t source_cx = obs_source_get_width(source);
	uint32_t source_cy = obs_source_get_height(source);

	// if the source's internal size has been set to 0 for whatever reason while resizing,
	//	do not update transform, otherwise source will be stuck invisible until a complete transform reset
	if (!source_cx || !source_cy)
		return;

	vec2 baseSize;
	vec2_set(&baseSize, float(source_cx), float(source_cy));

	vec2 size;
	vec2_set(&size, br.x - tl.x, br.y - tl.y);

	if (boundsType != OBS_BOUNDS_NONE)
	{
		if (shiftDown)
			clampAspect(tl, br, size, baseSize);

		if (tl.x > br.x)
			std::swap(tl.x, br.x);

		if (tl.y > br.y)
			std::swap(tl.y, br.y);

		vec2_abs(&size, &size);

		obs_sceneitem_set_bounds(m_stretchItem, &size);
	}
	else
	{
		obs_sceneitem_crop crop;
		obs_sceneitem_get_crop(m_stretchItem, &crop);

		baseSize.x -= float(crop.left + crop.right);
		baseSize.y -= float(crop.top + crop.bottom);

		if (!shiftDown)
			clampAspect(tl, br, size, baseSize);

		vec2_div(&size, &size, &baseSize);
		obs_sceneitem_set_scale(m_stretchItem, &size);
	}

	pos3 = calculateStretchPos(tl, br);
	vec3_transform(&pos3, &pos3, &m_itemToScreen);

	vec2 newPos;
	vec2_set(&newPos, std::round(pos3.x), std::round(pos3.y));
	obs_sceneitem_set_pos(m_stretchItem, &newPos);
}

void SlDualEditor::rotateItem(const struct vec2& pos, Qt::KeyboardModifiers mods)
{
	bool shiftDown = (mods & Qt::ShiftModifier);
	bool ctrlDown = (mods & Qt::ControlModifier);

	vec2 pos2;
	vec2_copy(&pos2, &pos);

	float angle = atan2(pos2.y - m_rotatePoint.y, pos2.x - m_rotatePoint.x) + RAD(90.0f);

#define ROT_SNAP(rot, thresh)                         \
	if (std::abs(angle - RAD(rot)) < RAD(thresh)) \
	{                                             \
		angle = RAD(rot);                     \
	}

	if (shiftDown)
	{
		for (int i = 0; i <= 360 / 15; i++)
		{
			ROT_SNAP(i * 15 - 90, 7.5);
		}
	}
	else if (!ctrlDown)
	{
		ROT_SNAP(m_rotateAngle, 5)

		ROT_SNAP(-90, 5)
		ROT_SNAP(-45, 5)
		ROT_SNAP(0, 5)
		ROT_SNAP(45, 5)
		ROT_SNAP(90, 5)
		ROT_SNAP(135, 5)
		ROT_SNAP(180, 5)
		ROT_SNAP(225, 5)
		ROT_SNAP(270, 5)
		ROT_SNAP(315, 5)
	}
#undef ROT_SNAP

	vec2 pos3;
	vec2_copy(&pos3, &m_offsetPoint);
	RotatePos(&pos3, angle);
	pos3.x += m_rotatePoint.x;
	pos3.y += m_rotatePoint.y;

	obs_sceneitem_set_rot(m_stretchItem, DEG(angle));
	obs_sceneitem_set_pos(m_stretchItem, &pos3);
}

/**
* Mouse events
*/

void SlDualEditor::mousePress(const QPointF& pos, Qt::MouseButton button, Qt::KeyboardModifiers mods)
{
	if (!scene())
		return;

	if (button != Qt::LeftButton)
		return;

	vec2 cpos;

	if (!widgetToCanvas(pos, cpos))
		return;

	bool altDown = (mods & Qt::AltModifier);
	bool shiftDown = (mods & Qt::ShiftModifier);
	bool ctrlDown = (mods & Qt::ControlModifier);

	m_mouseDown = true;

	{
		std::lock_guard<std::mutex> lock(m_selectMutex);
		m_selectedItems.clear();
	}

	if (altDown)
		m_cropping = true;

	if (altDown || shiftDown || ctrlDown)
	{
		vec2 s;
		vec2_zero(&s);
		SceneFindBoxData data(s, s);

		obs_scene_enum_items(scene(), FindSelected, &data);

		std::lock_guard<std::mutex> lock(m_selectMutex);
		m_selectedItems = data.sceneItems;
	}

	getStretchHandleData(cpos, false);

	cpos.x = std::round(cpos.x);
	cpos.y = std::round(cpos.y);
	m_startPos = cpos;

	m_mouseOverItems = selectedAtPos(m_startPos);
	vec2_zero(&m_lastMoveOffset);

	m_mousePos = m_startPos;
	m_dragSnapshot = snapshot();
	m_changed = false;
}

void SlDualEditor::mouseMove(const QPointF& pos, bool buttonDown, Qt::KeyboardModifiers mods)
{
	vec2 cpos;

	if (!widgetToCanvas(pos, cpos))
		return;

	if (m_mouseDown && buttonDown)
	{
		m_changed = true;

		if (!m_mouseMoved && !m_mouseOverItems && m_stretchHandle == SlItemHandle::None)
		{
			processClick(m_startPos, mods);
			m_mouseOverItems = selectedAtPos(m_startPos);
		}

		cpos.x = std::round(cpos.x);
		cpos.y = std::round(cpos.y);

		if (m_stretchHandle != SlItemHandle::None)
		{
			if (obs_sceneitem_locked(m_stretchItem))
				return;

			m_selectionBox = false;

			obs_scene_t* s = scene();
			obs_sceneitem_t* group = s ? obs_sceneitem_get_group(s, m_stretchItem) : nullptr;

			if (group)
			{
				vec3 group_pos;
				vec3_set(&group_pos, cpos.x, cpos.y, 0.0f);
				vec3_transform(&group_pos, &group_pos, &m_invGroupTransform);
				cpos.x = group_pos.x;
				cpos.y = group_pos.y;
			}

			if (m_stretchHandle == SlItemHandle::Rot)
			{
				rotateItem(cpos, mods);
				m_widget->setCursor(Qt::ClosedHandCursor);
			}
			else if (m_cropping)
			{
				cropItem(cpos);
			}
			else
			{
				stretchItem(cpos, mods);
			}

		}
		else if (m_mouseOverItems)
		{
			if (m_widget->cursor().shape() != Qt::SizeAllCursor)
				m_widget->setCursor(Qt::SizeAllCursor);
			m_selectionBox = false;
			moveItems(cpos, mods);
		}
		else
		{
			m_selectionBox = true;

			if (!m_mouseMoved)
				doSelect(m_startPos);
			boxItems(m_startPos, cpos);
		}

		m_mouseMoved = true;
		m_mousePos = cpos;
	}
	else
	{
		obs_sceneitem_t* item = getItemAtPos(cpos, true);

		{
			std::lock_guard<std::mutex> lock(m_selectMutex);
			m_hoveredPreviewItems.clear();
			m_hoveredPreviewItems.push_back(item);
		}

		if (!m_mouseMoved)
		{
			m_mousePos = cpos;
			getStretchHandleData(cpos, true);
			uint32_t stretchFlags = (uint32_t)m_stretchHandle;
			updateCursor(stretchFlags);
		}
	}
}

void SlDualEditor::mouseRelease(const QPointF& pos, Qt::MouseButton button, Qt::KeyboardModifiers mods)
{
	if (button != Qt::LeftButton)
		return;

	if (m_mouseDown)
	{
		vec2 cpos;

		if (!widgetToCanvas(pos, cpos))
			vec2_copy(&cpos, &m_mousePos);

		if (!m_mouseMoved)
			processClick(cpos, mods);

		if (m_selectionBox)
		{
			bool altDown = mods & Qt::AltModifier;
			bool shiftDown = mods & Qt::ShiftModifier;
			bool ctrlDown = mods & Qt::ControlModifier;

			std::lock_guard<std::mutex> lock(m_selectMutex);

			if (altDown || ctrlDown || shiftDown)
			{
				for (size_t i = 0; i < m_selectedItems.size(); i++)
				{
					obs_sceneitem_select(m_selectedItems[i], true);
				}
			}

			for (size_t i = 0; i < m_hoveredPreviewItems.size(); i++)
			{
				bool select = true;
				obs_sceneitem_t* item = m_hoveredPreviewItems[i];

				if (altDown)
				{
					select = false;
				}
				else if (ctrlDown)
				{
					select = !obs_sceneitem_selected(item);
				}

				obs_sceneitem_select(item, select);
			}
		}

		clearStretch();
		m_mouseDown = false;
		m_mouseMoved = false;
		m_cropping = false;
		m_selectionBox = false;
		m_widget->unsetCursor();

		obs_sceneitem_t* item = getItemAtPos(m_mousePos, true);

		std::lock_guard<std::mutex> lock(m_selectMutex);
		m_hoveredPreviewItems.clear();
		m_hoveredPreviewItems.push_back(item);
		m_selectedItems.clear();
	}

	std::string redoSnapshot = snapshot();

	if (!m_dragSnapshot.empty() && !redoSnapshot.empty() && m_changed && m_dragSnapshot != redoSnapshot)
	{
		auto loadStates = [](const std::string& data) { obs_scene_load_transform_states(data.c_str()); };
		m_undo.add("Transform", loadStates, loadStates, m_dragSnapshot, redoSnapshot);
	}

	m_dragSnapshot.clear();
}

void SlDualEditor::mouseDoubleClick(const QPointF& pos)
{
	vec2 cpos;

	if (!widgetToCanvas(pos, cpos))
		return;

	obs_sceneitem_t* hit = getItemAtPos(cpos, true);

	if (!hit)
		return;

	obs_source_t* source = obs_sceneitem_get_source(hit);

	if (source && obs_source_configurable(source))
		obs_frontend_open_source_properties(source);
}

void SlDualEditor::mouseLeave()
{
	std::lock_guard<std::mutex> lock(m_selectMutex);

	if (!m_selectionBox)
		m_hoveredPreviewItems.clear();
}

/**
* Keyboard
*/

bool SlDualEditor::keyPress(int key, Qt::KeyboardModifiers mods)
{
	if (mods & Qt::ControlModifier)
	{
		if (key == Qt::Key_Z && (mods & Qt::ShiftModifier))
			return redoOnce();

		if (key == Qt::Key_Z)
			return undoOnce();

		if (key == Qt::Key_Y)
			return redoOnce();
	}

	if (key == Qt::Key_Delete || key == Qt::Key_Backspace)
	{
		removeSelectedItems(m_widget);
		return true;
	}

	float step = (mods & Qt::ShiftModifier) ? 10.0f : 1.0f;
	switch (key)
	{
	case Qt::Key_Left:
	{
		nudgeSelected(-step, 0.0f);
		return true;
	}
	case Qt::Key_Right:
	{
		nudgeSelected(step, 0.0f);
		return true;
	}
	case Qt::Key_Up:
	{
		nudgeSelected(0.0f, -step);
		return true;
	}
	case Qt::Key_Down:
	{
		nudgeSelected(0.0f, step);
		return true;
	}
	default:
	{
		return false;
	}
	}
}

void SlDualEditor::nudgeSelected(float dx, float dy)
{
	obs_scene_t* s = scene();

	if (!s)
		return;

	transformAction("Nudge", [&]()
	{
		vec2 offset;
		vec2_set(&offset, dx, dy);
		obs_scene_enum_items(s, move_items, &offset);
	});
}

/**
* Undo helpers
*/

std::string SlDualEditor::snapshot() const
{
	obs_scene_t* s = scene();

	if (!s)
		return std::string();

	obs_data_t* data = obs_scene_save_transform_states(s, true);

	if (!data)
		return std::string();

	std::string json = obs_data_get_json(data);
	obs_data_release(data);
	return json;
}

void SlDualEditor::transformAction(const char* name, const std::function<void()>& fn)
{
	std::string before = snapshot();
	fn();
	std::string after = snapshot();

	if (!before.empty() && !after.empty() && before != after)
	{
		auto loadStates = [](const std::string& data) { obs_scene_load_transform_states(data.c_str()); };
		m_undo.add(name, loadStates, loadStates, before, after);
	}
}

void SlDualEditor::recordItemAdd(obs_sceneitem_t* item, const char* name)
{
	if (!item)
		return;

	obs_scene_t* s = obs_sceneitem_get_scene(item);
	std::string sceneUuid = obs_source_get_uuid(obs_scene_get_source(s));
	std::string payload = buildItemPayload(item);

	auto state = std::make_shared<int64_t>(obs_sceneitem_get_id(item));

	auto undoFn = [state, sceneUuid](const std::string&) { removeItemInScene(sceneUuid, *state); };
	auto redoFn = [state](const std::string& data) { *state = restoreItemFromPayload(data); };

	m_undo.add(name, undoFn, redoFn, std::string(), payload);
}

void SlDualEditor::recordItemRemoveAndRemove(obs_sceneitem_t* item)
{
	if (!item)
		return;

	obs_scene_t* s = obs_sceneitem_get_scene(item);
	std::string sceneUuid = obs_source_get_uuid(obs_scene_get_source(s));
	std::string payload = buildItemPayload(item);

	auto state = std::make_shared<int64_t>(obs_sceneitem_get_id(item));

	obs_sceneitem_remove(item);

	auto undoFn = [state](const std::string& data) { *state = restoreItemFromPayload(data); };
	auto redoFn = [state, sceneUuid](const std::string&) { removeItemInScene(sceneUuid, *state); };

	m_undo.add("Remove Item", undoFn, redoFn, payload, std::string());
}

bool SlDualEditor::undoOnce()
{
	return m_undo.undo();
}

bool SlDualEditor::redoOnce()
{
	return m_undo.redo();
}

/**
* Context menu / item operations
*/

void SlDualEditor::contextMenu(const QPointF& pos, QWidget* parent)
{
	obs_scene_t* s = scene();

	if (!s)
		return;

	vec2 cpos;
	obs_sceneitem_t* hit = widgetToCanvas(pos, cpos) ? getItemAtPos(cpos, true) : nullptr;

	QMenu menu(parent);

	if (hit)
	{
		if (!obs_sceneitem_selected(hit))
			doSelect(cpos);
		obs_sceneitem_addref(hit);
		buildItemMenu(menu, hit, parent);
		menu.exec(parent->mapToGlobal(pos.toPoint()));
		obs_sceneitem_release(hit);
	}
	else
	{
		buildSceneMenu(menu, parent);
		menu.exec(parent->mapToGlobal(pos.toPoint()));
	}
}

void SlDualEditor::buildItemMenu(QMenu& menu, obs_sceneitem_t* item, QWidget* parent)
{
	obs_source_t* source = obs_sceneitem_get_source(item);

	if (source && obs_source_configurable(source))
		menu.addAction("Properties", [source]() { obs_frontend_open_source_properties(source); });

	if (source)
		menu.addAction("Filters", [source]() { obs_frontend_open_source_filters(source); });

	if (!menu.isEmpty())
		menu.addSeparator();

	auto orderAction = [this, item](enum obs_order_movement movement)
	{
		obs_scene_t* s = obs_sceneitem_get_scene(item);
		std::string sceneUuid = obs_source_get_uuid(obs_scene_get_source(s));
		int64_t id = obs_sceneitem_get_id(item);
		int64_t before = itemOrderIndex(item);
		obs_sceneitem_set_order(item, movement);
		int64_t after = itemOrderIndex(item);

		if (before == after)
			return;

		auto apply = [sceneUuid, id](const std::string& data)
		{
			obs_scene_t* sc = sceneByUuid(sceneUuid);

			if (!sc)
				return;
			obs_sceneitem_t* it = obs_scene_find_sceneitem_by_id(sc, id);

			if (it)
				obs_sceneitem_set_order_position(it, atoi(data.c_str()));
		};

		m_undo.add("Reorder", apply, apply, std::to_string(before), std::to_string(after));
	};

	QMenu* order = menu.addMenu("Order");
	order->addAction("Move Up", [orderAction]() { orderAction(OBS_ORDER_MOVE_UP); });
	order->addAction("Move Down", [orderAction]() { orderAction(OBS_ORDER_MOVE_DOWN); });
	order->addAction("Move to Top", [orderAction]() { orderAction(OBS_ORDER_MOVE_TOP); });
	order->addAction("Move to Bottom", [orderAction]() { orderAction(OBS_ORDER_MOVE_BOTTOM); });

	SlDualCanvas* canvas = m_controller.canvas.get();
	QMenu* transform = menu.addMenu("Transform");

	if (canvas)
	{
		transform->addAction("Fill Canvas", [this, canvas, item]()
		{
			transformAction("Fill Canvas", [&]() { canvas->applyFillTransform(item); });
		});
		transform->addAction("Fit to Canvas", [this, canvas, item]()
		{
			transformAction("Fit to Canvas", [&]()
			{
				obs_transform_info info;
				obs_sceneitem_get_info2(item, &info);
				info.rot = 0.0f;
				vec2_set(&info.pos, (float)canvas->width() * 0.5f, (float)canvas->height() * 0.5f);
				vec2_set(&info.scale, 1.0f, 1.0f);
				info.alignment = OBS_ALIGN_CENTER;
				info.bounds_type = OBS_BOUNDS_SCALE_INNER;
				info.bounds_alignment = OBS_ALIGN_CENTER;
				vec2_set(&info.bounds, (float)canvas->width(), (float)canvas->height());
				obs_sceneitem_set_info2(item, &info);
			});
		});
		transform->addAction("Center", [this, canvas, item]()
		{
			transformAction("Center", [&]()
			{
				obs_transform_info info;
				obs_sceneitem_get_info2(item, &info);
				info.alignment = OBS_ALIGN_CENTER;
				vec2_set(&info.pos, (float)canvas->width() * 0.5f, (float)canvas->height() * 0.5f);
				obs_sceneitem_set_info2(item, &info);
			});
		});
	}

	transform->addAction("Reset", [this, item]()
	{
		transformAction("Reset Transform", [&]()
		{
			obs_transform_info info;
			obs_sceneitem_get_info2(item, &info);
			info.rot = 0.0f;
			vec2_set(&info.pos, 0.0f, 0.0f);
			vec2_set(&info.scale, 1.0f, 1.0f);
			info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
			info.bounds_type = OBS_BOUNDS_NONE;
			obs_sceneitem_set_info2(item, &info);

			obs_sceneitem_crop crop = {};
			obs_sceneitem_set_crop(item, &crop);
		});
	});
	transform->addSeparator();
	transform->addAction("Rotate 90 CW", [this, item]()
	{
		transformAction("Rotate", [&]() { obs_sceneitem_set_rot(item, obs_sceneitem_get_rot(item) + 90.0f); });
	});
	transform->addAction("Rotate 90 CCW", [this, item]()
	{
		transformAction("Rotate", [&]() { obs_sceneitem_set_rot(item, obs_sceneitem_get_rot(item) - 90.0f); });
	});
	transform->addAction("Rotate 180", [this, item]()
	{
		transformAction("Rotate", [&]() { obs_sceneitem_set_rot(item, obs_sceneitem_get_rot(item) + 180.0f); });
	});

	menu.addSeparator();

	QAction* visible = menu.addAction("Visible", [this, item](bool checked) { flagUndoable(item, true, checked); });
	visible->setCheckable(true);
	visible->setChecked(obs_sceneitem_visible(item));

	QAction* locked = menu.addAction("Locked", [this, item](bool checked) { flagUndoable(item, false, checked); });
	locked->setCheckable(true);
	locked->setChecked(obs_sceneitem_locked(item));

	menu.addSeparator();
	menu.addAction("Remove", [this, parent]() { removeSelectedItems(parent); });
}

void SlDualEditor::buildSceneMenu(QMenu& menu, QWidget* parent)
{
	Q_UNUSED(parent);

	QMenu* add = menu.addMenu("Add Source");
	buildAddSourceMenu(*add);
}

void SlDualEditor::showAddSourceMenu(const QPoint& globalPos, QWidget* parent)
{
	if (!scene())
		return;

	QMenu menu(parent);
	buildAddSourceMenu(menu);
	menu.exec(globalPos);
}

void SlDualEditor::showSceneMenu(const QPoint& globalPos, QWidget* parent)
{
	if (!scene())
		return;

	QMenu menu(parent);
	buildSceneMenu(menu, parent);
	menu.exec(globalPos);
}

void SlDualEditor::buildAddSourceMenu(QMenu& menu)
{
	QMenu* newMenu = menu.addMenu("New");
	const char* typeId = nullptr;

	for (size_t i = 0; obs_enum_source_types(i, &typeId); i++)
	{
		uint32_t flags = obs_get_source_output_flags(typeId);

		if (!(flags & OBS_SOURCE_VIDEO))
			continue;

		if (flags & (OBS_SOURCE_CAP_DISABLED | OBS_SOURCE_DEPRECATED))
			continue;

		const char* display = obs_source_get_display_name(typeId);
		std::string idCopy = typeId;
		newMenu->addAction(QString::fromUtf8(display ? display : typeId), [this, idCopy]() { addNewSource(idCopy); });
	}

	QMenu* existing = menu.addMenu("Existing");

	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	for (size_t i = 0; i < scenes.sources.num; i++)
	{
		const char* name = obs_source_get_name(scenes.sources.array[i]);

		if (!name)
			continue;
		std::string nameCopy = name;
		existing->addAction(QString::fromUtf8(name), [this, nameCopy]() { addExistingSource(nameCopy); });
	}

	obs_frontend_source_list_free(&scenes);

	existing->addSeparator();

	std::vector<std::string> sourceNames;
	obs_enum_sources(
		[](void* param, obs_source_t* source)
		{
			if (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO)
			{
				const char* name = obs_source_get_name(source);

				if (name)
					static_cast<std::vector<std::string>*>(param)->push_back(name);
			}

			return true;
		},
		&sourceNames);
	std::sort(sourceNames.begin(), sourceNames.end());

	for (const std::string& name : sourceNames)
		existing->addAction(QString::fromUtf8(name.c_str()), [this, name]() { addExistingSource(name); });
}

void SlDualEditor::placeNewItem(obs_sceneitem_t* item)
{
	SlDualCanvas* canvas = m_controller.canvas.get();

	if (!item || !canvas)
		return;

	obs_transform_info info;
	obs_sceneitem_get_info2(item, &info);
	info.alignment = OBS_ALIGN_CENTER;
	vec2_set(&info.pos, (float)canvas->width() * 0.5f, (float)canvas->height() * 0.5f);
	obs_sceneitem_set_info2(item, &info);

	obs_scene_t* s = obs_sceneitem_get_scene(item);
	obs_scene_enum_items(s, select_one, item);
}

void SlDualEditor::addNewSource(const std::string& typeId)
{
	obs_scene_t* s = scene();

	if (!s)
		return;

	const char* display = obs_source_get_display_name(typeId.c_str());
	QString name = uniqueSourceName(QString::fromUtf8(display ? display : typeId.c_str()));

	obs_source_t* source = obs_source_create(typeId.c_str(), name.toUtf8().constData(), nullptr, nullptr);

	if (!source)
	{
		blog(LOG_ERROR, SL_DUAL_LOG_PREFIX "failed to create source type '%s'", typeId.c_str());
		return;
	}

	obs_sceneitem_t* item = obs_scene_add(s, source);

	if (item)
	{
		placeNewItem(item);
		recordItemAdd(item, "Add Source");
	}

	if (obs_source_configurable(source))
		obs_frontend_open_source_properties(source);

	obs_source_release(source);
}

void SlDualEditor::addExistingSource(const std::string& name)
{
	obs_scene_t* s = scene();

	if (!s)
		return;

	obs_source_t* source = obs_get_source_by_name(name.c_str());

	if (!source)
		return;

	obs_sceneitem_t* item = obs_scene_add(s, source);

	if (item)
	{
		placeNewItem(item);
		recordItemAdd(item, "Add Source");
	}

	obs_source_release(source);
}

void SlDualEditor::removeSelectedItems(QWidget* parent)
{
	obs_scene_t* s = scene();

	if (!s)
		return;

	vec2 zero;
	vec2_zero(&zero);
	SceneFindBoxData data(zero, zero);
	obs_scene_enum_items(s, FindSelected, &data);

	if (data.sceneItems.empty())
		return;

	QString question = data.sceneItems.size() == 1 ? QString("Remove '%1'?").arg(QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(data.sceneItems[0])))) : QString("Remove %1 items?").arg(data.sceneItems.size());

	if (QMessageBox::question(parent, "Remove Items", question) != QMessageBox::Yes)
		return;

	clearStretch();
	{
		std::lock_guard<std::mutex> lock(m_selectMutex);
		m_hoveredPreviewItems.clear();
	}

	for (obs_sceneitem_t* item : data.sceneItems)
		recordItemRemoveAndRemove(item);
}

void SlDualEditor::flagUndoable(obs_sceneitem_t* item, bool isVisibility, bool value)
{
	obs_scene_t* s = obs_sceneitem_get_scene(item);
	std::string sceneUuid = obs_source_get_uuid(obs_scene_get_source(s));
	int64_t id = obs_sceneitem_get_id(item);
	bool oldValue = isVisibility ? obs_sceneitem_visible(item) : obs_sceneitem_locked(item);

	if (oldValue == value)
		return;

	if (isVisibility)
		obs_sceneitem_set_visible(item, value);
	else
		obs_sceneitem_set_locked(item, value);

	auto apply = [sceneUuid, id, isVisibility](const std::string& data)
	{
		obs_scene_t* sc = sceneByUuid(sceneUuid);

		if (!sc)
			return;
		obs_sceneitem_t* it = obs_scene_find_sceneitem_by_id(sc, id);

		if (!it)
			return;

		if (isVisibility)
			obs_sceneitem_set_visible(it, data == "1");
		else
			obs_sceneitem_set_locked(it, data == "1");
	};

	m_undo.add(isVisibility ? "Visibility" : "Lock", apply, apply, oldValue ? "1" : "0", value ? "1" : "0");
}

void SlDualEditor::setItemVisibleUndoable(obs_sceneitem_t* item, bool visible)
{
	flagUndoable(item, true, visible);
}

void SlDualEditor::showItemMenu(obs_sceneitem_t* item, const QPoint& globalPos, QWidget* parent)
{
	if (!item)
		return;

	QMenu menu(parent);
	obs_sceneitem_addref(item);
	buildItemMenu(menu, item, parent);
	menu.exec(globalPos);
	obs_sceneitem_release(item);
}

static std::vector<int64_t> currentOrderBottomToTop(obs_scene_t* scene)
{
	std::vector<int64_t> order;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t*, obs_sceneitem_t* item, void* param)
		{
			static_cast<std::vector<int64_t>*>(param)->push_back(obs_sceneitem_get_id(item));
			return true;
		},
		&order);
	return order;
}

static void applyOrder(obs_scene_t* scene, const std::vector<int64_t>& orderBottomToTop)
{
	for (size_t i = 0; i < orderBottomToTop.size(); i++)
	{
		obs_sceneitem_t* item = obs_scene_find_sceneitem_by_id(scene, orderBottomToTop[i]);

		if (item)
			obs_sceneitem_set_order_position(item, (int)i);
	}
}

static std::string orderToString(const std::vector<int64_t>& order)
{
	std::string out;

	for (int64_t id : order)
	{
		if (!out.empty())
			out += ",";
		out += std::to_string(id);
	}

	return out;
}

static std::vector<int64_t> orderFromString(const std::string& data)
{
	std::vector<int64_t> order;
	size_t start = 0;

	while (start < data.size())
	{
		size_t end = data.find(',', start);

		if (end == std::string::npos)
			end = data.size();
		order.push_back(std::strtoll(data.substr(start, end - start).c_str(), nullptr, 10));
		start = end + 1;
	}

	return order;
}

void SlDualEditor::applyOrderUndoable(const std::vector<int64_t>& newOrderBottomToTop)
{
	obs_scene_t* s = scene();

	if (!s)
		return;

	std::vector<int64_t> before = currentOrderBottomToTop(s);

	if (before == newOrderBottomToTop)
		return;

	applyOrder(s, newOrderBottomToTop);

	std::string sceneUuid = obs_source_get_uuid(obs_scene_get_source(s));
	auto apply = [sceneUuid](const std::string& data)
	{
		obs_scene_t* sc = sceneByUuid(sceneUuid);

		if (sc)
			applyOrder(sc, orderFromString(data));
	};

	m_undo.add("Reorder", apply, apply, orderToString(before), orderToString(newOrderBottomToTop));
}

/**
* Drawing (graphics thread)
*/

static void DrawLine(float x1, float y1, float x2, float y2, float thickness, vec2 scale)
{
	float cx;
	float cy;
	vec2 thickness_relative;
	bool is_x_axis = !close_float(x1, x2, TINY_EPSILON) || close_float(y1, y2, TINY_EPSILON);

	vec2_abs(&scale, &scale);

	thickness_relative.x = thickness / scale.x;
	thickness_relative.y = thickness / scale.y;

	if (is_x_axis)
	{
		cx = fabsf(x2 - x1) + thickness_relative.x;
		cy = thickness_relative.y;
	}
	else
	{
		cy = fabsf(y2 - y1) + thickness_relative.y;
		cx = thickness_relative.x;
	}

	x1 -= thickness_relative.x * 0.5f;
	y1 -= thickness_relative.y * 0.5f;

	gs_matrix_push();
	gs_matrix_translate3f(x1, y1, 0.0f);
	gs_draw_quadf(NULL, 0, cx, cy);
	gs_matrix_pop();
}

static void DrawRect(float thickness, vec2 scale)
{
	DrawLine(0.0f, 0.0f, 0.0f, 1.0f, thickness, scale);
	DrawLine(0.0f, 0.0f, 1.0f, 0.0f, thickness, scale);
	DrawLine(1.0f, 0.0f, 1.0f, 1.0f, thickness, scale);
	DrawLine(0.0f, 1.0f, 1.0f, 1.0f, thickness, scale);
}

static void DrawSquareAtPos(float x, float y, float pixelRatio)
{
	struct vec3 pos;
	vec3_set(&pos, x, y, 0.0f);

	struct matrix4 matrix;
	gs_matrix_get(&matrix);
	vec3_transform(&pos, &pos, &matrix);

	gs_matrix_push();
	gs_matrix_identity();
	gs_matrix_translate(&pos);

	gs_matrix_translate3f(-HANDLE_RADIUS * pixelRatio, -HANDLE_RADIUS * pixelRatio, 0.0f);
	gs_matrix_scale3f(HANDLE_RADIUS * pixelRatio * 2, HANDLE_RADIUS * pixelRatio * 2, 1.0f);
	gs_draw(GS_TRISTRIP, 0, 0);

	gs_matrix_pop();
}

static void DrawRotationHandle(gs_vertbuffer_t* circle, float rot, float pixelRatio, bool invert)
{
	struct vec3 pos;
	vec3_set(&pos, 0.5f, invert ? 1.0f : 0.0f, 0.0f);

	struct matrix4 matrix;
	gs_matrix_get(&matrix);
	vec3_transform(&pos, &pos, &matrix);

	const float thickness = 0.68f;

	gs_matrix_push();
	gs_matrix_identity();
	gs_matrix_translate(&pos);

	gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, RAD(rot));
	gs_matrix_translate3f(-HANDLE_RADIUS * 1.5f * pixelRatio, -HANDLE_RADIUS * 1.5f * pixelRatio, 0.0f);
	gs_matrix_scale3f(HANDLE_RADIUS * 3 * pixelRatio, HANDLE_RADIUS * 3 * pixelRatio, 1.0f);

	gs_matrix_push();
	gs_matrix_translate3f(0.5f - thickness / 2.0f / HANDLE_RADIUS, -2.0f, 0.0f);
	gs_draw_quadf(NULL, 0, thickness / HANDLE_RADIUS, 2.5f);
	gs_matrix_pop();

	gs_matrix_translate3f(0.0f, -HANDLE_RADIUS * 2 / 3, 0.0f);

	gs_load_vertexbuffer(circle);
	gs_draw(GS_TRISTRIP, 0, 0);

	gs_matrix_pop();
}

static bool crop_enabled(const obs_sceneitem_crop* crop)
{
	return crop->left > 0 || crop->top > 0 || crop->right > 0 || crop->bottom > 0;
}

struct SlDrawCtx
{
	SlDualEditor* editor;
	float pixelRatio;
	vec4 red;
	vec4 green;
	vec4 blue;
};

void SlDualEditor::ensureGraphics()
{
	if (!m_squareFill)
	{
		gs_render_start(true);
		gs_vertex2f(0.0f, 0.0f);
		gs_vertex2f(1.0f, 0.0f);
		gs_vertex2f(0.0f, 1.0f);
		gs_vertex2f(1.0f, 1.0f);
		m_squareFill = gs_render_save();
	}

	if (!m_circleFill)
	{
		gs_render_start(true);

		float angle = 180;

		for (int i = 0, l = 40; i < l; i++)
		{
			gs_vertex2f(sin(RAD(angle)) / 2 + 0.5f, cos(RAD(angle)) / 2 + 0.5f);
			angle += 360.0f / l;
			gs_vertex2f(sin(RAD(angle)) / 2 + 0.5f, cos(RAD(angle)) / 2 + 0.5f);
			gs_vertex2f(0.5f, 1.0f);
		}

		m_circleFill = gs_render_save();
	}

	if (!m_stripedLineEffect && !m_stripedLineTried)
	{
		m_stripedLineTried = true;
		std::string path = frontendDataFile("striped_line.effect");

		if (!path.empty())
			m_stripedLineEffect = gs_effect_create_from_file(path.c_str(), nullptr);
	}
}

void SlDualEditor::drawStripedLine(float x1, float y1, float x2, float y2, float thickness, struct vec2 scale)
{
	if (!m_stripedLineEffect)
	{
		// Fallback: solid line (striped_line.effect not found)
		gs_effect_t* solid = obs_get_base_effect(OBS_EFFECT_SOLID);

		while (gs_effect_loop(solid, "Solid"))
			DrawLine(x1, y1, x2, y2, thickness, scale);

		return;
	}

	float cx;
	float cy;
	float dist;
	float dist_scaled;
	vec2 thickness_relative;
	bool is_x_axis = !close_float(x1, x2, TINY_EPSILON) || close_float(y1, y2, TINY_EPSILON);

	vec2_abs(&scale, &scale);

	thickness_relative.x = thickness / scale.x;
	thickness_relative.y = thickness / scale.y;

	if (is_x_axis)
	{
		dist = fabsf(x2 - x1);
		dist_scaled = dist * scale.x;
		cx = dist + thickness_relative.x;
		cy = thickness_relative.y;
	}
	else
	{
		dist = fabsf(y2 - y1);
		dist_scaled = dist * scale.y;
		cy = dist + thickness_relative.y;
		cx = thickness_relative.x;
	}

	x1 -= thickness_relative.x * 0.5f;
	y1 -= thickness_relative.y * 0.5f;

	float stripe_length = dist_scaled / 15.0f;
	float f_stripes_inv = 1.0f / (dist_scaled / stripe_length);

	struct vec2 size;
	struct vec2 count_inv;

	if (is_x_axis)
	{
		vec2_set(&size, dist_scaled, 0.0f);
		vec2_set(&count_inv, f_stripes_inv, 0.0f);
	}
	else
	{
		vec2_set(&size, 0.0f, dist_scaled);
		vec2_set(&count_inv, 0.0f, f_stripes_inv);
	}

	gs_eparam_t* size_param = gs_effect_get_param_by_name(m_stripedLineEffect, "size");
	gs_eparam_t* count_inv_param = gs_effect_get_param_by_name(m_stripedLineEffect, "count_inv");

	gs_effect_set_vec2(size_param, &size);
	gs_effect_set_vec2(count_inv_param, &count_inv);

	gs_matrix_push();
	gs_matrix_translate3f(x1, y1, 0.0f);

	while (gs_effect_loop(m_stripedLineEffect, "StripedLine"))
	{
		gs_draw_quadf(nullptr, 0, cx, cy);
	}

	gs_matrix_pop();
}

bool SlDualEditor::drawSelectedItemProc(obs_scene_t* scene, obs_sceneitem_t* item, void* param)
{
	SlDrawCtx* ctx = static_cast<SlDrawCtx*>(param);
	SlDualEditor* self = ctx->editor;

	if (obs_sceneitem_locked(item))
		return true;

	if (!SceneItemHasVideo(item))
		return true;

	if (obs_sceneitem_is_group(item))
	{
		matrix4 mat;
		obs_transform_info groupInfo;
		obs_sceneitem_get_draw_transform(item, &mat);
		obs_sceneitem_get_info2(item, &groupInfo);

		self->m_groupRot = groupInfo.rot;

		gs_matrix_push();
		gs_matrix_mul(&mat);
		obs_sceneitem_group_enum_items(item, drawSelectedItemProc, param);
		gs_matrix_pop();

		self->m_groupRot = 0.0f;
	}

	bool hovered = false;
	{
		std::lock_guard<std::mutex> lock(self->m_selectMutex);

		for (size_t i = 0; i < self->m_hoveredPreviewItems.size(); i++)
		{
			if (self->m_hoveredPreviewItems[i] == item)
			{
				hovered = true;
				break;
			}
		}
	}

	bool selected = obs_sceneitem_selected(item);

	if (!selected && !hovered)
		return true;

	matrix4 boxTransform;
	matrix4 invBoxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);
	matrix4_inv(&invBoxTransform, &boxTransform);

	vec3 bounds[] = {
		{{{0.f, 0.f, 0.f, 0.f}}},
		{{{1.f, 0.f, 0.f, 0.f}}},
		{{{0.f, 1.f, 0.f, 0.f}}},
		{{{1.f, 1.f, 0.f, 0.f}}},
	};

	bool visible = std::all_of(std::begin(bounds), std::end(bounds), [&](const vec3& b)
	{
		vec3 pos;
		vec3_transform(&pos, &b, &boxTransform);
		vec3_transform(&pos, &pos, &invBoxTransform);
		return CloseFloat(pos.x, b.x) && CloseFloat(pos.y, b.y);
	});

	if (!visible)
		return true;

	float pixelRatio = ctx->pixelRatio;
	gs_effect_t* solid = obs_get_base_effect(OBS_EFFECT_SOLID);

	matrix4 curTransform;
	vec2 boxScale;
	gs_matrix_get(&curTransform);
	obs_sceneitem_get_box_scale(item, &boxScale);
	boxScale.x *= curTransform.x.x;
	boxScale.y *= curTransform.y.y;

	obs_transform_info info;
	obs_sceneitem_get_info2(item, &info);

	gs_matrix_push();
	gs_matrix_mul(&boxTransform);

	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);

	if (info.bounds_type == OBS_BOUNDS_NONE && crop_enabled(&crop))
	{
#define DRAW_SIDE(side, x1, y1, x2, y2)                                                          \
	if (hovered && !selected)                                                                \
	{                                                                                        \
		gs_eparam_t* colParam = gs_effect_get_param_by_name(solid, "color");             \
		gs_effect_set_vec4(colParam, &ctx->blue);                                        \
		while (gs_effect_loop(solid, "Solid"))                                           \
		{                                                                                \
			DrawLine(x1, y1, x2, y2, HANDLE_RADIUS * pixelRatio / 2, boxScale);      \
		}                                                                                \
	}                                                                                        \
	else if (crop.side > 0)                                                                  \
	{                                                                                        \
		gs_eparam_t* colParam = gs_effect_get_param_by_name(                             \
			self->m_stripedLineEffect ? self->m_stripedLineEffect : solid, "color"); \
		gs_effect_set_vec4(colParam, &ctx->green);                                       \
		self->drawStripedLine(x1, y1, x2, y2, HANDLE_RADIUS * pixelRatio / 2, boxScale); \
	}                                                                                        \
	else                                                                                     \
	{                                                                                        \
		gs_eparam_t* colParam = gs_effect_get_param_by_name(solid, "color");             \
		gs_effect_set_vec4(colParam, &ctx->red);                                         \
		while (gs_effect_loop(solid, "Solid"))                                           \
		{                                                                                \
			DrawLine(x1, y1, x2, y2, HANDLE_RADIUS * pixelRatio / 2, boxScale);      \
		}                                                                                \
	}

		DRAW_SIDE(left, 0.0f, 0.0f, 0.0f, 1.0f);
		DRAW_SIDE(top, 0.0f, 0.0f, 1.0f, 0.0f);
		DRAW_SIDE(right, 1.0f, 0.0f, 1.0f, 1.0f);
		DRAW_SIDE(bottom, 0.0f, 1.0f, 1.0f, 1.0f);
#undef DRAW_SIDE
	}
	else
	{
		gs_eparam_t* colParam = gs_effect_get_param_by_name(solid, "color");
		gs_effect_set_vec4(colParam, selected ? &ctx->red : &ctx->blue);

		while (gs_effect_loop(solid, "Solid"))
		{
			DrawRect(HANDLE_RADIUS * pixelRatio / 2, boxScale);
		}
	}

	gs_eparam_t* colParam = gs_effect_get_param_by_name(solid, "color");

	gs_technique_t* tech = gs_effect_get_technique(solid, "Solid");
	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);

	gs_load_vertexbuffer(self->m_squareFill);
	gs_effect_set_vec4(colParam, &ctx->red);

	if (selected)
	{
		DrawSquareAtPos(0.0f, 0.0f, pixelRatio);
		DrawSquareAtPos(0.0f, 1.0f, pixelRatio);
		DrawSquareAtPos(1.0f, 0.0f, pixelRatio);
		DrawSquareAtPos(1.0f, 1.0f, pixelRatio);
		DrawSquareAtPos(0.5f, 0.0f, pixelRatio);
		DrawSquareAtPos(0.0f, 0.5f, pixelRatio);
		DrawSquareAtPos(0.5f, 1.0f, pixelRatio);
		DrawSquareAtPos(1.0f, 0.5f, pixelRatio);

		bool invert = info.scale.y < 0.0f && info.bounds_type == OBS_BOUNDS_NONE;
		DrawRotationHandle(self->m_circleFill, info.rot + self->m_groupRot, pixelRatio, invert);
	}

	gs_matrix_pop();

	gs_technique_end_pass(tech);
	gs_technique_end(tech);

	UNUSED_PARAMETER(scene);
	return true;
}

bool SlDualEditor::drawSelectedOverflowProc(obs_scene_t*, obs_sceneitem_t* item, void* param)
{
	SlDrawCtx* ctx = static_cast<SlDrawCtx*>(param);
	SlDualEditor* self = ctx->editor;

	if (obs_sceneitem_locked(item))
		return true;

	if (!SceneItemHasVideo(item))
		return true;

	bool selectionHidden = userConfigBool("BasicWindow", "OverflowSelectionHidden");
	bool alwaysVisible = userConfigBool("BasicWindow", "OverflowAlwaysVisible");

	if (!selectionHidden && !obs_sceneitem_visible(item))
		return true;

	if (obs_sceneitem_is_group(item))
	{
		matrix4 mat;
		obs_sceneitem_get_draw_transform(item, &mat);

		gs_matrix_push();
		gs_matrix_mul(&mat);
		obs_sceneitem_group_enum_items(item, drawSelectedOverflowProc, param);
		gs_matrix_pop();
	}

	if (!alwaysVisible && !obs_sceneitem_selected(item))
		return true;

	matrix4 boxTransform;
	matrix4 invBoxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);
	matrix4_inv(&invBoxTransform, &boxTransform);

	vec3 bounds[] = {
		{{{0.f, 0.f, 0.f, 0.f}}},
		{{{1.f, 0.f, 0.f, 0.f}}},
		{{{0.f, 1.f, 0.f, 0.f}}},
		{{{1.f, 1.f, 0.f, 0.f}}},
	};

	bool visible = std::all_of(std::begin(bounds), std::end(bounds), [&](const vec3& b)
	{
		vec3 pos;
		vec3_transform(&pos, &b, &boxTransform);
		vec3_transform(&pos, &pos, &invBoxTransform);
		return CloseFloat(pos.x, b.x) && CloseFloat(pos.y, b.y);
	});

	if (!visible)
		return true;

	gs_effect_t* repeat = obs_get_base_effect(OBS_EFFECT_REPEAT);
	gs_eparam_t* image = gs_effect_get_param_by_name(repeat, "image");
	gs_eparam_t* scale = gs_effect_get_param_by_name(repeat, "scale");

	vec2 s;
	vec2_set(&s, boxTransform.x.x / 96, boxTransform.y.y / 96);

	gs_effect_set_vec2(scale, &s);
	gs_effect_set_texture(image, self->m_overflowTexture);

	gs_matrix_push();
	gs_matrix_mul(&boxTransform);

	while (gs_effect_loop(repeat, "Draw"))
	{
		gs_draw_sprite(self->m_overflowTexture, 0, 1, 1);
	}

	gs_matrix_pop();

	return true;
}

void SlDualEditor::drawOverflow(const ViewMap& map)
{
	if (userConfigBool("BasicWindow", "OverflowHidden"))
		return;

	if (!m_overflowTexture && !m_overflowTried)
	{
		m_overflowTried = true;
		std::string path = frontendDataFile("images/overflow.png");

		if (!path.empty())
			m_overflowTexture = gs_texture_create_from_file(path.c_str());
	}

	if (!m_overflowTexture)
		return;

	obs_scene_t* s = scene();

	if (!s)
		return;

	SlDrawCtx ctx{this, pixelRatio(), {}, {}, {}};

	gs_matrix_push();
	gs_matrix_scale3f(map.scale, map.scale, 1.0f);
	obs_scene_enum_items(s, drawSelectedOverflowProc, &ctx);
	gs_matrix_pop();

	gs_load_vertexbuffer(nullptr);
}

void SlDualEditor::drawSelectionBox(float x1, float y1, float x2, float y2)
{
	float pr = pixelRatio();

	x1 = std::round(x1);
	x2 = std::round(x2);
	y1 = std::round(y1);
	y2 = std::round(y2);

	gs_effect_t* eff = gs_get_effect();
	gs_eparam_t* colParam = gs_effect_get_param_by_name(eff, "color");

	vec4 fillColor;
	vec4_set(&fillColor, 0.7f, 0.7f, 0.7f, 0.5f);

	vec4 borderColor;
	vec4_set(&borderColor, 1.0f, 1.0f, 1.0f, 1.0f);

	vec2 scale;
	vec2_set(&scale, std::abs(x2 - x1), std::abs(y2 - y1));

	gs_matrix_push();
	gs_matrix_identity();

	gs_matrix_translate3f(x1, y1, 0.0f);
	gs_matrix_scale3f(x2 - x1, y2 - y1, 1.0f);

	gs_effect_set_vec4(colParam, &fillColor);
	gs_load_vertexbuffer(m_squareFill);
	gs_draw(GS_TRISTRIP, 0, 0);

	gs_effect_set_vec4(colParam, &borderColor);
	DrawRect(HANDLE_RADIUS * pr / 2, scale);

	gs_matrix_pop();
}

void SlDualEditor::drawSceneEditing(const ViewMap& map)
{
	obs_scene_t* s = scene();

	if (!s)
		return;

	SlDrawCtx ctx{this, pixelRatio(), accessibilityColor("SelectRed", 1.0f, 0.0f, 0.0f),
		      accessibilityColor("SelectGreen", 0.0f, 1.0f, 0.0f),
		      accessibilityColor("SelectBlue", 0.0f, 0.5f, 1.0f)};

	gs_matrix_push();
	gs_matrix_scale3f(map.scale, map.scale, 1.0f);
	obs_scene_enum_items(s, drawSelectedItemProc, &ctx);
	gs_matrix_pop();

	if (m_selectionBox)
	{
		gs_effect_t* solid = obs_get_base_effect(OBS_EFFECT_SOLID);

		while (gs_effect_loop(solid, "Solid"))
		{
			drawSelectionBox(m_startPos.x * map.scale, m_startPos.y * map.scale, m_mousePos.x * map.scale, m_mousePos.y * map.scale);
		}
	}

	gs_load_vertexbuffer(nullptr);
}

/**
* Spacing helpers
*/

static obs_source_t* CreateLabel(float pixelRatio, int i)
{
	obs_data_t* settings = obs_data_create();
	obs_data_t* font = obs_data_create();

	obs_data_set_string(font, "face", "Arial");

	// Bold text
	obs_data_set_int(font, "flags", 1);
	obs_data_set_int(font, "size", (int)(16 * pixelRatio));

	obs_data_set_obj(settings, "font", font);
	obs_data_set_bool(settings, "outline", true);
	obs_data_set_int(settings, "outline_color", 0x000000);
	obs_data_set_int(settings, "outline_size", 3);

	std::string name = "sl-dual spacing label " + std::to_string(i);
	obs_source_t* source = obs_source_create_private("text_gdiplus", name.c_str(), settings);

	obs_data_release(font);
	obs_data_release(settings);
	return source;
}

static void DrawLabel(obs_source_t* source, vec3& pos, vec3& viewport)
{
	if (!source)
		return;

	vec3_mul(&pos, &pos, &viewport);

	gs_matrix_push();
	gs_matrix_identity();
	gs_matrix_translate(&pos);
	obs_source_video_render(source);
	gs_matrix_pop();
}

static void DrawSpacingLine(vec3& start, vec3& end, vec3& viewport, float pixelRatio, const vec4& color)
{
	matrix4 transform;
	matrix4_identity(&transform);
	transform.x.x = viewport.x;
	transform.y.y = viewport.y;

	gs_effect_t* solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_technique_t* tech = gs_effect_get_technique(solid, "Solid");

	gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &color);

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);

	gs_matrix_push();
	gs_matrix_mul(&transform);

	vec2 scale;
	vec2_set(&scale, viewport.x, viewport.y);

	DrawLine(start.x, start.y, end.x, end.y, pixelRatio * (HANDLE_RADIUS / 2), scale);

	gs_matrix_pop();

	gs_load_vertexbuffer(nullptr);

	gs_technique_end_pass(tech);
	gs_technique_end(tech);
}

void SlDualEditor::renderSpacingHelper(int index, struct vec3& start, struct vec3& end, struct vec3& viewport, float pixelRatio, uint32_t baseW, uint32_t baseH)
{
	bool horizontal = (index == 2 || index == 3);

	// If outside of preview, don't render
	if (!((horizontal && (end.x >= start.x)) || (!horizontal && (end.y >= start.y))))
		return;

	float length = vec3_dist(&start, &end);

	float px;

	if (horizontal)
		px = length * (float)baseW;
	else
		px = length * (float)baseH;

	if (px <= 0.0f)
		return;

	obs_source_t* source = m_spacerLabel[index];
	vec3 labelSize, labelPos;
	vec3_set(&labelSize, (float)obs_source_get_width(source), (float)obs_source_get_height(source), 1.0f);

	vec3_div(&labelSize, &labelSize, &viewport);

	vec3 labelMargin;
	vec3_set(&labelMargin, SPACER_LABEL_MARGIN * pixelRatio, SPACER_LABEL_MARGIN * pixelRatio, 1.0f);
	vec3_div(&labelMargin, &labelMargin, &viewport);

	vec3_set(&labelPos, end.x, end.y, end.z);

	if (horizontal)
	{
		labelPos.x -= (end.x - start.x) / 2;
		labelPos.x -= labelSize.x / 2;
		labelPos.y -= labelMargin.y + (labelSize.y / 2) + (HANDLE_RADIUS / viewport.y);
	}
	else
	{
		labelPos.y -= (end.y - start.y) / 2;
		labelPos.y -= labelSize.y / 2;
		labelPos.x += labelMargin.x;
	}

	vec4 color = accessibilityColor("SelectRed", 1.0f, 0.0f, 0.0f);
	DrawSpacingLine(start, end, viewport, pixelRatio, color);

	int ipx = (int)px;

	if (source && ipx != m_spacerPx[index])
	{
		std::string text = std::to_string(ipx) + " px";
		obs_data_t* settings = obs_source_get_settings(source);
		obs_data_set_string(settings, "text", text.c_str());
		obs_source_update(source, settings);
		obs_data_release(settings);
		m_spacerPx[index] = ipx;
	}

	DrawLabel(source, labelPos, viewport);
}

void SlDualEditor::drawSpacingHelpers(const ViewMap& map)
{
	obs_scene_t* s = scene();

	if (!s)
		return;

	vec2 zero;
	vec2_zero(&zero);
	SceneFindBoxData data(zero, zero);
	obs_scene_enum_items(s, FindSelected, &data);

	if (data.sceneItems.size() != 1)
		return;

	obs_sceneitem_t* item = data.sceneItems[0];

	if (!item || obs_sceneitem_locked(item))
		return;

	vec2 itemSize = GetItemSize(item);

	if (itemSize.x == 0.0f || itemSize.y == 0.0f)
		return;

	obs_sceneitem_t* parentGroup = obs_sceneitem_get_group(s, item);

	if (parentGroup && obs_sceneitem_locked(parentGroup))
		return;

	matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	obs_transform_info oti;
	obs_sceneitem_get_info2(item, &oti);

	vec3 size;
	vec3_set(&size, (float)map.canvasW, (float)map.canvasH, 1.0f);

	vec3 left, right, top, bottom;
	vec3_set(&left, 0.0f, 0.5f, 1.0f);
	vec3_set(&right, 1.0f, 0.5f, 1.0f);
	vec3_set(&top, 0.5f, 0.0f, 1.0f);
	vec3_set(&bottom, 0.5f, 1.0f, 1.0f);

	float rot = oti.rot;

	if (parentGroup)
	{
		obs_transform_info groupOti;
		obs_sceneitem_get_info2(parentGroup, &groupOti);

		rot = oti.rot + groupOti.rot;

		matrix4_scale3f(&boxTransform, &boxTransform, groupOti.scale.x, groupOti.scale.y, 1.0f);
		matrix4_rotate_aa4f(&boxTransform, &boxTransform, 0.0f, 0.0f, 1.0f, RAD(groupOti.rot));
		matrix4_translate3f(&boxTransform, &boxTransform, groupOti.pos.x, groupOti.pos.y, 0.0f);
	}

	if (oti.scale.x < 0.0f && oti.bounds_type == OBS_BOUNDS_NONE)
	{
		vec3 l = left;
		vec3 r = right;
		vec3_copy(&left, &r);
		vec3_copy(&right, &l);
	}

	if (oti.scale.y < 0.0f && oti.bounds_type == OBS_BOUNDS_NONE)
	{
		vec3 t = top;
		vec3 b = bottom;
		vec3_copy(&top, &b);
		vec3_copy(&bottom, &t);
	}

	if (rot >= HELPER_ROT_BREAKPOINT)
	{
		for (float i = HELPER_ROT_BREAKPOINT; i <= 360.0f; i += 90.0f)
		{
			if (rot < i)
				break;

			vec3 l = left;
			vec3 r = right;
			vec3 t = top;
			vec3 b = bottom;

			vec3_copy(&top, &l);
			vec3_copy(&right, &t);
			vec3_copy(&bottom, &r);
			vec3_copy(&left, &b);
		}
	}
	else if (rot <= -HELPER_ROT_BREAKPOINT)
	{
		for (float i = -HELPER_ROT_BREAKPOINT; i >= -360.0f; i -= 90.0f)
		{
			if (rot > i)
				break;

			vec3 l = left;
			vec3 r = right;
			vec3 t = top;
			vec3 b = bottom;

			vec3_copy(&top, &r);
			vec3_copy(&right, &b);
			vec3_copy(&bottom, &l);
			vec3_copy(&left, &t);
		}
	}

	left = GetTransformedPos(left.x, left.y, boxTransform);
	right = GetTransformedPos(right.x, right.y, boxTransform);
	top = GetTransformedPos(top.x, top.y, boxTransform);
	bottom = GetTransformedPos(bottom.x, bottom.y, boxTransform);

	bottom.y = size.y - bottom.y;
	right.x = size.x - right.x;

	// Viewport in physical pixels of the rendered canvas area
	vec3 viewport;
	vec3_set(&viewport, (float)map.canvasW * map.scale, (float)map.canvasH * map.scale, 1.0f);

	vec3_div(&left, &left, &viewport);
	vec3_div(&right, &right, &viewport);
	vec3_div(&top, &top, &viewport);
	vec3_div(&bottom, &bottom, &viewport);

	vec3_mulf(&left, &left, map.scale);
	vec3_mulf(&right, &right, map.scale);
	vec3_mulf(&top, &top, map.scale);
	vec3_mulf(&bottom, &bottom, map.scale);

	float pr = pixelRatio();

	for (int i = 0; i < 4; i++)
	{
		if (!m_spacerLabel[i])
			m_spacerLabel[i] = CreateLabel(pr, i);
	}

	vec3 start, end;

	vec3_set(&start, top.x, 0.0f, 1.0f);
	vec3_set(&end, top.x, top.y, 1.0f);
	renderSpacingHelper(0, start, end, viewport, pr, map.canvasW, map.canvasH);

	vec3_set(&start, bottom.x, 1.0f - bottom.y, 1.0f);
	vec3_set(&end, bottom.x, 1.0f, 1.0f);
	renderSpacingHelper(1, start, end, viewport, pr, map.canvasW, map.canvasH);

	vec3_set(&start, 0.0f, left.y, 1.0f);
	vec3_set(&end, left.x, left.y, 1.0f);
	renderSpacingHelper(2, start, end, viewport, pr, map.canvasW, map.canvasH);

	vec3_set(&start, 1.0f - right.x, right.y, 1.0f);
	vec3_set(&end, 1.0f, right.y, 1.0f);
	renderSpacingHelper(3, start, end, viewport, pr, map.canvasW, map.canvasH);
}

/**
* Overlay entry (graphics thread)
*/

void SlDualEditor::drawOverlay(uint32_t cx, uint32_t cy)
{
	ViewMap map = viewMapFor(cx, cy);

	if (!map.valid)
		return;

	ensureGraphics();

	gs_viewport_push();
	gs_projection_push();
	gs_set_viewport(0, 0, (int)cx, (int)cy);
	gs_ortho(-map.offX, map.cxDisp - map.offX, -map.offY, map.cyDisp - map.offY, -100.0f, 100.0f);

	drawOverflow(map);
	drawSceneEditing(map);

	config_t* cfg = obs_frontend_get_user_config();

	if (cfg && config_get_bool(cfg, "BasicWindow", "SpacingHelpersEnabled"))
		drawSpacingHelpers(map);

	gs_projection_pop();
	gs_viewport_pop();
}
