/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "PlHelper.h"

#include "FileItemList.h"
#include "ServiceBroker.h"
#include "application/ApplicationPlayer.h"
#include "dialogs/GUIDialogFileBrowser.h"
#include "dialogs/GUIDialogYesNo.h"
#include "filesystem/File.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIWindowManager.h"
#include "resources/LocalizeStrings.h"
#include "resources/ResourcesComponent.h"
#include "settings/SettingsComponent.h"
#include "utils/URIUtils.h"
#include "utils/XMLUtils.h"
#include <FileItem.h>
#include <Windows.h>
#include <cmath>
#include <commons/ilog.h>
#include <cores/VideoSettings.h>
#include <dxgiformat.h>
#include <filesystem/Directory.h>
#include <filesystem/IDirectory.h>
#include <guilib/GUIKeyboardFactory.h>
#include <guilib/GUIMessage.h>
#include <guilib/GUIMessageIDs.h>
#include <libavutil/mathematics.h>
#include <libplacebo/colorspace.h>
#include <libplacebo/config.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/dither.h>
#include <libplacebo/filters.h>
#include <libplacebo/gamut_mapping.h>
#include <libplacebo/gpu.h>
#include <libplacebo/log.h>
#include <libplacebo/options.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/shaders/deinterlacing.h>
#include <libplacebo/shaders/dithering.h>
#include <libplacebo/shaders/lut.h>
#include <libplacebo/tone_mapping.h>
#include <memory>
#include <rendering/dx/DeviceResources.h>
#include <settings/lib/SettingDefinitions.h>
#include <string>
#include <string.h>
#include <tinyxml.h>
#include <utils/ComponentContainer.h>
#include <utils/StringUtils.h>
#include <utils/Variant.h>
#include <utils/XBMCTinyXML.h>
#include <utils/log.h>
#include <variant>
#include <vector>
#include <filesystem/SpecialProtocol.h>
#include <libplacebo/cache.h>


using namespace XFILE;

static void pl_log_cb(void*, enum pl_log_level level, const char* msg)
{
  switch (level) {
  case PL_LOG_FATAL:
	CLog::Log(LOGFATAL, "libPlacebo Fatal: {}", msg);
	break;
  case PL_LOG_ERR:
	CLog::Log(LOGERROR, "libPlacebo Error: {}", msg);

	break;
  case PL_LOG_WARN:
	CLog::Log(LOGWARNING, "libPlacebo Warning: {}", msg);
	break;
  case PL_LOG_INFO:
	CLog::Log(LOGINFO, "libPlacebo Info: {}", msg);
	break;
  case PL_LOG_DEBUG:
	CLog::Log(LOGDEBUG, "libPlacebo Debug: {}", msg);
	break;
  case PL_LOG_NONE:
  case PL_LOG_TRACE:
	CLog::Log(LOGNONE, "libPlacebo Trace: {}", msg);
	break;
  }
}

std::shared_ptr<PL::PLInstance> PL::PLInstance::Get()
{
  static std::shared_ptr<PLInstance> sPLResources(new PLInstance);
  return sPLResources;
}

PL::PLInstance::PLInstance()
  : m_plD3d11(nullptr),
  m_plLog(nullptr),
  m_plRenderer(nullptr),
  m_plSwapchain(nullptr),
  CurrentPrim(0),
  CurrentMatrix(0),
  Currenttransfer(0)
{

}

PL::PLInstance::~PLInstance() = default;

bool PL::PLInstance::Init()
{
  // Log
  pl_log_params log_param{};
  log_param.log_cb = pl_log_cb;
  log_param.log_level = PL_LOG_DEBUG;
  m_plLog = pl_log_create(PL_API_VER, &log_param);

  // D3D11
  pl_d3d11_params d3d_param{};
  d3d_param.device = DX::DeviceResources::Get()->GetD3DDevice();
  d3d_param.adapter = DX::DeviceResources::Get()->GetAdapter();
  d3d_param.adapter_luid = DX::DeviceResources::Get()->GetAdapterDesc().AdapterLuid;
  d3d_param.allow_software = true;
  d3d_param.force_software = false;
  d3d_param.no_compute = false;
  d3d_param.debug = false;
  //libplacebo dont touch it if 0
  d3d_param.max_frame_latency = 0;
  m_plD3d11 = pl_d3d11_create(m_plLog, &d3d_param);
  if (!m_plD3d11)
	return false;
  
  // Swapchain
  if(!CreateSwapchain())
	return false;

  SetupSwapchainCallback(*DX::DeviceResources::Get());

  //Renderer
  m_plRenderer = pl_renderer_create(m_plLog, m_plD3d11->gpu);

  // Cache
  std::string cacheDirectory = "special://temp/LibplaceboCache/";
  if(!XFILE::CDirectory::Exists(cacheDirectory))
	XFILE::CDirectory::Create(cacheDirectory);
  pl_cache_params cacheParams{};
  cacheParams.log = PL::PLInstance::Get()->m_plLog;
  cacheParams.max_object_size = 0; // No limit on individual object size
  cacheParams.max_total_size = 1 << 30; //cl 1 GB total cache size limit ?
  cacheParams.set = pl_cache_set_file;
  cacheParams.get = pl_cache_get_file;
  static const std::string cacheDir = CSpecialProtocol::TranslatePath(cacheDirectory);
  cacheParams.priv = (void*)cacheDir.c_str();
  m_plCache = pl_cache_create(&cacheParams);
  pl_gpu_set_cache(PL::PLInstance::Get()->GetGpu(), m_plCache);

  // Queue
  m_plQueue = pl_queue_create(PL::PLInstance::Get()->GetGpu());

  m_isInitialized = true;
  return true;
}

void PL::PLInstance::DestroySwapchain(void)
{
  if(m_plSwapchain)
  {
	pl_swapchain_destroy(&m_plSwapchain);
	m_plSwapchain = NULL;
  }
}

bool PL::PLInstance::CreateSwapchain(void)
{
  if(!m_plD3d11)
	return false;

  bool bIs8Bits = false;
  DXGI_SWAP_CHAIN_DESC1 desc;
  HRESULT hr = DX::DeviceResources::Get()->GetSwapChain()->GetDesc1(&desc);

  if(SUCCEEDED(hr)) {
	switch(desc.Format) {
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
	  bIs8Bits = true;
	  break;
	}}

  pl_d3d11_swapchain_params swapchain_param {};
  swapchain_param.swapchain = DX::DeviceResources::Get()->GetSwapChain();
  swapchain_param.disable_10bit_sdr = bIs8Bits;
  //everything else is not used
  m_plSwapchain = pl_d3d11_create_swapchain(m_plD3d11, &swapchain_param);
  if(!m_plSwapchain)
	return false;
  return true;
}

void  PL::PLInstance::SetupSwapchainCallback(DX::DeviceResources& publisher)
{
  m_swapchainCallbackId = DX::DeviceResources::Get()->RegisterSwapchainListener([this](const std::string& msg) 
	{
	this->OnSwapchainEventReceived(msg);
	});
}

void PL::PLInstance::TeardownSwapchainCallback(DX::DeviceResources& publisher) 
{
  publisher.UnregisterSwapchainListener(m_swapchainCallbackId);
}

void  PL::PLInstance::OnSwapchainEventReceived(const std::string& message)
{
  if(message=="DestroySwapChain")
  {
	DestroySwapchain();
  }
  else if(message=="CreateSwapChain")
  {
	CreateSwapchain();
  }
}

void PL::PLInstance::Reset()
{
  if(m_isInitialized)
  {
	if(m_plCache) pl_cache_destroy(&m_plCache);
	if(m_plQueue) pl_queue_destroy(&m_plQueue);
	if(m_plRenderer) pl_renderer_destroy(&m_plRenderer);
	TeardownSwapchainCallback(*DX::DeviceResources::Get());
	if(m_plSwapchain) pl_swapchain_destroy(&m_plSwapchain);
	if(m_plD3d11) pl_d3d11_destroy(&m_plD3d11);
	if(m_plLog) pl_log_destroy(&m_plLog);

	m_plCache = nullptr;
	m_plQueue = nullptr;
	m_plRenderer = nullptr;
	m_plSwapchain = nullptr;
	m_plD3d11 = nullptr;
	m_plLog = nullptr;

	m_isInitialized = false;
  }
}

void PL::PLInstance::LogCurrent()
{
  if (CurrentPrim == PL_COLOR_PRIM_COUNT)
	CurrentPrim = 0;
  if (CurrentMatrix == PL_COLOR_SYSTEM_COUNT)
	CurrentMatrix = 0;
  if (Currenttransfer == PL_COLOR_TRC_COUNT)
	Currenttransfer = 0;
  std::string sSys = pl_color_system_name((pl_color_system)CurrentMatrix);
  std::string sTrans = pl_color_transfer_name((pl_color_transfer)Currenttransfer);
  std::string sPrim = pl_color_primaries_name((pl_color_primaries)CurrentPrim);
  CLog::Log(LOGINFO, "LibPlaceboCurrent Color Settings: Primaries: {}", sPrim.c_str());
  CLog::Log(LOGINFO, "LibPlaceboCurrent Color Settings: Transfer: {}", sTrans.c_str());
  CLog::Log(LOGINFO, "LibPlaceboCurrent Color Settings: Matrix: {}", sSys.c_str());
}

