/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/VideoSettings.h"
#include "libplacebo/colorspace.h"
#include "libplacebo/d3d11.h"
#include "libplacebo/log.h"
#include "libplacebo/renderer.h"





extern "C" {
#include <libavutil/pixfmt.h>
#include <libavutil/dovi_meta.h>
}

#include <string>
#include <vector>
#include <strmif.h>
#include <d3d9types.h>
#include <dxva2api.h>
#include <memory>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/hdr_dynamic_metadata.h>
#include <settings/lib/SettingDefinitions.h>
#include <settings/SettingControl.h>
#include <tinyxml.h>
#include <libplacebo/shaders/lut.h>
#include <dxgiformat.h>
#include <libplacebo/gpu.h>
#include <libplacebo/swapchain.h>
#include <libplacebo/cache.h>
#include <libplacebo/utils/frame_queue.h>
#include <rendering/dx/DeviceResources.h>
#include <libplacebo/shaders/custom.h>

#define MAX_FRAME_PASSES 256
#define MAX_BLEND_PASSES 8
#define MAX_BLEND_FRAMES 8
namespace PL
{
  typedef struct pl_d3d_format {
	pl_bit_encoding bits;     // per picture
	DXGI_FORMAT planes[4];      // DXGI format per plane
	int components[4];          // number of components per plane
	pl_channel component_mapping[4][4];
	int width_div[4];       // divide full width by this for each plane
	int height_div[4];      // divide full height by this for each plane
	char description[16];       // short description
	int num_planes;         // actual number of planes used
  } pl_d3d_format;

  class PLInstance
  {
  public:
	static std::shared_ptr<PLInstance> Get();
	PLInstance();

	virtual ~PLInstance();
	bool Init();
	void Reset();
	bool CreateSwapchain(void);
	void DestroySwapchain(void);
	void SetupSwapchainCallback(DX::DeviceResources& publisher);
	void OnSwapchainEventReceived(const std::string& message);
	void TeardownSwapchainCallback(DX::DeviceResources& publisher);
	void InitGammaShader();
	void SetGammaShaderGamma(float gamma);


	pl_d3d11 GetD3d11() { return m_plD3d11; }
	pl_swapchain GetSwapchain() { return m_plSwapchain; }
	pl_renderer GetRenderer() { return m_plRenderer; }
	pl_gpu GetGpu() { if(!m_plD3d11) return nullptr; else return m_plD3d11->gpu; }
	pl_cache* GetCache() { return &m_plCache; }
	pl_queue* GetQueue() { return &m_plQueue; }
	std::shared_ptr<const pl_hook> GetGammaShaderHook() { return pGammaShaderHook; }
	void fill_d3d_format(pl_d3d_format* info, DXGI_FORMAT format);
	void LogCurrent();

  private:
	size_t m_swapchainCallbackId;
	bool m_isInitialized = false;
	pl_cache m_plCache = nullptr;
	pl_queue m_plQueue = nullptr;
	pl_log m_plLog = nullptr;
	pl_d3d11 m_plD3d11 = nullptr;
	pl_swapchain m_plSwapchain = nullptr;
	pl_renderer m_plRenderer = nullptr;
	std::shared_ptr<const pl_hook> pGammaShaderHook = nullptr;


	int CurrentPrim;
	int Currenttransfer;
	int CurrentMatrix;
  };
}

