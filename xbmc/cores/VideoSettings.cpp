/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "VideoSettings.h"

#include "..\VideoPlayer\VideoRenderers\windows\RendererPL.h"
#include "threads/CriticalSection.h"


#include "VideoRenderers/LibPlacebo/PlHelper.h"
#include "VideoRenderers/LibPlacebo/PlOptionsWrapper.h"
#include <mutex>

CVideoSettings::~CVideoSettings()
{
  delete m_placeboOptions;
  m_placeboOptions = nullptr;
}

//Copy constructor
CVideoSettings::CVideoSettings(const CVideoSettings& other)
{
  m_placeboOptions = new PlOptionsWrapper();
  copy(other);
}

//Copy assignment operator
CVideoSettings& CVideoSettings::operator=(const CVideoSettings& other)
{
  if (this != &other) {
	if (m_placeboOptions)
	{
	  delete m_placeboOptions; // cl needed? destructor doesn't change on content...
	}
	m_placeboOptions = new PlOptionsWrapper();
	copy(other);
  }
  return *this;
}


void CVideoSettings::copy(const CVideoSettings& other)
{
  // Kodi video settings
  m_InterlaceMethod = other.m_InterlaceMethod;
  m_ScalingMethod = other.m_ScalingMethod;
  m_ViewMode = other.m_ViewMode;
  m_CustomZoomAmount = other.m_CustomZoomAmount;
  m_CustomPixelRatio = other.m_CustomPixelRatio;
  m_CustomVerticalShift = other.m_CustomVerticalShift;
  m_CustomNonLinStretch = other.m_CustomNonLinStretch;
  m_AudioStream = other.m_AudioStream;
  m_SubtitleStream = other.m_SubtitleStream;
  m_SubtitleDelay = other.m_SubtitleDelay;
  m_subtitleVerticalPosition = other.m_subtitleVerticalPosition;
  m_subtitleVerticalPositionSave = other.m_subtitleVerticalPositionSave;
  m_SubtitleOn = other.m_SubtitleOn;
  m_Brightness = other.m_Brightness;
  m_Contrast = other.m_Contrast;
  m_Gamma = other.m_Gamma;
  m_Sharpness = other.m_Sharpness;
  m_NoiseReduction = other.m_NoiseReduction;
  m_PostProcess = other.m_PostProcess;
  m_VolumeAmplification = other.m_VolumeAmplification;
  m_AudioDelay = other.m_AudioDelay;
  m_ResumeTime = other.m_ResumeTime;
  m_StereoMode = other.m_StereoMode;
  m_StereoInvert = other.m_StereoInvert;
  m_VideoStream = other.m_VideoStream;
  m_ToneMapMethod = other.m_ToneMapMethod;
  m_ToneMapParam = other.m_ToneMapParam;
  m_Orientation = other.m_Orientation;
  m_CenterMixLevel = other.m_CenterMixLevel;

  m_PlaceboSkinZoom = other.m_PlaceboSkinZoom;
  m_PlaceboSkinZoomHint = other.m_PlaceboSkinZoomHint;
  m_PlaceboSkinZoomPosition = other.m_PlaceboSkinZoomPosition;

  // LibPLacebo specific CVideoSettings only video settings
  m_PlaceboShaderApply = other.m_PlaceboShaderApply;
  m_PlaceboUseHdrForSdr = other.m_PlaceboUseHdrForSdr;
  m_PlaceboSdrSaturation = other.m_PlaceboSdrSaturation;
  //m_PlaceboTest = other.m_PlaceboTest;
  m_PlaceboSdrColorMapInverseToneMapping = other.m_PlaceboSdrColorMapInverseToneMapping;
  m_PlaceboSdrColorMapGamutExpansion = other.m_PlaceboSdrColorMapGamutExpansion;
  m_PlaceboSdrColorMapIntent = other.m_PlaceboSdrColorMapIntent;
  m_PlaceboSdrColorMapGamutMapping = other.m_PlaceboSdrColorMapGamutMapping;
  m_PlaceboSdrColorMapToneMapping = other.m_PlaceboSdrColorMapToneMapping;
  m_PlaceboSdrToneConstantExposure = other.m_PlaceboSdrToneConstantExposure;
  m_PlaceboSdrToneConstantKneeAdaptation = other.m_PlaceboSdrToneConstantKneeAdaptation;
  m_PlaceboSdrToneConstantKneeDefault = other.m_PlaceboSdrToneConstantKneeDefault;
  m_PlaceboSdrToneConstantKneeMaximum = other.m_PlaceboSdrToneConstantKneeMaximum;
  m_PlaceboSdrToneConstantKneeMinimum = other.m_PlaceboSdrToneConstantKneeMinimum;
  m_PlaceboSdrToneConstantKneeOffset = other.m_PlaceboSdrToneConstantKneeOffset;
  m_PlaceboSdrToneConstantLinearKnee = other.m_PlaceboSdrToneConstantLinearKnee;
  m_PlaceboSdrToneConstantReinhardContrast = other.m_PlaceboSdrToneConstantReinhardContrast;
  m_PlaceboSdrToneConstantSlopeOffset = other.m_PlaceboSdrToneConstantSlopeOffset;
  m_PlaceboSdrToneConstantSlopeTuning = other.m_PlaceboSdrToneConstantSlopeTuning;
  m_PlaceboSdrToneConstantSplineContrast = other.m_PlaceboSdrToneConstantSplineContrast;
  m_PlaceboSdrToneConstantParam0 = other.m_PlaceboSdrToneConstantParam0;
  m_PlaceboSdrToneConstantParam1 = other.m_PlaceboSdrToneConstantParam1;
  m_PlaceboSdrToneConstantParam2 = other.m_PlaceboSdrToneConstantParam2;
  m_PlaceboSdrToneConstantParam3 = other.m_PlaceboSdrToneConstantParam3;
  m_PlaceboSdrToneConstantParam4 = other.m_PlaceboSdrToneConstantParam4;
  m_PlaceboSdrToneConstantParam5 = other.m_PlaceboSdrToneConstantParam5;
  m_PlaceboSdrToneConstantParam6 = other.m_PlaceboSdrToneConstantParam6;
  m_PlaceboSdrGamutConstantsColorimetricGamma = other.m_PlaceboSdrGamutConstantsColorimetricGamma;
  m_PlaceboSdrGamutConstantsPerceptualDeadzone = other.m_PlaceboSdrGamutConstantsPerceptualDeadzone;
  m_PlaceboSdrGamutConstantsPerceptualStrength = other.m_PlaceboSdrGamutConstantsPerceptualStrength;
  m_PlaceboSdrGamutConstantsSoftclipDesat = other.m_PlaceboSdrGamutConstantsSoftclipDesat;
  m_PlaceboSdrGamutConstantsSoftclipKnee = other.m_PlaceboSdrGamutConstantsSoftclipKnee;

  m_PlaceboDisplayHdrPeakLuminance = other.m_PlaceboDisplayHdrPeakLuminance;
  m_PlaceboDisplaySdrPeakLuminance = other.m_PlaceboDisplaySdrPeakLuminance;
  m_PlaceboTargetContrast = other.m_PlaceboTargetContrast;
  m_PlaceboNvRtxPipelineEnabled = other.m_PlaceboNvRtxPipelineEnabled;
  m_PlaceboNvSuperResolutionEnabled = other.m_PlaceboNvSuperResolutionEnabled;
  m_PlaceboNvRtxHdrEnabled = other.m_PlaceboNvRtxHdrEnabled;
  m_PlaceboNvRtxDisableScalers = other.m_PlaceboNvRtxDisableScalers;
  m_PlaceboSdrTargetContrast = other.m_PlaceboSdrTargetContrast;
  m_PlaceboTargetColorspaceHint = other.m_PlaceboTargetColorspaceHint;
  m_PlaceboTargetColorspaceHintMode = other.m_PlaceboTargetColorspaceHintMode;
  m_PlaceboDitherDepth = other.m_PlaceboDitherDepth;
  m_PlaceboLutFilename = other.m_PlaceboLutFilename;
  m_PlaceboLut = other.m_PlaceboLut;
  m_PlaceboShadersEnabled = other.m_PlaceboShadersEnabled;
  m_PlaceboShadersFilename = other.m_PlaceboShadersFilename;
  m_PlaceboShadersParams = other.m_PlaceboShadersParams;
  m_PlaceboShadersHooks = other.m_PlaceboShadersHooks;
  m_PlaceboFrameMixerRadiusFactor = other.m_PlaceboFrameMixerRadiusFactor;
  m_PlaceboFrameMixerBypassQueue = other.m_PlaceboFrameMixerBypassQueue;
  m_PlaceboCropBottom = other.m_PlaceboCropBottom;
  m_PlaceboBrightnessSdrSdr = other.m_PlaceboBrightnessSdrSdr;
  m_PlaceboContrastSdrSdr   = other.m_PlaceboContrastSdrSdr;
  m_PlaceboBrightnessHdrHdr = other.m_PlaceboBrightnessHdrHdr;
  m_PlaceboContrastHdrHdr   = other.m_PlaceboContrastHdrHdr;
  m_PlaceboBrightnessHdrSdr = other.m_PlaceboBrightnessHdrSdr;
  m_PlaceboContrastHdrSdr   = other.m_PlaceboContrastHdrSdr;
  m_PlaceboBrightnessSdrHdr = other.m_PlaceboBrightnessSdrHdr;
  m_PlaceboContrastSdrHdr   = other.m_PlaceboContrastSdrHdr;

  // Shallow copy and deep copy of m_placeboOptions content
  if (m_placeboOptions && other.m_placeboOptions)
  {
	m_placeboOptions->DeepCopy(*other.m_placeboOptions);
  }

  CPLHelper::UpdateVideoSettingsFromLibPLaceboParams(*this);
}