void PL::PLInstance::fill_d3d_format(pl_d3d_format* info, DXGI_FORMAT format)
{
  memset(info, 0, sizeof(pl_d3d_format));

  switch(format)
  {
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
	info->bits.color_depth = 16;
	info->bits.sample_depth = 16;
	info->bits.bit_shift = 0;
	info->planes [0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	info->num_planes = 1;
	info->components [0] = 4; 
	info->component_mapping [0][0] = PL_CHANNEL_R;
	info->component_mapping [0][1] = PL_CHANNEL_G;
	info->component_mapping [0][2] = PL_CHANNEL_B;
	info->component_mapping [0][3] = PL_CHANNEL_A;
	info->width_div [0] = 1;
	info->height_div [0] = 1;
	strcpy(info->description, "rgba16f");
	break;
  case DXGI_FORMAT_R10G10B10A2_UNORM:
	info->bits.color_depth = 10;
	info->bits.sample_depth = 10;
	info->bits.bit_shift = 0;
	info->planes [0] = DXGI_FORMAT_R10G10B10A2_UNORM;
	info->component_mapping [0][0] = PL_CHANNEL_R;
	info->component_mapping [0][1] = PL_CHANNEL_G;
	info->component_mapping [0][2] = PL_CHANNEL_B;
	info->component_mapping [0][3] = PL_CHANNEL_A;
	info->components [0] = 4;
	info->width_div [0] = 1;
	info->height_div [0] = 1;
	info->num_planes = 1;
	strcpy(info->description, "rgba10");
	break;

  case DXGI_FORMAT_R8G8B8A8_UNORM:
	info->bits.color_depth = 8;
	info->bits.sample_depth = 8;
	info->bits.bit_shift = 0;
	info->planes [0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	info->component_mapping [0][0] = PL_CHANNEL_R;
	info->component_mapping [0][1] = PL_CHANNEL_G;
	info->component_mapping [0][2] = PL_CHANNEL_B;
	info->component_mapping [0][3] = PL_CHANNEL_A;
	info->components [0] = 4;
	info->width_div [0] = 1;
	info->height_div [0] = 1;
	info->num_planes = 1;
	strcpy(info->description, "rgba");
	break;

  case DXGI_FORMAT_B8G8R8A8_UNORM:
	info->bits.color_depth = 8;
	info->bits.sample_depth = 8;
	info->bits.bit_shift = 0;
	info->planes [0] = DXGI_FORMAT_B8G8R8A8_UNORM;
	info->component_mapping [0][0] = PL_CHANNEL_R;
	info->component_mapping [0][1] = PL_CHANNEL_G;
	info->component_mapping [0][2] = PL_CHANNEL_B;
	info->component_mapping [0][3] = PL_CHANNEL_A;
	info->components [0] = 4;
	info->width_div [0] = 1;
	info->height_div [0] = 1;
	info->num_planes = 1;
	strcpy(info->description, "bgra");
	break;

  case DXGI_FORMAT_NV12:
	info->bits.color_depth = 8;
	info->bits.sample_depth = 8;
	info->bits.bit_shift = 0;
	info->planes [0] = DXGI_FORMAT_R8_UNORM; // Y plane
	info->planes [1] = DXGI_FORMAT_R8G8_UNORM; // UV plane
	info->component_mapping [0][0] = PL_CHANNEL_Y;
	info->component_mapping [1][0] = PL_CHANNEL_U;
	info->component_mapping [1][1] = PL_CHANNEL_V;
	info->components [0] = 1;
	info->components [1] = 2;
	info->width_div [0] = 1; // full width
	info->height_div [0] = 1; // full height
	info->width_div [1] = 2; // half width
	info->height_div [1] = 2; // half height
	info->num_planes = 2;
	strcpy(info->description, "nv12");
	break;

  case DXGI_FORMAT_P010:
	info->bits.color_depth = 10;
	info->bits.sample_depth = 16;
	info->bits.bit_shift = 6;
	info->planes [0] = DXGI_FORMAT_R16_UNORM; // Y plane
	info->planes [1] = DXGI_FORMAT_R16G16_UNORM; // UV plane
	info->component_mapping [0][0] = PL_CHANNEL_Y;
	info->component_mapping [1][0] = PL_CHANNEL_U;
	info->component_mapping [1][1] = PL_CHANNEL_V;
	info->components [0] = 1;
	info->components [1] = 2;
	info->width_div [0] = 1;
	info->height_div [0] = 1;
	info->width_div [1] = 2;
	info->height_div [1] = 2;
	info->num_planes = 2;
	strcpy(info->description, "p010");
	break;

  case DXGI_FORMAT_Y210:
	info->bits.color_depth = 10;
	info->bits.sample_depth = 16;
	info->bits.bit_shift = 6;
	info->planes [0] = DXGI_FORMAT_R16G16B16A16_UNORM; // Y plane
	info->component_mapping [0][0] = PL_CHANNEL_Y;
	info->component_mapping [0][1] = PL_CHANNEL_U;
	info->component_mapping [0][2] = PL_CHANNEL_V;
	info->component_mapping [0][3] = PL_CHANNEL_A;
	info->components [0] = 4;
	info->width_div [0] = 1;
	info->height_div [0] = 1;
	info->num_planes = 1;
	strcpy(info->description, "y210");
	break;

  case DXGI_FORMAT_P016:
	info->bits.color_depth = 16;
	info->bits.sample_depth = 16;
	info->bits.bit_shift = 0;
	info->planes [0] = DXGI_FORMAT_R16_UNORM;
	info->planes [1] = DXGI_FORMAT_R16G16_UNORM;
	info->component_mapping [0][0] = PL_CHANNEL_Y;
	info->component_mapping [1][0] = PL_CHANNEL_U;
	info->component_mapping [1][1] = PL_CHANNEL_V;
	info->components [0] = 1;
	info->components [1] = 2;
	info->width_div [0] = 1;
	info->height_div [0] = 1;
	info->width_div [1] = 2;
	info->height_div [1] = 2;
	info->num_planes = 2;
	strcpy(info->description, "p016");
	break;

  case DXGI_FORMAT_YUY2:
	info->bits.color_depth = 8;
	info->bits.sample_depth = 16; // packed 2 bytes per component pair
	info->bits.bit_shift = 0;
	info->planes [0] = DXGI_FORMAT_R8G8B8A8_UNORM; // pseudo-plane
	info->component_mapping [0][0] = PL_CHANNEL_R;
	info->component_mapping [0][1] = PL_CHANNEL_G;
	info->component_mapping [0][2] = PL_CHANNEL_B;
	info->component_mapping [0][3] = PL_CHANNEL_A;
	info->width_div [0] = 1;
	info->height_div [0] = 1;
	info->num_planes = 1;
	strcpy(info->description, "yuy2");
	break;

  default:
	info->num_planes = 0;
	strcpy(info->description, "unknown");
	break;
  }
}

double CPLHelper::BrightnessPl2Kodi(double plBrightness)
{
  return (plBrightness + 1.0) * 50.0;  //cl rounding, all
}
double CPLHelper::BrightnessKodi2Pl(double kodiBrightness)
{
  return kodiBrightness / 50.0 - 1.0;
}
double CPLHelper::ContrastPl2Kodi(double plContrast)
{
  return log10f(plContrast * 99.0 / 100.0 + 0.01) * 25.0 + 50.0;
}
double CPLHelper::ContrastKodi2Pl(double kodiContrast)
{
  return (std::pow(10.0, (kodiContrast - 50.0) / 25.0) - 0.01) * 100.0 / 99.0;
}
double CPLHelper::GammaKodi2Pl(double kodiGamma)
{
  return (std::pow(10.0, (kodiGamma-20.0)/40.0) - pow(10, -0.5)) / (1.0 - pow(10, -0.5));
}
double CPLHelper::GammaPl2Kodi(double plGamma)
{
  return log10f(plGamma * (1.0 - pow(10, -0.5)) + pow(10, -0.5)) * 40.0 + 20.0;
}

//cl this can be called from other threads which could cause a race condition
void CPLHelper::SetVideoSettings(CVideoSettings& vs)
{
  auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();
  appPlayer->SetVideoSettings(vs);
}

void CPLHelper::SkinZoomUpdate(void)
{
  auto& components = CServiceBroker::GetAppComponents();
  auto appPlayer = components.GetComponent<CApplicationPlayer>();
  CVideoSettings vs = appPlayer->GetVideoSettings();
  if (vs.m_PlaceboSkinZoomHint != vs.m_PlaceboSkinZoom)
  {
	vs.m_PlaceboSkinZoomHint = vs.m_PlaceboSkinZoom;
	appPlayer->SetVideoSettings(vs);
	CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_WINDOW_RESIZE);
	CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(msg);
  }
}

void CPLHelper::UpdateVideoSettingsFromLibPLaceboParams(CVideoSettings& vs)
{
  pl_options m_placeboOptions = vs.m_placeboOptions->getPlOptions();

  vs.m_Gamma = GammaPl2Kodi(m_placeboOptions->color_adjustment.gamma);

  vs.m_PlaceboColorAdjustmentEnabled = m_placeboOptions->params.color_adjustment != NULL;
  vs.m_PlaceboSaturation = log10f(m_placeboOptions->color_adjustment.saturation) * 40.0 + 50.0;
  vs.m_PlaceboHue = fmod(m_placeboOptions->color_adjustment.hue * 180.0 / M_PI, 360.0);
  vs.m_PlaceboTemperature = m_placeboOptions->color_adjustment.temperature * 3500.0 + 6500.0;

  vs.m_PlaceboPeakDetectEnabled = m_placeboOptions->params.peak_detect_params != NULL;
  vs.m_PlaceboPeakDetectSmoothingPeriod = m_placeboOptions->peak_detect_params.smoothing_period;
  vs.m_PlaceboPeakDetectSceneThresholdLow = m_placeboOptions->peak_detect_params.scene_threshold_low;
  vs.m_PlaceboPeakDetectSceneThresholdHigh = m_placeboOptions->peak_detect_params.scene_threshold_high;
  vs.m_PlaceboPeakDetectPercentile = m_placeboOptions->peak_detect_params.percentile;
  vs.m_PlaceboPeakDetectBlackCutoff = m_placeboOptions->peak_detect_params.black_cutoff;
  vs.m_PlaceboPeakDetectAllowDelayed = m_placeboOptions->peak_detect_params.allow_delayed;

  vs.m_PlaceboUpscaler = m_placeboOptions->params.upscaler == NULL ? -1 : CPLHelper::getFilterIndexFromDescription(m_placeboOptions->params.upscaler->description);
  vs.m_PlaceboDownscaler = m_placeboOptions->params.downscaler == NULL ? -1 : CPLHelper::getFilterIndexFromDescription(m_placeboOptions->params.downscaler->description);
  vs.m_PlaceboPlaneUpscaler = m_placeboOptions->params.plane_upscaler == NULL ? -1 : CPLHelper::getFilterIndexFromDescription(m_placeboOptions->params.plane_upscaler->description);
  vs.m_PlaceboPlaneDownscaler = m_placeboOptions->params.plane_downscaler == NULL ? -1 : CPLHelper::getFilterIndexFromDescription(m_placeboOptions->params.plane_downscaler->description);
  vs.m_PlaceboFrameMixer = m_placeboOptions->params.frame_mixer == NULL ? -1 : CPLHelper::getFilterIndexFromDescription(m_placeboOptions->params.frame_mixer->description);

  vs.m_PlaceboDebandEnabled = m_placeboOptions->params.deband_params != NULL;
  vs.m_PlaceboDebandGrain = m_placeboOptions->deband_params.grain;
  vs.m_PlaceboDebandGrainNeutral0 = m_placeboOptions->deband_params.grain_neutral[0];
  vs.m_PlaceboDebandGrainNeutral1 = m_placeboOptions->deband_params.grain_neutral[1];
  vs.m_PlaceboDebandGrainNeutral2 = m_placeboOptions->deband_params.grain_neutral[2];
  vs.m_PlaceboDebandIterations = m_placeboOptions->deband_params.iterations;
  vs.m_PlaceboDebandRadius = m_placeboOptions->deband_params.radius;
  vs.m_PlaceboDebandThreshold = m_placeboOptions->deband_params.threshold;

  vs.m_PlaceboColorMapEnabled = m_placeboOptions->params.color_map_params != NULL;
  vs.m_PlaceboColorMapGamutMapping = m_placeboOptions->color_map_params.gamut_mapping == NULL ? -1 : CPLHelper::getGamutMapIndexFromDescription(m_placeboOptions->color_map_params.gamut_mapping->description);
  vs.m_PlaceboColorMapToneMapping = m_placeboOptions->color_map_params.tone_mapping_function == NULL ? -1 : CPLHelper::getToneMapIndexFromDescription(m_placeboOptions->color_map_params.tone_mapping_function->description);
  vs.m_PlaceboColorMapContrastRecovery = m_placeboOptions->color_map_params.contrast_recovery;
  vs.m_PlaceboColorMapContrastSmoothness = m_placeboOptions->color_map_params.contrast_smoothness;
  vs.m_PlaceboColorMapGamutExpansion = m_placeboOptions->color_map_params.gamut_expansion;
  vs.m_PlaceboColorMapInverseToneMapping = m_placeboOptions->color_map_params.inverse_tone_mapping;
  vs.m_PlaceboColorMapLut3dSizeI = m_placeboOptions->color_map_params.lut3d_size[0];
  vs.m_PlaceboColorMapLut3dSizeC = m_placeboOptions->color_map_params.lut3d_size[1];
  vs.m_PlaceboColorMapLut3dSizeH = m_placeboOptions->color_map_params.lut3d_size[2];
  vs.m_PlaceboColorMapLut3dTricubic = m_placeboOptions->color_map_params.lut3d_tricubic;
  vs.m_PlaceboColorMapLutSize = m_placeboOptions->color_map_params.lut_size;
  vs.m_PlaceboColorMapShowClipping = m_placeboOptions->color_map_params.show_clipping;
  vs.m_PlaceboColorMapIntent = m_placeboOptions->color_map_params.intent;
  vs.m_PlaceboColorMapForceToneMappingLut = m_placeboOptions->color_map_params.force_tone_mapping_lut;

  vs.m_PlaceboDeinterlaceEnabled = m_placeboOptions->params.deinterlace_params != NULL;
  vs.m_PlaceboDeinterlaceAlgo = (int)m_placeboOptions->deinterlace_params.algo;
  vs.m_PlaceboDeinterlaceSkipSpatialCheck = m_placeboOptions->deinterlace_params.skip_spatial_check;

  vs.m_PlaceboSigmoidEnabled = m_placeboOptions->params.sigmoid_params != NULL;
  vs.m_PlaceboSigmoidCenter = m_placeboOptions->sigmoid_params.center;
  vs.m_PlaceboSigmoidSlope = m_placeboOptions->sigmoid_params.slope;

  vs.m_PlaceboConeEnabled = m_placeboOptions->params.cone_params != NULL;
  vs.m_PlaceboConeCones = (int)m_placeboOptions->cone_params.cones;
  vs.m_PlaceboConeStrength = m_placeboOptions->cone_params.strength;

  vs.m_PlaceboDitherEnabled = m_placeboOptions->params.dither_params != NULL;
  vs.m_PlaceboDitherMethod = (int)m_placeboOptions->dither_params.method;
  vs.m_PlaceboDitherLutSize = m_placeboOptions->dither_params.lut_size;
  vs.m_PlaceboDitherTemporal = m_placeboOptions->dither_params.temporal;
  vs.m_PlaceboDitherTransfer = (int)m_placeboOptions->dither_params.transfer;

  vs.m_PlaceboToneConstantExposure = m_placeboOptions->color_map_params.tone_constants.exposure;
  vs.m_PlaceboToneConstantKneeAdaptation = m_placeboOptions->color_map_params.tone_constants.knee_adaptation;
  vs.m_PlaceboToneConstantKneeDefault = m_placeboOptions->color_map_params.tone_constants.knee_default;
  vs.m_PlaceboToneConstantKneeMaximum = m_placeboOptions->color_map_params.tone_constants.knee_maximum;
  vs.m_PlaceboToneConstantKneeMinimum = m_placeboOptions->color_map_params.tone_constants.knee_minimum;
  vs.m_PlaceboToneConstantKneeOffset = m_placeboOptions->color_map_params.tone_constants.knee_offset;
  vs.m_PlaceboToneConstantLinearKnee = m_placeboOptions->color_map_params.tone_constants.linear_knee;
  vs.m_PlaceboToneConstantReinhardContrast = m_placeboOptions->color_map_params.tone_constants.reinhard_contrast;
  vs.m_PlaceboToneConstantSlopeOffset = m_placeboOptions->color_map_params.tone_constants.slope_offset;
  vs.m_PlaceboToneConstantSlopeTuning = m_placeboOptions->color_map_params.tone_constants.slope_tuning;
  vs.m_PlaceboToneConstantSplineContrast = m_placeboOptions->color_map_params.tone_constants.spline_contrast;

  vs.m_PlaceboColorMapVisualizeLut = m_placeboOptions->color_map_params.visualize_lut;
  vs.m_PlaceboColorMapVisualizeRectX0 = m_placeboOptions->color_map_params.visualize_rect.x0;
  vs.m_PlaceboColorMapVisualizeRectX1 = m_placeboOptions->color_map_params.visualize_rect.x1;
  vs.m_PlaceboColorMapVisualizeRectY0 = m_placeboOptions->color_map_params.visualize_rect.y0;
  vs.m_PlaceboColorMapVisualizeRectY1 = m_placeboOptions->color_map_params.visualize_rect.y1;
  vs.m_PlaceboColorMapVisualizeHue = fmod(m_placeboOptions->color_map_params.visualize_hue * 180.0 / M_PI, 360.0);
  vs.m_PlaceboColorMapVisualizeTheta = fmod(m_placeboOptions->color_map_params.visualize_theta * 180.0 / M_PI, 360.0);

  vs.m_PlaceboGamutConstantsColorimetricGamma = m_placeboOptions->color_map_params.gamut_constants.colorimetric_gamma;
  vs.m_PlaceboGamutConstantsPerceptualDeadzone = m_placeboOptions->color_map_params.gamut_constants.perceptual_deadzone;
  vs.m_PlaceboGamutConstantsPerceptualStrength = m_placeboOptions->color_map_params.gamut_constants.perceptual_strength;
  vs.m_PlaceboGamutConstantsSoftclipDesat = m_placeboOptions->color_map_params.gamut_constants.softclip_desat;
  vs.m_PlaceboGamutConstantsSoftclipKnee = m_placeboOptions->color_map_params.gamut_constants.softclip_knee;

  vs.m_PlaceboLutType = m_placeboOptions->params.lut_type; //check logic with params.lut in case of reset
  vs.m_PlaceboAntiringingStrength = m_placeboOptions->params.antiringing_strength;
  vs.m_PlaceboCorrectSubpixelOffset = m_placeboOptions->params.correct_subpixel_offsets;
  vs.m_PlaceboDisableBuiltinScalers = m_placeboOptions->params.disable_builtin_scalers;
  vs.m_PlaceboDisableDitherGammaCorrection = m_placeboOptions->params.disable_dither_gamma_correction;
  vs.m_PlaceboDisableLinearScaling = m_placeboOptions->params.disable_linear_scaling;
  vs.m_PlaceboDynamicConstant = m_placeboOptions->params.dynamic_constants;

  vs.m_PlaceboErrorDiffusion = m_placeboOptions->params.error_diffusion == NULL ? -1 : CPLHelper::getErrorDiffusionIndexFromDescription(m_placeboOptions->params.error_diffusion->description);
  vs.m_PlaceboForceDither = m_placeboOptions->params.force_dither;
  vs.m_PlaceboForceLowBitDepthFbos = m_placeboOptions->params.force_low_bit_depth_fbos;
  vs.m_PlaceboIgnoreIccProfiles = m_placeboOptions->params.ignore_icc_profiles;
  vs.m_PlaceboPreserveMixingCache = m_placeboOptions->params.preserve_mixing_cache;
  vs.m_PlaceboSkipAntiAliasing = m_placeboOptions->params.skip_anti_aliasing;
  vs.m_PlaceboSkipCachingSingleFrame = m_placeboOptions->params.skip_caching_single_frame;
  vs.m_PlaceboSkipTargetClearing = m_placeboOptions->params.skip_target_clearing;

  // Update overriden default values for placebo specific settings that are not directly stored in m_placeboOptions, but only in CVideoSettings
  /* //cl no! we assume the param settings are always meant for HDR/SDR and overriden in the renderer function if needed, never need to update them
  vs.m_PlaceboSdrSaturation = vs.m_PlaceboSaturation;
  vs.m_PlaceboSdrColorMapGamutMapping = vs.m_PlaceboColorMapGamutMapping;
  vs.m_PlaceboBrightnessHdr
  ...
  */

}

void CPLHelper::UpdateLibPLaceboParamsFromVideoSettings(CVideoSettings& vs)
{
  pl_options m_placeboOptions = vs.m_placeboOptions->getPlOptions();

  m_placeboOptions->params.lut = vs.m_PlaceboLutType == -1 ? NULL : vs.m_PlaceboLut.get();

  m_placeboOptions->params.color_adjustment = vs.m_PlaceboColorAdjustmentEnabled ? &m_placeboOptions->color_adjustment : NULL;
  //m_placeboOptions->color_adjustment.brightness = vs.m_Brightness / 50.0 - 1.0;
  //m_placeboOptions->color_adjustment.contrast = (pow(10.0, (vs.m_Contrast - 50.0) / 25.0) - 0.01) * 100.0 / 99.0;
  m_placeboOptions->color_adjustment.gamma = (pow(10.0, (vs.m_Gamma - 20.0) / 40.0) - pow(10, -0.5)) * 1.0 / (1.0 - pow(10, -0.5));
  m_placeboOptions->color_adjustment.saturation = pow(10.0, (vs.m_PlaceboSaturation - 50.0) / 40.0);
  m_placeboOptions->color_adjustment.hue = fmod(vs.m_PlaceboHue, 360.0) * M_PI / 180.0;
  m_placeboOptions->color_adjustment.temperature = (vs.m_PlaceboTemperature - 6500.0) / 3500.0;

  m_placeboOptions->params.peak_detect_params = vs.m_PlaceboPeakDetectEnabled ? &m_placeboOptions->peak_detect_params : NULL;
  m_placeboOptions->peak_detect_params.smoothing_period = vs.m_PlaceboPeakDetectSmoothingPeriod;
  m_placeboOptions->peak_detect_params.scene_threshold_low = vs.m_PlaceboPeakDetectSceneThresholdLow;
  m_placeboOptions->peak_detect_params.scene_threshold_high = vs.m_PlaceboPeakDetectSceneThresholdHigh;
  m_placeboOptions->peak_detect_params.percentile = vs.m_PlaceboPeakDetectPercentile;
  m_placeboOptions->peak_detect_params.black_cutoff = vs.m_PlaceboPeakDetectBlackCutoff;
  m_placeboOptions->peak_detect_params.allow_delayed = vs.m_PlaceboPeakDetectAllowDelayed;

  m_placeboOptions->params.upscaler = vs.m_PlaceboUpscaler == -1 ? NULL : pl_filter_configs[vs.m_PlaceboUpscaler];
  m_placeboOptions->params.downscaler = vs.m_PlaceboDownscaler == -1 ? NULL : pl_filter_configs[vs.m_PlaceboDownscaler];
  m_placeboOptions->params.plane_upscaler = vs.m_PlaceboPlaneUpscaler == -1 ? NULL : pl_filter_configs[vs.m_PlaceboPlaneUpscaler];
  m_placeboOptions->params.plane_downscaler = vs.m_PlaceboPlaneDownscaler == -1 ? NULL : pl_filter_configs[vs.m_PlaceboPlaneDownscaler];
  m_placeboOptions->params.frame_mixer = vs.m_PlaceboFrameMixer == -1 ? NULL : pl_filter_configs[vs.m_PlaceboFrameMixer];

  m_placeboOptions->params.deband_params = vs.m_PlaceboDebandEnabled ? &m_placeboOptions->deband_params : NULL;
  m_placeboOptions->deband_params.grain = vs.m_PlaceboDebandGrain;
  m_placeboOptions->deband_params.grain_neutral[0] = vs.m_PlaceboDebandGrainNeutral0;
  m_placeboOptions->deband_params.grain_neutral[1] = vs.m_PlaceboDebandGrainNeutral1;
  m_placeboOptions->deband_params.grain_neutral[2] = vs.m_PlaceboDebandGrainNeutral2;
  m_placeboOptions->deband_params.iterations = vs.m_PlaceboDebandIterations;
  m_placeboOptions->deband_params.radius = vs.m_PlaceboDebandRadius;
  m_placeboOptions->deband_params.threshold = vs.m_PlaceboDebandThreshold;

  m_placeboOptions->params.color_map_params = vs.m_PlaceboColorMapEnabled ? &m_placeboOptions->color_map_params : NULL;
  m_placeboOptions->color_map_params.contrast_recovery = vs.m_PlaceboColorMapContrastRecovery;
  m_placeboOptions->color_map_params.contrast_smoothness = vs.m_PlaceboColorMapContrastSmoothness;
  m_placeboOptions->color_map_params.gamut_expansion = vs.m_PlaceboColorMapGamutExpansion;
  m_placeboOptions->color_map_params.gamut_mapping = vs.m_PlaceboColorMapGamutMapping == -1 ? NULL : pl_gamut_map_functions[vs.m_PlaceboColorMapGamutMapping];
  m_placeboOptions->color_map_params.tone_mapping_function = vs.m_PlaceboColorMapToneMapping == -1 ? NULL : pl_tone_map_functions[vs.m_PlaceboColorMapToneMapping];
  m_placeboOptions->color_map_params.inverse_tone_mapping = vs.m_PlaceboColorMapInverseToneMapping;
  m_placeboOptions->color_map_params.lut3d_size[0] = vs.m_PlaceboColorMapLut3dSizeI;
  m_placeboOptions->color_map_params.lut3d_size[1] = vs.m_PlaceboColorMapLut3dSizeC;
  m_placeboOptions->color_map_params.lut3d_size[2] = vs.m_PlaceboColorMapLut3dSizeH;
  m_placeboOptions->color_map_params.lut3d_tricubic = vs.m_PlaceboColorMapLut3dTricubic;
  m_placeboOptions->color_map_params.lut_size = vs.m_PlaceboColorMapLutSize;
  m_placeboOptions->color_map_params.show_clipping = vs.m_PlaceboColorMapShowClipping;
  m_placeboOptions->color_map_params.intent = (pl_rendering_intent)vs.m_PlaceboColorMapIntent;
  m_placeboOptions->color_map_params.force_tone_mapping_lut = vs.m_PlaceboColorMapForceToneMappingLut;
  m_placeboOptions->color_map_params.visualize_lut = vs.m_PlaceboColorMapVisualizeLut;
  m_placeboOptions->color_map_params.visualize_rect.x0 = vs.m_PlaceboColorMapVisualizeRectX0;
  m_placeboOptions->color_map_params.visualize_rect.x1 = vs.m_PlaceboColorMapVisualizeRectX1;
  m_placeboOptions->color_map_params.visualize_rect.y0 = vs.m_PlaceboColorMapVisualizeRectY0;
  m_placeboOptions->color_map_params.visualize_rect.y1 = vs.m_PlaceboColorMapVisualizeRectY1;
  m_placeboOptions->color_map_params.visualize_hue = fmod(vs.m_PlaceboColorMapVisualizeHue, 360.0) * M_PI / 180.0;;
  m_placeboOptions->color_map_params.visualize_theta = fmod(vs.m_PlaceboColorMapVisualizeTheta, 360.0) * M_PI / 180.0;

  m_placeboOptions->params.deinterlace_params = vs.m_PlaceboDeinterlaceEnabled ? &m_placeboOptions->deinterlace_params : NULL;
  m_placeboOptions->deinterlace_params.algo = (enum pl_deinterlace_algorithm)vs.m_PlaceboDeinterlaceAlgo;
  m_placeboOptions->deinterlace_params.skip_spatial_check = vs.m_PlaceboDeinterlaceSkipSpatialCheck;
  m_placeboOptions->params.sigmoid_params = vs.m_PlaceboSigmoidEnabled ? &m_placeboOptions->sigmoid_params : NULL;
  m_placeboOptions->sigmoid_params.center = vs.m_PlaceboSigmoidCenter;
  m_placeboOptions->sigmoid_params.slope = vs.m_PlaceboSigmoidSlope;
  m_placeboOptions->params.cone_params = vs.m_PlaceboConeEnabled ? &m_placeboOptions->cone_params : NULL;
  m_placeboOptions->cone_params.cones = (enum pl_cone)vs.m_PlaceboConeCones;
  m_placeboOptions->cone_params.strength = vs.m_PlaceboConeStrength;

  m_placeboOptions->params.dither_params = vs.m_PlaceboDitherEnabled ? &m_placeboOptions->dither_params : NULL;
  m_placeboOptions->dither_params.method = (enum pl_dither_method)vs.m_PlaceboDitherMethod;
  m_placeboOptions->dither_params.lut_size = vs.m_PlaceboDitherLutSize;
  m_placeboOptions->dither_params.temporal = vs.m_PlaceboDitherTemporal;
  m_placeboOptions->dither_params.transfer = (enum pl_color_transfer)vs.m_PlaceboDitherTransfer;

  m_placeboOptions->color_map_params.tone_constants.exposure = vs.m_PlaceboToneConstantExposure;
  m_placeboOptions->color_map_params.tone_constants.knee_adaptation = vs.m_PlaceboToneConstantKneeAdaptation;
  m_placeboOptions->color_map_params.tone_constants.knee_default = vs.m_PlaceboToneConstantKneeDefault;
  m_placeboOptions->color_map_params.tone_constants.knee_maximum = vs.m_PlaceboToneConstantKneeMaximum;
  m_placeboOptions->color_map_params.tone_constants.knee_minimum = vs.m_PlaceboToneConstantKneeMinimum;
  m_placeboOptions->color_map_params.tone_constants.knee_offset = vs.m_PlaceboToneConstantKneeOffset;
  m_placeboOptions->color_map_params.tone_constants.linear_knee = vs.m_PlaceboToneConstantLinearKnee;
  m_placeboOptions->color_map_params.tone_constants.reinhard_contrast = vs.m_PlaceboToneConstantReinhardContrast;
  m_placeboOptions->color_map_params.tone_constants.slope_offset = vs.m_PlaceboToneConstantSlopeOffset;
  m_placeboOptions->color_map_params.tone_constants.slope_tuning = vs.m_PlaceboToneConstantSlopeTuning;
  m_placeboOptions->color_map_params.tone_constants.spline_contrast = vs.m_PlaceboToneConstantSplineContrast;

  m_placeboOptions->color_map_params.gamut_constants.colorimetric_gamma = vs.m_PlaceboGamutConstantsColorimetricGamma;
  m_placeboOptions->color_map_params.gamut_constants.perceptual_deadzone = vs.m_PlaceboGamutConstantsPerceptualDeadzone;
  m_placeboOptions->color_map_params.gamut_constants.perceptual_strength = vs.m_PlaceboGamutConstantsPerceptualStrength;
  m_placeboOptions->color_map_params.gamut_constants.softclip_desat = vs.m_PlaceboGamutConstantsSoftclipDesat;
  m_placeboOptions->color_map_params.gamut_constants.softclip_knee = vs.m_PlaceboGamutConstantsSoftclipKnee;

  m_placeboOptions->params.lut_type = (pl_lut_type)vs.m_PlaceboLutType;
  m_placeboOptions->params.antiringing_strength = vs.m_PlaceboAntiringingStrength;
  m_placeboOptions->params.correct_subpixel_offsets = vs.m_PlaceboCorrectSubpixelOffset;
  m_placeboOptions->params.disable_builtin_scalers = vs.m_PlaceboDisableBuiltinScalers;
  m_placeboOptions->params.disable_dither_gamma_correction = vs.m_PlaceboDisableDitherGammaCorrection;
  m_placeboOptions->params.disable_linear_scaling = vs.m_PlaceboDisableLinearScaling;
  m_placeboOptions->params.dynamic_constants = vs.m_PlaceboDynamicConstant;
  m_placeboOptions->params.error_diffusion = vs.m_PlaceboErrorDiffusion == -1 ? NULL : pl_error_diffusion_kernels[vs.m_PlaceboErrorDiffusion];
  m_placeboOptions->params.force_dither = vs.m_PlaceboForceDither;
  m_placeboOptions->params.force_low_bit_depth_fbos = vs.m_PlaceboForceLowBitDepthFbos;
  m_placeboOptions->params.ignore_icc_profiles = vs.m_PlaceboIgnoreIccProfiles;
  m_placeboOptions->params.preserve_mixing_cache = vs.m_PlaceboPreserveMixingCache;
  m_placeboOptions->params.skip_anti_aliasing = vs.m_PlaceboSkipAntiAliasing;
  m_placeboOptions->params.skip_caching_single_frame = vs.m_PlaceboSkipCachingSingleFrame;
  m_placeboOptions->params.skip_target_clearing = vs.m_PlaceboSkipTargetClearing;
}

void CPLHelper::SaveLibplaceboSettings(const CVideoSettings& vs, const std::string path)
{
  CXBMCTinyXML xmlDoc{};
  TiXmlElement rootElement("libPlaceboSettings");
  rootElement.SetAttribute(SETTING_XML_ROOT_VERSION, "1.0");
  TiXmlNode* lpNode = xmlDoc.InsertEndChild(rootElement);

  if (!lpNode)
  {
	CLog::LogF(LOGERROR, "Failed to create XML node for LibPLacebo settings file \"{}\"", path);
  }
  else
  {
	SaveLibplaceboSettings(vs, lpNode);
	if (!xmlDoc.SaveFile(path))
	  CLog::LogF(LOGERROR, "Failed to save LibPLacebo settings to file \"{}\"", path);
  }
}

void CPLHelper::SaveLibplaceboSettings(const CVideoSettings& vs, TiXmlNode* pNode)
{
  XMLUtils::SetInt(pNode, "placeboskinzoom", vs.m_PlaceboSkinZoom);
  XMLUtils::SetInt(pNode, "placeboskinzoomposition", vs.m_PlaceboSkinZoomPosition);
  XMLUtils::SetString(pNode, "placebolutfilename", vs.m_PlaceboLutFilename);
  XMLUtils::SetFloat(pNode, "placebodisplayhdrpeakluminance", vs.m_PlaceboDisplayHdrPeakLuminance);
  XMLUtils::SetFloat(pNode,"placebodisplaysdrpeakluminance",vs.m_PlaceboDisplaySdrPeakLuminance);
  XMLUtils::SetInt(pNode, "placebotargetcontrast", vs.m_PlaceboTargetContrast);
  XMLUtils::SetInt(pNode, "placebonvrtxpipelineenabled", vs.m_PlaceboNvRtxPipelineEnabled);
  XMLUtils::SetBoolean(pNode, "placebonvsuperresolutionenabled", vs.m_PlaceboNvSuperResolutionEnabled);
  XMLUtils::SetBoolean(pNode, "placebonvrtxhdrenabled", vs.m_PlaceboNvRtxHdrEnabled);
  XMLUtils::SetBoolean(pNode, "placebonvrtxdisablescalers", vs.m_PlaceboNvRtxDisableScalers);
  XMLUtils::SetInt(pNode, "placebosdrtargetcontrast", vs.m_PlaceboSdrTargetContrast);
  XMLUtils::SetInt(pNode, "placebotargetcolorspacehint", vs.m_PlaceboTargetColorspaceHint);
  XMLUtils::SetInt(pNode, "placebotargetcolorspacehintmode", vs.m_PlaceboTargetColorspaceHintMode);
  XMLUtils::SetInt(pNode, "placeboditherdepth", vs.m_PlaceboDitherDepth);
  XMLUtils::SetBoolean(pNode, "placebousehdrforsdr", vs.m_PlaceboUseHdrForSdr);
  XMLUtils::SetBoolean(pNode, "placeboshaderapply", vs.m_PlaceboShaderApply);
  XMLUtils::SetFloat(pNode, "placeboframemixerradiusfactor", vs.m_PlaceboFrameMixerRadiusFactor);
  XMLUtils::SetBoolean(pNode, "placebomixerbypassqueue", vs.m_PlaceboFrameMixerBypassQueue);
  XMLUtils::SetInt(pNode, "placebocropbottom", vs.m_PlaceboCropBottom);
  XMLUtils::SetFloat(pNode, "placebobrightnesssdrsdr", vs.m_PlaceboBrightnessSdrSdr);
  XMLUtils::SetFloat(pNode, "placebocontrastsdrsdr", vs.m_PlaceboContrastSdrSdr);
  XMLUtils::SetFloat(pNode, "placebobrightnesshdrhdr", vs.m_PlaceboBrightnessHdrHdr);
  XMLUtils::SetFloat(pNode, "placebocontrasthdrhdr", vs.m_PlaceboContrastHdrHdr);
  XMLUtils::SetFloat(pNode, "placebobrightnesshdrsdr", vs.m_PlaceboBrightnessHdrSdr);
  XMLUtils::SetFloat(pNode, "placebocontrasthdrsdr", vs.m_PlaceboContrastHdrSdr);
  XMLUtils::SetFloat(pNode, "placebobrightnesssdrhdr", vs.m_PlaceboBrightnessSdrHdr);
  XMLUtils::SetFloat(pNode, "placebocontrastsdrhdr", vs.m_PlaceboContrastSdrHdr);

  XMLUtils::SetFloat(pNode, "placebosdrsaturation", vs.m_PlaceboSdrSaturation);
  XMLUtils::SetBoolean(pNode, "placebosdrcolormapinversetonemapping", vs.m_PlaceboSdrColorMapInverseToneMapping);
  XMLUtils::SetBoolean(pNode, "placebosdrcolormapgamutexpansion", vs.m_PlaceboSdrColorMapGamutExpansion);
  XMLUtils::SetString(pNode, "placebosdrcolormapgamutmapping", vs.m_PlaceboSdrColorMapGamutMapping == -1 ? "disabled" : pl_gamut_map_functions [vs.m_PlaceboSdrColorMapGamutMapping]->description == nullptr ? "" : pl_gamut_map_functions [vs.m_PlaceboSdrColorMapGamutMapping]->description);
  XMLUtils::SetString(pNode, "placebosdrcolormaptonemapping", vs.m_PlaceboSdrColorMapToneMapping == -1 ? "disabled" : pl_tone_map_functions [vs.m_PlaceboSdrColorMapToneMapping]->description == nullptr ? "" : pl_tone_map_functions [vs.m_PlaceboSdrColorMapToneMapping]->description);
  XMLUtils::SetString(pNode, "placebosdrcolormapintent", CPLHelper::getColorMapIntentDescriptionFromIndex(vs.m_PlaceboSdrColorMapIntent));
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantexposure", vs.m_PlaceboSdrToneConstantExposure);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantkneeadaptation", vs.m_PlaceboSdrToneConstantKneeAdaptation);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantkneedefault", vs.m_PlaceboSdrToneConstantKneeDefault);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantkneemaximum", vs.m_PlaceboSdrToneConstantKneeMaximum);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantkneeminimum", vs.m_PlaceboSdrToneConstantKneeMinimum);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantkneeoffset", vs.m_PlaceboSdrToneConstantKneeOffset);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantlinearknee", vs.m_PlaceboSdrToneConstantLinearKnee);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantreinhardcontrast", vs.m_PlaceboSdrToneConstantReinhardContrast);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantslopeoffset", vs.m_PlaceboSdrToneConstantSlopeOffset);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantslopetuning", vs.m_PlaceboSdrToneConstantSlopeTuning);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantsplinecontrast", vs.m_PlaceboSdrToneConstantSplineContrast);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantparam0", vs.m_PlaceboSdrToneConstantParam0);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantparam1", vs.m_PlaceboSdrToneConstantParam1);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantparam2", vs.m_PlaceboSdrToneConstantParam2);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantparam3", vs.m_PlaceboSdrToneConstantParam3);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantparam4", vs.m_PlaceboSdrToneConstantParam4);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantparam5", vs.m_PlaceboSdrToneConstantParam5);
  XMLUtils::SetFloat(pNode, "placebosdrtoneconstantparam6", vs.m_PlaceboSdrToneConstantParam6);
  XMLUtils::SetFloat(pNode, "placebosdrgamutconstantscolorimetricgamma", vs.m_PlaceboSdrGamutConstantsColorimetricGamma);
  XMLUtils::SetFloat(pNode, "placebosdrgamutconstantsperceptualdeadzone", vs.m_PlaceboSdrGamutConstantsPerceptualDeadzone);
  XMLUtils::SetFloat(pNode, "placebosdrgamutconstantsperceptualstrength", vs.m_PlaceboSdrGamutConstantsPerceptualStrength);
  XMLUtils::SetFloat(pNode, "placebosdrgamutconstantssoftclipdesat", vs.m_PlaceboSdrGamutConstantsSoftclipDesat);
  XMLUtils::SetFloat(pNode, "placebosdrgamutconstantssoftclipknee", vs.m_PlaceboSdrGamutConstantsSoftclipKnee);

  XMLUtils::SetBoolean(pNode, "placebocoloradjustmentenabled", vs.m_PlaceboColorAdjustmentEnabled);
  XMLUtils::SetFloat(pNode, "saturation", vs.m_PlaceboSaturation);
  XMLUtils::SetFloat(pNode, "hue", vs.m_PlaceboHue);
  XMLUtils::SetFloat(pNode, "temperature", vs.m_PlaceboTemperature);

  XMLUtils::SetBoolean(pNode, "placebopeakdetectenabled", vs.m_PlaceboPeakDetectEnabled);
  XMLUtils::SetFloat(pNode, "placebopeakdetectsmoothingperiod", vs.m_PlaceboPeakDetectSmoothingPeriod);
  XMLUtils::SetFloat(pNode, "placebopeakdetectscenethresholdlow", vs.m_PlaceboPeakDetectSceneThresholdLow);
  XMLUtils::SetFloat(pNode, "placebopeakdetectscenethresholdhigh", vs.m_PlaceboPeakDetectSceneThresholdHigh);
  XMLUtils::SetFloat(pNode, "placebopeakdetectpercentile", vs.m_PlaceboPeakDetectPercentile);
  XMLUtils::SetFloat(pNode, "placebopeakdetectblackcutoff", vs.m_PlaceboPeakDetectBlackCutoff);
  XMLUtils::SetBoolean(pNode, "placebopeakdetectallowdelayed", vs.m_PlaceboPeakDetectAllowDelayed);

  XMLUtils::SetString(pNode, "placeboupscaler", vs.m_PlaceboUpscaler == -1 ? "disabled" : pl_filter_configs[vs.m_PlaceboUpscaler]->description == nullptr ? "" : pl_filter_configs[vs.m_PlaceboUpscaler]->description);
  XMLUtils::SetString(pNode, "placebodownscaler", vs.m_PlaceboDownscaler == -1 ? "disabled" : pl_filter_configs[vs.m_PlaceboDownscaler]->description == nullptr ? "" : pl_filter_configs[vs.m_PlaceboDownscaler]->description);
  XMLUtils::SetString(pNode, "placeboplaneupscaler", vs.m_PlaceboPlaneUpscaler == -1 ? "disabled" : pl_filter_configs[vs.m_PlaceboPlaneUpscaler]->description == nullptr ? "" : pl_filter_configs[vs.m_PlaceboPlaneUpscaler]->description);
  XMLUtils::SetString(pNode, "placeboplanedownscaler", vs.m_PlaceboPlaneDownscaler == -1 ? "disabled" : pl_filter_configs[vs.m_PlaceboPlaneDownscaler]->description == nullptr ? "" : pl_filter_configs[vs.m_PlaceboPlaneDownscaler]->description);
  XMLUtils::SetString(pNode, "placeboframemixer", vs.m_PlaceboFrameMixer == -1 ? "disabled" : pl_filter_configs[vs.m_PlaceboFrameMixer]->description == nullptr ? "" : pl_filter_configs[vs.m_PlaceboFrameMixer]->description);

  XMLUtils::SetBoolean(pNode, "placebodebandenabled", vs.m_PlaceboDebandEnabled);
  XMLUtils::SetFloat(pNode, "placebodebandgrain", vs.m_PlaceboDebandGrain);
  XMLUtils::SetFloat(pNode, "placebodebandgrainneutral0", vs.m_PlaceboDebandGrainNeutral0);
  XMLUtils::SetFloat(pNode, "placebodebandgrainneutral1", vs.m_PlaceboDebandGrainNeutral1);
  XMLUtils::SetFloat(pNode, "placebodebandgrainneutral2", vs.m_PlaceboDebandGrainNeutral2);
  XMLUtils::SetInt(pNode, "placebodebanditerations", vs.m_PlaceboDebandIterations);
  XMLUtils::SetFloat(pNode, "placebodebandradius", vs.m_PlaceboDebandRadius);
  XMLUtils::SetFloat(pNode, "placebodebandthreshold", vs.m_PlaceboDebandThreshold);

  XMLUtils::SetBoolean(pNode, "placebocolormapenabled", vs.m_PlaceboColorMapEnabled);
  XMLUtils::SetFloat(pNode, "placebocolormapcontrastrecovery", vs.m_PlaceboColorMapContrastRecovery);
  XMLUtils::SetFloat(pNode, "placebocolormapcontrastsmoothness", vs.m_PlaceboColorMapContrastSmoothness);
  XMLUtils::SetBoolean(pNode, "placebocolormapgamutexpansion", vs.m_PlaceboColorMapGamutExpansion);
  XMLUtils::SetString(pNode, "placebocolormapgamutmapping", vs.m_PlaceboColorMapGamutMapping == -1 ? "disabled" : pl_gamut_map_functions[vs.m_PlaceboColorMapGamutMapping]->description == nullptr ? "" : pl_gamut_map_functions[vs.m_PlaceboColorMapGamutMapping]->description);
  XMLUtils::SetString(pNode, "placebocolormaptonemapping", vs.m_PlaceboColorMapToneMapping == -1 ? "disabled" : pl_tone_map_functions[vs.m_PlaceboColorMapToneMapping]->description == nullptr ? "" : pl_tone_map_functions[vs.m_PlaceboColorMapToneMapping]->description);
  XMLUtils::SetBoolean(pNode, "placebocolormapinversetonemapping", vs.m_PlaceboColorMapInverseToneMapping);
  XMLUtils::SetInt(pNode, "placebocolormaplut3dsizei", vs.m_PlaceboColorMapLut3dSizeI);
  XMLUtils::SetInt(pNode, "placebocolormaplut3dsizec", vs.m_PlaceboColorMapLut3dSizeC);
  XMLUtils::SetInt(pNode, "placebocolormaplut3dsizeh", vs.m_PlaceboColorMapLut3dSizeH);
  XMLUtils::SetBoolean(pNode, "placebocolormaplut3dtricubic", vs.m_PlaceboColorMapLut3dTricubic);
  XMLUtils::SetInt(pNode, "placebocolormaplutsize", vs.m_PlaceboColorMapLutSize);
  XMLUtils::SetBoolean(pNode, "placebocolormapshowclipping", vs.m_PlaceboColorMapShowClipping);
  XMLUtils::SetString(pNode, "placebocolormapintent", CPLHelper::getColorMapIntentDescriptionFromIndex(vs.m_PlaceboColorMapIntent));
  XMLUtils::SetBoolean(pNode, "placebocolormapforcetonemappinglut", vs.m_PlaceboColorMapForceToneMappingLut);
  XMLUtils::SetBoolean(pNode, "placebocolormapvisualizelut", vs.m_PlaceboColorMapVisualizeLut);
  XMLUtils::SetFloat(pNode, "placebocolormapvisualizerectx0", vs.m_PlaceboColorMapVisualizeRectX0);
  XMLUtils::SetFloat(pNode, "placebocolormapvisualizerectx1", vs.m_PlaceboColorMapVisualizeRectX1);
  XMLUtils::SetFloat(pNode, "placebocolormapvisualizerecty0", vs.m_PlaceboColorMapVisualizeRectY0);
  XMLUtils::SetFloat(pNode, "placebocolormapvisualizerecty1", vs.m_PlaceboColorMapVisualizeRectY1);
  XMLUtils::SetFloat(pNode, "placebocolormapvisualizehue", vs.m_PlaceboColorMapVisualizeHue);
  XMLUtils::SetFloat(pNode, "placebocolormapvisualizetheta", vs.m_PlaceboColorMapVisualizeTheta);

  XMLUtils::SetBoolean(pNode, "placebodeinterlaceenabled", vs.m_PlaceboDeinterlaceEnabled);
  XMLUtils::SetString(pNode, "placebodeinterlacealgo", CPLHelper::getDeinterlaceAlgoDescriptionFromIndex(vs.m_PlaceboDeinterlaceAlgo));
  XMLUtils::SetBoolean(pNode, "placebodeinterlaceskipspatialcheck", vs.m_PlaceboDeinterlaceSkipSpatialCheck);
  XMLUtils::SetBoolean(pNode, "placeboconeenabled", vs.m_PlaceboConeEnabled);
  XMLUtils::SetString(pNode, "placeboconecones", CPLHelper::getConeConesDescriptionFromIndex(vs.m_PlaceboConeCones));
  XMLUtils::SetFloat(pNode, "placeboconestrength", vs.m_PlaceboConeStrength);
  XMLUtils::SetBoolean(pNode, "placebosigmoidenabled", vs.m_PlaceboSigmoidEnabled);
  XMLUtils::SetFloat(pNode, "placebosigmoidcenter", vs.m_PlaceboSigmoidCenter);
  XMLUtils::SetFloat(pNode, "placebosigmoidslope", vs.m_PlaceboSigmoidSlope);

  XMLUtils::SetBoolean(pNode, "placeboditherenabled", vs.m_PlaceboDitherEnabled);
  XMLUtils::SetString(pNode, "placebodithermethod", CPLHelper::getDitherMethodDescriptionFromIndex(vs.m_PlaceboDitherMethod));
  XMLUtils::SetInt(pNode, "placeboditherlutsize", vs.m_PlaceboDitherLutSize);
  XMLUtils::SetBoolean(pNode, "placebodithertemporal", vs.m_PlaceboDitherTemporal);
  XMLUtils::SetString(pNode, "placebodithertransfer", CPLHelper::getDitherTransferDescriptionFromIndex(vs.m_PlaceboDitherTransfer));

  XMLUtils::SetFloat(pNode, "placebotoneconstantexposure", vs.m_PlaceboToneConstantExposure);
  XMLUtils::SetFloat(pNode, "placebotoneconstantkneeadaptation", vs.m_PlaceboToneConstantKneeAdaptation);
  XMLUtils::SetFloat(pNode, "placebotoneconstantkneedefault", vs.m_PlaceboToneConstantKneeDefault);
  XMLUtils::SetFloat(pNode, "placebotoneconstantkneemaximum", vs.m_PlaceboToneConstantKneeMaximum);
  XMLUtils::SetFloat(pNode, "placebotoneconstantkneeminimum", vs.m_PlaceboToneConstantKneeMinimum);
  XMLUtils::SetFloat(pNode, "placebotoneconstantkneeoffset", vs.m_PlaceboToneConstantKneeOffset);
  XMLUtils::SetFloat(pNode, "placebotoneconstantlinearknee", vs.m_PlaceboToneConstantLinearKnee);
  XMLUtils::SetFloat(pNode, "placebotoneconstantreinhardcontrast", vs.m_PlaceboToneConstantReinhardContrast);
  XMLUtils::SetFloat(pNode, "placebotoneconstantslopeoffset", vs.m_PlaceboToneConstantSlopeOffset);
  XMLUtils::SetFloat(pNode, "placebotoneconstantslopetuning", vs.m_PlaceboToneConstantSlopeTuning);
  XMLUtils::SetFloat(pNode, "placebotoneconstantsplinecontrast", vs.m_PlaceboToneConstantSplineContrast);

  XMLUtils::SetFloat(pNode, "placebogamutconstantscolorimetricgamma", vs.m_PlaceboGamutConstantsColorimetricGamma);
  XMLUtils::SetFloat(pNode, "placebogamutconstantsperceptualdeadzone", vs.m_PlaceboGamutConstantsPerceptualDeadzone);
  XMLUtils::SetFloat(pNode,"placebogamutconstantsperceptualstrength",vs.m_PlaceboGamutConstantsPerceptualStrength);
  XMLUtils::SetFloat(pNode, "placebogamutconstantssoftclipdesat", vs.m_PlaceboGamutConstantsSoftclipDesat);
  XMLUtils::SetFloat(pNode, "placebogamutconstantssoftclipknee", vs.m_PlaceboGamutConstantsSoftclipKnee);

  XMLUtils::SetString(pNode, "placeboluttype", CPLHelper::getLutTypeDescriptionFromIndex(vs.m_PlaceboLutType));
  XMLUtils::SetFloat(pNode, "placeboantiringingstrength", vs.m_PlaceboAntiringingStrength);
  XMLUtils::SetBoolean(pNode, "placebocorrectsubpixeloffset", vs.m_PlaceboCorrectSubpixelOffset);
  XMLUtils::SetBoolean(pNode, "placebodisablebuiltinscalers", vs.m_PlaceboDisableBuiltinScalers);
  XMLUtils::SetBoolean(pNode, "placebodisabledithergammacorrection", vs.m_PlaceboDisableDitherGammaCorrection);
  XMLUtils::SetBoolean(pNode, "placebodisabledithergammacorrection", vs.m_PlaceboDisableDitherGammaCorrection);
  XMLUtils::SetBoolean(pNode, "placebodisabledithergammacorrection", vs.m_PlaceboDisableDitherGammaCorrection);
  XMLUtils::SetString(pNode, "placeboerrordiffusion", CPLHelper::getDiffusionKernelDescriptionFromIndex(vs.m_PlaceboErrorDiffusion));
  XMLUtils::SetBoolean(pNode, "placeboforcedither", vs.m_PlaceboForceDither);
  XMLUtils::SetBoolean(pNode, "placeboforcelowbitdepthfbos", vs.m_PlaceboForceLowBitDepthFbos);
  XMLUtils::SetBoolean(pNode, "placeboignoreiccprofiles", vs.m_PlaceboIgnoreIccProfiles);
  XMLUtils::SetBoolean(pNode, "placebopreservemixingcache", vs.m_PlaceboPreserveMixingCache);
  XMLUtils::SetBoolean(pNode, "placeboskipantialiasing", vs.m_PlaceboSkipAntiAliasing);
  XMLUtils::SetBoolean(pNode, "placeboskipcachingsingleframe", vs.m_PlaceboSkipCachingSingleFrame);
  XMLUtils::SetBoolean(pNode, "placeboskiptargetclearing", vs.m_PlaceboSkipTargetClearing);
  SerializeShaders(vs, pNode);
}

