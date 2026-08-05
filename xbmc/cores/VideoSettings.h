/*
 *  Copyright (C) 2005-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "..\VideoPlayer\VideoRenderers\LibPlacebo\PlOptionsWrapper.h"
#include "libplacebo/gpu.h"
#include "utils/Map.h"

#include <optional>
#include <memory>
#include <string_view>
#include <variant>

#include <fmt/base.h>
#include <fmt/format.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/shaders/lut.h>
#include <stdexcept>
#include <string>
#include <vector>

// VideoSettings.h: interface for the CVideoSettings class.
//
//////////////////////////////////////////////////////////////////////

enum EINTERLACEMETHOD
{
  VS_INTERLACEMETHOD_NONE=0,
  VS_INTERLACEMETHOD_AUTO=1,
  VS_INTERLACEMETHOD_RENDER_BLEND=2,
  VS_INTERLACEMETHOD_RENDER_WEAVE=4,
  VS_INTERLACEMETHOD_RENDER_BOB=6,
  VS_INTERLACEMETHOD_DEINTERLACE=7,
  VS_INTERLACEMETHOD_VDPAU_BOB=8,
  VS_INTERLACEMETHOD_VDPAU_INVERSE_TELECINE=11,
  VS_INTERLACEMETHOD_VDPAU_TEMPORAL=12,
  VS_INTERLACEMETHOD_VDPAU_TEMPORAL_HALF=13,
  VS_INTERLACEMETHOD_VDPAU_TEMPORAL_SPATIAL=14,
  VS_INTERLACEMETHOD_VDPAU_TEMPORAL_SPATIAL_HALF=15,
  VS_INTERLACEMETHOD_DEINTERLACE_HALF=16,
  VS_INTERLACEMETHOD_VAAPI_BOB = 22,
  VS_INTERLACEMETHOD_VAAPI_MADI = 23,
  VS_INTERLACEMETHOD_VAAPI_MACI = 24,
  VS_INTERLACEMETHOD_DXVA_AUTO = 32,
  VS_INTERLACEMETHOD_MAX // do not use and keep as last enum value.
};

template<>
struct fmt::formatter<EINTERLACEMETHOD> : fmt::formatter<std::string_view>
{
  template<typename FormatContext>
  constexpr auto format(const EINTERLACEMETHOD& interlaceMethod, FormatContext& ctx)
  {
    const auto it = interlaceMethodMap.find(interlaceMethod);
    if (it == interlaceMethodMap.cend())
      throw std::range_error("no interlace method string found");

    return fmt::formatter<string_view>::format(it->second, ctx);
  }

  static constexpr std::string_view ToString(EINTERLACEMETHOD m) noexcept
  {
    const auto it = interlaceMethodMap.find(m);
    return it != interlaceMethodMap.cend() ? it->second : "unknown";
  }

private:
  static constexpr auto interlaceMethodMap = make_map<EINTERLACEMETHOD, std::string_view>({
      {VS_INTERLACEMETHOD_NONE, "none"},
      {VS_INTERLACEMETHOD_AUTO, "auto"},
      {VS_INTERLACEMETHOD_RENDER_BLEND, "render blend"},
      {VS_INTERLACEMETHOD_RENDER_WEAVE, "render weave"},
      {VS_INTERLACEMETHOD_RENDER_BOB, "render bob"},
      {VS_INTERLACEMETHOD_DEINTERLACE, "deinterlace"},
      {VS_INTERLACEMETHOD_VDPAU_BOB, "vdpau bob"},
      {VS_INTERLACEMETHOD_VDPAU_INVERSE_TELECINE, "vdpau inverse telecine"},
      {VS_INTERLACEMETHOD_VDPAU_TEMPORAL, "vdpau temporal"},
      {VS_INTERLACEMETHOD_VDPAU_TEMPORAL_HALF, "vdpau temporal half"},
      {VS_INTERLACEMETHOD_VDPAU_TEMPORAL_SPATIAL, "vdpau temporal spatial"},
      {VS_INTERLACEMETHOD_VDPAU_TEMPORAL_SPATIAL_HALF, "vdpau temporal spatial half"},
      {VS_INTERLACEMETHOD_DEINTERLACE_HALF, "deinterlace half"},
      {VS_INTERLACEMETHOD_VAAPI_BOB, "vaapi bob"},
      {VS_INTERLACEMETHOD_VAAPI_MADI, "vaapi madi"},
      {VS_INTERLACEMETHOD_VAAPI_MACI, "vaapi maci"},
      {VS_INTERLACEMETHOD_DXVA_AUTO, "dxva auto"},
  });
};

enum ESCALINGMETHOD
{
  VS_SCALINGMETHOD_NEAREST=0,
  VS_SCALINGMETHOD_LINEAR,
  VS_SCALINGMETHOD_CUBIC_B_SPLINE,
  VS_SCALINGMETHOD_CUBIC_MITCHELL,
  VS_SCALINGMETHOD_CUBIC_CATMULL,
  VS_SCALINGMETHOD_CUBIC_0_075,
  VS_SCALINGMETHOD_CUBIC_0_1,
  VS_SCALINGMETHOD_LANCZOS2,
  VS_SCALINGMETHOD_LANCZOS3_FAST,
  VS_SCALINGMETHOD_LANCZOS3,
  VS_SCALINGMETHOD_SINC8,
  VS_SCALINGMETHOD_BICUBIC_SOFTWARE,
  VS_SCALINGMETHOD_LANCZOS_SOFTWARE,
  VS_SCALINGMETHOD_SINC_SOFTWARE,
  VS_SCALINGMETHOD_VDPAU_HARDWARE,
  VS_SCALINGMETHOD_DXVA_HARDWARE,
  VS_SCALINGMETHOD_AUTO,
  VS_SCALINGMETHOD_SPLINE36_FAST,
  VS_SCALINGMETHOD_SPLINE36,
  VS_SCALINGMETHOD_MAX // do not use and keep as last enum value.
};

template<>
struct fmt::formatter<ESCALINGMETHOD> : fmt::formatter<std::string_view>
{
public:
  template<typename FormatContext>
  constexpr auto format(const ESCALINGMETHOD& scalingMethod, FormatContext& ctx)
  {
    const auto it = scalingMethodMap.find(scalingMethod);
    if (it == scalingMethodMap.cend())
      throw std::range_error("no scaling method string found");

    return fmt::formatter<string_view>::format(it->second, ctx);
  }

  static constexpr std::string_view ToString(ESCALINGMETHOD m) noexcept
  {
    const auto it = scalingMethodMap.find(m);
    return it != scalingMethodMap.cend() ? it->second : "unknown";
  }

private:
  static constexpr auto scalingMethodMap = make_map<ESCALINGMETHOD, std::string_view>({
      {VS_SCALINGMETHOD_NEAREST, "nearest neighbour"},
      {VS_SCALINGMETHOD_LINEAR, "linear"},
      {VS_SCALINGMETHOD_CUBIC_B_SPLINE, "cubic b spline"},
      {VS_SCALINGMETHOD_CUBIC_MITCHELL, "cubic mitchell"},
      {VS_SCALINGMETHOD_CUBIC_CATMULL, "cubic catmull"},
      {VS_SCALINGMETHOD_CUBIC_0_075, "cubic 0/075"},
      {VS_SCALINGMETHOD_CUBIC_0_1, "cubic 0/1"},
      {VS_SCALINGMETHOD_LANCZOS2, "lanczos2"},
      {VS_SCALINGMETHOD_LANCZOS3_FAST, "lanczos3 fast"},
      {VS_SCALINGMETHOD_LANCZOS3, "lanczos3"},
      {VS_SCALINGMETHOD_SINC8, "sinc8"},
      {VS_SCALINGMETHOD_BICUBIC_SOFTWARE, "bicubic software"},
      {VS_SCALINGMETHOD_LANCZOS_SOFTWARE, "lanczos software"},
      {VS_SCALINGMETHOD_SINC_SOFTWARE, "sinc software"},
      {VS_SCALINGMETHOD_VDPAU_HARDWARE, "vdpau"},
      {VS_SCALINGMETHOD_DXVA_HARDWARE, "dxva"},
      {VS_SCALINGMETHOD_AUTO, "auto"},
      {VS_SCALINGMETHOD_SPLINE36_FAST, "spline36 fast"},
      {VS_SCALINGMETHOD_SPLINE36, "spline36"},
  });

  static_assert(VS_SCALINGMETHOD_MAX == scalingMethodMap.size(),
                "scalingMethodMap doesn't match the size of ESCALINGMETHOD, did you forget to "
                "add/remove a mapping?");
};

enum ETONEMAPMETHOD
{
  VS_TONEMAPMETHOD_OFF = 0,
  VS_TONEMAPMETHOD_REINHARD = 1,
  VS_TONEMAPMETHOD_ACES = 2,
  VS_TONEMAPMETHOD_HABLE = 3,
  VS_TONEMAPMETHOD_MAX
};

enum EPRESENTMODE
{
  VS_PRESENTMODE_MULTITHREADED = 0,
  VS_PRESENTMODE_CLASSIC = 1
};

template<>
struct fmt::formatter<ETONEMAPMETHOD> : fmt::formatter<std::string_view>
{
public:
  template<typename FormatContext>
  constexpr auto format(const ETONEMAPMETHOD& tonemapMethod, FormatContext& ctx)
  {
    const auto it = tonemapMethodMap.find(tonemapMethod);
    if (it == tonemapMethodMap.cend())
      throw std::range_error("no tonemap method string found");

    return fmt::formatter<string_view>::format(it->second, ctx);
  }

  static constexpr std::string_view ToString(ETONEMAPMETHOD m) noexcept
  {
    const auto it = tonemapMethodMap.find(m);
    return it != tonemapMethodMap.cend() ? it->second : "unknown";
  }

private:
  static constexpr auto tonemapMethodMap = make_map<ETONEMAPMETHOD, std::string_view>({
      {VS_TONEMAPMETHOD_OFF, "off"},
      {VS_TONEMAPMETHOD_REINHARD, "reinhard"},
      {VS_TONEMAPMETHOD_ACES, "aces"},
      {VS_TONEMAPMETHOD_HABLE, "hable"},
  });

  static_assert(VS_TONEMAPMETHOD_MAX == tonemapMethodMap.size(),
                "tonemapMethodMap doesn't match the size of ETONEMAPMETHOD, did you forget to "
                "add/remove a mapping?");
};

enum ViewMode
{
  ViewModeNormal = 0,
  ViewModeZoom,
  ViewModeStretch4x3,
  ViewModeWideZoom,
  ViewModeStretch16x9,
  ViewModeOriginal,
  ViewModeCustom,
  ViewModeStretch16x9Nonlin,
  ViewModeZoom120Width,
  ViewModeZoom110Width
};

class CPlaceboShaders
{
public:
  CPlaceboShaders() = default;
  ~CPlaceboShaders() = default;
  std::vector<std::shared_ptr<const pl_hook>> m_Hooks = {};
  std::vector<std::string> m_FileNames = {};
  std::vector<bool> m_Valid = {};
  bool m_bInit = false;

  void clear(void)
  {
	m_Hooks.clear();
	m_FileNames.clear();
	m_Valid.clear();
	m_bInit = false;
  }

  CPlaceboShaders& operator=(const CPlaceboShaders& other)
  {
	if (this != &other)
	{
	  m_FileNames = other.m_FileNames;
	  m_Valid = other.m_Valid;
	  m_Hooks = other.m_Hooks;
	  m_bInit = other.m_bInit;
	}
	return *this;
  }


  size_t size() const { return m_FileNames.size(); }

  void emplace_back(const std::shared_ptr<const pl_hook>& Hook, std::string FileName, bool Valid)
  {
	m_Hooks.emplace_back(Hook);
	m_FileNames.emplace_back(FileName);
	m_Valid.emplace_back(Valid);
  }
  void erase(int index)
  {
	m_Hooks.erase(m_Hooks.begin() + index);
	m_FileNames.erase(m_FileNames.begin() + index);
	m_Valid.erase(m_Valid.begin() + index);
  }
};


class CShaderParam
{

public:
  CShaderParam(std::string m_Name, pl_var_type m_Type, std::variant<float, int, unsigned int> m_Value) : m_Name(m_Name), m_Type(m_Type), m_Value(m_Value) {};
  CShaderParam(const CShaderParam& other) : m_Name(other.m_Name), m_Type(other.m_Type), m_Value(other.m_Value) {};
  CShaderParam() = default;

  ~CShaderParam() = default;
  std::string m_Name = "";
  pl_var_type m_Type = PL_VAR_SINT;
  std::variant<float, int, unsigned int> m_Value = 0;

  bool operator == (const CShaderParam& other) const
  {
	if ((this->m_Name == other.m_Name) && ((int)this->m_Type == (int)other.m_Type) && (this->m_Value == other.m_Value))
	{
	  return true;
	}
	return false;
  }
  bool operator != (const CShaderParam& other) const
  {
	if ((this->m_Name == other.m_Name) && ((int)this->m_Type == (int)other.m_Type) && (this->m_Value == other.m_Value))
	{
	  return false;
	}
	return true;
  }
  void emplace_back(std::string Name, pl_var_type Type, std::variant<float, int, unsigned int> Value)
  {
	m_Name = Name;
	m_Type = Type;
	m_Value = Value;
  }
  void clear(void)
  {
	m_Name.clear();
	m_Type = PL_VAR_SINT;
	m_Value = 0;
  }
};


enum VS_PLACEBO_SZ_POSITION {
  VS_SZ_POSITION_UPPER_LEFT = 0,
  VS_SZ_POSITION_UPPER_MIDDLE,
  VS_SZ_POSITION_UPPER_RIGHT,
  VS_SZ_POSITION_MIDDLE_RIGHT,
  VS_SZ_POSITION_BOTTOM_RIGHT,
  VS_SZ_POSITION_BOTTOM_MIDDLE,
  VS_SZ_POSITION_BOTTOM_LEFT,
  VS_SZ_POSITION_MIDDLE_LEFT
};

class CVideoSettings
{
public:
  CVideoSettings();
  ~CVideoSettings();
  CVideoSettings(const CVideoSettings& other);
  CVideoSettings& operator=(const CVideoSettings& other);
  void copy(const CVideoSettings& other);
  bool operator!=(const CVideoSettings& right) const;
  void ResetRenderSettings(PlOptionsWrapper::reset_type type = PlOptionsWrapper::DEFAULT);
  void ResetDitherSettings(PlOptionsWrapper::reset_type type);
  void ResetSdrToHdrSettings(PlOptionsWrapper::reset_type type);

  EINTERLACEMETHOD m_InterlaceMethod;
  ESCALINGMETHOD m_ScalingMethod;
  int m_ViewMode; // current view mode
  float m_CustomZoomAmount; // custom setting zoom amount
  float m_CustomPixelRatio; // custom setting pixel ratio
  float m_CustomVerticalShift; // custom setting vertical shift
  bool  m_CustomNonLinStretch;
  int m_AudioStream;
  float m_VolumeAmplification;
  int m_SubtitleStream;
  float m_SubtitleDelay;
  int m_subtitleVerticalPosition{0};
  bool m_subtitleVerticalPositionSave{false};
  bool m_SubtitleOn;
  float m_Brightness;
  float m_Contrast;
  float m_Gamma;
  float m_NoiseReduction;
  bool m_PostProcess;
  float m_Sharpness;
  float m_AudioDelay;
  int m_ResumeTime;
  int m_StereoMode;
  bool m_StereoInvert;
  int m_VideoStream;
  ETONEMAPMETHOD m_ToneMapMethod;
  float m_ToneMapParam;
  int m_Orientation;
  int m_CenterMixLevel; // relative to metadata or default

  int m_PlaceboSkinZoom;
  int m_PlaceboSkinZoomHint;
  VS_PLACEBO_SZ_POSITION m_PlaceboSkinZoomPosition;
  
  std::string m_PlaceboLutFilename;
  float m_PlaceboDisplayHdrPeakLuminance;
  float m_PlaceboDisplaySdrPeakLuminance;
  int m_PlaceboTargetContrast;
  int m_PlaceboNvRtxPipelineEnabled;
  bool m_PlaceboNvSuperResolutionEnabled;
  bool m_PlaceboNvRtxHdrEnabled;
  bool m_PlaceboNvRtxDisableScalers;
  int m_PlaceboSdrTargetContrast;
  int m_PlaceboTargetColorspaceHint;
  int m_PlaceboTargetColorspaceHintMode;
  int m_PlaceboDitherDepth;
  bool m_PlaceboShaderApply;
  bool m_PlaceboUseHdrForSdr;
  float m_PlaceboFrameMixerRadiusFactor;
  bool m_PlaceboFrameMixerBypassQueue;  
  int m_PlaceboCropBottom;
  float m_PlaceboBrightnessHdrHdr;
  float m_PlaceboBrightnessHdrSdr;
  float m_PlaceboBrightnessSdrHdr;
  float m_PlaceboBrightnessSdrSdr;
  float m_PlaceboContrastHdrHdr;
  float m_PlaceboContrastHdrSdr;
  float m_PlaceboContrastSdrHdr;
  float m_PlaceboContrastSdrSdr;
  float m_PlaceboVsrGammaCorrection;

  bool m_PlaceboColorAdjustmentEnabled;
  float m_PlaceboSaturation;
  float m_PlaceboHue;
  float m_PlaceboTemperature;

  bool m_PlaceboPeakDetectEnabled;
  float m_PlaceboPeakDetectSmoothingPeriod;
  float m_PlaceboPeakDetectSceneThresholdLow;
  float m_PlaceboPeakDetectSceneThresholdHigh;
  float m_PlaceboPeakDetectPercentile;
  float m_PlaceboPeakDetectBlackCutoff;
  bool m_PlaceboPeakDetectAllowDelayed;

  int m_PlaceboUpscaler;
  int m_PlaceboDownscaler;
  int m_PlaceboPlaneUpscaler;
  int m_PlaceboPlaneDownscaler;
  int m_PlaceboFrameMixer;

  bool m_PlaceboDebandEnabled;
  float m_PlaceboDebandGrain;
  float m_PlaceboDebandGrainNeutral0;
  float m_PlaceboDebandGrainNeutral1;
  float m_PlaceboDebandGrainNeutral2;
  int m_PlaceboDebandIterations;
  float m_PlaceboDebandRadius;
  float m_PlaceboDebandThreshold;

  bool m_PlaceboColorMapEnabled;
  bool m_PlaceboColorMapGamutExpansion;
  bool m_PlaceboColorMapInverseToneMapping;
  int  m_PlaceboColorMapLut3dSizeI;
  int  m_PlaceboColorMapLut3dSizeC;
  int  m_PlaceboColorMapLut3dSizeH;
  bool m_PlaceboColorMapLut3dTricubic;
  int  m_PlaceboColorMapLutSize;
  bool m_PlaceboColorMapShowClipping;
  int  m_PlaceboColorMapIntent;
  bool m_PlaceboColorMapForceToneMappingLut;
  float m_PlaceboColorMapContrastRecovery;
  float m_PlaceboColorMapContrastSmoothness;
  int m_PlaceboColorMapGamutMapping;
  int m_PlaceboColorMapToneMapping;
  float m_PlaceboColorMapToneMapParameter; //cl only used transitively

  bool m_PlaceboDeinterlaceEnabled;
  int m_PlaceboDeinterlaceAlgo;
  bool m_PlaceboDeinterlaceSkipSpatialCheck;

  bool m_PlaceboSigmoidEnabled;
  float m_PlaceboSigmoidCenter;
  float m_PlaceboSigmoidSlope;

  bool m_PlaceboConeEnabled;
  int m_PlaceboConeCones;
  float m_PlaceboConeStrength;

  bool m_PlaceboDitherEnabled;
  int m_PlaceboDitherMethod;
  int m_PlaceboDitherLutSize;
  bool m_PlaceboDitherTemporal;
  int m_PlaceboDitherTransfer;

  float m_PlaceboToneConstantExposure;
  float m_PlaceboToneConstantKneeAdaptation;
  float m_PlaceboToneConstantKneeDefault;
  float m_PlaceboToneConstantKneeMaximum;
  float m_PlaceboToneConstantKneeMinimum;
  float m_PlaceboToneConstantKneeOffset;
  float m_PlaceboToneConstantLinearKnee;
  float m_PlaceboToneConstantReinhardContrast;
  float m_PlaceboToneConstantSlopeOffset;
  float m_PlaceboToneConstantSlopeTuning;
  float m_PlaceboToneConstantSplineContrast;

  bool m_PlaceboColorMapVisualizeLut;
  float m_PlaceboColorMapVisualizeRectX0;
  float m_PlaceboColorMapVisualizeRectX1;
  float m_PlaceboColorMapVisualizeRectY0;
  float m_PlaceboColorMapVisualizeRectY1;
  float m_PlaceboColorMapVisualizeHue;
  float m_PlaceboColorMapVisualizeTheta;

  float m_PlaceboGamutConstantsColorimetricGamma;
  float m_PlaceboGamutConstantsPerceptualDeadzone;
  float m_PlaceboGamutConstantsPerceptualStrength;
  float m_PlaceboGamutConstantsSoftclipDesat;
  float m_PlaceboGamutConstantsSoftclipKnee;

  std::shared_ptr<const pl_custom_lut> m_PlaceboLut = nullptr;
  int m_PlaceboLutType;
  float m_PlaceboAntiringingStrength;
  bool m_PlaceboCorrectSubpixelOffset;
  bool m_PlaceboDisableBuiltinScalers;
  bool m_PlaceboDisableDitherGammaCorrection;
  bool m_PlaceboDisableLinearScaling;
  bool m_PlaceboDynamicConstant;
  int m_PlaceboErrorDiffusion;
  bool m_PlaceboForceDither;
  bool m_PlaceboForceLowBitDepthFbos;
  bool m_PlaceboIgnoreIccProfiles;
  bool m_PlaceboPreserveMixingCache;
  bool m_PlaceboSkipAntiAliasing;
  bool m_PlaceboSkipCachingSingleFrame;
  bool m_PlaceboSkipTargetClearing;

  //static constexpr std::size_t MAX_NUMBER_OF_SHADERS = 16;
  std::vector<bool> m_PlaceboShadersEnabled;
  std::vector<std::string> m_PlaceboShadersFilename;
  std::vector<std::vector<CShaderParam>> m_PlaceboShadersParams;
  CPlaceboShaders m_PlaceboShadersHooks;

  PlOptionsWrapper* m_placeboOptions;

  // SDR overrides when using SDR source in HDR mode, mostly anything affecting colorspace conversion and 
  // tone mapping, but also saturation as it can be used to mitigate desaturation from gamut mapping.
  //float m_PlaceboTest;
  float m_PlaceboSdrSaturation;
  bool m_PlaceboSdrColorMapInverseToneMapping;
  bool m_PlaceboSdrColorMapGamutExpansion;
  int m_PlaceboSdrColorMapIntent;
  int m_PlaceboSdrColorMapGamutMapping;
  int m_PlaceboSdrColorMapToneMapping;
  float m_PlaceboSdrColorMapToneMapParameter; //cl only used transitively

  float m_PlaceboSdrToneConstantExposure;
  float m_PlaceboSdrToneConstantKneeAdaptation;
  float m_PlaceboSdrToneConstantKneeDefault;
  float m_PlaceboSdrToneConstantKneeMaximum;
  float m_PlaceboSdrToneConstantKneeMinimum;
  float m_PlaceboSdrToneConstantKneeOffset;
  float m_PlaceboSdrToneConstantLinearKnee;
  float m_PlaceboSdrToneConstantReinhardContrast;
  float m_PlaceboSdrToneConstantSlopeOffset;
  float m_PlaceboSdrToneConstantSlopeTuning;
  float m_PlaceboSdrToneConstantSplineContrast;
  float m_PlaceboSdrToneConstantParam0;
  float m_PlaceboSdrToneConstantParam1;
  float m_PlaceboSdrToneConstantParam2;
  float m_PlaceboSdrToneConstantParam3;
  float m_PlaceboSdrToneConstantParam4;
  float m_PlaceboSdrToneConstantParam5;
  float m_PlaceboSdrToneConstantParam6;

  float m_PlaceboSdrGamutConstantsColorimetricGamma;
  float m_PlaceboSdrGamutConstantsPerceptualDeadzone;
  float m_PlaceboSdrGamutConstantsPerceptualStrength;
  float m_PlaceboSdrGamutConstantsSoftclipDesat;
  float m_PlaceboSdrGamutConstantsSoftclipKnee;

  // Unsaved values
  bool m_PlaceboDebugOsd = false;
  bool m_PlaceboVideoDebugOsd = false;  
  bool m_PlaceboDebugHide = false;  

  std::optional<bool>
      m_isDefaultVideoSettings; //!< true: default video settings, false: video specific
};

class CCriticalSection;
class CVideoSettingsLocked
{
public:
  CVideoSettingsLocked(CVideoSettings &vs, CCriticalSection &critSection);
  virtual ~CVideoSettingsLocked() = default;

  CVideoSettingsLocked(CVideoSettingsLocked const &) = delete;
  void operator=(CVideoSettingsLocked const &x) = delete;

  void SetSubtitleStream(int stream);
  void SetSubtitleVisible(bool visible);
  void SetAudioStream(int stream);
  void SetVideoStream(int stream);
  void SetAudioDelay(float delay);
  void SetSubtitleDelay(float delay);

  /*!
   * \brief Set the subtitle vertical position,
   * it depends on current screen resolution
   * \param value The subtitle position in pixels
   * \param save If true, the value will be saved to resolution info
   */
  void SetSubtitleVerticalPosition(int value, bool save);

  void SetViewMode(int mode, float zoom, float par, float shift, bool stretch);
  void SetVolumeAmplification(float amp);

protected:
  CVideoSettings &m_videoSettings;
  CCriticalSection &m_critSection;
};
