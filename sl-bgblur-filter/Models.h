#pragma once

#include <onnxruntime_cxx_api.h>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <memory>
#include <vector>

// ---------- Small helpers ----------
template<typename T> static inline T vectorProduct(const std::vector<T> &v)
{
	T product = 1;
	for (auto &i : v)
	{
		if (i > 0)
			product *= i; // treat 0/-1 as dynamic, map to 1
	}
	return product;
}

static inline void hwc_to_chw(cv::InputArray src, cv::OutputArray dst)
{
	std::vector<cv::Mat> channels;
	cv::split(src, channels);
	for (auto &img : channels)
		img = img.reshape(1, 1); // flatten each C plane
	cv::hconcat(channels, dst);
}

static inline void chw_to_hwc_32f(cv::InputArray src, cv::OutputArray dst)
{
	const cv::Mat srcMat = src.getMat();
	const int channels = srcMat.channels();
	const int height = srcMat.rows;
	const int width = srcMat.cols;
	const int dtype = srcMat.type();
	(void)dtype; // CV_32F expected

	const int channelStride = height * width;
	cv::Mat flat = srcMat.reshape(1, 1);

	std::vector<cv::Mat> chs(channels);
	for (int i = 0; i < channels; ++i)
	{
		chs[i] = cv::Mat(height, width, CV_MAKE_TYPE(CV_32F, 1), flat.ptr<float>(0) + i * channelStride);
	}
	cv::merge(chs, dst);
}

// ---------- Base model interface ----------
class Model
{
public:
	virtual ~Model() = default;

	// Names / shapes
	virtual void populateInputOutputNames(const std::unique_ptr<Ort::Session> &session, std::vector<Ort::AllocatedStringPtr> &inputNames, std::vector<Ort::AllocatedStringPtr> &outputNames)
	{
		Ort::AllocatorWithDefaultOptions allocator;
		inputNames.clear();
		outputNames.clear();
		inputNames.push_back(session->GetInputNameAllocated(0, allocator));
		outputNames.push_back(session->GetOutputNameAllocated(0, allocator));
	}

	virtual bool populateInputOutputShapes(const std::unique_ptr<Ort::Session> &session, std::vector<std::vector<int64_t>> &inputDims, std::vector<std::vector<int64_t>> &outputDims)
	{
		inputDims.clear();
		outputDims.clear();
		inputDims.push_back(std::vector<int64_t>());
		outputDims.push_back(std::vector<int64_t>());

		// Output
		{
			const Ort::TypeInfo outInfo = session->GetOutputTypeInfo(0);
			const auto outTensorInfo = outInfo.GetTensorTypeAndShapeInfo();
			outputDims[0] = outTensorInfo.GetShape();
			for (auto &i : outputDims[0])
				if (i == -1)
					i = 1;
		}
		// Input
		{
			const Ort::TypeInfo inInfo = session->GetInputTypeInfo(0);
			const auto inTensorInfo = inInfo.GetTensorTypeAndShapeInfo();
			inputDims[0] = inTensorInfo.GetShape();
			for (auto &i : inputDims[0])
				if (i == -1)
					i = 1;
		}

		return inputDims[0].size() >= 3 && outputDims[0].size() >= 3;
	}

	// Tensor buffers
	virtual void allocateTensorBuffers(const std::vector<std::vector<int64_t>> &inputDims, const std::vector<std::vector<int64_t>> &outputDims, std::vector<std::vector<float>> &outputTensorValues, std::vector<std::vector<float>> &inputTensorValues, std::vector<Ort::Value> &inputTensor,
					   std::vector<Ort::Value> &outputTensor)
	{
		outputTensorValues.clear();
		outputTensor.clear();
		inputTensorValues.clear();
		inputTensor.clear();

		Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtAllocatorType::OrtDeviceAllocator, OrtMemType::OrtMemTypeDefault);

