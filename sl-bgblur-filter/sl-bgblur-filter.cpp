#include <obs.hpp>
#include <obs-module.h>
#include <obs-config.h>
#include <util\platform.h>

#include "BgBlur.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("sl-bgblur-filter", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "SLABS BG Remover";
}

bool obs_module_load(void)
{
	AllocConsole();
	freopen("conin$", "r", stdin);
	freopen("conout$", "w", stdout);
	freopen("conout$", "w", stderr);

	struct obs_source_info sinfo = {};
	sinfo.id = "sl-bgblur-filter";
	sinfo.type = OBS_SOURCE_TYPE_FILTER;
	sinfo.output_flags = OBS_SOURCE_VIDEO;
	sinfo.get_name = BgBlur::obs_getname;
	sinfo.create = BgBlur::obs_create;
	sinfo.destroy = BgBlur::obs_destroy;
	sinfo.get_defaults = BgBlur::obs_defaults;
	sinfo.get_properties = BgBlur::obs_properties;
	sinfo.update = BgBlur::obs_update_settings;
	sinfo.activate = BgBlur::obs_activate;
	sinfo.deactivate = BgBlur::obs_deactivate;
	sinfo.video_tick = BgBlur::obs_video_tick;
	sinfo.video_render = BgBlur::obs_video_render;	
	obs_register_source(&sinfo);

	return true;
}

void obs_module_post_load(void)
{

}

void obs_module_unload(void)
{
	;
}