bool CPLHelper::LoadLibplaceboSettings(CVideoSettings& vs, std::string path)
{
  CXBMCTinyXML xmlDoc;
  std::string value;

  if (!xmlDoc.LoadFile(path))
  {
	CLog::LogF(LOGERROR, "CPLHelper: Error loading LipPlacebo settings {}, Line {}\n{}", path, xmlDoc.ErrorRow(), xmlDoc.ErrorDesc());
	return false;
  }
  CLog::LogF(LOGDEBUG, "CPLHelper: loading LipPlacebo settings from {}", path);
  const TiXmlElement* pElement = xmlDoc.FirstChildElement("libPlaceboSettings");
  if (!pElement)
  {
	CLog::LogF(LOGERROR, "CPLHelper: Error loading LipPlacebo settings, missing <libPlaceboSettings> element");
	return false;
  }

  CPLHelper::ResetShaders(vs);
  LoadLibplaceboSettings(vs, pElement);
  CPLHelper::InitializeShaders(PL::PLInstance::Get()->GetGpu(), vs);
  SkinZoomUpdate();
  UpdateLibPLaceboParamsFromVideoSettings(vs);
  SetVideoSettings(vs);
  return true;
}

bool CPLHelper::LoadLibplaceboSettings(CVideoSettings& vs, const TiXmlElement* pElement)
{
  std::string value;
  if (!pElement)
	return false;

  //std::unique_lock lock(m_critical);
  int temp;
  XMLUtils::GetInt(pElement, "placeboskinzoom", vs.m_PlaceboSkinZoom);
  XMLUtils::GetInt(pElement, "placeboskinzoomposition", temp); vs.m_PlaceboSkinZoomPosition = (VS_PLACEBO_SZ_POSITION) temp;
  XMLUtils::GetString(pElement, "placebolutfilename", vs.m_PlaceboLutFilename);  LoadLutFile(vs, vs.m_PlaceboLutFilename);
  XMLUtils::GetFloat(pElement, "placebodisplayhdrpeakluminance", vs.m_PlaceboDisplayHdrPeakLuminance);
  XMLUtils::GetInt(pElement, "placebotargetcontrast", vs.m_PlaceboTargetContrast);
  XMLUtils::GetInt(pElement, "placebonvrtxpipelineenabled", vs.m_PlaceboNvRtxPipelineEnabled);
  XMLUtils::GetBoolean(pElement, "placebonvsuperresolutionenabled", vs.m_PlaceboNvSuperResolutionEnabled);
  XMLUtils::GetBoolean(pElement, "placebonvrtxhdrenabled", vs.m_PlaceboNvRtxHdrEnabled);
  XMLUtils::GetBoolean(pElement, "placebonvrtxdisablescalers", vs.m_PlaceboNvRtxDisableScalers);
  XMLUtils::GetInt(pElement,"placebosdrtargetcontrast",vs.m_PlaceboSdrTargetContrast);
  XMLUtils::GetFloat(pElement,"placebodisplaysdrpeakluminance",vs.m_PlaceboDisplaySdrPeakLuminance);
  XMLUtils::GetInt(pElement, "placebotargetcolorspacehint", vs.m_PlaceboTargetColorspaceHint);
  XMLUtils::GetInt(pElement, "placebotargetcolorspacehintmode", vs.m_PlaceboTargetColorspaceHintMode);
  XMLUtils::GetInt(pElement, "placeboditherdepth", vs.m_PlaceboDitherDepth);
  XMLUtils::GetBoolean(pElement, "placebousehdrforsdr", vs.m_PlaceboUseHdrForSdr);
  XMLUtils::GetBoolean(pElement, "placeboshaderapply", vs.m_PlaceboShaderApply);
  XMLUtils::GetFloat(pElement, "placeboframemixerradiusfactor", vs.m_PlaceboFrameMixerRadiusFactor);
  XMLUtils::GetBoolean(pElement, "placebomixerbypassqueue", vs.m_PlaceboFrameMixerBypassQueue);
  XMLUtils::GetInt(pElement, "placebocropbottom", vs.m_PlaceboCropBottom);
  XMLUtils::GetFloat(pElement, "placebobrightnesssdrsdr", vs.m_PlaceboBrightnessSdrSdr);
  XMLUtils::GetFloat(pElement, "placebocontrastsdrsdr", vs.m_PlaceboContrastSdrSdr);
  XMLUtils::GetFloat(pElement, "placebobrightnesshdrhdr", vs.m_PlaceboBrightnessHdrHdr);
  XMLUtils::GetFloat(pElement, "placebocontrasthdrhdr", vs.m_PlaceboContrastHdrHdr);
  XMLUtils::GetFloat(pElement, "placebobrightnesssdrhdr", vs.m_PlaceboBrightnessSdrHdr);
  XMLUtils::GetFloat(pElement, "placebocontrastsdrhdr", vs.m_PlaceboContrastSdrHdr);
  XMLUtils::GetFloat(pElement, "placebobrightnesshdrsdr", vs.m_PlaceboBrightnessHdrSdr);
  XMLUtils::GetFloat(pElement, "placebocontrasthdrsdr", vs.m_PlaceboContrastHdrSdr);

  XMLUtils::GetFloat(pElement, "placebosdrsaturation", vs.m_PlaceboSdrSaturation);
  XMLUtils::GetBoolean(pElement, "placebosdrcolormapinversetonemapping", vs.m_PlaceboSdrColorMapInverseToneMapping);
  XMLUtils::GetBoolean(pElement, "placebosdrcolormapgamutexpansion", vs.m_PlaceboSdrColorMapGamutExpansion);
  XMLUtils::GetString(pElement, "placebosdrcolormapgamutmapping", value); vs.m_PlaceboColorMapGamutMapping = value == "" ? vs.m_PlaceboColorMapGamutMapping : CPLHelper::getGamutMapIndexFromDescription(value);
  XMLUtils::GetString(pElement, "placebosdrcolormaptoneMapping", value); vs.m_PlaceboColorMapToneMapping = value == "" ? vs.m_PlaceboColorMapToneMapping : CPLHelper::getToneMapIndexFromDescription(value);
  XMLUtils::GetString(pElement, "placebosdrcolormapintent", value); vs.m_PlaceboColorMapIntent = value == "" ? vs.m_PlaceboColorMapIntent : CPLHelper::getColorMapIntentIndexFromDescription(value);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantexposure", vs.m_PlaceboSdrToneConstantExposure);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantkneeadaptation", vs.m_PlaceboSdrToneConstantKneeAdaptation);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantkneedefault", vs.m_PlaceboSdrToneConstantKneeDefault);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantkneemaximum", vs.m_PlaceboSdrToneConstantKneeMaximum);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantkneeminimum", vs.m_PlaceboSdrToneConstantKneeMinimum);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantkneeoffset", vs.m_PlaceboSdrToneConstantKneeOffset);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantlinearknee", vs.m_PlaceboSdrToneConstantLinearKnee);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantreinhardcontrast", vs.m_PlaceboSdrToneConstantReinhardContrast);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantslopeoffset", vs.m_PlaceboSdrToneConstantSlopeOffset);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantslopetuning", vs.m_PlaceboSdrToneConstantSlopeTuning);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantsplinecontrast", vs.m_PlaceboSdrToneConstantSplineContrast);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantparam0", vs.m_PlaceboSdrToneConstantParam0);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantparam1", vs.m_PlaceboSdrToneConstantParam1);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantparam2", vs.m_PlaceboSdrToneConstantParam2);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantparam3", vs.m_PlaceboSdrToneConstantParam3);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantparam4", vs.m_PlaceboSdrToneConstantParam4);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantparam5", vs.m_PlaceboSdrToneConstantParam5);
  XMLUtils::GetFloat(pElement, "placebosdrtoneconstantparam6", vs.m_PlaceboSdrToneConstantParam6);
  XMLUtils::GetFloat(pElement, "placebosdrgamutconstantscolorimetricgamma", vs.m_PlaceboSdrGamutConstantsColorimetricGamma);
  XMLUtils::GetFloat(pElement, "placebosdrgamutconstantsperceptualdeadzone", vs.m_PlaceboSdrGamutConstantsPerceptualDeadzone);
  XMLUtils::GetFloat(pElement, "placebosdrgamutconstantsperceptualstrength", vs.m_PlaceboSdrGamutConstantsPerceptualStrength);
  XMLUtils::GetFloat(pElement, "placebosdrgamutconstantssoftclipdesat", vs.m_PlaceboSdrGamutConstantsSoftclipDesat);
  XMLUtils::GetFloat(pElement, "placebosdrgamutconstantssoftclipknee", vs.m_PlaceboSdrGamutConstantsSoftclipKnee);

  XMLUtils::GetBoolean(pElement, "placebocoloradjustmentenabled", vs.m_PlaceboColorAdjustmentEnabled);
  XMLUtils::GetFloat(pElement, "saturation", vs.m_PlaceboSaturation);
  XMLUtils::GetFloat(pElement, "hue", vs.m_PlaceboHue);
  XMLUtils::GetFloat(pElement, "temperature", vs.m_PlaceboTemperature);

  XMLUtils::GetBoolean(pElement, "placebopeakdetectenabled", vs.m_PlaceboPeakDetectEnabled);
  XMLUtils::GetFloat(pElement, "placebopeakdetectsmoothingperiod", vs.m_PlaceboPeakDetectSmoothingPeriod);
  XMLUtils::GetFloat(pElement, "placebopeakdetectscenethresholdlow", vs.m_PlaceboPeakDetectSceneThresholdLow);
  XMLUtils::GetFloat(pElement, "placebopeakdetectscenethresholdhigh", vs.m_PlaceboPeakDetectSceneThresholdHigh);
  XMLUtils::GetFloat(pElement, "placebopeakdetectpercentile", vs.m_PlaceboPeakDetectPercentile);
  XMLUtils::GetFloat(pElement, "placebopeakdetectblackcutoff", vs.m_PlaceboPeakDetectBlackCutoff);
  XMLUtils::GetBoolean(pElement, "placebopeakdetectallowdelayed", vs.m_PlaceboPeakDetectAllowDelayed);

  XMLUtils::GetString(pElement, "placeboupscaler", value); vs.m_PlaceboUpscaler = value == "" ? vs.m_PlaceboUpscaler : CPLHelper::getFilterIndexFromDescription(value);
  XMLUtils::GetString(pElement, "placebodownscaler", value); vs.m_PlaceboDownscaler = value == "" ? vs.m_PlaceboDownscaler : CPLHelper::getFilterIndexFromDescription(value);
  XMLUtils::GetString(pElement, "placeboplaneupscaler", value); vs.m_PlaceboPlaneUpscaler = value == "" ? vs.m_PlaceboPlaneUpscaler : CPLHelper::getFilterIndexFromDescription(value);
  XMLUtils::GetString(pElement, "placeboplanedownscaler", value); vs.m_PlaceboPlaneDownscaler = value == "" ? vs.m_PlaceboPlaneDownscaler : CPLHelper::getFilterIndexFromDescription(value);
  XMLUtils::GetString(pElement, "placeboframemixer", value); vs.m_PlaceboFrameMixer = value == "" ? vs.m_PlaceboFrameMixer : CPLHelper::getFilterIndexFromDescription(value);

  XMLUtils::GetBoolean(pElement, "placebodebandenabled", vs.m_PlaceboDebandEnabled);
  XMLUtils::GetFloat(pElement, "placebodebandgrain", vs.m_PlaceboDebandGrain);
  XMLUtils::GetFloat(pElement, "placebodebandgrainneutral0", vs.m_PlaceboDebandGrainNeutral0);
  XMLUtils::GetFloat(pElement, "placebodebandgrainneutral1", vs.m_PlaceboDebandGrainNeutral1);
  XMLUtils::GetFloat(pElement, "placebodebandgrainneutral2", vs.m_PlaceboDebandGrainNeutral2);
  XMLUtils::GetInt(pElement, "placebodebanditerations", vs.m_PlaceboDebandIterations);
  XMLUtils::GetFloat(pElement, "placebodebandradius", vs.m_PlaceboDebandRadius);
  XMLUtils::GetFloat(pElement, "placebodebandthreshold", vs.m_PlaceboDebandThreshold);

  XMLUtils::GetBoolean(pElement, "placebocolormapenabled", vs.m_PlaceboColorMapEnabled);
  XMLUtils::GetFloat(pElement, "placebocolormapcontrastrecovery", vs.m_PlaceboColorMapContrastRecovery);
  XMLUtils::GetFloat(pElement, "placebocolormapcontrastsmoothness", vs.m_PlaceboColorMapContrastSmoothness);
  XMLUtils::GetBoolean(pElement, "placebocolormapgamutexpansion", vs.m_PlaceboColorMapGamutExpansion);
  XMLUtils::GetString(pElement, "placebocolormapgamutmapping", value); vs.m_PlaceboColorMapGamutMapping = value == "" ? vs.m_PlaceboColorMapGamutMapping : CPLHelper::getGamutMapIndexFromDescription(value);
  XMLUtils::GetString(pElement, "placebocolormaptonemapping", value); vs.m_PlaceboColorMapToneMapping = value == "" ? vs.m_PlaceboColorMapToneMapping : CPLHelper::getToneMapIndexFromDescription(value);
  XMLUtils::GetBoolean(pElement, "placebocolormapinversetonemapping", vs.m_PlaceboColorMapInverseToneMapping);
  XMLUtils::GetInt(pElement, "placebocolormaplut3dsizei", vs.m_PlaceboColorMapLut3dSizeI);
  XMLUtils::GetInt(pElement, "placebocolormaplut3dsizec", vs.m_PlaceboColorMapLut3dSizeC);
  XMLUtils::GetInt(pElement, "placebocolormaplut3dsizeh", vs.m_PlaceboColorMapLut3dSizeH);
  XMLUtils::GetBoolean(pElement, "placebocolormaplut3dtricubic", vs.m_PlaceboColorMapLut3dTricubic);
  XMLUtils::GetInt(pElement, "placebocolormaplutsize", vs.m_PlaceboColorMapLutSize);
  XMLUtils::GetBoolean(pElement, "placebocolormapshowclipping", vs.m_PlaceboColorMapShowClipping);
  XMLUtils::GetString(pElement, "placebocolormapintent", value); vs.m_PlaceboColorMapIntent = value == "" ? vs.m_PlaceboColorMapIntent : CPLHelper::getColorMapIntentIndexFromDescription(value);
  XMLUtils::GetBoolean(pElement, "placebocolormapforcetonemappinglut", vs.m_PlaceboColorMapForceToneMappingLut);
  XMLUtils::GetBoolean(pElement, "placebocolormapvisualizelut", vs.m_PlaceboColorMapVisualizeLut);
  XMLUtils::GetFloat(pElement, "placebocolormapvisualizerectx0", vs.m_PlaceboColorMapVisualizeRectX0);
  XMLUtils::GetFloat(pElement, "placebocolormapvisualizerectx1", vs.m_PlaceboColorMapVisualizeRectX1);
  XMLUtils::GetFloat(pElement, "placebocolormapvisualizerecty0", vs.m_PlaceboColorMapVisualizeRectY0);
  XMLUtils::GetFloat(pElement, "placebocolormapvisualizerecty1", vs.m_PlaceboColorMapVisualizeRectY1);
  XMLUtils::GetFloat(pElement, "placebocolormapvisualizehue", vs.m_PlaceboColorMapVisualizeHue);
  XMLUtils::GetFloat(pElement, "placebocolormapvisualizetheta", vs.m_PlaceboColorMapVisualizeTheta);

  XMLUtils::GetBoolean(pElement, "placebodeinterlaceenabled", vs.m_PlaceboDeinterlaceEnabled);
  XMLUtils::GetString(pElement, "placebodeinterlacealgo", value); vs.m_PlaceboDeinterlaceAlgo = value == "" ? vs.m_PlaceboDeinterlaceAlgo : CPLHelper::getDeinterlaceAlgoIndexFromDescription(value);
  XMLUtils::GetBoolean(pElement, "placebodeinterlaceskipspatialcheck", vs.m_PlaceboDeinterlaceSkipSpatialCheck);
  XMLUtils::GetBoolean(pElement, "placebosigmoidenabled", vs.m_PlaceboSigmoidEnabled);
  XMLUtils::GetFloat(pElement, "placebosigmoidcenter", vs.m_PlaceboSigmoidCenter);
  XMLUtils::GetFloat(pElement, "placebosigmoidslope", vs.m_PlaceboSigmoidSlope);
  XMLUtils::GetBoolean(pElement, "placeboconeenabled", vs.m_PlaceboConeEnabled);
  XMLUtils::GetString(pElement, "placeboconecones", value); vs.m_PlaceboConeCones = value == "" ? vs.m_PlaceboConeCones: CPLHelper::getConeConesIndexFromDescription(value);
  XMLUtils::GetFloat(pElement, "placeboconestrength", vs.m_PlaceboConeStrength);

  XMLUtils::GetBoolean(pElement, "placeboditherenabled", vs.m_PlaceboDitherEnabled);
  XMLUtils::GetString(pElement, "placebodithermethod", value); vs.m_PlaceboDitherMethod = value == "" ? vs.m_PlaceboDitherMethod: CPLHelper::getDitherMethodIndexFromDescription(value);
  XMLUtils::GetInt(pElement, "placeboditherlutsize", vs.m_PlaceboDitherLutSize);
  XMLUtils::GetBoolean(pElement, "placebodithertemporal", vs.m_PlaceboDitherTemporal);
  XMLUtils::GetString(pElement, "placebodithertransfer", value); vs.m_PlaceboDitherTransfer = value == "" ? vs.m_PlaceboDitherTransfer : CPLHelper::getDitherTransferIndexFromDescription(value);

  XMLUtils::GetFloat(pElement, "placebotoneconstantexposure", vs.m_PlaceboToneConstantExposure);
  XMLUtils::GetFloat(pElement, "placebotoneconstantkneeadaptation", vs.m_PlaceboToneConstantKneeAdaptation);
  XMLUtils::GetFloat(pElement, "placebotoneconstantkneedefault", vs.m_PlaceboToneConstantKneeDefault);
  XMLUtils::GetFloat(pElement, "placebotoneconstantkneemaximum", vs.m_PlaceboToneConstantKneeMaximum);
  XMLUtils::GetFloat(pElement, "placebotoneconstantkneeminimum", vs.m_PlaceboToneConstantKneeMinimum);
  XMLUtils::GetFloat(pElement, "placebotoneconstantkneeoffset", vs.m_PlaceboToneConstantKneeOffset);
  XMLUtils::GetFloat(pElement, "placebotoneconstantlinearknee", vs.m_PlaceboToneConstantLinearKnee);
  XMLUtils::GetFloat(pElement, "placebotoneconstantreinhardcontrast", vs.m_PlaceboToneConstantReinhardContrast);
  XMLUtils::GetFloat(pElement, "placebotoneconstantslopeoffset", vs.m_PlaceboToneConstantSlopeOffset);
  XMLUtils::GetFloat(pElement, "placebotoneconstantslopetuning", vs.m_PlaceboToneConstantSlopeTuning);
  XMLUtils::GetFloat(pElement, "placebotoneconstantsplinecontrast", vs.m_PlaceboToneConstantSplineContrast);

  XMLUtils::GetFloat(pElement, "placebogamutconstantscolorimetricgamma", vs.m_PlaceboGamutConstantsColorimetricGamma);
  XMLUtils::GetFloat(pElement, "placebogamutconstantsperceptualdeadzone", vs.m_PlaceboGamutConstantsPerceptualDeadzone);
  XMLUtils::GetFloat(pElement,"placebogamutconstantsperceptualstrength",vs.m_PlaceboGamutConstantsPerceptualStrength);
  XMLUtils::GetFloat(pElement, "placebogamutconstantssoftclipdesat", vs.m_PlaceboGamutConstantsSoftclipDesat);
  XMLUtils::GetFloat(pElement, "placebogamutconstantssoftclipknee", vs.m_PlaceboGamutConstantsSoftclipKnee);

  XMLUtils::GetString(pElement, "placeboluttype", value); vs.m_PlaceboLutType = value == "" ? vs.m_PlaceboLutType : CPLHelper::getLutTypeIndexFromDescription(value); //params.lut will be updated in UpdateLibPLaceboParamsFromVideoSettings 
  XMLUtils::GetFloat(pElement, "placeboantiringingstrength", vs.m_PlaceboAntiringingStrength);
  XMLUtils::GetBoolean(pElement, "placebocorrectsubpixeloffset", vs.m_PlaceboCorrectSubpixelOffset);
  XMLUtils::GetBoolean(pElement, "placebodisablebuiltinscalers", vs.m_PlaceboDisableBuiltinScalers);
  XMLUtils::GetBoolean(pElement, "placebodisabledithergammacorrection", vs.m_PlaceboDisableDitherGammaCorrection);
  XMLUtils::GetBoolean(pElement, "placebodisablelinearscaling", vs.m_PlaceboDisableLinearScaling);
  XMLUtils::GetBoolean(pElement, "placebodynamicconstant", vs.m_PlaceboDynamicConstant);
  XMLUtils::GetString(pElement, "placeboerrordiffusion", value); vs.m_PlaceboErrorDiffusion = value == "" ? vs.m_PlaceboErrorDiffusion : CPLHelper::getErrorDiffusionIndexFromDescription(value);
  XMLUtils::GetBoolean(pElement, "placeboforcedither", vs.m_PlaceboForceDither);
  XMLUtils::GetBoolean(pElement, "placeboforcelowbitdepthfbos", vs.m_PlaceboForceLowBitDepthFbos);
  XMLUtils::GetBoolean(pElement, "placeboignoreiccprofiles", vs.m_PlaceboIgnoreIccProfiles);
  XMLUtils::GetBoolean(pElement, "placebopreservemixingcache", vs.m_PlaceboPreserveMixingCache);
  XMLUtils::GetBoolean(pElement, "placeboskipantialiasing", vs.m_PlaceboSkipAntiAliasing);
  XMLUtils::GetBoolean(pElement, "placeboskipcachingsingleframe", vs.m_PlaceboSkipCachingSingleFrame);
  XMLUtils::GetBoolean(pElement, "placeboskiptargetclearing", vs.m_PlaceboSkipTargetClearing);
  LoadShaderSettings(vs, pElement);
  return true;
}


