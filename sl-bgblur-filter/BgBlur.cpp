#include "BgBlur.h"

#include <util\platform.h>

#include <filesystem>
#include <wchar.h>
#include <windows.h>

#include "models/ModelSINET.h"
#include "models/ModelMediapipe.h"
#include "models/ModelSelfie.h"
#include "models/ModelRVM.h"
#include "models/ModelPPHumanSeg.h"
#include "models/ModelTCMonoDepth.h"
#include "models/ModelRMBG.h"

#include "FilterData.h"

BgBlur::BgBlur()
{

}

BgBlur::~BgBlur()
{

}

/*static*/
const char *BgBlur::getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "BackgroundRemoval";
}

/*static*/
void *BgBlur::create(obs_data_t *settings, obs_source_t *source)
{
	blog(LOG_INFO, "BgBlur::create");
	FilterData *tf = new FilterData;

	tf->source = source;
	tf->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);

	std::string instanceName{"background-removal-inference"};
	tf->env = std::make_unique<Ort::Env>(OrtLoggingLevel::ORT_LOGGING_LEVEL_ERROR, instanceName.c_str());

	tf->modelSelection = MODEL_MEDIAPIPE;
	updateSettings(tf, settings);
	return (void *)tf;
}

/*static*/
void BgBlur::videoTick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	FilterData *tf = (FilterData*)data;

	if (tf->isDisabled || !obs_source_enabled(tf->source) || !tf->model)
		return;

	cv::Mat imageBGRA;

	{
		std::unique_lock<std::mutex> lock(tf->inputBGRALock);

		// Nothing to process
		if (tf->inputBGRA.empty())
			return;

		imageBGRA = tf->inputBGRA.clone();
	}

	if (tf->enableImageSimilarity)
	{
		if (!tf->lastImageBGRA.empty() && !imageBGRA.empty() && tf->lastImageBGRA.size() == imageBGRA.size())
		{
			// If the image is almost the same as the previous one then skip processing.
			if (cv::PSNR(tf->lastImageBGRA, imageBGRA) > tf->imageSimilarityThreshold)
				return;
		}

		tf->lastImageBGRA = imageBGRA.clone();
	}

	// First frame. Initialize the background mask.
	if (tf->backgroundMask.empty())
		tf->backgroundMask = cv::Mat(imageBGRA.size(), CV_8UC1, cv::Scalar(255));

	tf->maskEveryXFramesCount++;
	tf->maskEveryXFramesCount %= tf->maskEveryXFrames;

	try
	{
		if (tf->maskEveryXFramesCount != 0 && !tf->backgroundMask.empty())
		{
			// We are skipping processing of the mask for this frame.
			// Get the background mask previously generated.
		}
		else
		{
			cv::Mat backgroundMask;

			{
				std::unique_lock<std::mutex> lock(tf->modelMutex);								
				cv::Mat outputImage;

				if (runFilterModelInference(tf, imageBGRA, outputImage))
				{
					if (tf->enableThreshold)
					{
						// Assume outputImage is now a single channel, uint8 image with values between 0 and 255
						// If we have a threshold, apply it. Otherwise, just use the output image as the mask
						// We need to make tf->threshold (float [0,1]) be in that range
						const uint8_t threshold_value = (uint8_t)(tf->threshold * 255.0f);
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
			if (tf->temporalSmoothFactor > 0.0 && tf->temporalSmoothFactor < 1.0 && !tf->lastBackgroundMask.empty() && tf->lastBackgroundMask.size() == backgroundMask.size())
			{

				float temporalSmoothFactor = tf->temporalSmoothFactor;

				// The temporal smooth factor can't be smaller than the threshold
				if (tf->enableThreshold)
					temporalSmoothFactor = std::max(temporalSmoothFactor, tf->threshold);

				cv::addWeighted(backgroundMask, temporalSmoothFactor, tf->lastBackgroundMask, 1.0 - temporalSmoothFactor, 0.0, backgroundMask);
			}

			tf->lastBackgroundMask = backgroundMask.clone();

			// Contour processing
			// Only applicable if we are thresholding (and get a binary image)
			if (tf->enableThreshold)
			{
				if (tf->contourFilter > 0.0 && tf->contourFilter < 1.0)
				{
					std::vector<std::vector<cv::Point>> contours;
					findContours(backgroundMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

					std::vector<std::vector<cv::Point>> filteredContours;
					const double contourSizeThreshold = (double)(backgroundMask.total()) * tf->contourFilter;

					for (auto &contour : contours)
					{
						if (cv::contourArea(contour) > (double)contourSizeThreshold)
							filteredContours.push_back(contour);
					}

					backgroundMask.setTo(0);
					drawContours(backgroundMask, filteredContours, -1, cv::Scalar(255), -1);
				}

				if (tf->smoothContour > 0.0)
				{
					int k_size = (int)(3 + 11 * tf->smoothContour);
					k_size += k_size % 2 == 0 ? 1 : 0;
					cv::stackBlur(backgroundMask, backgroundMask, cv::Size(k_size, k_size));
				}

				// Resize the size of the mask back to the size of the original input.
				cv::resize(backgroundMask, backgroundMask, imageBGRA.size());

				// Additional contour processing at full resolution
				// If the mask was smoothed, apply a threshold to get a binary mask
				if (tf->smoothContour > 0.0)
					backgroundMask = backgroundMask > 128;

				// Feather (blur) the mask
				if (tf->feather > 0.0)
				{
					int k_size = (int)(40 * tf->feather);
					k_size += k_size % 2 == 0 ? 1 : 0;
					cv::dilate(backgroundMask, backgroundMask, cv::Mat(), cv::Point(-1, -1), k_size / 3);
					cv::boxFilter(backgroundMask, backgroundMask, tf->backgroundMask.depth(), cv::Size(k_size, k_size));
				}
			}

			// Save the mask for the next frame
			backgroundMask.copyTo(tf->backgroundMask);
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
void BgBlur::defaults(obs_data_t *settings)
{

}

/*static*/
obs_properties_t *BgBlur::properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_int_slider(props, "blur_background", obs_module_text("BlurBackgroundFactor0NoBlurUseColor"), 0, 20, 1);

	// Model selection Props
	obs_property_t *p_model_select = obs_properties_add_list(props, "model_select", "Background Removal Quality", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p_model_select, "Fast (MediaPipe, CPU-friendly)", MODEL_MEDIAPIPE);
	obs_property_list_add_string(p_model_select, "Very Fast / Low Quality (Selfie Segmentation)", MODEL_SELFIE);
	obs_property_list_add_string(p_model_select, "Balanced (PPHumanSeg, CPU)", MODEL_PPHUMANSEG);
	obs_property_list_add_string(p_model_select, "Best Quality (Robust Video Matting, GPU)", MODEL_RVM);
	obs_property_list_add_string(p_model_select, "Sharp Cutout (RMBG, GPU recommended)", MODEL_RMBG);
	obs_property_list_add_string(p_model_select, "Legacy / Slow (SINet, CPU)", MODEL_SINET);
	obs_property_list_add_string(p_model_select, "Experimental Depth Blur (TCMonoDepth)", MODEL_DEPTH_TCMONODEPTH);

	obs_properties_add_text(props, "info", "Hello World", OBS_TEXT_INFO);
	return props;
}

/*static*/
void BgBlur::updateSettings(void *data, obs_data_t *settings)
{
	FilterData *tf = (FilterData*)(data);

	tf->isDisabled = true;

	tf->enableThreshold = (float)obs_data_get_bool(settings, "enable_threshold");
	tf->threshold = (float)obs_data_get_double(settings, "threshold");
	tf->contourFilter = (float)obs_data_get_double(settings, "contour_filter");
	tf->smoothContour = (float)obs_data_get_double(settings, "smooth_contour");
	tf->feather = (float)obs_data_get_double(settings, "feather");
	tf->maskEveryXFrames = (int)obs_data_get_int(settings, "mask_every_x_frames");
	tf->maskEveryXFramesCount = (int)(0);
	tf->blurBackground = obs_data_get_int(settings, "blur_background");
	tf->enableFocalBlur = (float)obs_data_get_bool(settings, "enable_focal_blur");
	tf->blurFocusPoint = (float)obs_data_get_double(settings, "blur_focus_point");
	tf->blurFocusDepth = (float)obs_data_get_double(settings, "blur_focus_depth");
	tf->temporalSmoothFactor = (float)obs_data_get_double(settings, "temporal_smooth_factor");
	tf->imageSimilarityThreshold = (float)obs_data_get_double(settings, "image_similarity_threshold");
	tf->enableImageSimilarity = (float)obs_data_get_bool(settings, "enable_image_similarity");

	const std::string newUseGpu = obs_data_get_string(settings, "useGPU");
	const std::string newModel = obs_data_get_string(settings, "model_select");
	const uint32_t newNumThreads = (uint32_t)obs_data_get_int(settings, "numThreads");

	if (tf->modelSelection.empty() || tf->modelSelection != newModel || tf->useGPU != newUseGpu || tf->numThreads != newNumThreads)
	{
		// lock modelMutex
		std::unique_lock<std::mutex> lock(tf->modelMutex);

		// Re-initialize model if it's not already the selected one or switching inference device
		tf->modelSelection = newModel;
		tf->useGPU = newUseGpu;
		tf->numThreads = newNumThreads;

		if (tf->modelSelection == MODEL_SINET)
			tf->model = std::make_unique<ModelSINET>();
		else if (tf->modelSelection == MODEL_SELFIE)
			tf->model = std::make_unique<ModelSelfie>();
		else if(tf->modelSelection == MODEL_MEDIAPIPE)
			tf->model = std::make_unique<ModelMediaPipe>();
		else if(tf->modelSelection == MODEL_RVM)
			tf->model = std::make_unique<ModelRVM>();
		else if(tf->modelSelection == MODEL_PPHUMANSEG)
			tf->model = std::make_unique<ModelPPHumanSeg>();
		else if(tf->modelSelection == MODEL_DEPTH_TCMONODEPTH)
			tf->model = std::make_unique<ModelTCMonoDepth>();
		else if(tf->modelSelection == MODEL_RMBG)
			tf->model = std::make_unique<ModelRMBG>();
		else
			blog(LOG_WARNING, "BgBlur::updateSettings modelSelection = %s", tf->modelSelection.c_str());

		int ortSessionResult = createOrtSession(tf);
		if (ortSessionResult != OBS_BGREMOVAL_ORT_SESSION_SUCCESS)
		{
			blog(LOG_ERROR, "Failed to create ONNXRuntime session. Error code: %d", ortSessionResult);

			// disable filter
			tf->isDisabled = true;
			tf->model.reset();
			return;
		}
	}

	obs_enter_graphics();

	char *effect_path = obs_module_file(EFFECT_PATH);
	gs_effect_destroy(tf->effect);
	tf->effect = gs_effect_create_from_file(effect_path, NULL);
	bfree(effect_path);

	char *kawaseBlurEffectPath = obs_module_file(KAWASE_BLUR_EFFECT_PATH);
	gs_effect_destroy(tf->kawaseBlurEffect);
	tf->kawaseBlurEffect = gs_effect_create_from_file(kawaseBlurEffectPath, NULL);
	bfree(kawaseBlurEffectPath);

	obs_leave_graphics();

	// enable
	tf->isDisabled = false;
}

/*static*/
int BgBlur::createOrtSession(FilterData *tf)
{
	if (tf->model.get() == nullptr)
	{
		blog(LOG_ERROR, "BgBlur::createOrtSession null model");
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_MODEL;
	}

	Ort::SessionOptions sessionOptions;
	sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

	if (tf->useGPU != USEGPU_CPU)
	{
		sessionOptions.DisableMemPattern();
		sessionOptions.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
	}
	else
	{
		sessionOptions.SetInterOpNumThreads(tf->numThreads);
		sessionOptions.SetIntraOpNumThreads(tf->numThreads);
	}

	char *modelFilepath_rawPtr = obs_module_file(tf->modelSelection.c_str());

	if (modelFilepath_rawPtr == nullptr)
	{
		blog(LOG_ERROR, "obs_module_file returned null for %s", tf->modelSelection.c_str());
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_FILE_NOT_FOUND;
	}

	int outLength = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, modelFilepath_rawPtr, -1, nullptr, 0);
	tf->modelFilepath = std::wstring(outLength, L'\0');
	MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, modelFilepath_rawPtr, -1, tf->modelFilepath.data(), outLength);
	bfree(modelFilepath_rawPtr);

	try
	{
		if (tf->useGPU == USEGPU_DML)
		{
			auto &api = Ort::GetApi();
			OrtDmlApi *dmlApi = nullptr;
			Ort::ThrowOnError(api.GetExecutionProviderApi("DML", ORT_API_VERSION, (const void **)&dmlApi));
			Ort::ThrowOnError(dmlApi->SessionOptionsAppendExecutionProvider_DML(sessionOptions, 0));
		}

		tf->session = std::make_unique<Ort::Session>(*tf->env, tf->modelFilepath.c_str(), sessionOptions);
	}
	catch (const std::exception &e)
	{
		blog(LOG_ERROR, "%s", e.what());
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_STARTUP;
	}

	Ort::AllocatorWithDefaultOptions allocator;

	tf->model->populateInputOutputNames(tf->session, tf->inputNames, tf->outputNames);

	if (!tf->model->populateInputOutputShapes(tf->session, tf->inputDims, tf->outputDims))
	{
		blog(LOG_ERROR, "Unable to get model input and output shapes");
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_INPUT_OUTPUT;
	}

	for (size_t i = 0; i < tf->inputNames.size(); i++)
	{
		blog(LOG_INFO, "Model %s input %d: name %s shape (%d dim) %d x %d x %d x %d", tf->modelSelection.c_str(), (int)i, tf->inputNames[i].get(), (int)tf->inputDims[i].size(), (int)tf->inputDims[i][0], ((int)tf->inputDims[i].size() > 1) ? (int)tf->inputDims[i][1] : 0,
			((int)tf->inputDims[i].size() > 2) ? (int)tf->inputDims[i][2] : 0, ((int)tf->inputDims[i].size() > 3) ? (int)tf->inputDims[i][3] : 0);
	}
	for (size_t i = 0; i < tf->outputNames.size(); i++)
	{
		blog(LOG_INFO, "Model %s output %d: name %s shape (%d dim) %d x %d x %d x %d", tf->modelSelection.c_str(), (int)i, tf->outputNames[i].get(), (int)tf->outputDims[i].size(), (int)tf->outputDims[i][0], ((int)tf->outputDims[i].size() > 1) ? (int)tf->outputDims[i][1] : 0,
			((int)tf->outputDims[i].size() > 2) ? (int)tf->outputDims[i][2] : 0, ((int)tf->outputDims[i].size() > 3) ? (int)tf->outputDims[i][3] : 0);
	}

	// Allocate buffers
	tf->model->allocateTensorBuffers(tf->inputDims, tf->outputDims, tf->outputTensorValues, tf->inputTensorValues, tf->inputTensor, tf->outputTensor);

	return OBS_BGREMOVAL_ORT_SESSION_SUCCESS;
}

/*static*/
bool BgBlur::runFilterModelInference(FilterData *tf, const cv::Mat &imageBGRA, cv::Mat &output)
{
	// Preprocesses a BGRA video frame, resizes and converts it for the neural network, runs inference
	//	through the loaded model session, retrieves the output tensor, postprocesses it, and converts the result back to an 8-bit image.

	if (tf->session.get() == nullptr || tf->model.get() == nullptr)
		return false;

	cv::Mat imageRGB;
	cv::cvtColor(imageBGRA, imageRGB, cv::COLOR_BGRA2RGB);

	// Resize to network input size
	uint32_t inputWidth, inputHeight;
	tf->model->getNetworkInputSize(tf->inputDims, inputWidth, inputHeight);

	cv::Mat resizedImageRGB;
	cv::resize(imageRGB, resizedImageRGB, cv::Size(inputWidth, inputHeight));

	cv::Mat resizedImage, preprocessedImage;
	resizedImageRGB.convertTo(resizedImage, CV_32F);

	tf->model->prepareInputToNetwork(resizedImage, preprocessedImage);
	tf->model->loadInputToTensor(preprocessedImage, inputWidth, inputHeight, tf->inputTensorValues);
	tf->model->runNetworkInference(tf->session, tf->inputNames, tf->outputNames, tf->inputTensor, tf->outputTensor);

	cv::Mat outputImage = tf->model->getNetworkOutput(tf->outputDims, tf->outputTensorValues);
	tf->model->assignOutputToInput(tf->outputTensorValues, tf->inputTensorValues);
	tf->model->postprocessOutput(outputImage);
	outputImage.convertTo(output, CV_8U, 255.0);
	return true;
}

/*static*/
bool BgBlur::getRGBAFromStageSurface(FilterData *tf, uint32_t &width, uint32_t &height)
{
	// Captures a live video frame from a source, renders it to a texture, transfers it onto
	//	a staging surface, maps it into CPU-accessible memory, then it wraps the pixel buffer into an OpenCV cv::Mat (BGRA format)

	if (!obs_source_enabled(tf->source))
		return false;

	obs_source_t *target = obs_filter_get_target(tf->source);

	if (!target)
		return false;

	width = obs_source_get_base_width(target);
	height = obs_source_get_base_height(target);

	if (width == 0 || height == 0)
		return false;

	gs_texrender_reset(tf->texrender);

	if (!gs_texrender_begin(tf->texrender, width, height))
		return false;

	struct vec4 background;
	vec4_zero(&background);
	gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
	gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	obs_source_video_render(target);
	gs_blend_state_pop();
	gs_texrender_end(tf->texrender);

	if (tf->stagesurface)
	{
		uint32_t stagesurf_width = gs_stagesurface_get_width(tf->stagesurface);
		uint32_t stagesurf_height = gs_stagesurface_get_height(tf->stagesurface);

		if (stagesurf_width != width || stagesurf_height != height)
		{
			gs_stagesurface_destroy(tf->stagesurface);
			tf->stagesurface = nullptr;
		}
	}

	if (!tf->stagesurface)
		tf->stagesurface = gs_stagesurface_create(width, height, GS_BGRA);

	gs_stage_texture(tf->stagesurface, gs_texrender_get_texture(tf->texrender));

	uint8_t *video_data;
	uint32_t linesize;

	if (!gs_stagesurface_map(tf->stagesurface, &video_data, &linesize))
		return false;

	{
		std::lock_guard<std::mutex> lock(tf->inputBGRALock);
		tf->inputBGRA = cv::Mat(height, width, CV_8UC4, video_data, linesize);
	}

	gs_stagesurface_unmap(tf->stagesurface);
	return true;
}
