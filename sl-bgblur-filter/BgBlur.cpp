#include "BgBlur.h"

#include <util\platform.h>

#include <filesystem>
#include <wchar.h>
#include <windows.h>

#include "Models.h"

#include "FilterData.h"

BgBlur::BgBlur()
{

}

BgBlur::~BgBlur()
{

}

/*static*/
const char *BgBlur::obs_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "Background Removal";
}

/*static*/
void *BgBlur::obs_create(obs_data_t *settings, obs_source_t *source)
{
	blog(LOG_INFO, "BgBlur::create");

	FilterData *filterD = new FilterData;
	filterD->source = source;
	filterD->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	filterD->env = std::make_unique<Ort::Env>(OrtLoggingLevel::ORT_LOGGING_LEVEL_ERROR, "bgremove-ort");
	filterD->modelSelection = MODEL_MEDIAPIPE;

	obs_update_settings(filterD, settings);
	return (void *)filterD;
}

/*static*/
void BgBlur::obs_video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	FilterData *filterD = (FilterData*)data;

	if (filterD->isDisabled || !obs_source_enabled(filterD->source) || !filterD->model)
		return;

	cv::Mat imageBGRA;

	{
		std::unique_lock<std::mutex> lock(filterD->inputBGRALock);

		// Nothing to process
		if (filterD->inputBGRA.empty())
			return;

		imageBGRA = filterD->inputBGRA.clone();
	}

	if (filterD->enableImageSimilarity)
	{
		if (!filterD->lastImageBGRA.empty() && !imageBGRA.empty() && filterD->lastImageBGRA.size() == imageBGRA.size())
		{
			// If the image is almost the same as the previous one then skip processing.
			if (cv::PSNR(filterD->lastImageBGRA, imageBGRA) > filterD->imageSimilarityThreshold)
				return;
		}

		filterD->lastImageBGRA = imageBGRA.clone();
	}

	// First frame. Initialize the background mask.
	if (filterD->backgroundMask.empty())
		filterD->backgroundMask = cv::Mat(imageBGRA.size(), CV_8UC1, cv::Scalar(255));

	filterD->maskEveryXFramesCount++;
	filterD->maskEveryXFramesCount %= filterD->maskEveryXFrames;

	try
	{
		if (filterD->maskEveryXFramesCount != 0 && !filterD->backgroundMask.empty())
		{
			// We are skipping processing of the mask for this frame.
			// Get the background mask previously generated.
		}
		else
		{
			cv::Mat backgroundMask;

			{
				std::unique_lock<std::mutex> lock(filterD->modelMutex);								
				cv::Mat outputImage;

				if (BgBlurGraphics::runFilterModelInference(filterD, imageBGRA, outputImage))
				{
					if (filterD->enableThreshold)
					{
						// Assume outputImage is now a single channel, uint8 image with values between 0 and 255
						// If we have a threshold, apply it. Otherwise, just use the output image as the mask
						// We need to make filterD->threshold (float [0,1]) be in that range
						const uint8_t threshold_value = (uint8_t)(filterD->threshold * 255.0f);
						backgroundMask = outputImage < threshold_value;
					}
					else
					{
						backgroundMask = 255 - outputImage;
					}
				}
			}

			if (backgroundMask.empty())
			{
				blog(LOG_WARNING, "BgBlur::videoTick - Background mask is empty. This shouldn't happen. Using previous mask.");
				return;
			}

			// Temporal smoothing
			if (filterD->temporalSmoothFactor > 0.0 && filterD->temporalSmoothFactor < 1.0 && !filterD->lastBackgroundMask.empty() && filterD->lastBackgroundMask.size() == backgroundMask.size())
			{

				float temporalSmoothFactor = filterD->temporalSmoothFactor;

				// The temporal smooth factor can't be smaller than the threshold
				if (filterD->enableThreshold)
					temporalSmoothFactor = std::max(temporalSmoothFactor, filterD->threshold);

				cv::addWeighted(backgroundMask, temporalSmoothFactor, filterD->lastBackgroundMask, 1.0 - temporalSmoothFactor, 0.0, backgroundMask);
			}

			filterD->lastBackgroundMask = backgroundMask.clone();

			// Contour processing
			// Only applicable if we are thresholding (and get a binary image)
			if (filterD->enableThreshold)
			{
				if (filterD->contourFilter > 0.0 && filterD->contourFilter < 1.0)
				{
					std::vector<std::vector<cv::Point>> contours;
					findContours(backgroundMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

					std::vector<std::vector<cv::Point>> filteredContours;
					const double contourSizeThreshold = (double)(backgroundMask.total()) * filterD->contourFilter;

					for (auto &contour : contours)
					{
						if (cv::contourArea(contour) > (double)contourSizeThreshold)
							filteredContours.push_back(contour);
					}

					backgroundMask.setTo(0);
					drawContours(backgroundMask, filteredContours, -1, cv::Scalar(255), -1);
				}

				if (filterD->smoothContour > 0.0)
				{
					int k_size = (int)(3 + 11 * filterD->smoothContour);
					k_size += k_size % 2 == 0 ? 1 : 0;
					cv::stackBlur(backgroundMask, backgroundMask, cv::Size(k_size, k_size));
				}

				// Resize the size of the mask back to the size of the original input.
				cv::resize(backgroundMask, backgroundMask, imageBGRA.size());

				// Additional contour processing at full resolution
				// If the mask was smoothed, apply a threshold to get a binary mask
				if (filterD->smoothContour > 0.0)
					backgroundMask = backgroundMask > 128;

				// Feather (blur) the mask
				if (filterD->feather > 0.0)
				{
					int k_size = (int)(40 * filterD->feather);
					k_size += k_size % 2 == 0 ? 1 : 0;
					cv::dilate(backgroundMask, backgroundMask, cv::Mat(), cv::Point(-1, -1), k_size / 3);
					cv::boxFilter(backgroundMask, backgroundMask, filterD->backgroundMask.depth(), cv::Size(k_size, k_size));
				}
			}

			// Save the mask for the next frame
			backgroundMask.copyTo(filterD->backgroundMask);
		}
	}
	catch (const Ort::Exception &e)
	{
		blog(LOG_ERROR, "ONNXRuntime Exception: %s", e.what());
	}
	catch (const std::exception &e)
	{
		blog(LOG_ERROR, "%s", e.what());
	}
}