		for (size_t i = 0; i < inputDims.size(); ++i)
		{
			inputTensorValues.emplace_back(vectorProduct(inputDims[i]), 0.0f);
			inputTensor.push_back(Ort::Value::CreateTensor<float>(memInfo, inputTensorValues[i].data(), inputTensorValues[i].size(), inputDims[i].data(), inputDims[i].size()));
		}
		for (size_t i = 0; i < outputDims.size(); ++i)
		{
			outputTensorValues.emplace_back(vectorProduct(outputDims[i]), 0.0f);
			outputTensor.push_back(Ort::Value::CreateTensor<float>(memInfo, outputTensorValues[i].data(), outputTensorValues[i].size(), outputDims[i].data(), outputDims[i].size()));
		}
	}

	// IO sizes / pre-post
	virtual void getNetworkInputSize(const std::vector<std::vector<int64_t>> &inputDims, uint32_t &inputWidth, uint32_t &inputHeight)
	{
		// Default BHWC
		inputWidth = (uint32_t)inputDims[0][2];
		inputHeight = (uint32_t)inputDims[0][1];
	}

	virtual void prepareInputToNetwork(cv::Mat &resizedImage, cv::Mat &preprocessedImage) { preprocessedImage = resizedImage / 255.f; }

	virtual void postprocessOutput(cv::Mat &output) { (void)output; }

	virtual void loadInputToTensor(const cv::Mat &preprocessedImage, uint32_t inputWidth, uint32_t inputHeight, std::vector<std::vector<float>> &inputTensorValues) { preprocessedImage.copyTo(cv::Mat(inputHeight, inputWidth, CV_32FC3, &(inputTensorValues[0][0]))); }

	virtual cv::Mat getNetworkOutput(const std::vector<std::vector<int64_t>> &outputDims, std::vector<std::vector<float>> &outputTensorValues)
	{
		// Default BHWC → CV_32F(C)
		const uint32_t W = (uint32_t)outputDims[0].at(2);
		const uint32_t H = (uint32_t)outputDims[0].at(1);
		const int Ctype = CV_MAKE_TYPE(CV_32F, (int)outputDims[0].at(3));
		return cv::Mat(H, W, Ctype, outputTensorValues[0].data());
	}

	virtual void assignOutputToInput(std::vector<std::vector<float>> &, std::vector<std::vector<float>> &) {}

	// Inference
	virtual void runNetworkInference(const std::unique_ptr<Ort::Session> &session, const std::vector<Ort::AllocatedStringPtr> &inputNames, const std::vector<Ort::AllocatedStringPtr> &outputNames, const std::vector<Ort::Value> &inputTensor, std::vector<Ort::Value> &outputTensor)
	{
		if (inputNames.empty() || outputNames.empty() || inputTensor.empty() || outputTensor.empty())
			return;

		std::vector<const char *> inNames;
		inNames.reserve(inputNames.size());
		std::vector<const char *> outNames;
		outNames.reserve(outputNames.size());
		for (auto &n : inputNames)
			inNames.push_back(n.get());
		for (auto &n : outputNames)
			outNames.push_back(n.get());

		session->Run(Ort::RunOptions{nullptr}, inNames.data(), inputTensor.data(), (size_t)inNames.size(), outNames.data(), outputTensor.data(), (size_t)outNames.size());
	}
};

// ---------- BCHW specialization ----------
class ModelBCHW : public Model
{
public:
	void prepareInputToNetwork(cv::Mat &resizedImage, cv::Mat &preprocessedImage) override
	{
		resizedImage = resizedImage / 255.f;
		hwc_to_chw(resizedImage, preprocessedImage);
	}

	void postprocessOutput(cv::Mat &output) override
	{
		cv::Mat hwc;
		chw_to_hwc_32f(output, hwc);
		hwc.copyTo(output);
	}

	void getNetworkInputSize(const std::vector<std::vector<int64_t>> &inputDims, uint32_t &inputWidth, uint32_t &inputHeight) override
	{
		// BCHW
		inputWidth = (uint32_t)inputDims[0][3];
		inputHeight = (uint32_t)inputDims[0][2];
	}

	cv::Mat getNetworkOutput(const std::vector<std::vector<int64_t>> &outputDims, std::vector<std::vector<float>> &outputTensorValues) override
	{
		// BCHW
		const uint32_t W = (uint32_t)outputDims[0].at(3);
		const uint32_t H = (uint32_t)outputDims[0].at(2);
		const int Ctype = CV_MAKE_TYPE(CV_32F, (int)outputDims[0].at(1));
		return cv::Mat(H, W, Ctype, outputTensorValues[0].data());
	}