class CPLHelper
{
public:
  static void SkinZoomUpdate(void);
  static void InitializeShaders(pl_gpu gpu);
  static void ResetShaders(CVideoSettings& vs);
  static void InitializeShaders(pl_gpu gpu, CVideoSettings& vs);
  static int getErrorDiffusionIndexFromDescription(std::string description);
  static int getColorMapIntentIndexFromDescription(std::string description);
  static int getDitherMethodIndexFromDescription(std::string description);
  static int getDitherTransferIndexFromDescription(std::string description);
  static int getConeConesIndexFromDescription(std::string description);
  static int getFilterIndexFromDescription(std::string description);
  static int getToneMapIndexFromDescription(std::string description);
  static int getGamutMapIndexFromDescription(std::string description);
  static int getLutTypeIndexFromDescription(std::string description);
  static int getDeinterlaceAlgoIndexFromDescription(std::string description);
  static std::string getColorMapIntentDescriptionFromIndex(int index);
  static std::string getConeConesDescriptionFromIndex(int index);
  static std::string getDitherMethodDescriptionFromIndex(int index);
  static std::string getDitherTransferDescriptionFromIndex(int index);
  static std::string getDiffusionKernelDescriptionFromIndex(int index);
  static std::string getLutTypeDescriptionFromIndex(int index);
  static std::string getDeinterlaceAlgoDescriptionFromIndex(int index);
  static void PlUpscalerOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current);
  static void PlDownscalerOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current);
  static void PlFrameMixerOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current);
  static void PlColorMapGamutMapFunctionOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current);
  static void PlColorMapToneMapFunctionOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current);
  static void PlDeinterlaceAlgoOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current);
  static void PlConeConesOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current);
  static void PlDitherTransferOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current);
  static void PlDitherMethodOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current);
  static void PlColorMapIntentOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current);
  static void PlShaderOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<StringSettingOption>& list, std::string& current);
  static void PlLutOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<StringSettingOption>& list, std::string& current);
  static void PlLutTypeOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current);
  static void PlDiffusionKernelOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current);
  static void SetVideoSettings(CVideoSettings& vs);
  static double BrightnessPl2Kodi(double plBrightness);
  static double BrightnessKodi2Pl(double kodiBrightness);
  static double ContrastPl2Kodi(double plContrast);
  static double ContrastKodi2Pl(double kodiContrast);
  static double GammaKodi2Pl(double kodiGamma);
  static double GammaPl2Kodi(double plGamma);

  static void UpdateVideoSettingsFromLibPLaceboParams(CVideoSettings& vs);
  static void UpdateLibPLaceboParamsFromVideoSettings(CVideoSettings& vs);
  static void LoadLibplaceboSettings(CVideoSettings& vs);
  static bool LoadLibplaceboSettings(CVideoSettings& vs, std::string path);
  static bool LoadLibplaceboSettings(CVideoSettings& vs, const TiXmlElement* pElement);
  static void SaveLibplaceboSettings(const CVideoSettings& vs, std::string path);
  static void SaveLibplaceboSettings(const CVideoSettings& vs, TiXmlNode* settings);
  static void LoadLutFile(CVideoSettings& vs, const std::string& path);
  static void AddShaderFile(pl_gpu gpu, CVideoSettings& vs, const std::string& fileName);
  static void SerializeShaders(const CVideoSettings& vs, std::string& serializedData);
  static void SerializeShaders(const CVideoSettings& vs, TiXmlNode* pNode);
  static void SaveShadersSettings(const CVideoSettings& vs, TiXmlNode* lpnode);
  static void LoadShaderSettings(CVideoSettings& vs, const std::string& data);
  static void LoadShaderSettings(CVideoSettings& vs, const TiXmlElement* pElement);
  static std::shared_ptr<const pl_custom_lut> ReadLut(const std::string& fileName);

  class CMonitor {
  private:
	std::vector<double> history;
	size_t writeIndex = 0;
	size_t maxFrames = 0;
	bool bufferFull = false;
	size_t skipCount = 0;

	// Helper to get active frame count in the buffer
	size_t getActiveCount() const {
	  return bufferFull ? maxFrames : writeIndex;
	}

  public:
	CMonitor(size_t frameWindowSize) : maxFrames(frameWindowSize) {
	  history.resize(maxFrames, 0.0);
	}

	void update(double jitterUs) {
	  if(skipCount < 20)
	  {
		skipCount++;
		return;
	  }
	  history [writeIndex] = jitterUs;

	  writeIndex++;
	  if(writeIndex >= maxFrames) {
		writeIndex = 0;
		bufferFull = true;
	  }
	}

	void calculateAll(double& mean, double& var, double& min, double& max) {
	  size_t count = getActiveCount();
	  if(count ==0)
	  {
		mean = 0;
		var = 0;
		min = 0;
		max = 0;
		return;
	  }

	  double sum = 0.0;
	  double maxVal = history [0];
	  double minVal = history [0];

	  double current_mean = 0.0;
	  double M2 = 0.0; // Tracks sum of squared differences dynamically

	  for(size_t i = 0; i < count; ++i)
	  {
		double x = history [i];

		if(x > maxVal) maxVal = x;
		if(x < minVal) minVal = x;

		size_t n = i + 1;
		double delta = x - current_mean;
		current_mean += delta / n;
		double delta2 = x - current_mean;
		M2 += delta * delta2;
	  }

	  mean = current_mean;
	  var = (count > 1) ? (M2 / (count - 1)) : 0.0;
	  min = minVal;
	  max = maxVal;
	}

	double calculateVariance(double& mean) {
	  size_t count = getActiveCount();
	  if(count < 2) return 0.0;

	  double sum = 0.0;
	  for(size_t i = 0; i < count; ++i) {
		sum += history [i];
	  }
	  mean = sum / count;

	  double varianceSum = 0.0;
	  for(size_t i = 0; i < count; ++i) {
		varianceSum += std::pow(history [i] - mean, 2);
	  }

	  return varianceSum / (count - 1);
	}

	double calculatePeak() const {
	  size_t count = getActiveCount();
	  if(count == 0) return 0.0;

	  double maxVal = history [0];
	  for(size_t i = 1; i < count; ++i) {
		if(history [i] > maxVal) {
		  maxVal = history [i];
		}
	  }
	  return maxVal;
	}

	double calculateMin() const {
	  size_t count = getActiveCount();
	  if(count == 0) return 0.0;

	  double minVal = history [0];
	  for(size_t i = 1; i < count; ++i) {
		if(history [i] < minVal) {
		  minVal = history [i];
		}
	  }
	  return minVal;
	}


	void reset(void) {
	  writeIndex = 0;
	  skipCount = 0;
	  bufferFull = false;
	}
  };

};