CVideoSettings::CVideoSettings()
{

  // Kodi video settings
  m_InterlaceMethod = VS_INTERLACEMETHOD_AUTO;
  m_ScalingMethod = VS_SCALINGMETHOD_LINEAR;
  m_ViewMode = ViewModeNormal;
  m_CustomZoomAmount = 1.0f;
  m_CustomPixelRatio = 1.0f;
  m_CustomVerticalShift = 0.0f;
  m_CustomNonLinStretch = false;
  m_AudioStream = -1;
  m_SubtitleStream = -1;
  m_SubtitleDelay = 0.0f;
  m_subtitleVerticalPosition = 0;
  m_subtitleVerticalPositionSave = false;
  m_SubtitleOn = true;
  m_Brightness = 50.0f;
  m_Contrast = 50.0f;
  m_Gamma = 20.0f;
  m_Sharpness = 0.0f;
  m_NoiseReduction = 0;
  m_PostProcess = false;
  m_VolumeAmplification = 0;
  m_AudioDelay = 0.0f;
  m_ResumeTime = 0;
  m_StereoMode = 0;
  m_StereoInvert = false;
  m_VideoStream = -1;
  m_ToneMapMethod = VS_TONEMAPMETHOD_OFF;
  m_ToneMapParam = 1.0f;
  m_Orientation = 0;
  m_CenterMixLevel = 0;

  m_PlaceboSkinZoom = 0;
  m_PlaceboSkinZoomHint = 0;
  m_PlaceboSkinZoomPosition = VS_SZ_POSITION_UPPER_LEFT;

  // LibPLacebo specific video settings
  m_PlaceboUseHdrForSdr = false;
  m_PlaceboDisplayHdrPeakLuminance = 700.0;
  m_PlaceboShaderApply = true;
  m_PlaceboTargetColorspaceHint = (int)SettinglibPlaceboTargetColorspaceHint::AUTO;
  m_PlaceboTargetColorspaceHintMode = (int)SettinglibPlaceboTargetColorspaceHintMode::TARGET;
  m_PlaceboLutFilename = "";
  m_PlaceboLut = nullptr;
  m_PlaceboFrameMixerRadiusFactor = 1.0;
  m_PlaceboFrameMixerBypassQueue = true;
  m_PlaceboCropBottom = 0;                    //cl reset these settings along with libplacebo reset to defaults ????
  m_PlaceboBrightnessSdrSdr = 50.0f;
  m_PlaceboContrastSdrSdr   = 50.0f;
  m_PlaceboBrightnessHdrHdr = 50.0f;
  m_PlaceboContrastHdrHdr   = 50.0f;
  m_PlaceboBrightnessHdrSdr = 50.0f;
  m_PlaceboContrastHdrSdr   = 50.0f;
  m_PlaceboBrightnessSdrHdr = 50.0f;
  m_PlaceboContrastSdrHdr   = 50.0f;

  m_PlaceboShadersEnabled = {};
  m_PlaceboShadersFilename = {};
  m_PlaceboShadersParams = {};

  m_placeboOptions = new PlOptionsWrapper();
  ResetRenderSettings();
  CPLHelper::UpdateVideoSettingsFromLibPLaceboParams(*this);


}