/*static*/
void BgBlur::obs_video_render(void *data, gs_effect_t *_effect)
{
	UNUSED_PARAMETER(_effect);
	FilterData *filterD = (FilterData *)data;

	if (filterD->isDisabled)
	{
		if (filterD->source)
			obs_source_skip_video_filter(filterD->source);

		return;
	}

	uint32_t width, height;

	if (!BgBlurGraphics::getRGBAFromStageSurface(filterD, width, height))
	{
		if (filterD->source)
			obs_source_skip_video_filter(filterD->source);

		return;
	}

	if (!filterD->maskEffect)
	{
		// Effect failed to load, skip rendering
		if (filterD->source)
			obs_source_skip_video_filter(filterD->source);

		return;
	}

	gs_texture_t *alphaTexture = nullptr;

	{
		std::lock_guard<std::mutex> lock(filterD->outputLock);
		alphaTexture = gs_texture_create(filterD->backgroundMask.cols, filterD->backgroundMask.rows, GS_R8, 1, (const uint8_t **)&filterD->backgroundMask.data, 0);

		if (!alphaTexture)
		{
			blog(LOG_ERROR, "Failed to create alpha texture");

			if (filterD->source)
				obs_source_skip_video_filter(filterD->source);

			return;
		}
	}

	// Output the masked image
	gs_texture_t* blurredTexture = BgBlurGraphics::blurBackground(filterD, width, height, alphaTexture);

	if (!obs_source_process_filter_begin(filterD->source, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
	{
		if (filterD->source)
			obs_source_skip_video_filter(filterD->source);

		gs_texture_destroy(alphaTexture);
		gs_texture_destroy(blurredTexture);
		return;
	}

	gs_eparam_t* alphamask = gs_effect_get_param_by_name(filterD->maskEffect, "alphamask");
	gs_eparam_t* blurredBackground = gs_effect_get_param_by_name(filterD->maskEffect, "blurredBackground");

	gs_effect_set_texture(alphamask, alphaTexture);

	if (filterD->blurBackground > 0)
		gs_effect_set_texture(blurredBackground, blurredTexture);

	gs_blend_state_push();
	gs_reset_blend_state();

	const char *techName = "";

	if (filterD->blurBackground > 0)
	{
		if (filterD->enableFocalBlur)
			techName = "DrawWithFocalBlur";
		else
			techName = "DrawWithBlur";
	}
	else
	{
		techName = "DrawWithoutBlur";
	}

	obs_source_process_filter_tech_end(filterD->source, filterD->maskEffect, 0, 0, techName);

	gs_blend_state_pop();
	gs_texture_destroy(alphaTexture);
	gs_texture_destroy(blurredTexture);
}

/*static*/
void BgBlur::obs_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "enable_threshold", true);
	obs_data_set_default_double(settings, "threshold", 0.5);
	obs_data_set_default_double(settings, "contour_filter", 0.05);
	obs_data_set_default_double(settings, "smooth_contour", 0.5);
	obs_data_set_default_double(settings, "feather", 0.0);
	obs_data_set_default_string(settings, "useGPU", USEGPU_DML);
	obs_data_set_default_string(settings, "model_select", MODEL_MEDIAPIPE);
	obs_data_set_default_int(settings, "mask_every_x_frames", 1);
	obs_data_set_default_int(settings, "blur_background", 0);
	obs_data_set_default_int(settings, "numThreads", 1);
	obs_data_set_default_bool(settings, "enable_focal_blur", false);
	obs_data_set_default_double(settings, "temporal_smooth_factor", 0.85);
	obs_data_set_default_double(settings, "image_similarity_threshold", 35.0);
	obs_data_set_default_bool(settings, "enable_image_similarity", true);
	obs_data_set_default_double(settings, "blur_focus_point", 0.1);
	obs_data_set_default_double(settings, "blur_focus_depth", 0.0);
}