void CPLHelper::LoadLutFile(CVideoSettings& vs, const std::string& path)
{
  //cl test vs.m_PlaceboIccProfile = CRendererPL::ReadIcc("C:/Users/Pooky/source/repos/kodi/kodi-build.x64/Debug/portable_data/userdata/small.icc");
  vs.m_PlaceboLut = ReadLut(path);
  CLog::LogF(LOGDEBUG, "CPLHelper: loading LUT file from {}", path);
  vs.m_placeboOptions->getPlOptions()->params.lut = vs.m_PlaceboLutType == -1 ? NULL : vs.m_PlaceboLut.get();
}
/*
// Deprecated, use pl_frame.icc
videoSettings.m_placeboOptions->icc_params;
videoSettings.m_placeboOptions->icc_params.cache->params;
videoSettings.m_placeboOptions->icc_params.cache_load;
videoSettings.m_placeboOptions->icc_params.cache_priv;
videoSettings.m_placeboOptions->icc_params.cache_save;
videoSettings.m_placeboOptions->icc_params.force_bpc;
videoSettings.m_placeboOptions->icc_params.intent;
videoSettings.m_placeboOptions->icc_params.max_luma;
videoSettings.m_placeboOptions->icc_params.size_b;
videoSettings.m_placeboOptions->icc_params.size_g;
videoSettings.m_placeboOptions->icc_params.size_r;

pl_icc_params is included in pl_icc_object which can be included in every pl_frame.icc
struct pl_icc_params {
	// The rendering intent to use, for profiles with multiple intents. A
	// recommended value is PL_INTENT_RELATIVE_COLORIMETRIC for color-accurate
	// video reproduction, or PL_INTENT_PERCEPTUAL for profiles containing
	// meaningful perceptual mapping tables for some more suitable color space
	// like BT.709.
	//
	// If this is set to the special value PL_INTENT_AUTO, will use the
	// preferred intent provided by the profile header.
	enum pl_rendering_intent intent;

	// The size of the 3DLUT to generate. If left as NULL, these individually
	// default to values appropriate for the profile. (Based on internal
	// precision heuristics)
	//
	// Note: Setting this manually is strongly discouraged, as it can result
	// in excessively high 3DLUT sizes where a much smaller LUT would have
	// sufficed.
	int size_r, size_g, size_b;

	// This field can be used to override the detected brightness level of the
	// ICC profile. If you set this to the special value 0 (or a negative
	// number), libplacebo will attempt reading the brightness value from the
	// ICC profile's tagging (if available), falling back to PL_COLOR_SDR_WHITE
	// if unavailable.
	float max_luma;

	// Force black point compensation. May help avoid crushed or raised black
	// points on "improper" profiles containing e.g. colorimetric tables that
	// do not round-trip. Should not be required on well-behaved profiles,
	// or when using PL_INTENT_PERCEPTUAL, but YMMV.
	bool force_bpc;

	// If provided, this pl_cache instance will be used, instead of the
	// GPU-internal cache, to cache the generated 3DLUTs. Note that these can
	// get large, especially for large values of size_{r,g,b}, so the user may
	// wish to split this cache off from the main shader cache. (Optional)
	pl_cache cache;
};
 */

 /* Available options leftover from structure
 *
 //upscaler/downscaler/frame_mixer/plane upscaler/plane downscaler have a list of preset filters but using the "custom" filter, the values can be set individiually
 videoSettings.m_placeboOptions->upscaler;
 videoSettings.m_placeboOptions->upscaler.allowed;
 videoSettings.m_placeboOptions->upscaler.antiring;
 videoSettings.m_placeboOptions->upscaler.blur;
 videoSettings.m_placeboOptions->upscaler.clamp;
 videoSettings.m_placeboOptions->upscaler.description;
 videoSettings.m_placeboOptions->upscaler.kernel; ///
 videoSettings.m_placeboOptions->upscaler.name;
 videoSettings.m_placeboOptions->upscaler.params;
 videoSettings.m_placeboOptions->upscaler.polar;
 videoSettings.m_placeboOptions->upscaler.radius;
 videoSettings.m_placeboOptions->upscaler.recommended;
 videoSettings.m_placeboOptions->upscaler.taper;
 videoSettings.m_placeboOptions->upscaler.window; ///
 videoSettings.m_placeboOptions->upscaler.wparams;

 // More for kodi integration
 videoSettings.m_placeboOptions->params.background;                  // no
 videoSettings.m_placeboOptions->params.background_color;            // no
 videoSettings.m_placeboOptions->params.background_transparency;     // no
 videoSettings.m_placeboOptions->params.blend_against_tiles;         // no
 videoSettings.m_placeboOptions->params.blur_radius;                 // no
 videoSettings.m_placeboOptions->params.border;                      // no
 videoSettings.m_placeboOptions->params.corner_rounding;             // no
 videoSettings.m_placeboOptions->params.disable_fbos;                // no
 videoSettings.m_placeboOptions->params.hooks;                       // no
 videoSettings.m_placeboOptions->params.info_callback;               // allows gathering stats on shader usage, cache usage, etc. not really useful for end-users but could be used for debugging or advanced users
 videoSettings.m_placeboOptions->params.info_priv;                   // "
 videoSettings.m_placeboOptions->params.num_hooks;                   // no
 videoSettings.m_placeboOptions->params.tile_colors;                 // no
 videoSettings.m_placeboOptions->params.tile_size;                   // no
 videoSettings.m_placeboOptions->params.polar_cutoff;                // deprecated // hard-coded as 1e-3
 videoSettings.m_placeboOptions->params.allow_delayed_peak_detect;   //deprecated
 videoSettings.m_placeboOptions->params.skip_target_clearing;        //deprecated
 videoSettings.m_placeboOptions->color_map_params.tone_mapping_crosstalk; // deprecated, now hard-coded as 0.04
 videoSettings.m_placeboOptions->color_map_params.tone_mapping_mode;      // deprecated
 videoSettings.m_placeboOptions->color_map_params.tone_mapping_param;     // deprecated
 videoSettings.m_placeboOptions->color_map_params.hybrid_mix;             // deprecated

 // More for kodi integration
 videoSettings.m_placeboOptions->color_map_params.metadata;               // {"any",  PL_HDR_METADATA_ANY},{"none", PL_HDR_METADATA_NONE},{"hdr10",PL_HDR_METADATA_HDR10},{"hdr10plus", PL_HDR_METADATA_HDR10PLUS},{"cie_y",PL_HDR_METADATA_CIE_Y})),

 // More for kodi integration
 videoSettings.m_placeboOptions->blend_params;
 videoSettings.m_placeboOptions->blend_params.dst_alpha;
 videoSettings.m_placeboOptions->blend_params.dst_rgb;
 videoSettings.m_placeboOptions->blend_params.src_alpha;
 videoSettings.m_placeboOptions->blend_params.src_rgb;

 // More for kodi integration
 videoSettings.m_placeboOptions->distort_params;
 videoSettings.m_placeboOptions->distort_params.address_mode;
 videoSettings.m_placeboOptions->distort_params.alpha_mode;
 videoSettings.m_placeboOptions->distort_params.bicubic;
 videoSettings.m_placeboOptions->distort_params.constrain;
 videoSettings.m_placeboOptions->distort_params.transform;
 videoSettings.m_placeboOptions->distort_params.transform.c;
 videoSettings.m_placeboOptions->distort_params.transform.mat;
 videoSettings.m_placeboOptions->distort_params.unscaled;
   */