	void loadInputToTensor(const cv::Mat &preprocessedImage, uint32_t, uint32_t, std::vector<std::vector<float>> &inputTensorValues) override { inputTensorValues[0].assign(preprocessedImage.begin<float>(), preprocessedImage.end<float>()); }
};

// MediaPipe (BHWC 2-channel output, keep 2nd channel)
class ModelMediaPipe : public Model
{
public:
	cv::Mat getNetworkOutput(const std::vector<std::vector<int64_t>> &outputDims, std::vector<std::vector<float>> &outputTensorValues) override
	{
		const uint32_t W = (uint32_t)outputDims[0].at(2);
		const uint32_t H = (uint32_t)outputDims[0].at(1);
		return cv::Mat(H, W, CV_32FC2, outputTensorValues[0].data());
	}
	void postprocessOutput(cv::Mat &outputImage) override
	{
		std::vector<cv::Mat> splitv;
		cv::split(outputImage, splitv);
		outputImage = splitv[1]; // keep channel 1
	}
};

// PPHumanSeg (BCHW input, BHWC-like 2ch output; take ch-1, normalize)
class ModelPPHumanSeg : public ModelBCHW
{
public:
	void prepareInputToNetwork(cv::Mat &resizedImage, cv::Mat &preprocessedImage) override
	{
		resizedImage = (resizedImage / 256.0 - cv::Scalar(0.5, 0.5, 0.5)) / cv::Scalar(0.5, 0.5, 0.5);
		hwc_to_chw(resizedImage, preprocessedImage);
	}
	cv::Mat getNetworkOutput(const std::vector<std::vector<int64_t>> &outputDims, std::vector<std::vector<float>> &outputTensorValues) override
	{
		const uint32_t W = (uint32_t)outputDims[0].at(2);
		const uint32_t H = (uint32_t)outputDims[0].at(1);
		return cv::Mat(H, W, CV_32FC2, outputTensorValues[0].data());
	}
	void postprocessOutput(cv::Mat &outputImage) override
	{
		std::vector<cv::Mat> splitv;
		cv::split(outputImage, splitv);
		cv::normalize(splitv[1], outputImage, 1.0, 0.0, cv::NORM_MINMAX);
	}
};

// RMBG (BCHW, force output dims to match input H/W)
class ModelRMBG : public ModelBCHW
{
public:
	bool populateInputOutputShapes(const std::unique_ptr<Ort::Session> &session, std::vector<std::vector<int64_t>> &inputDims, std::vector<std::vector<int64_t>> &outputDims) override
	{
		ModelBCHW::populateInputOutputShapes(session, inputDims, outputDims);
		// output NCHW: match input H/W
		outputDims[0][2] = inputDims[0][2];
		outputDims[0][3] = inputDims[0][3];
		return true;
	}
};

// RVM (BCHW with recurrent states; multiple IOs)
class ModelRVM : public ModelBCHW
{
public:
	void populateInputOutputNames(const std::unique_ptr<Ort::Session> &session, std::vector<Ort::AllocatedStringPtr> &inputNames, std::vector<Ort::AllocatedStringPtr> &outputNames) override
	{
		Ort::AllocatorWithDefaultOptions allocator;
		inputNames.clear();
		outputNames.clear();
		for (size_t i = 0; i < session->GetInputCount(); ++i)
			inputNames.push_back(session->GetInputNameAllocated(i, allocator));
		for (size_t i = 1; i < session->GetOutputCount(); ++i) // skip first? (BGRA?)
			outputNames.push_back(session->GetOutputNameAllocated(i, allocator));
	}