/*static*/
obs_properties_t *BgBlur::obs_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_int_slider(props, "blur_background", "Blur Amount", 0, 20, 1);

	// Model selection Props
	obs_property_t *p_model_select = obs_properties_add_list(props, "model_select", "Background Removal Quality", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p_model_select, "Fast (MediaPipe, CPU-friendly)", MODEL_MEDIAPIPE);
	obs_property_list_add_string(p_model_select, "Very Fast / Low Quality (Selfie Segmentation)", MODEL_SELFIE);
	obs_property_list_add_string(p_model_select, "Balanced (PPHumanSeg, CPU)", MODEL_PPHUMANSEG);
	obs_property_list_add_string(p_model_select, "Best Quality (Robust Video Matting, GPU)", MODEL_RVM);
	obs_property_list_add_string(p_model_select, "Sharp Cutout (RMBG, GPU recommended)", MODEL_RMBG);
	obs_property_list_add_string(p_model_select, "Legacy / Slow (SINet, CPU)", MODEL_SINET);
	obs_property_list_add_string(p_model_select, "Experimental Depth Blur (TCMonoDepth)", MODEL_DEPTH_TCMONODEPTH);

	// GPU, CPU and performance Props
	obs_property_t *p_use_gpu = obs_properties_add_list(props, "useGPU", "Rendering Method", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p_use_gpu, "CPU (Slow, works everywhere)", USEGPU_CPU);
	obs_property_list_add_string(p_use_gpu, "GPU - DirectML (Windows, AMD/Intel/NVIDIA)", USEGPU_DML);
	obs_property_list_add_string(p_use_gpu, "GPU - CUDA (NVIDIA only, fast)", USEGPU_CUDA);
	obs_property_list_add_string(p_use_gpu, "GPU - TensorRT (NVIDIA, max speed, setup needed)", USEGPU_TENSORRT);

	//obs_properties_add_text(props, "info", "Hello World", OBS_TEXT_INFO);
	return props;
}