void CVideoSettings::ResetRenderSettings(PlOptionsWrapper::reset_type type)
{
  m_placeboOptions->resetPlOptions(type);

  // Reset only the placebo options //cl rethink the whole VideoSettings thing ???
  ResetDitherSettings(type);
  ResetSdrToHdrSettings(type);
}

void CVideoSettings::ResetDitherSettings(PlOptionsWrapper::reset_type type)
{
  // Reset only the placebo options //cl rethink the whole thing with settings...
  m_PlaceboDitherDepth = 0; //cl 0=auto
}

void CVideoSettings::ResetSdrToHdrSettings(PlOptionsWrapper::reset_type type)
{
  // Overriding SDR default values for SDR to HDR mapping
  m_PlaceboDisplaySdrPeakLuminance = 480;
  m_PlaceboTargetContrast = -1;
  m_PlaceboNvRtxPipelineEnabled = (int) SettinglibPlaceboNvRtxPipelineEnabled::AUTO;
  m_PlaceboNvSuperResolutionEnabled = false;
  m_PlaceboNvRtxHdrEnabled = false;
  m_PlaceboNvRtxDisableScalers = true;
  m_PlaceboSdrTargetContrast = -1;
  m_PlaceboSdrSaturation = 57.0; 
  //m_PlaceboTest = 0.0f;
  m_PlaceboSdrColorMapInverseToneMapping = true;
  m_PlaceboSdrColorMapGamutExpansion = true;
  m_PlaceboSdrColorMapIntent = PL_INTENT_PERCEPTUAL; // PL_INTENT_SATURATION PL_INTENT_PERCEPTUAL
  m_PlaceboSdrColorMapGamutMapping = CPLHelper::getGamutMapIndexFromDescription("Perceptual mapping");   //cl "Perceptual mapping", "Saturation mapping"
  m_PlaceboSdrColorMapToneMapping = CPLHelper::getToneMapIndexFromDescription("Custom Spline"); //cl the ones supporting inverse (should filter?):"ITU-R BT.2446 Method A", "Single-pivot polynomial spline", "Perceptually linear stretch", "Linear light stretch", "Custom spline"
  m_PlaceboSdrToneConstantParam0 = 74.0;
  m_PlaceboSdrToneConstantParam1 = 4.0;
  m_PlaceboSdrToneConstantParam2 = 0.065;
  m_PlaceboSdrToneConstantParam3 = 0.33;
  m_PlaceboSdrToneConstantParam4 = 1.65;
  m_PlaceboSdrToneConstantParam5 = 0.375;
  m_PlaceboSdrToneConstantParam6 = -20.0;

  // Just use defaults for the other ones
  static bool bInit = false;
  static PlOptionsWrapper* opt;
  if(!bInit)
  {
    opt = new PlOptionsWrapper(); // get defaults, function could be called from other places than constructor, do it only once.
    bInit = true;
  }

  m_PlaceboSdrToneConstantExposure = opt->getPlOptions()->color_map_params.tone_constants.exposure;
  m_PlaceboSdrToneConstantKneeAdaptation = opt->getPlOptions()->color_map_params.tone_constants.knee_adaptation;
  m_PlaceboSdrToneConstantKneeDefault = opt->getPlOptions()->color_map_params.tone_constants.knee_default;
  m_PlaceboSdrToneConstantKneeMaximum = opt->getPlOptions()->color_map_params.tone_constants.knee_maximum;
  m_PlaceboSdrToneConstantKneeMinimum = opt->getPlOptions()->color_map_params.tone_constants.knee_minimum;
  m_PlaceboSdrToneConstantKneeOffset = opt->getPlOptions()->color_map_params.tone_constants.knee_offset;
  m_PlaceboSdrToneConstantLinearKnee = opt->getPlOptions()->color_map_params.tone_constants.linear_knee;
  m_PlaceboSdrToneConstantReinhardContrast = opt->getPlOptions()->color_map_params.tone_constants.reinhard_contrast;
  m_PlaceboSdrToneConstantSlopeOffset = opt->getPlOptions()->color_map_params.tone_constants.slope_offset;
  m_PlaceboSdrToneConstantSlopeTuning = opt->getPlOptions()->color_map_params.tone_constants.slope_tuning;
  m_PlaceboSdrToneConstantSplineContrast = opt->getPlOptions()->color_map_params.tone_constants.spline_contrast;

  m_PlaceboSdrGamutConstantsColorimetricGamma = opt->getPlOptions()->color_map_params.gamut_constants.colorimetric_gamma;
  m_PlaceboSdrGamutConstantsPerceptualDeadzone = opt->getPlOptions()->color_map_params.gamut_constants.perceptual_deadzone;
  m_PlaceboSdrGamutConstantsPerceptualStrength = opt->getPlOptions()->color_map_params.gamut_constants.perceptual_strength;
  m_PlaceboSdrGamutConstantsSoftclipDesat = opt->getPlOptions()->color_map_params.gamut_constants.softclip_desat;
  m_PlaceboSdrGamutConstantsSoftclipKnee = opt->getPlOptions()->color_map_params.gamut_constants.softclip_knee;
}