std::shared_ptr<const pl_custom_lut> CPLHelper::ReadLut(const std::string& fileName)
{
  std::shared_ptr<const pl_custom_lut> lutPtr = nullptr;

  if (fileName.empty())
	return nullptr;

  CFile lutFile;
  if (!lutFile.Open(fileName))
  {
	CLog::LogF(LOGERROR, "{}: Could not open 3DLUT file: {}", __FUNCTION__, fileName);
	return nullptr;
  }

  // Read entire file to memory
  ULONGLONG fileSize = lutFile.GetLength();
  if (fileSize > 0)
  {
	BYTE* pBuffer = new BYTE[(size_t)fileSize];
	UINT bytesRead = lutFile.Read(pBuffer, (UINT)fileSize);

	pl_custom_lut* lut = nullptr;
	lut = pl_lut_parse_cube(NULL, (const char*)pBuffer, (size_t)bytesRead);
	std::shared_ptr<const pl_custom_lut> lutPtr2(lut, [](pl_custom_lut* p) { pl_lut_free(&p); });
	lutPtr = lutPtr2;
	delete[] pBuffer;
  }
  lutFile.Close();
  return lutPtr;
}

void CPLHelper::LoadShaderSettings(CVideoSettings& vs, const std::string& data)
{
  CXBMCTinyXML xmlDoc;
  std::string value;

  if (!xmlDoc.LoadString(data))
  {
	CLog::LogF(LOGERROR, "CGUIDialogVideoSettings: Error loading LipPlacebo Shadings from database, Line {}\n{}", xmlDoc.ErrorRow(), xmlDoc.ErrorDesc());
	return;
  }
  const TiXmlElement* pElement = xmlDoc.FirstChildElement("libPlaceboShaders");
  if (!pElement)
  {
	CLog::LogF(LOGERROR, "CGUIDialogVideoSettings: Error loading LipPlacebo settings, missing <libPlaceboShaders> element");
	return;
  }

  LoadShaderSettings(vs, pElement);
}

