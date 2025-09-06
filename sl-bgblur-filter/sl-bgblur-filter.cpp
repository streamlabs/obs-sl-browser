#include <obs.hpp>
#include <obs-module.h>
#include <obs-config.h>
#include <util\platform.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("sl-bgblur-filter", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "SLABS BG Blur Filter";
}

bool obs_module_load(void)
{
	//struct obs_source_info sinfo = {};
	//sinfo.id = "sl-bgblur-filter",
	//sinfo.type = OBS_SOURCE_TYPE_FILTER,
	//sinfo.output_flags = OBS_SOURCE_VIDEO,
	//sinfo.get_name = background_filter_getname,
	//sinfo.create = background_filter_create,
	//sinfo.destroy = background_filter_destroy,
	//sinfo.get_defaults = background_filter_defaults,
	//sinfo.get_properties = background_filter_properties,
	//sinfo.update = background_filter_update,
	//sinfo.activate = background_filter_activate,
	//sinfo.deactivate = background_filter_deactivate,
	//sinfo.video_tick = background_filter_video_tick,
	//sinfo.video_render = background_filter_video_render,
	//
	//obs_register_source(&vst_filter);

	return true;
}

void obs_module_post_load(void)
{

}

void obs_module_unload(void)
{
	;
}