bool CVideoSettings::operator!=(const CVideoSettings& right) const
{
  if (m_InterlaceMethod != right.m_InterlaceMethod) return true;
  if (m_ScalingMethod != right.m_ScalingMethod) return true;
  if (m_ViewMode != right.m_ViewMode) return true;
  if (m_CustomZoomAmount != right.m_CustomZoomAmount) return true;
  if (m_CustomPixelRatio != right.m_CustomPixelRatio) return true;
  if (m_CustomVerticalShift != right.m_CustomVerticalShift) return true;
  if (m_CustomNonLinStretch != right.m_CustomNonLinStretch) return true;
  if (m_AudioStream != right.m_AudioStream) return true;
  if (m_SubtitleStream != right.m_SubtitleStream) return true;
  if (m_SubtitleDelay != right.m_SubtitleDelay) return true;
  //cl m_subtitleVerticalPosition+save used here in comparison but not stored in database, which means every file that 
  // eventually displays subtitles will change these value and result in unchanged settings being stored in the database, 
  // without the changed fields...To investigate further
  if (m_subtitleVerticalPositionSave == true)
	if (m_subtitleVerticalPosition != right.m_subtitleVerticalPosition) return true;
  //if (m_subtitleVerticalPositionSave != right.m_subtitleVerticalPositionSave) return true;
  if (m_SubtitleOn != right.m_SubtitleOn) return true;
  if (m_Brightness != right.m_Brightness) return true;
  if (m_Contrast != right.m_Contrast) return true;
  if (m_Gamma != right.m_Gamma) return true;
  if (m_Sharpness != right.m_Sharpness) return true;
  if (m_NoiseReduction != right.m_NoiseReduction) return true;
  if (m_PostProcess != right.m_PostProcess) return true;
  if (m_VolumeAmplification != right.m_VolumeAmplification) return true;
  if (m_AudioDelay != right.m_AudioDelay) return true;
  if (m_ResumeTime != right.m_ResumeTime) return true;
  if (m_StereoMode != right.m_StereoMode) return true;
  if (m_StereoInvert != right.m_StereoInvert) return true;
  if (m_VideoStream != right.m_VideoStream) return true;
  if (m_ToneMapMethod != right.m_ToneMapMethod) return true;
  if (m_ToneMapParam != right.m_ToneMapParam) return true;
  if (m_Orientation != right.m_Orientation) return true;
  if (m_CenterMixLevel != right.m_CenterMixLevel) return true;

  if (m_PlaceboSkinZoom != right.m_PlaceboSkinZoom) return true;
  //if (m_PlaceboSkinZoomHint != right.m_PlaceboSkinZoomHint) return true; //No!
  if(m_PlaceboSkinZoomPosition != right.m_PlaceboSkinZoomPosition) return true;

  if (m_PlaceboShaderApply != right.m_PlaceboShaderApply) return true;
  if (m_PlaceboUseHdrForSdr != right.m_PlaceboUseHdrForSdr) return true;  

  if(m_PlaceboSdrSaturation != right.m_PlaceboSdrSaturation) return true;
  //if(m_PlaceboTest != right.m_PlaceboTest) return true;
  if(m_PlaceboSdrColorMapInverseToneMapping != right.m_PlaceboSdrColorMapInverseToneMapping) return true;
  if(m_PlaceboSdrColorMapGamutExpansion != right.m_PlaceboSdrColorMapGamutExpansion) return true;
  if(m_PlaceboSdrColorMapIntent != right.m_PlaceboSdrColorMapIntent) return true;
  if(m_PlaceboSdrColorMapGamutMapping != right.m_PlaceboSdrColorMapGamutMapping) return true;
  if(m_PlaceboSdrColorMapToneMapping != right.m_PlaceboSdrColorMapToneMapping) return true;
  if(m_PlaceboSdrToneConstantExposure != right.m_PlaceboSdrToneConstantExposure) return true;
  if(m_PlaceboSdrToneConstantKneeAdaptation != right.m_PlaceboSdrToneConstantKneeAdaptation) return true;
  if(m_PlaceboSdrToneConstantKneeDefault != right.m_PlaceboSdrToneConstantKneeDefault) return true;
  if(m_PlaceboSdrToneConstantKneeMaximum != right.m_PlaceboSdrToneConstantKneeMaximum) return true;
  if(m_PlaceboSdrToneConstantKneeMinimum != right.m_PlaceboSdrToneConstantKneeMinimum) return true;
  if(m_PlaceboSdrToneConstantKneeOffset != right.m_PlaceboSdrToneConstantKneeOffset) return true;
  if(m_PlaceboSdrToneConstantLinearKnee != right.m_PlaceboSdrToneConstantLinearKnee) return true;
  if(m_PlaceboSdrToneConstantReinhardContrast != right.m_PlaceboSdrToneConstantReinhardContrast) return true;
  if(m_PlaceboSdrToneConstantSlopeOffset != right.m_PlaceboSdrToneConstantSlopeOffset) return true;
  if(m_PlaceboSdrToneConstantSlopeTuning != right.m_PlaceboSdrToneConstantSlopeTuning) return true;
  if(m_PlaceboSdrToneConstantSplineContrast != right.m_PlaceboSdrToneConstantSplineContrast) return true;
  if(m_PlaceboSdrToneConstantParam0 != right.m_PlaceboSdrToneConstantParam0) return true;
  if(m_PlaceboSdrToneConstantParam1 != right.m_PlaceboSdrToneConstantParam1) return true;
  if(m_PlaceboSdrToneConstantParam2 != right.m_PlaceboSdrToneConstantParam2) return true;
  if(m_PlaceboSdrToneConstantParam3 != right.m_PlaceboSdrToneConstantParam3) return true;
  if(m_PlaceboSdrToneConstantParam4 != right.m_PlaceboSdrToneConstantParam4) return true;
  if(m_PlaceboSdrToneConstantParam5 != right.m_PlaceboSdrToneConstantParam5) return true;
  if(m_PlaceboSdrToneConstantParam6 != right.m_PlaceboSdrToneConstantParam6) return true;
  if(m_PlaceboSdrGamutConstantsColorimetricGamma != right.m_PlaceboSdrGamutConstantsColorimetricGamma) return true;
  if(m_PlaceboSdrGamutConstantsPerceptualDeadzone != right.m_PlaceboSdrGamutConstantsPerceptualDeadzone) return true;
  if(m_PlaceboSdrGamutConstantsPerceptualStrength != right.m_PlaceboSdrGamutConstantsPerceptualStrength) return true;
  if(m_PlaceboSdrGamutConstantsSoftclipDesat != right.m_PlaceboSdrGamutConstantsSoftclipDesat) return true;
  if(m_PlaceboSdrGamutConstantsSoftclipKnee != right.m_PlaceboSdrGamutConstantsSoftclipKnee) return true;

  if (m_PlaceboDisplayHdrPeakLuminance != right.m_PlaceboDisplayHdrPeakLuminance) return true;
  if (m_PlaceboDisplaySdrPeakLuminance != right.m_PlaceboDisplaySdrPeakLuminance) return true;
  if (m_PlaceboTargetContrast != right.m_PlaceboTargetContrast) return true;
  if (m_PlaceboNvRtxPipelineEnabled != right.m_PlaceboNvRtxPipelineEnabled) return true;
  if (m_PlaceboNvSuperResolutionEnabled != right.m_PlaceboNvSuperResolutionEnabled) return true;
  if (m_PlaceboNvRtxHdrEnabled != right.m_PlaceboNvRtxHdrEnabled) return true;
  if(m_PlaceboNvRtxDisableScalers != right.m_PlaceboNvRtxDisableScalers) return true;
  if (m_PlaceboSdrTargetContrast != right.m_PlaceboSdrTargetContrast) return true;
  if (m_PlaceboTargetColorspaceHint != right.m_PlaceboTargetColorspaceHint) return true;
  if (m_PlaceboTargetColorspaceHintMode != right.m_PlaceboTargetColorspaceHintMode) return true;
  if (m_PlaceboDitherDepth != right.m_PlaceboDitherDepth) return true;
  if (m_PlaceboLutFilename != right.m_PlaceboLutFilename) return true;
  if (m_PlaceboFrameMixerRadiusFactor != right.m_PlaceboFrameMixerRadiusFactor) return true;
  if(m_PlaceboFrameMixerBypassQueue != right.m_PlaceboFrameMixerBypassQueue) return true;
  if(m_PlaceboCropBottom != right.m_PlaceboCropBottom) return true;
  if(m_PlaceboBrightnessSdrSdr != right.m_PlaceboBrightnessSdrSdr ) return true;
  if(m_PlaceboContrastSdrSdr != right.m_PlaceboContrastSdrSdr) return true;
  if(m_PlaceboBrightnessHdrHdr != right.m_PlaceboBrightnessHdrHdr) return true;
  if(m_PlaceboContrastHdrHdr != right.m_PlaceboContrastHdrHdr) return true;
  if(m_PlaceboBrightnessHdrSdr != right.m_PlaceboBrightnessHdrSdr) return true;
  if(m_PlaceboContrastHdrSdr != right.m_PlaceboContrastHdrSdr) return true;
  if(m_PlaceboBrightnessSdrHdr != right.m_PlaceboBrightnessSdrHdr) return true;
  if(m_PlaceboContrastSdrHdr != right.m_PlaceboContrastSdrHdr) return true;

  if (m_PlaceboShadersEnabled != right.m_PlaceboShadersEnabled) return true;
  if (m_PlaceboShadersFilename != right.m_PlaceboShadersFilename) return true;
  if (m_PlaceboShadersParams != right.m_PlaceboShadersParams) return true;

  if (m_PlaceboColorAdjustmentEnabled != right.m_PlaceboColorAdjustmentEnabled) return true;
  if (m_PlaceboSaturation != right.m_PlaceboSaturation) return true;
  if (m_PlaceboHue != right.m_PlaceboHue) return true;
  if (m_PlaceboTemperature != right.m_PlaceboTemperature) return true;

  if (m_PlaceboPeakDetectEnabled != right.m_PlaceboPeakDetectEnabled) return true;
  if (m_PlaceboPeakDetectSmoothingPeriod != right.m_PlaceboPeakDetectSmoothingPeriod) return true;
  if (m_PlaceboPeakDetectSceneThresholdLow != right.m_PlaceboPeakDetectSceneThresholdLow) return true;
  if (m_PlaceboPeakDetectSceneThresholdHigh != right.m_PlaceboPeakDetectSceneThresholdHigh) return true;
  if (m_PlaceboPeakDetectPercentile != right.m_PlaceboPeakDetectPercentile) return true;
  if (m_PlaceboPeakDetectBlackCutoff != right.m_PlaceboPeakDetectBlackCutoff) return true;
  if (m_PlaceboPeakDetectAllowDelayed != right.m_PlaceboPeakDetectAllowDelayed) return true;

  if (m_PlaceboUpscaler != right.m_PlaceboUpscaler) return true;
  if (m_PlaceboDownscaler != right.m_PlaceboDownscaler) return true;
  if (m_PlaceboPlaneUpscaler != right.m_PlaceboPlaneUpscaler) return true;
  if (m_PlaceboPlaneDownscaler != right.m_PlaceboPlaneDownscaler) return true;
  if (m_PlaceboFrameMixer != right.m_PlaceboFrameMixer) return true;

  if (m_PlaceboDebandEnabled != right.m_PlaceboDebandEnabled) return true;
  if (m_PlaceboDebandGrain != right.m_PlaceboDebandGrain) return true;
  if (m_PlaceboDebandGrainNeutral0 != right.m_PlaceboDebandGrainNeutral0) return true;
  if (m_PlaceboDebandGrainNeutral1 != right.m_PlaceboDebandGrainNeutral1) return true;
  if (m_PlaceboDebandGrainNeutral2 != right.m_PlaceboDebandGrainNeutral2) return true;
  if (m_PlaceboDebandIterations != right.m_PlaceboDebandIterations) return true;
  if (m_PlaceboDebandRadius != right.m_PlaceboDebandRadius) return true;
  if (m_PlaceboDebandThreshold != right.m_PlaceboDebandThreshold) return true;

  if (m_PlaceboColorMapEnabled != right.m_PlaceboColorMapEnabled) return true;
  if (m_PlaceboColorMapGamutMapping != right.m_PlaceboColorMapGamutMapping) return true;
  if (m_PlaceboColorMapToneMapping != right.m_PlaceboColorMapToneMapping) return true;
  if (m_PlaceboColorMapContrastRecovery != right.m_PlaceboColorMapContrastRecovery) return true;
  if (m_PlaceboColorMapContrastSmoothness != right.m_PlaceboColorMapContrastSmoothness) return true;
  if (m_PlaceboColorMapGamutExpansion != right.m_PlaceboColorMapGamutExpansion) return true;
  if (m_PlaceboColorMapInverseToneMapping != right.m_PlaceboColorMapInverseToneMapping) return true;
  if (m_PlaceboColorMapLut3dSizeI != right.m_PlaceboColorMapLut3dSizeI) return true;
  if (m_PlaceboColorMapLut3dSizeC != right.m_PlaceboColorMapLut3dSizeC) return true;
  if (m_PlaceboColorMapLut3dSizeH != right.m_PlaceboColorMapLut3dSizeH) return true;
  if (m_PlaceboColorMapLut3dTricubic != right.m_PlaceboColorMapLut3dTricubic) return true;
  if (m_PlaceboColorMapLutSize != right.m_PlaceboColorMapLutSize) return true;
  if (m_PlaceboColorMapShowClipping != right.m_PlaceboColorMapShowClipping) return true;
  if (m_PlaceboColorMapIntent != right.m_PlaceboColorMapIntent) return true;
  if (m_PlaceboColorMapForceToneMappingLut != right.m_PlaceboColorMapForceToneMappingLut) return true;

  if (m_PlaceboDeinterlaceEnabled != right.m_PlaceboDeinterlaceEnabled) return true;
  if (m_PlaceboDeinterlaceAlgo != right.m_PlaceboDeinterlaceAlgo) return true;
  if (m_PlaceboDeinterlaceSkipSpatialCheck != right.m_PlaceboDeinterlaceSkipSpatialCheck) return true;

  if (m_PlaceboSigmoidEnabled != right.m_PlaceboSigmoidEnabled) return true;
  if (m_PlaceboSigmoidCenter != right.m_PlaceboSigmoidCenter) return true;
  if (m_PlaceboSigmoidSlope != right.m_PlaceboSigmoidSlope) return true;

  if (m_PlaceboConeEnabled != right.m_PlaceboConeEnabled) return true;
  if (m_PlaceboConeCones != right.m_PlaceboConeCones) return true;
  if (m_PlaceboConeStrength != right.m_PlaceboConeStrength) return true;

  if (m_PlaceboDitherEnabled != right.m_PlaceboDitherEnabled) return true;
  if (m_PlaceboDitherMethod != right.m_PlaceboDitherMethod) return true;
  if (m_PlaceboDitherLutSize != right.m_PlaceboDitherLutSize) return true;
  if (m_PlaceboDitherTemporal != right.m_PlaceboDitherTemporal) return true;
  if (m_PlaceboDitherTransfer != right.m_PlaceboDitherTransfer) return true;

  if (m_PlaceboToneConstantExposure != right.m_PlaceboToneConstantExposure) return true;
  if (m_PlaceboToneConstantKneeAdaptation != right.m_PlaceboToneConstantKneeAdaptation) return true;
  if (m_PlaceboToneConstantKneeDefault != right.m_PlaceboToneConstantKneeDefault) return true;
  if (m_PlaceboToneConstantKneeMaximum != right.m_PlaceboToneConstantKneeMaximum) return true;
  if (m_PlaceboToneConstantKneeMinimum != right.m_PlaceboToneConstantKneeMinimum) return true;
  if (m_PlaceboToneConstantKneeOffset != right.m_PlaceboToneConstantKneeOffset) return true;
  if (m_PlaceboToneConstantLinearKnee != right.m_PlaceboToneConstantLinearKnee) return true;
  if (m_PlaceboToneConstantReinhardContrast != right.m_PlaceboToneConstantReinhardContrast) return true;
  if (m_PlaceboToneConstantSlopeOffset != right.m_PlaceboToneConstantSlopeOffset) return true;
  if (m_PlaceboToneConstantSlopeTuning != right.m_PlaceboToneConstantSlopeTuning) return true;
  if (m_PlaceboToneConstantSplineContrast != right.m_PlaceboToneConstantSplineContrast) return true;

  if (m_PlaceboGamutConstantsColorimetricGamma != right.m_PlaceboGamutConstantsColorimetricGamma) return true;
  if (m_PlaceboGamutConstantsPerceptualDeadzone != right.m_PlaceboGamutConstantsPerceptualDeadzone) return true;
  if(m_PlaceboGamutConstantsPerceptualStrength != right.m_PlaceboGamutConstantsPerceptualStrength) return true;
  if (m_PlaceboGamutConstantsSoftclipDesat != right.m_PlaceboGamutConstantsSoftclipDesat) return true;
  if (m_PlaceboGamutConstantsSoftclipKnee != right.m_PlaceboGamutConstantsSoftclipKnee) return true;

  if (m_PlaceboColorMapVisualizeLut != right.m_PlaceboColorMapVisualizeLut) return true;
  if (m_PlaceboColorMapVisualizeRectX0 != right.m_PlaceboColorMapVisualizeRectX0) return true;
  if (m_PlaceboColorMapVisualizeRectX1 != right.m_PlaceboColorMapVisualizeRectX1) return true;
  if (m_PlaceboColorMapVisualizeRectY0 != right.m_PlaceboColorMapVisualizeRectY0) return true;
  if (m_PlaceboColorMapVisualizeRectY1 != right.m_PlaceboColorMapVisualizeRectY1) return true;
  if (m_PlaceboColorMapVisualizeHue != right.m_PlaceboColorMapVisualizeHue) return true;
  if (m_PlaceboColorMapVisualizeTheta != right.m_PlaceboColorMapVisualizeTheta) return true;

  if (m_PlaceboLutType != right.m_PlaceboLutType) return true;
  if (m_PlaceboAntiringingStrength != right.m_PlaceboAntiringingStrength) return true;
  if (m_PlaceboCorrectSubpixelOffset != right.m_PlaceboCorrectSubpixelOffset) return true;
  if (m_PlaceboDisableBuiltinScalers != right.m_PlaceboDisableBuiltinScalers) return true;
  if (m_PlaceboDisableDitherGammaCorrection != right.m_PlaceboDisableDitherGammaCorrection) return true;
  if (m_PlaceboDisableLinearScaling != right.m_PlaceboDisableLinearScaling) return true;
  if (m_PlaceboDynamicConstant != right.m_PlaceboDynamicConstant) return true;
  if (m_PlaceboErrorDiffusion != right.m_PlaceboErrorDiffusion) return true;
  if (m_PlaceboForceDither != right.m_PlaceboForceDither) return true;
  if (m_PlaceboForceLowBitDepthFbos != right.m_PlaceboForceLowBitDepthFbos) return true;
  if (m_PlaceboIgnoreIccProfiles != right.m_PlaceboIgnoreIccProfiles) return true;
  if (m_PlaceboPreserveMixingCache != right.m_PlaceboPreserveMixingCache) return true;
  if (m_PlaceboSkipAntiAliasing != right.m_PlaceboSkipAntiAliasing) return true;
  if (m_PlaceboSkipCachingSingleFrame != right.m_PlaceboSkipCachingSingleFrame) return true;
  if(m_PlaceboSkipTargetClearing != right.m_PlaceboSkipTargetClearing) return true;

  return false;
}

