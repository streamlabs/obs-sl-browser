#include <obs.hpp>
#include <obs-module.h>
#include <obs-config.h>
#include <util\platform.h>

#include "models/ModelSINET.h"
#include "models/ModelMediapipe.h"
#include "models/ModelSelfie.h"
#include "models/ModelRVM.h"
#include "models/ModelPPHumanSeg.h"
#include "models/ModelTCMonoDepth.h"
#include "models/ModelRMBG.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("sl-bgblur-filter", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "SLABS BG Blur Filter";
}

bool obs_module_load(void)
{
	return true;
}

void obs_module_post_load(void)
{

}

void obs_module_unload(void)
{
	;
}
