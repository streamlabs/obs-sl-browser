#pragma once

#include <obs.h>
#include <obs-module.h>

#include "Models.h"

struct FilterData : public ORTModelData
{
public:
	// --- Inference / Model configuration ---
	std::string useGPU;
	uint32_t numThreads = 0;
	std::string modelSelection;
	std::unique_ptr<Model> model;
	std::wstring modelFilepath;
	std::mutex modelMutex;

	// --- OBS / Graphics handles ---
	obs_source_t* source = nullptr;
	gs_texrender_t* texrender = nullptr;
	gs_stagesurf_t* stagesurface = nullptr;
	gs_effect_t* maskEffect = nullptr;
	gs_effect_t* kawaseBlurEffect = nullptr;

	// --- Frame data ---
	cv::Mat inputBGRA;
	cv::Mat backgroundMask;
	cv::Mat lastBackgroundMask;
	cv::Mat lastImageBGRA;

	// --- Concurrency ---
	std::mutex inputBGRALock;
	std::mutex outputLock;

	// --- State flags ---
	bool isDisabled = false;

	// --- Threshold / Masking controls ---
	bool enableThreshold = true;
	float threshold = 0.5f;
	cv::Scalar backgroundColor{0, 0, 0, 0};
	float contourFilter = 0.05f;
	float smoothContour = 0.5f;
	float feather = 0.0f;
	int maskEveryXFrames = 1;
	int maskEveryXFramesCount = 0;

	// --- Similarity & temporal smoothing ---
	float temporalSmoothFactor = 0.0f;
	float imageSimilarityThreshold = 35.0f;
	bool enableImageSimilarity = true;

	// --- Blur / Depth settings ---
	int64_t blurBackground = 0;
	bool enableFocalBlur = false;
	float blurFocusPoint = 0.1f;
	float blurFocusDepth = 0.1f;
};