/*static*/
void BgBlur::obs_update_settings(void *data, obs_data_t *settings)
{
	FilterData *filterD = (FilterData*)(data);

	filterD->isDisabled = true;

	filterD->enableThreshold = (float)obs_data_get_bool(settings, "enable_threshold");
	filterD->threshold = (float)obs_data_get_double(settings, "threshold");
	filterD->contourFilter = (float)obs_data_get_double(settings, "contour_filter");
	filterD->smoothContour = (float)obs_data_get_double(settings, "smooth_contour");
	filterD->feather = (float)obs_data_get_double(settings, "feather");
	filterD->maskEveryXFrames = (int)obs_data_get_int(settings, "mask_every_x_frames");
	filterD->maskEveryXFramesCount = (int)(0);
	filterD->blurBackground = obs_data_get_int(settings, "blur_background");
	filterD->enableFocalBlur = (float)obs_data_get_bool(settings, "enable_focal_blur");
	filterD->blurFocusPoint = (float)obs_data_get_double(settings, "blur_focus_point");
	filterD->blurFocusDepth = (float)obs_data_get_double(settings, "blur_focus_depth");
	filterD->temporalSmoothFactor = (float)obs_data_get_double(settings, "temporal_smooth_factor");
	filterD->imageSimilarityThreshold = (float)obs_data_get_double(settings, "image_similarity_threshold");
	filterD->enableImageSimilarity = (float)obs_data_get_bool(settings, "enable_image_similarity");

	const std::string newUseGpu = obs_data_get_string(settings, "useGPU");
	const std::string newModel = obs_data_get_string(settings, "model_select");
	const uint32_t newNumThreads = (uint32_t)obs_data_get_int(settings, "numThreads");

	if (filterD->modelSelection.empty() || filterD->modelSelection != newModel || filterD->useGPU != newUseGpu || filterD->numThreads != newNumThreads)
	{
		// lock modelMutex
		std::unique_lock<std::mutex> lock(filterD->modelMutex);

		// Re-initialize model if it's not already the selected one or switching inference device
		filterD->modelSelection = newModel;
		filterD->useGPU = newUseGpu;
		filterD->numThreads = newNumThreads;

		if (filterD->modelSelection == MODEL_SINET)
			filterD->model = std::make_unique<ModelSINET>();
		else if (filterD->modelSelection == MODEL_SELFIE)
			filterD->model = std::make_unique<ModelSelfie>();
		else if(filterD->modelSelection == MODEL_MEDIAPIPE)
			filterD->model = std::make_unique<ModelMediaPipe>();
		else if(filterD->modelSelection == MODEL_RVM)
			filterD->model = std::make_unique<ModelRVM>();
		else if(filterD->modelSelection == MODEL_PPHUMANSEG)
			filterD->model = std::make_unique<ModelPPHumanSeg>();
		else if(filterD->modelSelection == MODEL_DEPTH_TCMONODEPTH)
			filterD->model = std::make_unique<ModelTCMonoDepth>();
		else if(filterD->modelSelection == MODEL_RMBG)
			filterD->model = std::make_unique<ModelRMBG>();
		else
			blog(LOG_WARNING, "BgBlur::updateSettings modelSelection = %s", filterD->modelSelection.c_str());

		int ortSessionResult = BgBlurGraphics::createOrtSession(filterD);
		if (ortSessionResult != OBS_BGREMOVAL_ORT_SESSION_SUCCESS)
		{
			blog(LOG_ERROR, "Failed to create ONNXRuntime session. Error code: %d", ortSessionResult);

			// disable filter
			filterD->isDisabled = true;
			filterD->model.reset();
			return;
		}
	}

	obs_enter_graphics();

	gs_effect_destroy(filterD->maskEffect);
	filterD->maskEffect = gs_effect_create_from_file((std::filesystem::path(obs_get_module_binary_path(obs_current_module())).parent_path() / MASK_EFFECT_PATH).string().c_str(), NULL);

	gs_effect_destroy(filterD->kawaseBlurEffect);
	filterD->kawaseBlurEffect = gs_effect_create_from_file((std::filesystem::path(obs_get_module_binary_path(obs_current_module())).parent_path() / KAWASE_BLUR_EFFECT_PATH).string().c_str(), NULL);

	obs_leave_graphics();

	// enable
	filterD->isDisabled = false;
}

/*static*/
void BgBlur::obs_activate(void *data)
{
	FilterData* filterD = (FilterData*)data;
	filterD->isDisabled = false;
}

/*static*/
void BgBlur::obs_destroy(void *data)
{
	blog(LOG_INFO, "BgBlur::destroy");

	if (FilterData* filterD = (FilterData *)data)
	{
		filterD->isDisabled = true;

		obs_enter_graphics();
		gs_texrender_destroy(filterD->texrender);

		if (filterD->stagesurface)
			gs_stagesurface_destroy(filterD->stagesurface);
		
		gs_effect_destroy(filterD->maskEffect);
		gs_effect_destroy(filterD->kawaseBlurEffect);
		obs_leave_graphics();

		delete filterD;
	}
}

/*static*/
void BgBlur::obs_deactivate(void *data)
{
	FilterData *filterD = (FilterData *)data;
	filterD->isDisabled = true;
}