	bool populateInputOutputShapes(const std::unique_ptr<Ort::Session> &session, std::vector<std::vector<int64_t>> &inputDims, std::vector<std::vector<int64_t>> &outputDims) override
	{
		inputDims.clear();
		outputDims.clear();

		for (size_t i = 0; i < session->GetInputCount(); ++i)
		{
			const auto ti = session->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo();
			inputDims.push_back(ti.GetShape());
		}
		for (size_t i = 1; i < session->GetOutputCount(); ++i)
		{
			const auto to = session->GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo();
			outputDims.push_back(to.GetShape());
		}

		const int base_w = 320, base_h = 192;

		// Input[0]=frame, [1..4]=states, [5]=downsample ratio scalar
		inputDims[0][0] = 1;
		inputDims[0][2] = base_h;
		inputDims[0][3] = base_w;
		for (size_t i = 1; i < 5; ++i)
		{
			inputDims[i][0] = 1;
			inputDims[i][1] = (i == 1) ? 16 : (i == 2) ? 20 : (i == 3) ? 40 : 64;
			inputDims[i][2] = base_h / (2 << (i - 1));
			inputDims[i][3] = base_w / (2 << (i - 1));
		}

		// Outputs: [0]=fgr? then [1..4]=states, match sizes
		outputDims[0][0] = 1;
		outputDims[0][2] = base_h;
		outputDims[0][3] = base_w;
		for (size_t i = 1; i < 5; ++i)
		{
			outputDims[i][0] = 1;
			outputDims[i][2] = base_h / (2 << (i - 1));
			outputDims[i][3] = base_w / (2 << (i - 1));
		}
		return true;
	}

	void loadInputToTensor(const cv::Mat &preprocessedImage, uint32_t, uint32_t, std::vector<std::vector<float>> &inputTensorValues) override
	{
		inputTensorValues[0].assign(preprocessedImage.begin<float>(), preprocessedImage.end<float>());
		inputTensorValues[5][0] = 1.0f; // downsample ratio
	}

	void assignOutputToInput(std::vector<std::vector<float>> &outputTensorValues, std::vector<std::vector<float>> &inputTensorValues) override
	{
		// feed recurrent states back
		for (size_t i = 1; i < 5; ++i)
			inputTensorValues[i].assign(outputTensorValues[i].begin(), outputTensorValues[i].end());
	}
};

// Selfie (BHWC normalize to 0..1)
class ModelSelfie : public Model
{
public:
	void postprocessOutput(cv::Mat &outputImage) override { cv::normalize(outputImage, outputImage, 1.0, 0.0, cv::NORM_MINMAX); }
};

// SINET (BCHW, custom mean/std, output 2ch where we keep ch-1)
class ModelSINET : public ModelBCHW
{
public:
	void prepareInputToNetwork(cv::Mat &resizedImage, cv::Mat &preprocessedImage) override
	{
		resizedImage = (resizedImage - cv::Scalar(102.890434, 111.25247, 126.91212)) / cv::Scalar(62.93292 * 255.0, 62.82138 * 255.0, 66.355705 * 255.0);
		hwc_to_chw(resizedImage, preprocessedImage);
	}
	cv::Mat getNetworkOutput(const std::vector<std::vector<int64_t>> &, std::vector<std::vector<float>> &outputTensorValues) override { return cv::Mat(320, 320, CV_32FC2, outputTensorValues[0].data()); }
	void postprocessOutput(cv::Mat &outputImage) override
	{
		cv::Mat hwc;
		chw_to_hwc_32f(outputImage, hwc);
		std::vector<cv::Mat> splitv;
		cv::split(hwc, splitv);
		outputImage = splitv[1];
	}
};

// TCMonoDepth (BCHW, do not normalize [0,255]→[0,1], output normalized)
class ModelTCMonoDepth : public ModelBCHW
{
public:
	void prepareInputToNetwork(cv::Mat &resizedImage, cv::Mat &preprocessedImage) override
	{
		// keep 0..255
		hwc_to_chw(resizedImage, preprocessedImage);
	}
	void postprocessOutput(cv::Mat &outputImage) override { cv::normalize(outputImage, outputImage, 1.0, 0.0, cv::NORM_MINMAX); }
};

// ---------- ORT model data bundle used by FilterData ----------
struct ORTModelData
{
	std::unique_ptr<Ort::Session> session;
	std::unique_ptr<Ort::Env> env;
	std::vector<Ort::AllocatedStringPtr> inputNames;
	std::vector<Ort::AllocatedStringPtr> outputNames;
	std::vector<Ort::Value> inputTensor;
	std::vector<Ort::Value> outputTensor;
	std::vector<std::vector<int64_t>> inputDims;
	std::vector<std::vector<int64_t>> outputDims;
	std::vector<std::vector<float>> outputTensorValues;
	std::vector<std::vector<float>> inputTensorValues;
};