void CPLHelper::LoadShaderSettings(CVideoSettings& vs, const TiXmlElement* pElement)
{
  int numShaders;
  XMLUtils::GetInt(pElement, "placeboShadersCount", numShaders);
  CLog::LogF(LOGDEBUG,"LoadShaderSettings from node, numShaders = {}",numShaders);
  for (int i = 0; i < numShaders; i++)
  {
	bool bValue;
	std::string fileName;
	XMLUtils::GetString(pElement, ("placeboShaderFilename" + StringUtils::Format("_{:02}", i)).c_str(), fileName);
	XMLUtils::GetBoolean(pElement, ("placeboShaderEnabled" + StringUtils::Format("_{:02}", i)).c_str(), bValue);

	vs.m_PlaceboShadersFilename.emplace_back(fileName);
	vs.m_PlaceboShadersEnabled.emplace_back(bValue);

	int numParams;
	XMLUtils::GetInt(pElement, ("placeboShadersParamsCount" + StringUtils::Format("_{:02}", i)).c_str(), numParams);
	vs.m_PlaceboShadersParams.emplace_back();
	for (int j = 0; j < numParams; j++)
	{
	  std::string name;
	  std::string valueStr;
	  std::variant<float, int, unsigned int> value;

	  XMLUtils::GetString(pElement, ("placeboShaderParamName" + StringUtils::Format("_{:02}", i) + StringUtils::Format("_{:02}", j)).c_str(), name);
	  XMLUtils::GetString(pElement, ("placeboShaderParamValue" + StringUtils::Format("_{:02}", i) + StringUtils::Format("_{:02}", j)).c_str(), valueStr);
	  std::string typeStr = valueStr.substr(0, valueStr.find(':'));
	  pl_var_type type = typeStr == "float" ? PL_VAR_FLOAT : typeStr == "sint" ? PL_VAR_SINT : typeStr == "uint" ? PL_VAR_UINT : PL_VAR_INVALID;

	  value = 0;
	  if (type == PL_VAR_FLOAT)
	  {
		value = std::stof(valueStr.substr(valueStr.find(':') + 1));
	  }
	  else if (type == PL_VAR_SINT)
	  {
		value = std::stoi(valueStr.substr(valueStr.find(':') + 1));
	  }
	  else if (type == PL_VAR_UINT)
	  {
		value = std::stoul(valueStr.substr(valueStr.find(':') + 1));
	  }
	  else
	  {
		//cl make it invalid, reload from scratch?
		CLog::LogF(LOGERROR, "CGUIDialogVideoSettings: Error loading LipPlacebo shader parameter, unknown type: {}", valueStr);
	  }

	  vs.m_PlaceboShadersParams[i].emplace_back(name, type, value);
	}
  }
}