//------------------------------------------------------------------------------
// CVideoSettingsLocked
//------------------------------------------------------------------------------
CVideoSettingsLocked::CVideoSettingsLocked(CVideoSettings& vs, CCriticalSection& critSection) :
  m_videoSettings(vs), m_critSection(critSection)
{
}

void CVideoSettingsLocked::SetSubtitleStream(int stream)
{
  std::unique_lock lock(m_critSection);
  m_videoSettings.m_SubtitleStream = stream;
}

void CVideoSettingsLocked::SetSubtitleVisible(bool visible)
{
  std::unique_lock lock(m_critSection);
  m_videoSettings.m_SubtitleOn = visible;
}

void CVideoSettingsLocked::SetAudioStream(int stream)
{
  std::unique_lock lock(m_critSection);
  m_videoSettings.m_AudioStream = stream;
}

void CVideoSettingsLocked::SetVideoStream(int stream)
{
  std::unique_lock lock(m_critSection);
  m_videoSettings.m_VideoStream = stream;
}

void CVideoSettingsLocked::SetAudioDelay(float delay)
{
  std::unique_lock lock(m_critSection);
  m_videoSettings.m_AudioDelay = delay;
}

void CVideoSettingsLocked::SetSubtitleDelay(float delay)
{
  std::unique_lock lock(m_critSection);
  m_videoSettings.m_SubtitleDelay = delay;
}

void CVideoSettingsLocked::SetSubtitleVerticalPosition(int value, bool save)
{
  std::unique_lock lock(m_critSection);
  m_videoSettings.m_subtitleVerticalPosition = value;
  m_videoSettings.m_subtitleVerticalPositionSave = save;
}

void CVideoSettingsLocked::SetViewMode(int mode, float zoom, float par, float shift, bool stretch)
{
  std::unique_lock lock(m_critSection);
  m_videoSettings.m_ViewMode = mode;
  m_videoSettings.m_CustomZoomAmount = zoom;
  m_videoSettings.m_CustomPixelRatio = par;
  m_videoSettings.m_CustomVerticalShift = shift;
  m_videoSettings.m_CustomNonLinStretch = stretch;
}

void CVideoSettingsLocked::SetVolumeAmplification(float amp)
{
  std::unique_lock lock(m_critSection);
  m_videoSettings.m_VolumeAmplification = amp;
}