void CPLHelper::SerializeShaders(const CVideoSettings& vs, std::string& serializedData)
{
  CXBMCTinyXML xmlDoc;
  TiXmlElement rootElement("libPlaceboShaders");
  rootElement.SetAttribute(SETTING_XML_ROOT_VERSION, "1.0");
  TiXmlNode* pNode = xmlDoc.InsertEndChild(rootElement);

  if (!pNode)
  {
	CLog::LogF(LOGERROR, "Failed to create XML node for LibPlacebo shaders serialization \"{}\"", rootElement.Value());
	return;
  }
  else
  {
	SerializeShaders(vs, pNode);
  }
  xmlDoc.SaveString(serializedData);
}

void CPLHelper::SerializeShaders(const CVideoSettings& vs, TiXmlNode* pNode)
{
  XMLUtils::SetInt(pNode, "placeboShadersCount", vs.m_PlaceboShadersFilename.size());
  for (int i = 0; i < vs.m_PlaceboShadersFilename.size(); i++)
  {
	XMLUtils::SetString(pNode, ("placeboShaderFilename" + StringUtils::Format("_{:02}", i)).c_str(), vs.m_PlaceboShadersFilename[i]);
	XMLUtils::SetBoolean(pNode, ("placeboShaderEnabled" + StringUtils::Format("_{:02}", i)).c_str(), vs.m_PlaceboShadersEnabled[i]);
	XMLUtils::SetInt(pNode, ("placeboShadersParamsCount" + StringUtils::Format("_{:02}", i)).c_str(), vs.m_PlaceboShadersParams[i].size());
	for (int j = 0; j < vs.m_PlaceboShadersParams[i].size(); j++)
	{
	  XMLUtils::SetString(pNode, ("placeboShaderParamName" + StringUtils::Format("_{:02}", i) + StringUtils::Format("_{:02}", j)).c_str(), vs.m_PlaceboShadersParams[i][j].m_Name);
	  if (vs.m_PlaceboShadersParams[i][j].m_Type == PL_VAR_FLOAT)
	  {
		XMLUtils::SetString(pNode, ("placeboShaderParamValue" + StringUtils::Format("_{:02}", i) + StringUtils::Format("_{:02}", j)).c_str(), "float:" + std::to_string(std::get<float>(vs.m_PlaceboShadersParams[i][j].m_Value)));
	  }
	  else if (vs.m_PlaceboShadersParams[i][j].m_Type == PL_VAR_SINT)
	  {
		XMLUtils::SetString(pNode, ("placeboShaderParamValue" + StringUtils::Format("_{:02}", i) + StringUtils::Format("_{:02}", j)).c_str(), "sint:" + std::to_string(std::get<int>(vs.m_PlaceboShadersParams[i][j].m_Value)));
	  }
	  else if (vs.m_PlaceboShadersParams[i][j].m_Type == PL_VAR_UINT)
	  {
		XMLUtils::SetString(pNode, ("placeboShaderParamValue" + StringUtils::Format("_{:02}", i) + StringUtils::Format("_{:02}", j)).c_str(), "uint:" + std::to_string(std::get<unsigned int>(vs.m_PlaceboShadersParams[i][j].m_Value)));
	  }
	  else
	  {
		//cl 
		CLog::LogF(LOGERROR, "CGUIDialogVideoSettings: Error serializing LipPlacebo shader parameter, unknown type: {}", vs.m_PlaceboShadersParams[i][j].m_Type);
	  }
	}
  }
}



void CPLHelper::AddShaderFile(pl_gpu gpu, CVideoSettings& vs, const std::string& fileName)
{
  if (fileName.empty())
	return;

  CFile shaderFile;
  if (!shaderFile.Open(fileName))
  {
	CLog::LogF(LOGERROR, "Could not open shader file: {}", fileName);
	return;
  }

  // Read entire file to memory
  ULONGLONG fileSize = shaderFile.GetLength();
  if (fileSize > 0)
  {
	BYTE* pBuffer = new BYTE[(size_t)fileSize];
	UINT bytesRead = shaderFile.Read(pBuffer, (UINT)fileSize);

	const pl_hook* pHook = pl_mpv_user_shader_parse(gpu, (const char*)pBuffer, (size_t)bytesRead);
	delete[] pBuffer;
	if (pHook)
	{
	  std::shared_ptr<const pl_hook> SharedHook(pHook, [](const pl_hook* p) { pl_mpv_user_shader_destroy(&p); });

	  std::string shortFileName = URIUtils::GetFileName(fileName);
	  vs.m_PlaceboShadersFilename.push_back(fileName);
	  vs.m_PlaceboShadersEnabled.push_back(true);
	  vs.m_PlaceboShadersParams.push_back({});
	  vs.m_PlaceboShadersHooks.m_FileNames.push_back(shortFileName);
	  vs.m_PlaceboShadersHooks.m_Hooks.push_back(SharedHook);
	  vs.m_PlaceboShadersHooks.m_Valid.push_back(true);

	  for (int i = 0; i < pHook->num_parameters; ++i)
	  {
		CShaderParam param;
		param.m_Name = pHook->parameters[i].name ? pHook->parameters[i].name : "";
		param.m_Type = pHook->parameters[i].type;
		param.m_Value = param.m_Type == PL_VAR_FLOAT ? pHook->parameters[i].data->f : param.m_Type == PL_VAR_SINT ? pHook->parameters[i].data->i : param.m_Type == PL_VAR_UINT ? pHook->parameters[i].data->u : PL_VAR_INVALID;

		vs.m_PlaceboShadersParams.back().emplace_back(param.m_Name, param.m_Type, param.m_Value);
	  }
	}
	else
	{
	  CLog::LogF(LOGERROR, "Error parsing shader file: {}", fileName);
	  std::string shortFileName = URIUtils::GetFileName(fileName);
	  vs.m_PlaceboShadersFilename.push_back(fileName);
	  vs.m_PlaceboShadersEnabled.push_back(false);
	  vs.m_PlaceboShadersParams.push_back({});
	  vs.m_PlaceboShadersHooks.m_FileNames.push_back(shortFileName);
	  vs.m_PlaceboShadersHooks.m_Hooks.push_back(nullptr);
	  vs.m_PlaceboShadersHooks.m_Valid.push_back(false);
	}


  }
  shaderFile.Close();
}

void CPLHelper::ResetShaders(CVideoSettings& vs)
{
  vs.m_PlaceboShadersHooks.clear();
  vs.m_PlaceboShadersFilename.clear();
  vs.m_PlaceboShadersEnabled.clear();
  vs.m_PlaceboShadersParams.clear();
}

void CPLHelper::InitializeShaders(pl_gpu gpu)
{
  auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();
  CVideoSettings vs = appPlayer->GetVideoSettings();
  if(vs.m_PlaceboShadersHooks.m_bInit == false)
  {
	InitializeShaders(gpu, vs);
    appPlayer->SetVideoSettings(vs);
  }
}

void CPLHelper::InitializeShaders(pl_gpu gpu, CVideoSettings& vs)
{

  // This function could be called more than onced upon player startup....
  // we need to make sure we load shaders only once
  if (vs.m_PlaceboShadersHooks.m_bInit == false)
  {
	vs.m_PlaceboShadersHooks.m_bInit = true;
	CLog::LogF(LOGDEBUG,"InitializeShaders number of shaders ={}",vs.m_PlaceboShadersFilename.size());

	for (int i = 0; i < vs.m_PlaceboShadersFilename.size(); ++i)
	{
	  std::string shortFileName = URIUtils::GetFileName(vs.m_PlaceboShadersFilename[i]);

	  // Create default entries in case we don't complete adding a new shader. all vectors must be kept in sink...
	  //cl Maybe a class for all of this
	  vs.m_PlaceboShadersHooks.m_FileNames.push_back(shortFileName);
	  vs.m_PlaceboShadersHooks.m_Hooks.push_back(nullptr);
	  vs.m_PlaceboShadersHooks.m_Valid.push_back(false);

	  if (vs.m_PlaceboShadersFilename[i].empty())
	    return;
	  CFile shaderFile;
	  if (!shaderFile.Open(vs.m_PlaceboShadersFilename[i]))
	  {
		CLog::LogF(LOGERROR, "Could not open shader file: {}", vs.m_PlaceboShadersFilename[i]);
		return;
	  }

	  // Read entire file to memory
	  ULONGLONG fileSize = shaderFile.GetLength();
	  if (fileSize == 0)
	  {
		CLog::LogF(LOGERROR, "Error parsing shader file: {}", vs.m_PlaceboShadersFilename[i]);
		return;
	  }

	  BYTE* pBuffer = new BYTE[(size_t)fileSize];
	  UINT bytesRead = shaderFile.Read(pBuffer, (UINT)fileSize);
	  shaderFile.Close();

	  const pl_hook* pHook = pl_mpv_user_shader_parse(gpu, (const char*)pBuffer, (size_t)bytesRead);
	  delete[] pBuffer;
	  if (!pHook)
	  {
		CLog::LogF(LOGERROR, "Error parsing shader file: {}", vs.m_PlaceboShadersFilename[i]);
		return;
	  }

	  std::shared_ptr<const pl_hook> SharedHook(pHook, [](const pl_hook* p) { pl_mpv_user_shader_destroy(&p); });

	  vs.m_PlaceboShadersHooks.m_FileNames[i] = shortFileName;
	  vs.m_PlaceboShadersHooks.m_Hooks[i] = SharedHook;
	  vs.m_PlaceboShadersHooks.m_Valid[i] = true;
	  for (int j = 0; j < pHook->num_parameters; ++j)
	  {
		if (vs.m_PlaceboShadersParams[i][j].m_Type == PL_VAR_FLOAT)
		  pHook->parameters[j].data->f = std::get<float>(vs.m_PlaceboShadersParams[i][j].m_Value);
		else if (vs.m_PlaceboShadersParams[i][j].m_Type == PL_VAR_SINT)
		  pHook->parameters[j].data->i = std::get<int>(vs.m_PlaceboShadersParams[i][j].m_Value);
		else if (vs.m_PlaceboShadersParams[i][j].m_Type == PL_VAR_UINT)
		  pHook->parameters[j].data->u = std::get<unsigned int>(vs.m_PlaceboShadersParams[i][j].m_Value);
	  }
	}
  }
}

int CPLHelper::getErrorDiffusionIndexFromDescription(std::string description)
{
  const struct pl_error_diffusion_kernel* f;
  for (int i = 0; i < pl_num_error_diffusion_kernels; i++)
  {
	f = pl_error_diffusion_kernels[i];
	if (f->description != nullptr)
	  if (description == std::string(f->description))
		return i;
  }
  return -1;
}

int CPLHelper::getColorMapIntentIndexFromDescription(std::string description)
{
  std::vector<IntegerSettingOption> alist;
  int value;
  const std::shared_ptr<const CSetting> asetting;
  PlColorMapIntentOptionFiller(asetting, alist, value);
  if (description == "")
	return -1;

  for (int i = 0; i < alist.size(); i++)
  {
	if (alist[i].label == description)
	  return alist [i].value; 
  }
  return -1;
}

int CPLHelper::getDitherMethodIndexFromDescription(std::string description)
{
  std::vector<IntegerSettingOption> alist;
  int value;
  const std::shared_ptr<const CSetting> asetting;
  PlDitherMethodOptionFiller(asetting, alist, value);
  if (description == "")
	return -1;

  for (int i = 0; i < alist.size(); i++)
  {
	if (alist[i].label == description)
	  return i;
  }
  return -1;
}

int CPLHelper::getDitherTransferIndexFromDescription(std::string description)
{
  for (int i = 0; i < PL_COLOR_TRC_COUNT; i++)
  {
	if (pl_color_transfer_name(static_cast<pl_color_transfer>(i)) == description)
	  return i;
  }
  return -1;
}

int CPLHelper::getConeConesIndexFromDescription(std::string description)
{
  std::vector<IntegerSettingOption> alist;
  int value;
  const std::shared_ptr<const CSetting> asetting;
  PlConeConesOptionFiller(asetting, alist, value);
  if (description == "")
	return -1;

  for (int i = 0; i < alist.size(); i++)
  {
	if (alist[i].label == description)
	  return i;
  }
  return -1;
}


int CPLHelper::getFilterIndexFromDescription(std::string description)
{
  const struct pl_filter_config* f;
  for (int i = 0; i < pl_num_filter_configs; i++)
  {
	f = pl_filter_configs[i];
	if (f->description != nullptr)
	  if (description == std::string(f->description))
		return i;
  }
  return -1;
}

int CPLHelper::getToneMapIndexFromDescription(std::string description)
{
  const struct pl_tone_map_function* f;
  for (int i = 0; i < pl_num_tone_map_functions; i++)
  {
	f = pl_tone_map_functions[i];
	if (f->description != nullptr)
	  if (description == f->description)
		return i;
  }
  return -1;
}

int CPLHelper::getGamutMapIndexFromDescription(std::string description)
{
  const struct pl_gamut_map_function* f;
  for (int i = 0; i < pl_num_gamut_map_functions; i++)
  {
	f = pl_gamut_map_functions[i];
	if (f->description != nullptr)
	  if (description == f->description)
		return i;
  }
  return -1;
}

int CPLHelper::getLutTypeIndexFromDescription(std::string description)
{
  std::vector<IntegerSettingOption> alist;
  int value;
  const std::shared_ptr<const CSetting> asetting;
  PlLutTypeOptionFiller(asetting, alist, value);
  if (description == "")
	return -1;

  for (int i = 0; i < alist.size(); i++)
  {
	if (alist[i].label == description)
	  return i;
  }
  return -1; // auto
}

int CPLHelper::getDeinterlaceAlgoIndexFromDescription(std::string description)
{
  std::vector<IntegerSettingOption> alist;
  int value;
  const std::shared_ptr<const CSetting> asetting;
  PlDeinterlaceAlgoOptionFiller(asetting, alist, value);
  if (description == "")
	return -1;

  for (int i = 0; i < alist.size(); i++)
  {
	if (alist[i].label == description)
	  return i;
  }
  return -1;
}
std::string CPLHelper::getDiffusionKernelDescriptionFromIndex(int index)
{
  std::vector<IntegerSettingOption> alist;
  int value;
  const std::shared_ptr<const CSetting> asetting;

  PlDiffusionKernelOptionFiller(asetting, alist, value);

  if (index == -1)
	return "disabled";
  else if (index < 0 || index >= alist.size())
	return "";
  else
	return alist[index].label;
}

std::string CPLHelper::getDitherTransferDescriptionFromIndex(int index)
{
  std::vector<IntegerSettingOption> alist;
  int value;
  const std::shared_ptr<const CSetting> asetting;

  PlDitherTransferOptionFiller(asetting, alist, value);

  if (index < 0 || index >= alist.size())
	return "";
  else
	return alist[index].label;
}

std::string CPLHelper::getDitherMethodDescriptionFromIndex(int index)
{
  std::vector<IntegerSettingOption> alist;
  int value;
  const std::shared_ptr<const CSetting> asetting;

  PlDitherMethodOptionFiller(asetting, alist, value);

  if (index < 0 || index >= alist.size())
	return "";
  else
	return alist[index].label;
}

std::string CPLHelper::getLutTypeDescriptionFromIndex(int index)
{
  std::vector<IntegerSettingOption> alist;
  int value;
  const std::shared_ptr<const CSetting> asetting;

  PlLutTypeOptionFiller(asetting, alist, value);

  if (index == -1)
	return "disabled";
  if (index < 0 || index >= alist.size())
	return "disabled";
  else
	return alist[index].label;
}


std::string CPLHelper::getDeinterlaceAlgoDescriptionFromIndex(int index)
{
  std::vector<IntegerSettingOption> alist;
  int value;
  const std::shared_ptr<const CSetting> asetting;

  PlDeinterlaceAlgoOptionFiller(asetting, alist, value);

  if (index < 0 || index >= alist.size())
	return "";
  else
	return alist[index].label;
}


std::string CPLHelper::getConeConesDescriptionFromIndex(int index)
{
  std::vector<IntegerSettingOption> alist;
  int value;
  const std::shared_ptr<const CSetting> asetting;

  PlConeConesOptionFiller(asetting, alist, value);

  if (index < 0 || index >= alist.size())
	return "";
  else
	return alist[index].label;
}

std::string CPLHelper::getColorMapIntentDescriptionFromIndex(int index)
{
  std::vector<IntegerSettingOption> alist;
  int value;
  const std::shared_ptr<const CSetting> asetting;

  PlColorMapIntentOptionFiller(asetting, alist, value);

  if (index + 1 < 0 || index + 1 >= alist.size())
	return "";
  else
	return alist[index + 1].label;
}

void CPLHelper::PlLutOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<StringSettingOption>& list, std::string& current)
{
  CFileItemList items;
  auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();
  CVideoSettings vs = appPlayer->GetVideoSettings();

  std::string currentLut = vs.m_PlaceboLutFilename;
  XFILE::CDirectory::GetDirectory("special://masterprofile/", items, ".cube", DIR_FLAG_NO_FILE_DIRS | DIR_FLAG_NO_FILE_INFO);

  for (int i = 0; i < items.Size(); i++)
  {
	if (items[i]->IsType(".cube"))
	  list.emplace_back(items[i]->GetLabel(), items[i]->GetPath());
  }
}

void CPLHelper::PlLutTypeOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current)
{
  list.emplace_back("Disabled", -1);
  list.emplace_back("Auto (unknown)", PL_LUT_UNKNOWN);
  list.emplace_back("Raw RGB (native)", PL_LUT_NATIVE);
  list.emplace_back("Linear RGB (normalized)", PL_LUT_NORMALIZED);
  list.emplace_back("Gamut conversion (native)", PL_LUT_CONVERSION);
}

void CPLHelper::PlDiffusionKernelOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current)
{
  const struct pl_error_diffusion_kernel* f;
  list.emplace_back("Disabled", -1);
  for (int i = 0; i < pl_num_error_diffusion_kernels; i++)
  {
	f = pl_error_diffusion_kernels[i];
	if (f->description)
	  list.emplace_back(f->description, i);
  }
}

void CPLHelper::PlColorMapIntentOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current)
{
  list.emplace_back("Auto", PL_INTENT_AUTO);
  list.emplace_back("Perceptual", PL_INTENT_PERCEPTUAL);
  list.emplace_back("Relative colorimetric", PL_INTENT_RELATIVE_COLORIMETRIC);
  list.emplace_back("Saturation", PL_INTENT_SATURATION);
  list.emplace_back("Absolute colorimetric", PL_INTENT_ABSOLUTE_COLORIMETRIC);
}

void CPLHelper::PlDitherMethodOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current)
{
  list.emplace_back("Blue noise", PL_DITHER_BLUE_NOISE);
  list.emplace_back("Ordered LUT", PL_DITHER_ORDERED_LUT);
  list.emplace_back("Ordered fixed", PL_DITHER_ORDERED_FIXED);
  list.emplace_back("White noise", PL_DITHER_WHITE_NOISE);
}

void CPLHelper::PlDitherTransferOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current)
{
  const struct pl_tone_map_function* f;
  for (int i = 0; i < PL_COLOR_TRC_COUNT; i++)
  {
	if (pl_color_transfer_name(static_cast<pl_color_transfer>(i)))
	  list.emplace_back(pl_color_transfer_name(static_cast<pl_color_transfer>(i)), i);
  }
}

void CPLHelper::PlConeConesOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current)
{
  list.emplace_back("None", PL_CONE_NONE);
  list.emplace_back("L", PL_CONE_L);
  list.emplace_back("M", PL_CONE_M);
  list.emplace_back("S", PL_CONE_S);
  list.emplace_back("LM", PL_CONE_L | PL_CONE_M);
  list.emplace_back("MS", PL_CONE_M | PL_CONE_S);
  list.emplace_back("LS", PL_CONE_L | PL_CONE_S);
  list.emplace_back("LMS", PL_CONE_L | PL_CONE_M | PL_CONE_S);

}

void CPLHelper::PlDeinterlaceAlgoOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current)
{
  list.emplace_back("WEAVE", PL_DEINTERLACE_WEAVE);
  list.emplace_back("BOB", PL_DEINTERLACE_BOB);
  list.emplace_back("YADIF", PL_DEINTERLACE_YADIF);
  list.emplace_back("BWDIF", PL_DEINTERLACE_BWDIF);

}

void CPLHelper::PlColorMapToneMapFunctionOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current)
{
  const struct pl_tone_map_function* f;

  list.emplace_back("Disabled", -1);
  for (int i = 0; i < pl_num_tone_map_functions; i++)
  {
	std::string strItem;
	f = pl_tone_map_functions[i];
	if (f->description)
	  list.emplace_back(f->description, i);
  }
}

void CPLHelper::PlColorMapGamutMapFunctionOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current)
{
  const struct pl_gamut_map_function* f;

  list.emplace_back("Disabled", -1);
  for (int i = 0; i < pl_num_gamut_map_functions; i++)
  {
	std::string strItem;
	f = pl_gamut_map_functions[i];
	if (f->description)
	  list.emplace_back(f->description, i);
  }
}

void CPLHelper::PlUpscalerOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current)
{
  const struct pl_filter_config* f;

  list.emplace_back("Disabled", -1);
  for (int i = 0; i < pl_num_filter_configs; i++)
  {
	std::string strItem;
	f = pl_filter_configs[i];
	if (!f->description)
	  continue;
	if (!(f->recommended & PL_FILTER_UPSCALING))
	  continue;
	char display_name [128];
	snprintf(display_name, sizeof(display_name), "[COLOR gold]★[/COLOR] %s", f->description);
	list.emplace_back(display_name, i);
  }
  for(int i = 0; i < pl_num_filter_configs; i++)
  {
	std::string strItem;
	f = pl_filter_configs [i];
	if(!f->description)
	  continue;
	if(!(f->allowed & (PL_FILTER_UPSCALING | PL_FILTER_SCALING | PL_FILTER_ALL)))
	    continue;
	if((f->recommended & PL_FILTER_UPSCALING))
	  continue;
	list.emplace_back(f->description, i);
  }

}

void CPLHelper::PlDownscalerOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current)
{
  const struct pl_filter_config* f;

  list.emplace_back("Disabled", -1);
  for (int i = 0; i < pl_num_filter_configs; i++)
  {
	std::string strItem;
	f = pl_filter_configs[i];
	if (!f->description)
	  continue;
	if (!(f->recommended & PL_FILTER_DOWNSCALING))
	  continue;
	char display_name [128];
	snprintf(display_name, sizeof(display_name), "[COLOR gold]★[/COLOR] %s", f->description);
	list.emplace_back(display_name, i);
  }
  for(int i = 0; i < pl_num_filter_configs; i++)
  {
	std::string strItem;
	f = pl_filter_configs [i];
	if(!f->description)
	  continue;
	if (!(f->allowed & (PL_FILTER_DOWNSCALING | PL_FILTER_SCALING | PL_FILTER_ALL)))
	  continue;
	if((f->recommended & PL_FILTER_DOWNSCALING))
	  continue;
	list.emplace_back(f->description, i);
  }
}

void CPLHelper::PlFrameMixerOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current)
{
  const struct pl_filter_config* f;

  list.emplace_back("Disabled", -1);
  for (int i = 0; i < pl_num_filter_configs; i++)
  {
	std::string strItem;
	f = pl_filter_configs[i];
	if (!f->description)
	  continue;
	if (!(f->recommended & PL_FILTER_FRAME_MIXING))
	  continue;
	char display_name [128];
	snprintf(display_name, sizeof(display_name), "[COLOR gold]★[/COLOR] %s", f->description);
	list.emplace_back(display_name, i);
  }
  for(int i = 0; i < pl_num_filter_configs; i++)
  {
	std::string strItem;
	f = pl_filter_configs [i];
	if(!f->description)
	  continue;
	if(!(f->allowed & (PL_FILTER_FRAME_MIXING | PL_FILTER_ALL)))
	  continue;
	if ((f->recommended & PL_FILTER_FRAME_MIXING))
	  continue;
	list.emplace_back(f->description, i);
  }
}



