/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "GUIDialogVideoSettings.h"

#include "GUIPassword.h"
#include "ServiceBroker.h"
#include "addons/Skin.h"
#include "application/ApplicationPlayer.h"
#include "cores/VideoPlayer/VideoRenderers/BaseRenderer.h"
#include "cores\VideoPlayer\VideoRenderers\windows\RendererPL.h"
#include "dialogs/GUIDialogFileBrowser.h"
#include "dialogs/GUIDialogYesNo.h"
#include "filesystem/file.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIWindowManager.h"
#include "libplacebo/options.h"
#include "profiles/ProfileManager.h"
#include "resources/LocalizeStrings.h"
#include "resources/ResourcesComponent.h"
#include "settings/MediaSettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "settings/lib/Setting.h"
#include "settings/lib/SettingDefinitions.h"
#include "settings/lib/SettingsManager.h"
#include "utils/ComponentContainer.h"
#include "utils/LangCodeExpander.h"
#include "utils/StringUtils.h"
#include "utils/Variant.h"
#include "utils/log.h"
#include "video/VideoDatabase.h"
#include "video/ViewModeSettings.h"
#include "windowing/WinSystem.h"
#include <guilib/GUIWindow.h>
#include <utils/URIUtils.h>
#include <utils/XBMCtinyxml.h>
#include <GUIUserMessages.h>

#include <GUIPassword.cpp>
#include <Interface/StreamInfo.h>
#include <LockMode.h>
#include <VideoRenderers/LibPlacebo/PlHelper.h>
#include <VideoRenderers/LibPlacebo/PlOptionsWrapper.h>
#include <Windows.h>
#include <cmath>
#include <commons/ilog.h>
#include <cores/IPlayer.h>
#include <cores/VideoSettings.h>
#include <cstdlib>
#include <guilib/GUIKeyboardFactory.h>
#include <guilib/GUIMacros.h>
#include <guilib/GUIMessage.h>
#include <guilib/GUIMessageIDs.h>
#include <guilib/WindowIDs.h>
#include <libavutil/mathematics.h>
#include <libplacebo/colorspace.h>
#include <libplacebo/dither.h>
#include <libplacebo/filters.h>
#include <libplacebo/gamut_mapping.h>
#include <libplacebo/gpu.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/colorspace.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/shaders/deinterlacing.h>
#include <libplacebo/shaders/dithering.h>
#include <libplacebo/shaders/sampling.h>
#include <libplacebo/tone_mapping.h>
#include <memory>
#include <rendering/RenderSystemTypes.h>
#include <settings/dialogs/GUIDialogSettingsBase.h>
#include <settings/dialogs/GUIDialogSettingsManualBase.h>
#include <settings/lib/SettingLevel.h>
#include <string>
#include <string.h>
#include <vector>
using namespace XFILE;

#define SETTING_VIDEO_VIEW_MODE           "video.viewmode"
#define SETTING_VIDEO_ZOOM                "video.zoom"
#define SETTING_VIDEO_PIXEL_RATIO         "video.pixelratio"
#define SETTING_VIDEO_BRIGHTNESS          "video.brightness"
#define SETTING_VIDEO_CONTRAST            "video.contrast"
#define SETTING_VIDEO_GAMMA               "video.gamma"
#define SETTING_VIDEO_NONLIN_STRETCH      "video.nonlinearstretch"
#define SETTING_VIDEO_POSTPROCESS         "video.postprocess"
#define SETTING_VIDEO_VERTICAL_SHIFT      "video.verticalshift"
#define SETTING_VIDEO_TONEMAP_METHOD      "video.tonemapmethod"
#define SETTING_VIDEO_TONEMAP_PARAM       "video.tonemapparam"
#define SETTING_VIDEO_ORIENTATION         "video.orientation"

#define SETTING_VIDEO_VDPAU_NOISE         "vdpau.noise"
#define SETTING_VIDEO_VDPAU_SHARPNESS     "vdpau.sharpness"

#define SETTING_VIDEO_INTERLACEMETHOD     "video.interlacemethod"
#define SETTING_VIDEO_SCALINGMETHOD       "video.scalingmethod"

#define SETTING_VIDEO_STEREOSCOPICMODE    "video.stereoscopicmode"
#define SETTING_VIDEO_STEREOSCOPICINVERT  "video.stereoscopicinvert"

#define SETTING_VIDEO_LIBPLACEBO          "video.libplacebo"

#define SETTING_VIDEO_MAKE_DEFAULT        "video.save"
#define SETTING_VIDEO_CALIBRATION         "video.calibration"
#define SETTING_VIDEO_STREAM              "video.stream"

#define SETTING_LIB_PLACEBO_SKIN_ZOOM             "video.libplacebo.skin_zoom"
#define SETTING_LIB_PLACEBO_SKIN_ZOOM_POSITION     "video.libplacebo.skin_zoom_position"
#define SETTING_LIB_PLACEBO_COLOR_ADJUSTMENT_ENABLED "video.libplacebo.color_adjustment.enabled"
#define SETTING_LIB_PLACEBO_SATURATION            "video.libplacebo.color_adjustment.saturation"
#define SETTING_LIB_PLACEBO_SDR_SATURATION        "video.libplacebo.color_adjustment.sdr_saturation"
//#define SETTING_LIB_PLACEBO_TEST                  "video.libplacebo.test"  
#define SETTING_LIB_PLACEBO_SDR_COLOR_MAP_INVERSE_TONE_MAPPING "video.libplacebo.sdr_color_map_inverse_tone_mapping"
#define SETTING_LIB_PLACEBO_SDR_TO_HDR_LABEL             "video.libplacebo.sdr_to_hdr_label"
#define SETTING_LIB_PLACEBO_SDR_TO_HDR_LOAD_PRESET    "video.libplacebo.sdr_to_hdr_load_preset"
#define SETTING_LIB_PLACEBO_HUE                   "video.libplacebo.hue"
#define SETTING_LIB_PLACEBO_TEMPERATURE           "video.libplacebo.color_adjustment.temperature"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_ENABLED              "video.libplacebo.peak_detect_params.enabled"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_SMOOTHING_PERIOD     "video.libplacebo.peak_detect_params.smoothing_period"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_SCENE_THRESHOLD_LOW  "video.libplacebo.peak_detect_params.scene_threshold_low"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_SCENE_THRESHOLD_HIGH "video.libplacebo.peak_detect_params.scene_threshold_high"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_PERCENTILE           "video.libplacebo.peak_detect_params.percentile"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_BLACK_CUTOFF         "video.libplacebo.peak_detect_params.black_cutoff"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_ALLOW_DELAYED        "video.libplacebo.peak_detect_params.allow_delayed"
#define SETTING_LIB_PLACEBO_UPSCALER                                "video.libplacebo.upscaler"
#define SETTING_LIB_PLACEBO_DOWNSCALER                              "video.libplacebo.downscaler"
#define SETTING_LIB_PLACEBO_PLANE_UPSCALER                          "video.libplacebo.plane_upscaler"
#define SETTING_LIB_PLACEBO_PLANE_DOWNSCALER                        "video.libplacebo.plane_dowsnscaler"
#define SETTING_LIB_PLACEBO_FRAME_MIXER                             "video.libplacebo.frame_mixer"
#define SETTING_LIB_PLACEBO_FRAME_MIXER_RADIUS_FACTOR 		        "video.libplacebo.frame_mixer_radius_factor"
#define SETTING_LIB_PLACEBO_COLOR_MAP_GAMUT_MAPPING                 "video.libplacebo.gamut_map_funtion"
#define SETTING_LIB_PLACEBO_SDR_COLOR_MAP_INTENT  			        "video.libplacebo.sdr_gamut_map_intent"
#define SETTING_LIB_PLACEBO_SDR_COLOR_MAP_GAMUT_MAPPING             "video.libplacebo.sdr_gamut_map_funtion"
#define SETTING_LIB_PLACEBO_COLOR_MAP_TONE_MAPPING                  "video.libplacebo.tone_map_funtion"
#define SETTING_LIB_PLACEBO_SDR_COLOR_MAP_TONE_MAPPING              "video.libplacebo.sdr_tone_map_funtion"
#define SETTING_LIB_PLACEBO_COLOR_MAP_TONE_MAPPING_PARAMETER        "video.libplacebo.tone_map_function_parameter"
#define SETTING_LIB_PLACEBO_SDR_COLOR_MAP_TONE_MAPPING_PARAMETER    "video.libplacebo.sdr_tone_map_function_parameter"
#define SETTING_LIB_PLACEBO_COLOR_MAP_CONTRAST_RECOVERY             "video.libplacebo.color_map_contrast_recovery"
#define SETTING_LIB_PLACEBO_COLOR_MAP_CONTRAST_SMOOTHNESS           "video.libplacebo.color_map_contrast_smoothness"
#define SETTING_LIB_PLACEBO_LOAD_PRESET_DEFAULT                     "video.libplacebo.load_preset_default"
#define SETTING_LIB_PLACEBO_LOAD_PRESET_FAST                        "video.libplacebo.load_preset_fast"
#define SETTING_LIB_PLACEBO_LOAD_PRESET_HIGH_QUALITY                "video.libplacebo.load_preset_high_quality"
#define SETTING_LIB_PLACEBO_LOAD_FROM_FILE                          "video.libplacebo.load_from_file"
#define SETTING_LIB_PLACEBO_SAVE_TO_FILE                            "video.libplacebo.save_to_file"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_LOAD_PRESET_DEFAULT         "video.libplacebo.peak_detect_load_preset_default"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_LOAD_PRESET_HIGH_QUALITY    "video.libplacebo.peak_detect_load_preset_high_quality"
#define SETTING_LIB_PLACEBO_DEBAND_ENABLED                          "video.libplacebo.deband_enabled"
#define SETTING_LIB_PLACEBO_DEBAND_LOAD_PRESET                      "video.libplacebo.deband_load_preset"
#define SETTING_LIB_PLACEBO_DEBAND_GRAIN                            "video.libplacebo.deband_grain"
#define SETTING_LIB_PLACEBO_DEBAND_GRAIN_NEUTRAL0                   "video.libplacebo.deband_grain_neutral0"
#define SETTING_LIB_PLACEBO_DEBAND_GRAIN_NEUTRAL1                   "video.libplacebo.deband_grain_neutral1"
#define SETTING_LIB_PLACEBO_DEBAND_GRAIN_NEUTRAL2                   "video.libplacebo.deband_grain_neutral2"
#define SETTING_LIB_PLACEBO_DEBAND_ITERATIONS                       "video.libplacebo.deband_iterations"
#define SETTING_LIB_PLACEBO_DEBAND_RADIUS                           "video.libplacebo.deband_radius"
#define SETTING_LIB_PLACEBO_DEBAND_THRESHOLD                        "video.libplacebo.deband_threshold"
#define SETTING_LIB_PLACEBO_COLOR_MAP_LOAD_PRESET_DEFAULT           "video.libplacebo.color_map_load_preset_default"
#define SETTING_LIB_PLACEBO_COLOR_MAP_LOAD_PRESET_HIGH_QUALITY      "video.libplacebo.color_map_load_preset_high_quality"
#define SETTING_LIB_PLACEBO_COLOR_MAP_ENABLED                       "video.libplacebo.color_map_enabled"
#define SETTING_LIB_PLACEBO_DEINTERLACE_ENABLED                     "video.libplacebo.deinterlace_enabled"
#define SETTING_LIB_PLACEBO_DEINTERLACE_ALGO                        "video.libplacebo.deinterlace_algo"
#define SETTING_LIB_PLACEBO_DEINTERLACE_SKIP_SPATIAL_CHECK          "video.libplacebo.deinterlace_spatial_check"
#define SETTING_LIB_PLACEBO_SIGMOID_ENABLED                         "video.libplacebo.sigmoid_enabled"
#define SETTING_LIB_PLACEBO_SIGMOID_LOAD_PRESET_DEFAULT             "video.libplacebo.sigmoid_load_preset_default"
#define SETTING_LIB_PLACEBO_SIGMOID_CENTER                          "video.libplacebo.sigmoid_center"
#define SETTING_LIB_PLACEBO_SIGMOID_SLOPE                           "video.libplacebo.sigmoid_slope"
#define SETTING_LIB_PLACEBO_CONE_ENABLED                            "video.libplacebo.cone_enabled"
#define SETTING_LIB_PLACEBO_CONE_CONES                              "video.libplacebo.cone_cones"
#define SETTING_LIB_PLACEBO_CONE_STRENGTH                           "video.libplacebo.cone_strength"
#define SETTING_LIB_PLACEBO_DITHER_ENABLED                          "video.libplacebo.dither_enabled"
#define SETTING_LIB_PLACEBO_DITHER_LOAD_PRESET_DEFAULT              "video.libplacebo.dither_load_preset_default"
#define SETTING_LIB_PLACEBO_DITHER_METHOD                           "video.libplacebo.dither_method"
#define SETTING_LIB_PLACEBO_DITHER_LUT_SIZE                         "video.libplacebo.dither_lut_size"
#define SETTING_LIB_PLACEBO_DITHER_TEMPORAL                         "video.libplacebo.dither_temporal"
#define SETTING_LIB_PLACEBO_DITHER_TRANSFER                         "video.libplacebo.dither_transfer"
#define SETTING_LIB_PLACEBO_TONE_CONSTANTS_KNEE_MAXIMUM             "video.libplacebo.tone_constants_knee_maximum"
#define SETTING_LIB_PLACEBO_TONE_CONSTANTS_KNEE_MINIMUM             "video.libplacebo.tone_constants_knee_minimum"
#define SETTING_LIB_PLACEBO_SDR_TONE_CONSTANTS_KNEE_MAXIMUM         "video.libplacebo.sdr_tone_constants_knee_maximum"
#define SETTING_LIB_PLACEBO_SDR_TONE_CONSTANTS_KNEE_MINIMUM         "video.libplacebo.sdr_tone_constants_knee_minimum"
#define SETTING_LIB_PLACEBO_TONE_CONSTANTS_SLOPE_OFFSET             "video.libplacebo.tone_constants_slope_offset"
#define SETTING_LIB_PLACEBO_TONE_CONSTANTS_SLOPE_TUNING             "video.libplacebo.tone_constants_slope_tuning"
#define SETTING_LIB_PLACEBO_SDR_TONE_CONSTANTS_SLOPE_OFFSET         "video.libplacebo.sdr_tone_constants_slope_offset"
#define SETTING_LIB_PLACEBO_SDR_TONE_CONSTANTS_SLOPE_TUNING         "video.libplacebo.sdr_tone_constants_slope_tuning"
#define SETTING_LIB_PLACEBO_GAMUT_CONSTANTS_COLORIMETRIC_GAMMA      "video.libplacebo.gamut_constants_colorimetric_gamma"
#define SETTING_LIB_PLACEBO_GAMUT_CONSTANTS_PERCEPTUAL_DEADZONE     "video.libplacebo.gamut_constants_perceptual_deadzone"
#define SETTING_LIB_PLACEBO_GAMUT_CONSTANTS_PERCEPTUAL_STRENGTH     "video.libplacebo.gamut_constants_perceptual_strength"
#define SETTING_LIB_PLACEBO_GAMUT_CONSTANTS_SOFTCLIP_DESAT          "video.libplacebo.gamut_constants_softclip_desat"
#define SETTING_LIB_PLACEBO_GAMUT_CONSTANTS_SOFTCLIP_KNEE           "video.libplacebo.gamut_constants_softclip_knee"
#define SETTING_LIB_PLACEBO_SDR_GAMUT_CONSTANTS_COLORIMETRIC_GAMMA      "video.libplacebo.sdr_gamut_constants_colorimetric_gamma"
#define SETTING_LIB_PLACEBO_SDR_GAMUT_CONSTANTS_PERCEPTUAL_DEADZONE     "video.libplacebo.sdr_gamut_constants_perceptual_deadzone"
#define SETTING_LIB_PLACEBO_SDR_GAMUT_CONSTANTS_PERCEPTUAL_STRENGTH     "video.libplacebo.sdr_gamut_constants_perceptual_strength"
#define SETTING_LIB_PLACEBO_SDR_GAMUT_CONSTANTS_SOFTCLIP_DESAT          "video.libplacebo.sdr_gamut_constants_softclip_desat"
#define SETTING_LIB_PLACEBO_SDR_GAMUT_CONSTANTS_SOFTCLIP_KNEE           "video.libplacebo.sdr_gamut_constants_softclip_knee"
#define SETTING_LIB_PLACEBO_COLOR_MAP_GAMUT_EXPANSION                   "video.libplacebo.gamut_expansion"
#define SETTING_LIB_PLACEBO_SDR_COLOR_MAP_GAMUT_EXPANSION               "video.libplacebo.sdr_gamut_expansion"
#define SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_HUE          "video.libplacebo.color_map_visualize_hue"
#define SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_LUT          "video.libplacebo.color_map_visualize_lut"
#define SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_RECT_X0      "video.libplacebo.color_map_visualize_rect_x0"
#define SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_RECT_X1      "video.libplacebo.color_map_visualize_rect_x1"
#define SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_RECT_Y0      "video.libplacebo.color_map_visualize_rect_y0"
#define SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_RECT_Y1      "video.libplacebo.color_map_visualize_rect_y1"
#define SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_THETA        "video.libplacebo.color_map_visualize_theta"
#define SETTING_LIB_PLACEBO_COLOR_MAP_INVERSE_TONE_MAPPING "video.libplacebo.color_map_inverse_tone_mapping"
#define SETTING_LIB_PLACEBO_COLOR_MAP_LUT3D_SIZE_I         "video.libplacebo.color_map_lut3d_size_i"
#define SETTING_LIB_PLACEBO_COLOR_MAP_LUT3D_SIZE_C         "video.libplacebo.color_map_lut3d_size_c"
#define SETTING_LIB_PLACEBO_COLOR_MAP_LUT3D_SIZE_H         "video.libplacebo.color_map_lut3d_size_h"
#define SETTING_LIB_PLACEBO_COLOR_MAP_LUT3D_TRICUBIC       "video.libplacebo.color_map_lut3d_tricubic"
#define SETTING_LIB_PLACEBO_COLOR_MAP_LUT_SIZE             "video.libplacebo.color_map_lut_size"
#define SETTING_LIB_PLACEBO_COLOR_MAP_SHOW_CLIPPING        "video.libplacebo.color_map_show_clipping"
#define SETTING_LIB_PLACEBO_COLOR_MAP_INTENT               "video.libplacebo.color_map_map_intent"
#define SETTING_LIB_PLACEBO_COLOR_MAP_FORCE_TONE_MAPPING_LUT "video.libplacebo.color_map_force_tone_mapping_lut"
#define SETTING_LIB_PLACEBO_LUT_FILENAME                        "video.libplacebo.lut_filename"
#define SETTING_LIB_PLACEBO_LUT_TYPE                            "video.libplacebo.lut_type"
#define SETTING_LIB_PLACEBO_ANTIRINGING_STRENGTH                "video.libplacebo.antiringing_strength"
#define SETTING_LIB_PLACEBO_CORRECT_SUBPIXEL_OFFSET             "video.libplacebo.correct_subpixel_offset"
#define SETTING_LIB_PLACEBO_DISABLE_BUILTIN_SCALERS             "video.libplacebo.disable_builtin_scalers"
#define SETTING_LIB_PLACEBO_DISABLE_DITHER_GAMMA_CORRECTION     "video.libplacebo.disable_dither_gamma_correction"
#define SETTING_LIB_PLACEBO_DISABLE_LINEAR_SCALING              "video.libplacebo.disable_linear_scaling"
#define SETTING_LIB_PLACEBO_DYNAMIC_CONSTANTS                   "video.libplacebo.dynamic_constant"
#define SETTING_LIB_PLACEBO_ERROR_DIFFUSION                     "video.libplacebo.error_diffusion"
#define SETTING_LIB_PLACEBO_FORCE_DITHER                        "video.libplacebo.force_dither"
#define SETTING_LIB_PLACEBO_FORCE_LOW_BIT_DEPTH_FBOS            "video.libplacebo.force_low_bit_depth_fbos"
#define SETTING_LIB_PLACEBO_IGNORE_ICC_PROFILES                 "video.libplacebo.ignore_icc_profiles"
#define SETTING_LIB_PLACEBO_PRESERVE_MIXING_CACHE               "video.libplacebo.preserve_mixing_cache"
#define SETTING_LIB_PLACEBO_SKIP_ANTI_ALIASING                  "video.libplacebo.skip_anti_aliasing"
#define SETTING_LIB_PLACEBO_SKIP_CACHING_SINGLE_FRAME           "video.libplacebo.skip_caching_single_frame"
#define SETTING_LIB_PLACEBO_SKIP_TARGET_CLEARING                "video.libplacebo.skip_target_clearing"
#define SETTING_LIB_PLACEBO_DISPLAY_HDR_PEAK_LUMINANCE          "video.libplacebo.display_hdr_peak_luminance"
#define SETTING_LIB_PLACEBO_DISPLAY_SDR_PEAK_LUMINANCE          "video.libplacebo.display_sdr_peak_luminance"
#define SETTING_LIB_PLACEBO_TARGET_COLORSPACE_HINT              "video.libplacebo.target_colorspace_hint"
#define SETTING_LIB_PLACEBO_USE_HDR_FOR_SDR                     "video.libplacebo.use_hdr_for_sdr"
#define SETTING_LIB_PLACEBO_TARGET_COLORSPACE_HINT_MODE         "video.libplacebo.target_colorspace_hint_mode"
#define SETTING_LIB_PLACEBO_DITHER_DEPTH                        "video.libplacebo.dither_depth"
#define SETTING_LIB_PLACEBO_SHADER_ADD                          "video.libplacebo.shader_add"
#define SETTING_LIB_PLACEBO_SHADER_REMOVE                       "video.libplacebo.shader_remove"
#define SETTING_LIB_PLACEBO_SHADER_MOVE_UP                      "video.libplacebo.shader_move_up"
#define SETTING_LIB_PLACEBO_SHADER_MOVE_DOWN                    "video.libplacebo.shader_move_down"
#define SETTING_LIB_PLACEBO_SHADER_ENABLED                      "video.libplacebo.shader_enabled"
#define SETTING_LIB_PLACEBO_SHADER_PARAM                        "video.libplacebo.shader_param"
#define SETTING_LIB_PLACEBO_SHADER_APPLY                        "video.libplacebo.shader_apply"
#define SETTING_LIB_PLACEBO_SHADER_INVALID                      "video.libplacebo.shader_invalid"
#define SETTING_LIB_PLACEBO_VIDEO_DEBUG_OSD                     "video.libplacebo.video_debug_osd"
#define SETTING_LIB_PLACEBO_DEBUG_OSD                           "video.libplacebo.debug_osd"
#define SETTING_LIB_PLACEBO_DEBUG_OSD                           "video.libplacebo.debug_osd"
#define SETTING_LIB_PLACEBO_DEBUG_HIDE                          "video.libplacebo.debug_hide"
#define SETTING_LIB_PLACEBO_FRAME_MIXER_BYPASS_QUEUE            "video.libplacebo.mixer_bypass_queue"
#define SETTING_LIB_PLACEBO_CROP_BOTTOM                         "video.libplacebo.crop_bottom"

#define CreateGroup(thegroup,thecategory) std::shared_ptr<CSettingGroup> thegroup = AddGroup(thecategory); if (thegroup == NULL) {CLog::Log(LOGERROR, "CGUIDialogLibplacebo: unable to setup settings");  return; }


CGUIDialogVideoSettings::CGUIDialogVideoSettings()
  : CGUIDialogSettingsManualBase(WINDOW_DIALOG_VIDEO_OSD_SETTINGS, "DialogSettings.xml")
{
}

CGUIDialogVideoSettings::~CGUIDialogVideoSettings() = default;


void CGUIDialogVideoSettings::OnSettingChanged(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
	return;

  CGUIDialogSettingsManualBase::OnSettingChanged(setting);

  auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();

  const std::string& settingId = setting->GetId();
  CVideoSettings vs = appPlayer->GetVideoSettings();
  pl_options m_placeboOptions = vs.m_placeboOptions->getPlOptions();
  if (settingId == SETTING_VIDEO_INTERLACEMETHOD)
  {
	vs.m_InterlaceMethod = static_cast<EINTERLACEMETHOD>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
  }
  else if (settingId == SETTING_VIDEO_SCALINGMETHOD)
  {
	vs.m_ScalingMethod = static_cast<ESCALINGMETHOD>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
  }
  else if (settingId == SETTING_VIDEO_STREAM)
  {
	m_videoStream = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
	// only change the video stream if a different one has been asked for
	if (appPlayer->GetVideoStream() != m_videoStream)
	{
	  appPlayer->SetVideoStream(m_videoStream); // Set the video stream to the one selected
	}
  }
  else if (settingId == SETTING_VIDEO_VIEW_MODE)
  {
	int value = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();

	appPlayer->SetRenderViewMode(value, vs.m_CustomZoomAmount, vs.m_CustomPixelRatio,
	  vs.m_CustomVerticalShift, vs.m_CustomNonLinStretch);

	m_viewModeChanged = true;
	GetSettingsManager()->SetNumber(SETTING_VIDEO_ZOOM, static_cast<double>(vs.m_CustomZoomAmount));
	GetSettingsManager()->SetNumber(SETTING_VIDEO_PIXEL_RATIO,
	  static_cast<double>(vs.m_CustomPixelRatio));
	GetSettingsManager()->SetNumber(SETTING_VIDEO_VERTICAL_SHIFT,
	  static_cast<double>(vs.m_CustomVerticalShift));
	GetSettingsManager()->SetBool(SETTING_VIDEO_NONLIN_STRETCH, vs.m_CustomNonLinStretch);
	m_viewModeChanged = false;
  }
  else if (settingId == SETTING_VIDEO_ZOOM ||
	settingId == SETTING_VIDEO_VERTICAL_SHIFT ||
	settingId == SETTING_VIDEO_PIXEL_RATIO ||
	settingId == SETTING_VIDEO_NONLIN_STRETCH)
  {
	if (settingId == SETTING_VIDEO_ZOOM)
	  vs.m_CustomZoomAmount = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	else if (settingId == SETTING_VIDEO_VERTICAL_SHIFT)
	  vs.m_CustomVerticalShift = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	else if (settingId == SETTING_VIDEO_PIXEL_RATIO)
	  vs.m_CustomPixelRatio = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	else if (settingId == SETTING_VIDEO_NONLIN_STRETCH)
	  vs.m_CustomNonLinStretch = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();

	// try changing the view mode to custom. If it already is set to custom
	// manually call the render manager
	if (GetSettingsManager()->GetInt(SETTING_VIDEO_VIEW_MODE) != ViewModeCustom)
	  GetSettingsManager()->SetInt(SETTING_VIDEO_VIEW_MODE, ViewModeCustom);
	else
	  appPlayer->SetRenderViewMode(vs.m_ViewMode, vs.m_CustomZoomAmount, vs.m_CustomPixelRatio,
		vs.m_CustomVerticalShift, vs.m_CustomNonLinStretch);
  }
  else if (settingId == SETTING_VIDEO_POSTPROCESS)
  {
	vs.m_PostProcess = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_BRIGHTNESS)
  {
	vs.m_Brightness = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	vs.m_Brightness = std::round(vs.m_Brightness * 20.0) * 0.05;
	m_placeboOptions->color_adjustment.brightness = vs.m_Brightness / 50.0 - 1.0;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_CONTRAST)
  {
	vs.m_Contrast = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	vs.m_Contrast = std::round(vs.m_Contrast*50.0) * 0.02;
	m_placeboOptions->color_adjustment.contrast = (pow(10.0, (vs.m_Contrast - 50.0) / 25.0) - 0.01) * 100.0 / 99.99;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_GAMMA)
  {
	vs.m_Gamma = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	vs.m_Gamma = std::round(vs.m_Gamma * 10.0) * 0.1;
	m_placeboOptions->color_adjustment.gamma = (pow(10.0, (vs.m_Gamma - 20.0) / 40.0) - pow(10, -0.5)) * 1.0 / (1.0 - pow(10, -0.5));
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_VDPAU_NOISE)
  {
	vs.m_NoiseReduction = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_VDPAU_SHARPNESS)
  {
	vs.m_Sharpness = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_TONEMAP_METHOD)
  {
	vs.m_ToneMapMethod = static_cast<ETONEMAPMETHOD>(
	  std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_TONEMAP_PARAM)
  {
	vs.m_ToneMapParam = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_ORIENTATION)
  {
	vs.m_Orientation = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_STEREOSCOPICMODE)
  {
	vs.m_StereoMode = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_STEREOSCOPICINVERT)
  {
	vs.m_StereoInvert = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_SATURATION)
  {
	vs.m_PlaceboSaturation = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_adjustment.saturation = pow(10.0, (vs.m_PlaceboSaturation - 50.0) / 40.0);
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_SATURATION)
  {
	vs.m_PlaceboSdrSaturation = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  //else if(settingId == SETTING_LIB_PLACEBO_TEST)
  //{
  //vs.m_PlaceboTest = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
  //appPlayer->SetVideoSettings(vs);
  //}
  else if(settingId == SETTING_LIB_PLACEBO_SDR_COLOR_MAP_INVERSE_TONE_MAPPING)
  {
	vs.m_PlaceboSdrColorMapInverseToneMapping = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_HUE)
  {
	vs.m_PlaceboHue = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_adjustment.hue = fmod(vs.m_PlaceboHue, 360.0) * M_PI / 180.0;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_TEMPERATURE)
  {
	vs.m_PlaceboTemperature = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_adjustment.temperature = (vs.m_PlaceboTemperature - 6500.0) / 3500.0;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_ENABLED)
  {
	vs.m_PlaceboPeakDetectEnabled = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.peak_detect_params = vs.m_PlaceboPeakDetectEnabled ? &m_placeboOptions->peak_detect_params : NULL;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_ADJUSTMENT_ENABLED)
  {
	vs.m_PlaceboColorAdjustmentEnabled = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.color_adjustment = vs.m_PlaceboColorAdjustmentEnabled ? &m_placeboOptions->color_adjustment : NULL;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_SMOOTHING_PERIOD)
  {
	vs.m_PlaceboPeakDetectSmoothingPeriod = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->peak_detect_params.smoothing_period = vs.m_PlaceboPeakDetectSmoothingPeriod;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_SCENE_THRESHOLD_LOW)
  {
	vs.m_PlaceboPeakDetectSceneThresholdLow = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->peak_detect_params.scene_threshold_low = vs.m_PlaceboPeakDetectSceneThresholdLow;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_SCENE_THRESHOLD_HIGH)
  {
	vs.m_PlaceboPeakDetectSceneThresholdHigh = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->peak_detect_params.scene_threshold_high = vs.m_PlaceboPeakDetectSceneThresholdHigh;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_PERCENTILE)
  {
	vs.m_PlaceboPeakDetectPercentile = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->peak_detect_params.percentile = vs.m_PlaceboPeakDetectPercentile;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_BLACK_CUTOFF)
  {
	vs.m_PlaceboPeakDetectBlackCutoff = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->peak_detect_params.black_cutoff = vs.m_PlaceboPeakDetectBlackCutoff;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_ALLOW_DELAYED)
  {
	vs.m_PlaceboPeakDetectAllowDelayed = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->peak_detect_params.allow_delayed = vs.m_PlaceboPeakDetectAllowDelayed;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_UPSCALER)
  {

	vs.m_PlaceboUpscaler = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
	m_placeboOptions->params.upscaler = vs.m_PlaceboUpscaler == -1 ? NULL : pl_filter_configs[vs.m_PlaceboUpscaler];
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DOWNSCALER)
  {
	vs.m_PlaceboDownscaler = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
	m_placeboOptions->params.downscaler = vs.m_PlaceboDownscaler == -1 ? NULL : pl_filter_configs[vs.m_PlaceboDownscaler];
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_PLANE_UPSCALER)
  {
	vs.m_PlaceboPlaneUpscaler = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
	m_placeboOptions->params.plane_upscaler = vs.m_PlaceboPlaneUpscaler == -1 ? NULL : pl_filter_configs[vs.m_PlaceboPlaneUpscaler];
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_PLANE_DOWNSCALER)
  {
	vs.m_PlaceboPlaneDownscaler = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
	m_placeboOptions->params.plane_downscaler = vs.m_PlaceboPlaneDownscaler == -1 ? NULL : pl_filter_configs[vs.m_PlaceboPlaneDownscaler];
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_FRAME_MIXER)
  {
	vs.m_PlaceboFrameMixer = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
	m_placeboOptions->params.frame_mixer = vs.m_PlaceboFrameMixer == -1 ? NULL : pl_filter_configs[vs.m_PlaceboFrameMixer];
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_GAMUT_MAPPING)
  {
	vs.m_PlaceboColorMapGamutMapping = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
	m_placeboOptions->color_map_params.gamut_mapping = vs.m_PlaceboColorMapGamutMapping == -1 ? NULL : pl_gamut_map_functions[vs.m_PlaceboColorMapGamutMapping];
	appPlayer->SetVideoSettings(vs);
	SetupView();
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_COLOR_MAP_GAMUT_MAPPING)
  {
	vs.m_PlaceboSdrColorMapGamutMapping = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
	appPlayer->SetVideoSettings(vs);
	SetupView();
	}
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_TONE_MAPPING)
  {
	vs.m_PlaceboColorMapToneMapping = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
	m_placeboOptions->color_map_params.tone_mapping_function = vs.m_PlaceboColorMapToneMapping == -1 ? NULL : pl_tone_map_functions[vs.m_PlaceboColorMapToneMapping];

	vs.m_PlaceboColorMapToneMapParameter = 0.0;
	if(vs.m_PlaceboColorMapToneMapping  != -1)
	{
	  std::string desc = {};
	  std::string funcDesc = {};
	  if(pl_tone_map_functions[vs.m_PlaceboColorMapToneMapping]->param_desc)
		desc = pl_tone_map_functions[vs.m_PlaceboColorMapToneMapping]->param_desc;
	  if(pl_tone_map_functions[vs.m_PlaceboColorMapToneMapping]->description)
		funcDesc = pl_tone_map_functions[vs.m_PlaceboColorMapToneMapping]->description;

	  if(desc == "Knee point target")
		vs.m_PlaceboColorMapToneMapParameter = vs.m_PlaceboToneConstantKneeAdaptation;
	  else if(desc == "Knee offset")
		vs.m_PlaceboColorMapToneMapParameter = vs.m_PlaceboToneConstantKneeOffset;
	  else if((desc == "Contrast") && (funcDesc == "Single-pivot polynomial spline"))
		vs.m_PlaceboColorMapToneMapParameter = vs.m_PlaceboToneConstantSplineContrast;
	  else if((desc == "Contrast") && (funcDesc == "Reinhard"))
		vs.m_PlaceboColorMapToneMapParameter = vs.m_PlaceboToneConstantReinhardContrast;
	  else if(desc == "Exposure")
		vs.m_PlaceboColorMapToneMapParameter = vs.m_PlaceboToneConstantExposure;
	  else if(desc == "Knee point")
		vs.m_PlaceboColorMapToneMapParameter = vs.m_PlaceboToneConstantLinearKnee;
	}
	appPlayer->SetVideoSettings(vs);
	SetupView();
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_COLOR_MAP_TONE_MAPPING)
  {
	vs.m_PlaceboSdrColorMapToneMapping = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();

	vs.m_PlaceboSdrColorMapToneMapParameter = 0.0;
	if(vs.m_PlaceboSdrColorMapToneMapping != -1)
	{
	  std::string desc = {};
	  std::string funcDesc = {};
	  if(pl_tone_map_functions [vs.m_PlaceboSdrColorMapToneMapping]->param_desc)
		desc = pl_tone_map_functions [vs.m_PlaceboSdrColorMapToneMapping]->param_desc;
	  if(pl_tone_map_functions [vs.m_PlaceboSdrColorMapToneMapping]->description)
		funcDesc = pl_tone_map_functions [vs.m_PlaceboSdrColorMapToneMapping]->description;

	  if(desc == "Knee point target")
		vs.m_PlaceboSdrColorMapToneMapParameter = vs.m_PlaceboSdrToneConstantKneeAdaptation;
	  else if(desc == "Knee offset")
		vs.m_PlaceboSdrColorMapToneMapParameter = vs.m_PlaceboSdrToneConstantKneeOffset;
	  else if((desc == "Contrast") && (funcDesc == "Single-pivot polynomial spline"))
		vs.m_PlaceboSdrColorMapToneMapParameter = vs.m_PlaceboSdrToneConstantSplineContrast;
	  else if((desc == "Contrast") && (funcDesc == "Reinhard"))
		vs.m_PlaceboSdrColorMapToneMapParameter = vs.m_PlaceboSdrToneConstantReinhardContrast;
	  else if(desc == "Exposure")
		vs.m_PlaceboSdrColorMapToneMapParameter = vs.m_PlaceboSdrToneConstantExposure;
	  else if(desc == "Knee point")
		vs.m_PlaceboSdrColorMapToneMapParameter = vs.m_PlaceboSdrToneConstantLinearKnee;
	}
	appPlayer->SetVideoSettings(vs);
	SetupView();
  }
  else if(settingId == SETTING_LIB_PLACEBO_COLOR_MAP_TONE_MAPPING_PARAMETER)
  {
	vs.m_PlaceboColorMapToneMapParameter = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());

	std::string desc = {};
	std::string funcDesc = {};
	if(pl_tone_map_functions[vs.m_PlaceboColorMapToneMapping]->param_desc)
	  desc = pl_tone_map_functions[vs.m_PlaceboColorMapToneMapping]->param_desc;
	if(pl_tone_map_functions[vs.m_PlaceboColorMapToneMapping]->description)
	  funcDesc = pl_tone_map_functions[vs.m_PlaceboColorMapToneMapping]->description;

	if(desc == "Knee point target")
	{
	  vs.m_PlaceboToneConstantKneeAdaptation = vs.m_PlaceboColorMapToneMapParameter;
	  m_placeboOptions->color_map_params.tone_constants.knee_adaptation = vs.m_PlaceboToneConstantKneeAdaptation;
	  appPlayer->SetVideoSettings(vs);
	}
	else if(desc == "Knee offset")
	{
	  vs.m_PlaceboToneConstantKneeOffset = vs.m_PlaceboColorMapToneMapParameter;
	  m_placeboOptions->color_map_params.tone_constants.knee_offset = vs.m_PlaceboToneConstantKneeOffset;
	  appPlayer->SetVideoSettings(vs);
	}
	else if((desc == "Contrast") && (funcDesc == "Single-pivot polynomial spline"))
	{
	  vs.m_PlaceboToneConstantSplineContrast = vs.m_PlaceboColorMapToneMapParameter;
	  m_placeboOptions->color_map_params.tone_constants.spline_contrast = vs.m_PlaceboToneConstantSplineContrast;
	  appPlayer->SetVideoSettings(vs);
	}
	else if((desc == "Contrast") && (funcDesc == "Reinhard"))
	{
	  vs.m_PlaceboToneConstantReinhardContrast = vs.m_PlaceboColorMapToneMapParameter;
	  m_placeboOptions->color_map_params.tone_constants.reinhard_contrast = vs.m_PlaceboToneConstantReinhardContrast;
	  appPlayer->SetVideoSettings(vs);
	}
	else if(desc == "Exposure")
	{
	  vs.m_PlaceboToneConstantExposure = vs.m_PlaceboColorMapToneMapParameter;
	  m_placeboOptions->color_map_params.tone_constants.exposure = vs.m_PlaceboToneConstantExposure;
	  appPlayer->SetVideoSettings(vs);
	}
	else if(desc == "Knee point")
	{
	  vs.m_PlaceboToneConstantLinearKnee = vs.m_PlaceboColorMapToneMapParameter;
	  m_placeboOptions->color_map_params.tone_constants.linear_knee = vs.m_PlaceboToneConstantLinearKnee;
	  appPlayer->SetVideoSettings(vs);
	}
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_COLOR_MAP_TONE_MAPPING_PARAMETER)
  {
	vs.m_PlaceboSdrColorMapToneMapParameter = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());

	std::string desc = {};
	std::string funcDesc = {};
	if(pl_tone_map_functions [vs.m_PlaceboSdrColorMapToneMapping]->param_desc)
	  desc = pl_tone_map_functions [vs.m_PlaceboSdrColorMapToneMapping]->param_desc;
	if(pl_tone_map_functions [vs.m_PlaceboSdrColorMapToneMapping]->description)
	  funcDesc = pl_tone_map_functions [vs.m_PlaceboSdrColorMapToneMapping]->description;

	if(desc == "Knee point target")
	{
	  vs.m_PlaceboSdrToneConstantKneeAdaptation = vs.m_PlaceboSdrColorMapToneMapParameter;
	  appPlayer->SetVideoSettings(vs);
	}
	else if(desc == "Knee offset")
	{
	  vs.m_PlaceboSdrToneConstantKneeOffset = vs.m_PlaceboSdrColorMapToneMapParameter;
	  appPlayer->SetVideoSettings(vs);
	}
	else if((desc == "Contrast") && (funcDesc == "Single-pivot polynomial spline"))
	{
	  vs.m_PlaceboSdrToneConstantSplineContrast = vs.m_PlaceboSdrColorMapToneMapParameter;
	  appPlayer->SetVideoSettings(vs);
	}
	else if((desc == "Contrast") && (funcDesc == "Reinhard"))
	{
	  vs.m_PlaceboSdrToneConstantReinhardContrast = vs.m_PlaceboSdrColorMapToneMapParameter;
	  appPlayer->SetVideoSettings(vs);
	}
	else if(desc == "Exposure")
	{
	  vs.m_PlaceboSdrToneConstantExposure = vs.m_PlaceboSdrColorMapToneMapParameter;
	  appPlayer->SetVideoSettings(vs);
	}
	else if(desc == "Knee point")
	{
	  vs.m_PlaceboSdrToneConstantLinearKnee = vs.m_PlaceboSdrColorMapToneMapParameter;
	  appPlayer->SetVideoSettings(vs);
	}
  }

  else if (settingId == SETTING_LIB_PLACEBO_DEBAND_ENABLED)
  {
	vs.m_PlaceboDebandEnabled = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.deband_params = vs.m_PlaceboDebandEnabled ? &m_placeboOptions->deband_params : NULL;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DEBAND_GRAIN)
  {
	vs.m_PlaceboDebandGrain = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->deband_params.grain = vs.m_PlaceboDebandGrain;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DEBAND_GRAIN_NEUTRAL0)
  {
	vs.m_PlaceboDebandGrainNeutral0 = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->deband_params.grain_neutral[0] = vs.m_PlaceboDebandGrainNeutral0;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DEBAND_GRAIN_NEUTRAL1)
  {
	vs.m_PlaceboDebandGrainNeutral1 = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->deband_params.grain_neutral[1] = vs.m_PlaceboDebandGrainNeutral1;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DEBAND_GRAIN_NEUTRAL2)
  {
	vs.m_PlaceboDebandGrainNeutral2 = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->deband_params.grain_neutral[2] = vs.m_PlaceboDebandGrainNeutral2;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DEBAND_ITERATIONS)
  {
	vs.m_PlaceboDebandIterations = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	m_placeboOptions->deband_params.iterations = vs.m_PlaceboDebandIterations;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DEBAND_RADIUS)
  {
	vs.m_PlaceboDebandRadius = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->deband_params.radius = vs.m_PlaceboDebandRadius;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DEBAND_THRESHOLD)
  {
	vs.m_PlaceboDebandThreshold = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->deband_params.threshold = vs.m_PlaceboDebandThreshold;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_ENABLED)
  {
	vs.m_PlaceboColorMapEnabled = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.color_map_params = vs.m_PlaceboColorMapEnabled ? &m_placeboOptions->color_map_params : NULL;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_SKIN_ZOOM)
  {
	vs.m_PlaceboSkinZoom = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	vs.m_PlaceboSkinZoomHint = vs.m_PlaceboSkinZoom;
	appPlayer->SetVideoSettings(vs);
	CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_WINDOW_RESIZE);
	CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(msg);
  }
  else if(settingId == SETTING_LIB_PLACEBO_SKIN_ZOOM_POSITION)
  {
	vs.m_PlaceboSkinZoomPosition = static_cast<VS_PLACEBO_SZ_POSITION>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
	CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_WINDOW_RESIZE);
	CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(msg);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DISPLAY_HDR_PEAK_LUMINANCE)
  {
	vs.m_PlaceboDisplayHdrPeakLuminance = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_DISPLAY_SDR_PEAK_LUMINANCE)
  {
	vs.m_PlaceboDisplaySdrPeakLuminance = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_SHADER_APPLY)
  {
	vs.m_PlaceboShaderApply = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_USE_HDR_FOR_SDR)
  {
	vs.m_PlaceboUseHdrForSdr = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_TARGET_COLORSPACE_HINT)
  {
	vs.m_PlaceboTargetColorspaceHint = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_TARGET_COLORSPACE_HINT_MODE)
  {
	vs.m_PlaceboTargetColorspaceHintMode = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_DITHER_DEPTH)
  {
	vs.m_PlaceboDitherDepth = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_FRAME_MIXER_RADIUS_FACTOR)
  {
	vs.m_PlaceboFrameMixerRadiusFactor = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_FRAME_MIXER_BYPASS_QUEUE)
  {
	vs.m_PlaceboFrameMixerBypassQueue = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	appPlayer->SetVideoSettings(vs);
	}
  else if(settingId == SETTING_LIB_PLACEBO_CROP_BOTTOM)
  {
	vs.m_PlaceboCropBottom = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DEINTERLACE_ENABLED)
  {
	vs.m_PlaceboDeinterlaceEnabled = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.deinterlace_params = vs.m_PlaceboDeinterlaceEnabled ? &m_placeboOptions->deinterlace_params : NULL;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DEINTERLACE_ALGO)
  {
	vs.m_PlaceboDeinterlaceAlgo = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	m_placeboOptions->deinterlace_params.algo = (enum pl_deinterlace_algorithm)vs.m_PlaceboDeinterlaceAlgo;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DEINTERLACE_SKIP_SPATIAL_CHECK)
  {
	vs.m_PlaceboDeinterlaceSkipSpatialCheck = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->deinterlace_params.skip_spatial_check = vs.m_PlaceboDeinterlaceSkipSpatialCheck;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_SIGMOID_ENABLED)
  {
	vs.m_PlaceboSigmoidEnabled = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.sigmoid_params = vs.m_PlaceboSigmoidEnabled ? &m_placeboOptions->sigmoid_params : NULL;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_SIGMOID_CENTER)
  {
	vs.m_PlaceboSigmoidCenter = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->sigmoid_params.center = vs.m_PlaceboSigmoidCenter;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_SIGMOID_SLOPE)
  {
	vs.m_PlaceboSigmoidSlope = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
	m_placeboOptions->sigmoid_params.slope = vs.m_PlaceboSigmoidSlope;
  }
  else if (settingId == SETTING_LIB_PLACEBO_CONE_ENABLED)
  {
	vs.m_PlaceboConeEnabled = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.cone_params = vs.m_PlaceboConeEnabled ? &m_placeboOptions->cone_params : NULL;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_CONE_CONES)
  {
	vs.m_PlaceboConeCones = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	m_placeboOptions->cone_params.cones = (enum pl_cone)vs.m_PlaceboConeCones;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_CONE_STRENGTH)
  {
	vs.m_PlaceboConeStrength = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->cone_params.strength = vs.m_PlaceboConeStrength;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DITHER_ENABLED)
  {
	vs.m_PlaceboDitherEnabled = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.dither_params = vs.m_PlaceboDitherEnabled ? &m_placeboOptions->dither_params : NULL;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DITHER_METHOD)
  {
	vs.m_PlaceboDitherMethod = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	m_placeboOptions->dither_params.method = (enum pl_dither_method)vs.m_PlaceboDitherMethod;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DITHER_LUT_SIZE)
  {
	vs.m_PlaceboDitherLutSize = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	m_placeboOptions->dither_params.lut_size = vs.m_PlaceboDitherLutSize;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DITHER_TEMPORAL)
  {
	vs.m_PlaceboDitherTemporal = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->dither_params.temporal = vs.m_PlaceboDitherTemporal;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DITHER_TRANSFER)
  {
	vs.m_PlaceboDitherTransfer = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	m_placeboOptions->dither_params.transfer = (enum pl_color_transfer)vs.m_PlaceboDitherTransfer;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_TONE_CONSTANTS_KNEE_MAXIMUM)
  {
	vs.m_PlaceboToneConstantKneeMaximum = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.tone_constants.knee_maximum = vs.m_PlaceboToneConstantKneeMaximum;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_TONE_CONSTANTS_KNEE_MINIMUM)
  {
	vs.m_PlaceboToneConstantKneeMinimum = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.tone_constants.knee_minimum = vs.m_PlaceboToneConstantKneeMinimum;
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_TONE_CONSTANTS_KNEE_MAXIMUM)
  {
	vs.m_PlaceboSdrToneConstantKneeMaximum = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_TONE_CONSTANTS_KNEE_MINIMUM)
  {
	vs.m_PlaceboSdrToneConstantKneeMinimum = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_TONE_CONSTANTS_SLOPE_OFFSET)
  {
	vs.m_PlaceboToneConstantSlopeOffset = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.tone_constants.slope_offset = vs.m_PlaceboToneConstantSlopeOffset;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_TONE_CONSTANTS_SLOPE_TUNING)
  {
	vs.m_PlaceboToneConstantSlopeTuning = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.tone_constants.slope_tuning = vs.m_PlaceboToneConstantSlopeTuning;
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_TONE_CONSTANTS_SLOPE_OFFSET)
  {
	vs.m_PlaceboSdrToneConstantSlopeOffset = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_TONE_CONSTANTS_SLOPE_TUNING)
  {
	vs.m_PlaceboSdrToneConstantSlopeTuning = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_LUT)
  {
	vs.m_PlaceboColorMapVisualizeLut = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->color_map_params.visualize_lut = vs.m_PlaceboColorMapVisualizeLut;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_RECT_X0)
  {
	vs.m_PlaceboColorMapVisualizeRectX0 = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.visualize_rect.x0 = vs.m_PlaceboColorMapVisualizeRectX0;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_RECT_X1)
  {
	vs.m_PlaceboColorMapVisualizeRectX1 = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.visualize_rect.x1 = vs.m_PlaceboColorMapVisualizeRectX1;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_RECT_Y0)
  {
	vs.m_PlaceboColorMapVisualizeRectY0 = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.visualize_rect.y0 = vs.m_PlaceboColorMapVisualizeRectY0;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_RECT_Y1)
  {
	vs.m_PlaceboColorMapVisualizeRectY1 = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.visualize_rect.y1 = vs.m_PlaceboColorMapVisualizeRectY1;
	appPlayer->SetVideoSettings(vs);
  }

  if(settingId == SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_HUE) //cl break "elses" because of compiler limitations on else if statements
  {
	vs.m_PlaceboColorMapVisualizeHue = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.visualize_hue = fmod(vs.m_PlaceboColorMapVisualizeHue, 360.0) * M_PI / 180.0;;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_THETA)
  {
	vs.m_PlaceboColorMapVisualizeTheta = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.visualize_theta = fmod(vs.m_PlaceboColorMapVisualizeTheta, 360.0) * M_PI / 180.0;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_CONTRAST_RECOVERY)
  {
	vs.m_PlaceboColorMapContrastRecovery = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.contrast_recovery = vs.m_PlaceboColorMapContrastRecovery;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_CONTRAST_SMOOTHNESS)
  {
	vs.m_PlaceboColorMapContrastSmoothness = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.contrast_smoothness = vs.m_PlaceboColorMapContrastSmoothness;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_GAMUT_EXPANSION)
  {
	vs.m_PlaceboColorMapGamutExpansion = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->color_map_params.gamut_expansion = vs.m_PlaceboColorMapGamutExpansion;
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_COLOR_MAP_GAMUT_EXPANSION)
  {
	vs.m_PlaceboSdrColorMapGamutExpansion = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_GAMUT_CONSTANTS_COLORIMETRIC_GAMMA)
  {
	vs.m_PlaceboGamutConstantsColorimetricGamma = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.gamut_constants.colorimetric_gamma = vs.m_PlaceboGamutConstantsColorimetricGamma;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_GAMUT_CONSTANTS_PERCEPTUAL_DEADZONE)
  {
	vs.m_PlaceboGamutConstantsPerceptualDeadzone = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.gamut_constants.perceptual_deadzone = vs.m_PlaceboGamutConstantsPerceptualDeadzone;
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_GAMUT_CONSTANTS_PERCEPTUAL_STRENGTH)
  {
	vs.m_PlaceboGamutConstantsPerceptualStrength = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.gamut_constants.perceptual_strength = vs.m_PlaceboGamutConstantsPerceptualStrength;
	appPlayer->SetVideoSettings(vs);
	}
  else if (settingId == SETTING_LIB_PLACEBO_GAMUT_CONSTANTS_SOFTCLIP_DESAT)
  {
	vs.m_PlaceboGamutConstantsSoftclipDesat = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.gamut_constants.softclip_desat = vs.m_PlaceboGamutConstantsSoftclipDesat;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_GAMUT_CONSTANTS_SOFTCLIP_KNEE)
  {
	vs.m_PlaceboGamutConstantsSoftclipKnee = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->color_map_params.gamut_constants.softclip_knee = vs.m_PlaceboGamutConstantsSoftclipKnee;
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_GAMUT_CONSTANTS_COLORIMETRIC_GAMMA)
  {
	vs.m_PlaceboSdrGamutConstantsColorimetricGamma = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_GAMUT_CONSTANTS_PERCEPTUAL_DEADZONE)
  {
	vs.m_PlaceboSdrGamutConstantsPerceptualDeadzone = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_GAMUT_CONSTANTS_PERCEPTUAL_STRENGTH)
  {
	vs.m_PlaceboSdrGamutConstantsPerceptualStrength = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_GAMUT_CONSTANTS_SOFTCLIP_DESAT)
  {
	vs.m_PlaceboSdrGamutConstantsSoftclipDesat = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_GAMUT_CONSTANTS_SOFTCLIP_KNEE)
  {
	vs.m_PlaceboSdrGamutConstantsSoftclipKnee = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_INVERSE_TONE_MAPPING)
  {
	vs.m_PlaceboColorMapInverseToneMapping = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->color_map_params.inverse_tone_mapping = vs.m_PlaceboColorMapInverseToneMapping;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_LUT3D_SIZE_I)
  {
	vs.m_PlaceboColorMapLut3dSizeI = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	m_placeboOptions->color_map_params.lut3d_size[0] = vs.m_PlaceboColorMapLut3dSizeI;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_LUT3D_SIZE_C)
  {
	vs.m_PlaceboColorMapLut3dSizeC = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	m_placeboOptions->color_map_params.lut3d_size[1] = vs.m_PlaceboColorMapLut3dSizeC;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_LUT3D_SIZE_H)
  {
	vs.m_PlaceboColorMapLut3dSizeH = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	m_placeboOptions->color_map_params.lut3d_size[2] = vs.m_PlaceboColorMapLut3dSizeH;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_LUT3D_TRICUBIC)
  {
	vs.m_PlaceboColorMapLut3dTricubic = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->color_map_params.lut3d_tricubic = vs.m_PlaceboColorMapLut3dTricubic;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_LUT_SIZE)
  {
	vs.m_PlaceboColorMapLutSize = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	m_placeboOptions->color_map_params.lut_size = vs.m_PlaceboColorMapLutSize;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_SHOW_CLIPPING)
  {
	vs.m_PlaceboColorMapShowClipping = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->color_map_params.show_clipping = vs.m_PlaceboColorMapShowClipping;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_INTENT)
  {
	vs.m_PlaceboColorMapIntent = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	m_placeboOptions->color_map_params.intent = (pl_rendering_intent)vs.m_PlaceboColorMapIntent;
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_COLOR_MAP_INTENT)
  {
	vs.m_PlaceboSdrColorMapIntent = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	appPlayer->SetVideoSettings(vs);
	}
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_FORCE_TONE_MAPPING_LUT)
  {
	vs.m_PlaceboColorMapForceToneMappingLut = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->color_map_params.force_tone_mapping_lut = vs.m_PlaceboColorMapForceToneMappingLut;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_LUT_FILENAME)
  {
	vs.m_PlaceboLutFilename = std::static_pointer_cast<const CSettingString>(setting)->GetValue();
	CPLHelper::LoadLutFile(vs, vs.m_PlaceboLutFilename);
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_LUT_TYPE)
  {
	vs.m_PlaceboLutType = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	m_placeboOptions->params.lut_type = (pl_lut_type)vs.m_PlaceboLutType;
	m_placeboOptions->params.lut = vs.m_PlaceboLutType == -1 ? NULL : vs.m_PlaceboLut.get();
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_ANTIRINGING_STRENGTH)
  {
	vs.m_PlaceboAntiringingStrength = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	m_placeboOptions->params.antiringing_strength = vs.m_PlaceboAntiringingStrength;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_CORRECT_SUBPIXEL_OFFSET)
  {
	vs.m_PlaceboCorrectSubpixelOffset = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.correct_subpixel_offsets = vs.m_PlaceboCorrectSubpixelOffset;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DISABLE_BUILTIN_SCALERS)
  {
	vs.m_PlaceboDisableBuiltinScalers = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.disable_builtin_scalers = vs.m_PlaceboDisableBuiltinScalers;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DISABLE_DITHER_GAMMA_CORRECTION)
  {
	vs.m_PlaceboDisableDitherGammaCorrection = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.disable_dither_gamma_correction = vs.m_PlaceboDisableDitherGammaCorrection;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DISABLE_LINEAR_SCALING)
  {
	vs.m_PlaceboDisableLinearScaling = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.disable_linear_scaling = vs.m_PlaceboDisableLinearScaling;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_DYNAMIC_CONSTANTS)
  {
	vs.m_PlaceboDynamicConstant = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.dynamic_constants = vs.m_PlaceboDynamicConstant;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_ERROR_DIFFUSION)
  {
	vs.m_PlaceboErrorDiffusion = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	m_placeboOptions->params.error_diffusion = vs.m_PlaceboErrorDiffusion == -1 ? NULL : pl_error_diffusion_kernels[vs.m_PlaceboErrorDiffusion];
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_FORCE_DITHER)
  {
	vs.m_PlaceboForceDither = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.force_dither = vs.m_PlaceboForceDither;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_FORCE_LOW_BIT_DEPTH_FBOS)
  {
	vs.m_PlaceboForceLowBitDepthFbos = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.force_low_bit_depth_fbos = vs.m_PlaceboForceLowBitDepthFbos;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_IGNORE_ICC_PROFILES)
  {
	vs.m_PlaceboIgnoreIccProfiles = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.ignore_icc_profiles = vs.m_PlaceboIgnoreIccProfiles;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_PRESERVE_MIXING_CACHE)
  {
	vs.m_PlaceboPreserveMixingCache = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.preserve_mixing_cache = vs.m_PlaceboPreserveMixingCache;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_SKIP_ANTI_ALIASING)
  {
	vs.m_PlaceboSkipAntiAliasing = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.skip_anti_aliasing = vs.m_PlaceboSkipAntiAliasing;
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId == SETTING_LIB_PLACEBO_SKIP_CACHING_SINGLE_FRAME)
  {
	vs.m_PlaceboSkipCachingSingleFrame = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.skip_caching_single_frame = vs.m_PlaceboSkipCachingSingleFrame;
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_SKIP_TARGET_CLEARING)
  {
	vs.m_PlaceboSkipTargetClearing = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	m_placeboOptions->params.skip_target_clearing = vs.m_PlaceboSkipTargetClearing;
	appPlayer->SetVideoSettings(vs);
  }
  else if(settingId == SETTING_LIB_PLACEBO_VIDEO_DEBUG_OSD)
  {
	vs.m_PlaceboVideoDebugOsd = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	appPlayer->SetVideoDebug(vs.m_PlaceboVideoDebugOsd);
	appPlayer->SetVideoSettings(vs);
	}
  else if(settingId == SETTING_LIB_PLACEBO_DEBUG_HIDE)
  {
	vs.m_PlaceboDebugHide = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();

	CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_HIDE_ONSCREEN_DEBUG, vs.m_PlaceboDebugHide);  //cl more specific routing?
	CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(msg);
	appPlayer->SetVideoSettings(vs);
	}
  else if(settingId == SETTING_LIB_PLACEBO_DEBUG_OSD)
  {
	vs.m_PlaceboDebugOsd = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	appPlayer->SetDebug(vs.m_PlaceboDebugOsd);
	appPlayer->SetVideoSettings(vs);
	}

  else if (settingId.starts_with(SETTING_LIB_PLACEBO_SHADER_ENABLED))
  {
	int shaderNumber = std::stoi(settingId.substr(std::strlen(SETTING_LIB_PLACEBO_SHADER_ENABLED) + 1, 2));
	vs.m_PlaceboShadersEnabled[shaderNumber] = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
	appPlayer->SetVideoSettings(vs);
  }
  else if (settingId.starts_with(SETTING_LIB_PLACEBO_SHADER_PARAM))
  {
	int shaderNumber = std::stoi(settingId.substr(std::strlen(SETTING_LIB_PLACEBO_SHADER_PARAM) + 1, 2));
	int parameterNumber = std::stoi(settingId.substr(std::strlen(SETTING_LIB_PLACEBO_SHADER_PARAM) + 4, 2));
	const pl_hook* hook = vs.m_PlaceboShadersHooks.m_Hooks[shaderNumber].get();
	if (hook->parameters[parameterNumber].type == PL_VAR_FLOAT)
	{
	  float value = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
	  hook->parameters[parameterNumber].data->f = value;
	  vs.m_PlaceboShadersParams[shaderNumber][parameterNumber].m_Value = value;

	}
	else if (hook->parameters[parameterNumber].type == PL_VAR_SINT)
	{
	  int value = static_cast<int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
	  hook->parameters[parameterNumber].data->i = value;
	  vs.m_PlaceboShadersParams[shaderNumber][parameterNumber].m_Value = value;
	}
	else if (hook->parameters[parameterNumber].type == PL_VAR_UINT)
	{
	  unsigned int value = static_cast<unsigned int>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue()); //cl CSettingUint not supported
	  hook->parameters[parameterNumber].data->u = value;
	  vs.m_PlaceboShadersParams[shaderNumber][parameterNumber].m_Value = value;
	}
	appPlayer->SetVideoSettings(vs);
  }
}




void CGUIDialogVideoSettings::OnSettingAction(const std::shared_ptr<const CSetting>& setting)
{
  auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();

  if (setting == NULL)
	return;

  CGUIDialogSettingsManualBase::OnSettingChanged(setting);

  const std::string& settingId = setting->GetId();
  CVideoSettings vs = appPlayer->GetVideoSettings();
  pl_options m_placeboOptions = vs.m_placeboOptions->getPlOptions();
  if (settingId == SETTING_VIDEO_CALIBRATION)
  {
	const std::shared_ptr<CProfileManager> profileManager = CServiceBroker::GetSettingsComponent()->GetProfileManager();

	auto settingsComponent = CServiceBroker::GetSettingsComponent();
	if (!settingsComponent)
	  return;

	auto settings = settingsComponent->GetSettings();
	if (!settings)
	  return;

	auto calibsetting = settings->GetSetting(CSettings::SETTING_VIDEOSCREEN_GUICALIBRATION);
	if (!calibsetting)
	{
	  CLog::Log(LOGERROR, "Failed to load setting for: {}",
		CSettings::SETTING_VIDEOSCREEN_GUICALIBRATION);
	  return;
	}

	// launch calibration window
	if (profileManager->GetMasterProfile().getLockMode() != LockMode::EVERYONE &&
	  g_passwordManager.CheckSettingLevelLock(calibsetting->GetLevel()))
	  return;

	CServiceBroker::GetGUI()->GetWindowManager().ForceActivateWindow(WINDOW_SCREEN_CALIBRATION);
  }
  else if (settingId == SETTING_VIDEO_MAKE_DEFAULT)
  {
	Save();
  }
  else if (settingId == SETTING_LIB_PLACEBO_LOAD_PRESET_DEFAULT)
  {
	if (CGUIDialogYesNo::ShowAndGetInput(CVariant(55231), CVariant(55339)))
	{
	  vs.ResetRenderSettings(PlOptionsWrapper::DEFAULT);
	  CPLHelper::UpdateVideoSettingsFromLibPLaceboParams(vs);
	  appPlayer->SetVideoSettings(vs);
	  SetupView();
	}
  }
  else if (settingId == SETTING_LIB_PLACEBO_LOAD_PRESET_FAST)
  {
	if (CGUIDialogYesNo::ShowAndGetInput(CVariant(55232), CVariant(55339)))
	{
	  vs.ResetRenderSettings(PlOptionsWrapper::FAST);
	  CPLHelper::UpdateVideoSettingsFromLibPLaceboParams(vs);
	  appPlayer->SetVideoSettings(vs);
	  SetupView();
	}
  }
  else if (settingId == SETTING_LIB_PLACEBO_LOAD_PRESET_HIGH_QUALITY)
  {
	if (CGUIDialogYesNo::ShowAndGetInput(CVariant(55233), CVariant(55339)))
	{
	  vs.ResetRenderSettings(PlOptionsWrapper::HIGH_QUALITY);
	  CPLHelper::UpdateVideoSettingsFromLibPLaceboParams(vs);
	  appPlayer->SetVideoSettings(vs);
	  SetupView();
	}
  }
  else if (settingId == SETTING_LIB_PLACEBO_PEAK_DETECT_LOAD_PRESET_DEFAULT)
  {
	if (CGUIDialogYesNo::ShowAndGetInput(CVariant(55234), CVariant(55339)))
	{
	  m_placeboOptions->peak_detect_params = pl_peak_detect_default_params;
	  CPLHelper::UpdateVideoSettingsFromLibPLaceboParams(vs);
	  appPlayer->SetVideoSettings(vs);
	  SetupView();
	}
  }
  else if (settingId == SETTING_LIB_PLACEBO_PEAK_DETECT_LOAD_PRESET_HIGH_QUALITY)
  {
	if (CGUIDialogYesNo::ShowAndGetInput(CVariant(55235), CVariant(55339)))
	{
	  m_placeboOptions->peak_detect_params = pl_peak_detect_high_quality_params;
	  CPLHelper::UpdateVideoSettingsFromLibPLaceboParams(vs);
	  appPlayer->SetVideoSettings(vs);
	  SetupView();
	}
  }
  else if (settingId == SETTING_LIB_PLACEBO_DEBAND_LOAD_PRESET)
  {
	if (CGUIDialogYesNo::ShowAndGetInput(CVariant(55245), CVariant(55339)))
	{
	  m_placeboOptions->deband_params = pl_deband_default_params;
	  CPLHelper::UpdateVideoSettingsFromLibPLaceboParams(vs);
	  appPlayer->SetVideoSettings(vs);
	  SetupView();
	}
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_LOAD_PRESET_DEFAULT)
  {
	if (CGUIDialogYesNo::ShowAndGetInput(CVariant(55246), CVariant(55339)))
	{
	  m_placeboOptions->color_map_params = pl_color_map_default_params;
	  CPLHelper::UpdateVideoSettingsFromLibPLaceboParams(vs);
	  appPlayer->SetVideoSettings(vs);
	  SetupView();
	}
  }
  else if (settingId == SETTING_LIB_PLACEBO_COLOR_MAP_LOAD_PRESET_HIGH_QUALITY)
  {
	if (CGUIDialogYesNo::ShowAndGetInput(CVariant(55247), CVariant(55339)))
	{
	  m_placeboOptions->color_map_params = pl_color_map_high_quality_params;
	  CPLHelper::UpdateVideoSettingsFromLibPLaceboParams(vs);
	  appPlayer->SetVideoSettings(vs);
	  SetupView();
	}
  }
  else if (settingId == SETTING_LIB_PLACEBO_SIGMOID_LOAD_PRESET_DEFAULT)
  {
	if (CGUIDialogYesNo::ShowAndGetInput(CVariant(55255), CVariant(55339)))
	{
	  m_placeboOptions->sigmoid_params = pl_sigmoid_default_params;
	  CPLHelper::UpdateVideoSettingsFromLibPLaceboParams(vs);
	  appPlayer->SetVideoSettings(vs);
	  SetupView();
	}
  }
  else if (settingId == SETTING_LIB_PLACEBO_DITHER_LOAD_PRESET_DEFAULT)
  {
	if (CGUIDialogYesNo::ShowAndGetInput(CVariant(55261), CVariant(55339)))
	{
	  m_placeboOptions->dither_params = pl_dither_default_params; //cl move to ResetDitherSettings()
	  vs.ResetDitherSettings(PlOptionsWrapper::DEFAULT);
	  CPLHelper::UpdateVideoSettingsFromLibPLaceboParams(vs);
	  appPlayer->SetVideoSettings(vs);
	  SetupView();
	}
  }
  else if(settingId == SETTING_LIB_PLACEBO_SDR_TO_HDR_LOAD_PRESET)
  {
	if(CGUIDialogYesNo::ShowAndGetInput(CVariant(55353), CVariant(55339)))
	{
	  vs.ResetSdrToHdrSettings(PlOptionsWrapper::DEFAULT);
	  //cl CPLHelper::UpdateVideoSettingsFromLibPLaceboParams(vs);
	  appPlayer->SetVideoSettings(vs);
	  SetupView();
	}
  }
	
  else if (settingId == SETTING_LIB_PLACEBO_LOAD_FROM_FILE)
  {
    vs.ResetRenderSettings(PlOptionsWrapper::DEFAULT); //make sure all options are set, even if not present in the file
	CPLHelper::LoadLibplaceboSettings(vs);
	appPlayer->SetVideoSettings(vs);
	SetupView();
  }
  else if (settingId == SETTING_LIB_PLACEBO_SAVE_TO_FILE)
  {
	SaveLibplaceboSettings(vs);
	appPlayer->SetVideoSettings(vs);
	SetupView();
  }
  else if (settingId == SETTING_LIB_PLACEBO_SHADER_ADD)
  {
	std::string path;
	if (!CGUIDialogFileBrowser::ShowAndGetFile("special://masterprofile/", ".glsl|.hook", CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(55334), path))
	{
	  return;
	}

	CLog::Log(LOGDEBUG, "CGUIDialogVideoSettings: loading shader file from {}", path);
	CPLHelper::AddShaderFile(PL::PLInstance::Get()->GetGpu(), vs, path);
	appPlayer->SetVideoSettings(vs);
	SetupView();
  }
  else if (settingId.starts_with(SETTING_LIB_PLACEBO_SHADER_REMOVE))
  {
	int shaderNumber = std::atoi(settingId.substr(settingId.find_last_of('_') + 1).c_str());

	vs.m_PlaceboShadersFilename.erase(vs.m_PlaceboShadersFilename.begin() + shaderNumber);
	vs.m_PlaceboShadersEnabled.erase(vs.m_PlaceboShadersEnabled.begin() + shaderNumber);
	vs.m_PlaceboShadersParams.erase(vs.m_PlaceboShadersParams.begin() + shaderNumber);
	vs.m_PlaceboShadersHooks.erase(shaderNumber);

	appPlayer->SetVideoSettings(vs);
	SetupView();
  }
  else if (settingId.starts_with(SETTING_LIB_PLACEBO_SHADER_MOVE_UP))
  {
	int shaderNumber = std::atoi(settingId.substr(settingId.find_last_of('_') + 1).c_str());
	if (shaderNumber > 0)
	{
	  std::swap(vs.m_PlaceboShadersHooks.m_FileNames[shaderNumber], vs.m_PlaceboShadersHooks.m_FileNames[shaderNumber - 1]);
	  std::swap(vs.m_PlaceboShadersHooks.m_Hooks[shaderNumber], vs.m_PlaceboShadersHooks.m_Hooks[shaderNumber - 1]);
	  bool temp = vs.m_PlaceboShadersHooks.m_Valid[shaderNumber];
	  vs.m_PlaceboShadersHooks.m_Valid[shaderNumber] = vs.m_PlaceboShadersHooks.m_Valid[shaderNumber - 1];
	  vs.m_PlaceboShadersHooks.m_Valid[shaderNumber - 1] = temp;
	  std::swap(vs.m_PlaceboShadersFilename[shaderNumber], vs.m_PlaceboShadersFilename[shaderNumber - 1]);
	  std::swap(vs.m_PlaceboShadersParams[shaderNumber], vs.m_PlaceboShadersParams[shaderNumber - 1]);
	}
	appPlayer->SetVideoSettings(vs);
	SetupView();
  }
  else if (settingId.starts_with(SETTING_LIB_PLACEBO_SHADER_MOVE_DOWN))
  {
	int shaderNumber = std::atoi(settingId.substr(settingId.find_last_of('_') + 1).c_str());
	if (shaderNumber < vs.m_PlaceboShadersHooks.m_FileNames.size() - 1)
	{
	  std::swap(vs.m_PlaceboShadersHooks.m_FileNames[shaderNumber], vs.m_PlaceboShadersHooks.m_FileNames[shaderNumber + 1]);
	  std::swap(vs.m_PlaceboShadersHooks.m_Hooks[shaderNumber], vs.m_PlaceboShadersHooks.m_Hooks[shaderNumber + 1]);
	  bool temp = vs.m_PlaceboShadersHooks.m_Valid[shaderNumber];
	  vs.m_PlaceboShadersHooks.m_Valid[shaderNumber] = vs.m_PlaceboShadersHooks.m_Valid[shaderNumber + 1];
	  vs.m_PlaceboShadersHooks.m_Valid[shaderNumber + 1] = temp;
	  std::swap(vs.m_PlaceboShadersFilename[shaderNumber], vs.m_PlaceboShadersFilename[shaderNumber + 1]);
	  std::swap(vs.m_PlaceboShadersParams[shaderNumber], vs.m_PlaceboShadersParams[shaderNumber + 1]);
	}
	appPlayer->SetVideoSettings(vs);
	SetupView();
  }
}

bool CGUIDialogVideoSettings::Save()
{
  const std::shared_ptr<CProfileManager> profileManager = CServiceBroker::GetSettingsComponent()->GetProfileManager();

  if (profileManager->GetMasterProfile().getLockMode() != LockMode::EVERYONE &&
	!g_passwordManager.CheckSettingLevelLock(::SettingLevel::Expert))
	return true;

  // prompt user if they are sure
  if (CGUIDialogYesNo::ShowAndGetInput(CVariant(12376), CVariant(12377)))
  { 
	// reset the settings
	CVideoDatabase db;
	if (!db.Open())
	  return true;
	db.EraseAllVideoSettings();
	db.Close();

	const auto& components = CServiceBroker::GetAppComponents();
	const auto appPlayer = components.GetComponent<CApplicationPlayer>();

	CMediaSettings::GetInstance().SetDefaultVideoSettings(appPlayer->GetVideoSettings());
	CMediaSettings::GetInstance().GetDefaultVideoSettings().m_SubtitleStream = -1;
	CMediaSettings::GetInstance().GetDefaultVideoSettings().m_AudioStream = -1;
	CServiceBroker::GetSettingsComponent()->GetSettings()->Save();
  }
  return true;
}

void CGUIDialogVideoSettings::SaveLibplaceboSettings(const CVideoSettings& vs)
{
  const std::shared_ptr<CProfileManager> profileManager = CServiceBroker::GetSettingsComponent()->GetProfileManager();

  // prompt for a name
  std::string fileName;
  std::string filePath;
  if (!CGUIKeyboardFactory::ShowAndGetInput(fileName, CVariant{ CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(55323) }, false) || fileName.empty())
  {
	return;
  }

  if (!URIUtils::HasExtension(fileName, ".xml"))
	fileName += ".xml";
  filePath = URIUtils::AddFileToFolder("special://masterprofile/", fileName);
  if (CFile::Exists(filePath))
  {
	if (!CGUIDialogYesNo::ShowAndGetInput(CVariant(55323), CVariant(55340)))
	  return;
  }

  CPLHelper::SaveLibplaceboSettings(vs, filePath);
}

void CPLHelper::LoadLibplaceboSettings(CVideoSettings& vs)
{
  std::string path;
  if (!CGUIDialogFileBrowser::ShowAndGetFile("special://masterprofile/", ".xml", CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(55322), path))
  {
	return;
  }

  if (!CGUIDialogYesNo::ShowAndGetInput(CVariant(55322), CVariant(55339)))
  {
	return;
  }

  CXBMCTinyXML xmlDoc;
  if (!xmlDoc.LoadFile(path))
  {
	CLog::Log(LOGERROR, "CGUIDialogVideoSettings: Error loading LipPlacebo settings {}, Line {}\n{}", path, xmlDoc.ErrorRow(), xmlDoc.ErrorDesc());
	return;
  }
  CLog::Log(LOGDEBUG, "CGUIDialogVideoSettings: loading LipPlacebo settings from {}", path);
  CPLHelper::LoadLibplaceboSettings(vs, path);
}



void CGUIDialogVideoSettings::SetupView()
{
  CGUIDialogSettingsManualBase::SetupView();

  SetHeading(13395);
  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_OKAY_BUTTON);
  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_CUSTOM_BUTTON);
  SET_CONTROL_LABEL(CONTROL_SETTINGS_CANCEL_BUTTON, 15067);
}


bool CGUIDialogVideoSettings::OnMessage(CGUIMessage& message)
{
  switch (message.GetMessage())
  {
  case GUI_MSG_WINDOW_DEINIT:
  {
	{
	  auto& components = CServiceBroker::GetAppComponents();
	  auto appPlayer = components.GetComponent<CApplicationPlayer>();
	  CVideoSettings videoSettings = appPlayer->GetVideoSettings();

	  CLog::Log(LOGINFO, "CGUIDialogVideoSettings: Setting resetting skinzoom to 0");
	  videoSettings.m_PlaceboSkinZoomHint = 0;
	  appPlayer->SetVideoSettings(videoSettings);
	  CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_WINDOW_RESIZE);
	  CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(msg);
	}
  }
  }
  return CGUIDialogSettingsBase::OnMessage(message);
}

bool CGUIDialogVideoSettings::OnBack(int actionID)
{
  return CGUIDialogSettingsBase::OnBack(actionID);
}

//-------------------------------------------------------
// 
// 
// 
//-------------------------------------------------------
void CGUIDialogVideoSettings::InitializeSettings()
{
  bool bHdr = true;
  CGUIDialogSettingsManualBase::InitializeSettings();

  int renderMethod = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_VIDEOPLAYER_RENDERMETHOD);


  auto& components = CServiceBroker::GetAppComponents();
  auto appPlayer = components.GetComponent<CApplicationPlayer>();
  CVideoSettings videoSettings = appPlayer->GetVideoSettings();

  if (renderMethod == RENDER_METHOD_LIBPLACEBO)
  {
	VideoStreamInfo info;
	appPlayer->GetVideoStreamInfo(appPlayer->GetVideoStream(), info);
	bHdr = info.hdrType != StreamHdrType::HDR_TYPE_NONE;

	videoSettings.m_PlaceboSkinZoomHint = videoSettings.m_PlaceboSkinZoom;
	appPlayer->SetVideoSettings(videoSettings);
	CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_WINDOW_RESIZE);
	CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(msg);
  }

  const std::shared_ptr<CSettingCategory> category = AddCategory("videosettings", -1);
  if (category == NULL)
  {
	CLog::Log(LOGERROR, "CGUIDialogVideoSettings: unable to setup settings");
	return;
  }

  // get all necessary setting groups
  CreateGroup(groupVideoStream, category);
  CreateGroup(groupVideo, category);
  CreateGroup(groupStereoscopic, category);
  CreateGroup(groupSaveAsDefault, category);
  CreateGroup(groupLpFile, category);
  CreateGroup(groupLpReset, category);
  CreateGroup(groupOptions, category);
  CreateGroup(groupColorAjustment, category);
  CreateGroup(groupPeakDetect, category);
  CreateGroup(groupColorMap, category);
  CreateGroup(groupToneMappingConstants, category);
  CreateGroup(groupGamutMappingConstants, category);
  CreateGroup(groupSdr, category);
  CreateGroup(groupScaler, category);
  CreateGroup(groupMixer, category);
  CreateGroup(groupDeinterlace, category);
  CreateGroup(groupDeband, category);
  CreateGroup(groupSigmoid, category);
  CreateGroup(groupDither, category);
  CreateGroup(groupCone, category);
  CreateGroup(groupMisc, category);
  CreateGroup(groupLut, category);

  auto skin = CServiceBroker::GetGUI()->GetSkinInfo();
  const bool usePopup = skin && skin->HasSkinFile("DialogSlider.xml");

  TranslatableIntegerSettingOptions entries;

  // cl not sure how to handle interlacing...
  if (renderMethod == RENDER_METHOD_LIBPLACEBO)
  {
  }
  else
  {
	entries.clear();
	entries.emplace_back(16039, VS_INTERLACEMETHOD_NONE);
	entries.emplace_back(16019, VS_INTERLACEMETHOD_AUTO);
	entries.emplace_back(20131, VS_INTERLACEMETHOD_RENDER_BLEND);
	entries.emplace_back(20129, VS_INTERLACEMETHOD_RENDER_WEAVE);
	entries.emplace_back(16021, VS_INTERLACEMETHOD_RENDER_BOB);
	entries.emplace_back(16020, VS_INTERLACEMETHOD_DEINTERLACE);
	entries.emplace_back(16036, VS_INTERLACEMETHOD_DEINTERLACE_HALF);
	entries.emplace_back(16311, VS_INTERLACEMETHOD_VDPAU_TEMPORAL_SPATIAL);
	entries.emplace_back(16310, VS_INTERLACEMETHOD_VDPAU_TEMPORAL);
	entries.emplace_back(16325, VS_INTERLACEMETHOD_VDPAU_BOB);
	entries.emplace_back(16318, VS_INTERLACEMETHOD_VDPAU_TEMPORAL_SPATIAL_HALF);
	entries.emplace_back(16317, VS_INTERLACEMETHOD_VDPAU_TEMPORAL_HALF);
	entries.emplace_back(16327, VS_INTERLACEMETHOD_VAAPI_BOB);
	entries.emplace_back(16328, VS_INTERLACEMETHOD_VAAPI_MADI);
	entries.emplace_back(16329, VS_INTERLACEMETHOD_VAAPI_MACI);
	entries.emplace_back(16320, VS_INTERLACEMETHOD_DXVA_AUTO);

	/* remove unsupported methods */
	for (TranslatableIntegerSettingOptions::iterator it = entries.begin(); it != entries.end(); )
	{
	  if (appPlayer->Supports(static_cast<EINTERLACEMETHOD>(it->value)))
		++it;
	  else
		it = entries.erase(it);
	}
	if (!entries.empty())
	{
	  EINTERLACEMETHOD method = videoSettings.m_InterlaceMethod;
	  if (!appPlayer->Supports(method))
	  {
		method = appPlayer->GetDeinterlacingMethodDefault();
	  }
	  AddSpinner(groupVideo, SETTING_VIDEO_INTERLACEMETHOD, 16038, SettingLevel::Basic, static_cast<int>(method), entries);
	}

	entries.clear();
	entries.emplace_back(16301, VS_SCALINGMETHOD_NEAREST);
	entries.emplace_back(16302, VS_SCALINGMETHOD_LINEAR);
	entries.emplace_back(16303, VS_SCALINGMETHOD_CUBIC_B_SPLINE);
	entries.emplace_back(16314, VS_SCALINGMETHOD_CUBIC_MITCHELL);
	entries.emplace_back(16321, VS_SCALINGMETHOD_CUBIC_CATMULL);
	entries.emplace_back(16326, VS_SCALINGMETHOD_CUBIC_0_075);
	entries.emplace_back(16330, VS_SCALINGMETHOD_CUBIC_0_1);
	entries.emplace_back(16304, VS_SCALINGMETHOD_LANCZOS2);
	entries.emplace_back(16323, VS_SCALINGMETHOD_SPLINE36_FAST);
	entries.emplace_back(16315, VS_SCALINGMETHOD_LANCZOS3_FAST);
	entries.emplace_back(16322, VS_SCALINGMETHOD_SPLINE36);
	entries.emplace_back(16305, VS_SCALINGMETHOD_LANCZOS3);
	entries.emplace_back(16306, VS_SCALINGMETHOD_SINC8);
	entries.emplace_back(16307, VS_SCALINGMETHOD_BICUBIC_SOFTWARE);
	entries.emplace_back(16308, VS_SCALINGMETHOD_LANCZOS_SOFTWARE);
	entries.emplace_back(16309, VS_SCALINGMETHOD_SINC_SOFTWARE);
	entries.emplace_back(13120, VS_SCALINGMETHOD_VDPAU_HARDWARE);
	entries.emplace_back(16319, VS_SCALINGMETHOD_DXVA_HARDWARE);
	entries.emplace_back(16316, VS_SCALINGMETHOD_AUTO);

	/* remove unsupported methods */
	for (TranslatableIntegerSettingOptions::iterator it = entries.begin(); it != entries.end(); )
	{
	  if (appPlayer->Supports(static_cast<ESCALINGMETHOD>(it->value)))
		++it;
	  else
		it = entries.erase(it);
	}
	AddSpinner(groupVideo, SETTING_VIDEO_SCALINGMETHOD, 16300, SettingLevel::Basic, static_cast<int>(videoSettings.m_ScalingMethod), entries);
  }


  AddVideoStreams(groupVideoStream, SETTING_VIDEO_STREAM);

  if (appPlayer->Supports(RENDERFEATURE_STRETCH) || appPlayer->Supports(RENDERFEATURE_PIXEL_RATIO))
  {
	AddList(groupVideo, SETTING_VIDEO_VIEW_MODE, 629, SettingLevel::Basic, videoSettings.m_ViewMode, CViewModeSettings::ViewModesFiller, 629);
  }
  if (appPlayer->Supports(RENDERFEATURE_ZOOM))
	AddSlider(groupVideo, SETTING_VIDEO_ZOOM, 216, SettingLevel::Basic,
	  videoSettings.m_CustomZoomAmount, "{:2.2f}", 0.5f, 0.01f, 2.0f, 216, usePopup);
  if (appPlayer->Supports(RENDERFEATURE_VERTICAL_SHIFT))
	AddSlider(groupVideo, SETTING_VIDEO_VERTICAL_SHIFT, 225, SettingLevel::Basic,
	  videoSettings.m_CustomVerticalShift, "{:2.2f}", -2.0f, 0.01f, 2.0f, 225, usePopup);
  if (appPlayer->Supports(RENDERFEATURE_PIXEL_RATIO))
	AddSlider(groupVideo, SETTING_VIDEO_PIXEL_RATIO, 217, SettingLevel::Basic,
	  videoSettings.m_CustomPixelRatio, "{:2.3f}", 0.5f, 0.01f, 2.0f, 217, usePopup);
  if(renderMethod == RENDER_METHOD_LIBPLACEBO)
	AddSlider(groupVideo, SETTING_LIB_PLACEBO_CROP_BOTTOM, 55367, SettingLevel::Basic, videoSettings.m_PlaceboCropBottom, -1, 0, 1, 50, 55367);

  AddList(groupVideo, SETTING_VIDEO_ORIENTATION, 21843, SettingLevel::Basic, videoSettings.m_Orientation, CGUIDialogVideoSettings::VideoOrientationFiller, 21843);

  if (appPlayer->Supports(RENDERFEATURE_POSTPROCESS))    AddToggle(groupVideo, SETTING_VIDEO_POSTPROCESS, 16400, SettingLevel::Basic, videoSettings.m_PostProcess);
  if (renderMethod != RENDER_METHOD_LIBPLACEBO)
  {
	if (appPlayer->Supports(RENDERFEATURE_BRIGHTNESS))     AddPercentageSlider(groupVideo, SETTING_VIDEO_BRIGHTNESS, 464, SettingLevel::Basic, static_cast<int>(videoSettings.m_Brightness), 14047, 1, 464, usePopup);
	if (appPlayer->Supports(RENDERFEATURE_CONTRAST))       AddPercentageSlider(groupVideo, SETTING_VIDEO_CONTRAST, 465, SettingLevel::Basic, static_cast<int>(videoSettings.m_Contrast), 14047, 1, 465, usePopup);
	if (appPlayer->Supports(RENDERFEATURE_GAMMA))          AddPercentageSlider(groupVideo, SETTING_VIDEO_GAMMA, 466, SettingLevel::Basic, static_cast<int>(videoSettings.m_Gamma), 14047, 1, 466, usePopup);
  }
  if (appPlayer->Supports(RENDERFEATURE_NOISE))          AddSlider(groupVideo, SETTING_VIDEO_VDPAU_NOISE, 16312, SettingLevel::Basic, videoSettings.m_NoiseReduction, "{:2.2f}", 0.0f, 0.01f, 1.0f, 16312, usePopup);
  if (appPlayer->Supports(RENDERFEATURE_SHARPNESS))      AddSlider(groupVideo, SETTING_VIDEO_VDPAU_SHARPNESS, 16313, SettingLevel::Basic, videoSettings.m_Sharpness, "{:2.2f}", -1.0f, 0.02f, 1.0f, 16313, usePopup);
  if (appPlayer->Supports(RENDERFEATURE_NONLINSTRETCH))  AddToggle(groupVideo, SETTING_VIDEO_NONLIN_STRETCH, 659, SettingLevel::Basic, videoSettings.m_CustomNonLinStretch);

  // tone mapping
  if (appPlayer->Supports(RENDERFEATURE_TONEMAP))
  {
	const bool visible = !CServiceBroker::GetWinSystem()->IsHDRDisplaySettingEnabled();
	entries.clear();
	entries.emplace_back(36554, VS_TONEMAPMETHOD_OFF);
	entries.emplace_back(36555, VS_TONEMAPMETHOD_REINHARD);
	entries.emplace_back(36557, VS_TONEMAPMETHOD_ACES);
	entries.emplace_back(36558, VS_TONEMAPMETHOD_HABLE);

	AddSpinner(groupVideo, SETTING_VIDEO_TONEMAP_METHOD, 36553, SettingLevel::Basic,
	  videoSettings.m_ToneMapMethod, entries, false, visible);
	AddSlider(groupVideo, SETTING_VIDEO_TONEMAP_PARAM, 36556, SettingLevel::Basic,
	  videoSettings.m_ToneMapParam, "{:2.2f}", 0.1f, 0.1f, 5.0f, 36556, usePopup, false,
	  visible);
  }

  // stereoscopic settings
  entries.clear();
  entries.emplace_back(16316, static_cast<int>(RenderStereoMode::OFF));
  entries.emplace_back(36503, static_cast<int>(RenderStereoMode::SPLIT_HORIZONTAL));
  entries.emplace_back(36504, static_cast<int>(RenderStereoMode::SPLIT_VERTICAL));
  AddSpinner(groupStereoscopic, SETTING_VIDEO_STEREOSCOPICMODE, 36535, SettingLevel::Basic, videoSettings.m_StereoMode, entries);
  AddToggle(groupStereoscopic, SETTING_VIDEO_STEREOSCOPICINVERT, 36536, SettingLevel::Basic, videoSettings.m_StereoInvert);

  // general settings
  AddButton(groupSaveAsDefault, SETTING_VIDEO_MAKE_DEFAULT, 12376, SettingLevel::Basic);
  AddButton(groupSaveAsDefault, SETTING_VIDEO_CALIBRATION, 214, SettingLevel::Basic);

  // libplacebo settings

  if (renderMethod == RENDER_METHOD_LIBPLACEBO)
  {
	entries.clear();
	entries.emplace_back(55358, VS_SZ_POSITION_UPPER_LEFT);
	entries.emplace_back(55359, VS_SZ_POSITION_UPPER_MIDDLE);
	entries.emplace_back(55360, VS_SZ_POSITION_UPPER_RIGHT);
	entries.emplace_back(55361, VS_SZ_POSITION_MIDDLE_RIGHT);
	entries.emplace_back(55362, VS_SZ_POSITION_BOTTOM_RIGHT);
	entries.emplace_back(55363, VS_SZ_POSITION_BOTTOM_MIDDLE);
	entries.emplace_back(55364, VS_SZ_POSITION_BOTTOM_LEFT);
	entries.emplace_back(55365, VS_SZ_POSITION_MIDDLE_LEFT);

	AddSlider(groupLpFile, SETTING_LIB_PLACEBO_SKIN_ZOOM, 55333, SettingLevel::Basic, videoSettings.m_PlaceboSkinZoom, -1, -80, 1, 0, 55292, false);
	AddSpinner(groupLpFile, SETTING_LIB_PLACEBO_SKIN_ZOOM_POSITION, 55357, SettingLevel::Basic, videoSettings.m_PlaceboSkinZoomPosition, entries);
	AddButton(groupLpFile, SETTING_LIB_PLACEBO_SAVE_TO_FILE, 55323, SettingLevel::Basic);
	AddButton(groupLpFile, SETTING_LIB_PLACEBO_LOAD_FROM_FILE, 55322, SettingLevel::Basic);

	AddButton(groupLpReset, SETTING_LIB_PLACEBO_LOAD_PRESET_DEFAULT, 55231, SettingLevel::Basic);
	AddButton(groupLpReset, SETTING_LIB_PLACEBO_LOAD_PRESET_FAST, 55232, SettingLevel::Basic);
	AddButton(groupLpReset, SETTING_LIB_PLACEBO_LOAD_PRESET_HIGH_QUALITY, 55233, SettingLevel::Basic);

	// Render Options
	AddSlider(groupOptions, SETTING_LIB_PLACEBO_DISPLAY_HDR_PEAK_LUMINANCE, 55313, SettingLevel::Basic, videoSettings.m_PlaceboDisplayHdrPeakLuminance, "{0:5.0f}", (float)0.0, (float)10, (float)10000.0, 55313, usePopup);

	entries.clear();
	entries.emplace_back(55315, static_cast<int>(SettinglibPlaceboTargetColorspaceHint::AUTO));
	entries.emplace_back(55316, static_cast<int>(SettinglibPlaceboTargetColorspaceHint::NO));
	entries.emplace_back(55317, static_cast<int>(SettinglibPlaceboTargetColorspaceHint::YES));
	AddSpinner(groupOptions, SETTING_LIB_PLACEBO_TARGET_COLORSPACE_HINT, 55314, SettingLevel::Basic, videoSettings.m_PlaceboTargetColorspaceHint, entries);
	entries.clear();
	entries.emplace_back(55319, static_cast<int>(SettinglibPlaceboTargetColorspaceHintMode::TARGET));
	entries.emplace_back(55320, static_cast<int>(SettinglibPlaceboTargetColorspaceHintMode::SOURCE));
	entries.emplace_back(55321, static_cast<int>(SettinglibPlaceboTargetColorspaceHintMode::SOURCE_DYNAMIC));
	AddSpinner(groupOptions, SETTING_LIB_PLACEBO_TARGET_COLORSPACE_HINT_MODE, 55318, SettingLevel::Basic, videoSettings.m_PlaceboTargetColorspaceHintMode, entries);

	// Color_Adjustment
	AddToggle(groupColorAjustment, SETTING_LIB_PLACEBO_COLOR_ADJUSTMENT_ENABLED, 55221, SettingLevel::Basic, videoSettings.m_PlaceboColorAdjustmentEnabled);
	//AddPercentageSlider(groupColorAjustment, SETTING_VIDEO_BRIGHTNESS, 464, SettingLevel::Basic, static_cast<int>(videoSettings.m_Brightness), 14047, 1, 464, usePopup);
	//AddPercentageSlider(groupColorAjustment, SETTING_VIDEO_CONTRAST, 465, SettingLevel::Basic, static_cast<int>(videoSettings.m_Contrast), 14047, 1, 465, usePopup);
	//AddPercentageSlider(groupColorAjustment, SETTING_VIDEO_GAMMA, 466, SettingLevel::Basic, static_cast<int>(videoSettings.m_Gamma), 14047, 1, 466, usePopup);
	AddSlider(groupColorAjustment, SETTING_VIDEO_BRIGHTNESS, 464, SettingLevel::Basic, videoSettings.m_Brightness, "{:2.2f}", 0.0f, 0.05f, 100.0f, 464, usePopup);
	AddSlider(groupColorAjustment, SETTING_VIDEO_CONTRAST, 465, SettingLevel::Basic, videoSettings.m_Contrast, "{:2.2f}", 0.0f, 0.02f, 100.0f, 465, usePopup);
	AddSlider(groupColorAjustment, SETTING_VIDEO_GAMMA, 466, SettingLevel::Basic, videoSettings.m_Gamma, "{:2.2f}", 0.0f, 0.1f, 100.0f, 466, usePopup);
	AddSlider(groupColorAjustment, SETTING_LIB_PLACEBO_HUE, 55222, SettingLevel::Basic, videoSettings.m_PlaceboHue, "{0:3.0f}", (float)0.0, (float)1.0, (float)360.0, 55222, usePopup);
	AddSlider(groupColorAjustment, SETTING_LIB_PLACEBO_SATURATION, 55210, SettingLevel::Basic, videoSettings.m_PlaceboSaturation, "{0:4.1f}", (float)0.0, (float)0.5, (float)100.0, 55210, usePopup);
	AddSlider(groupColorAjustment, SETTING_LIB_PLACEBO_TEMPERATURE, 55212, SettingLevel::Basic, videoSettings.m_PlaceboTemperature, "{0:6.0f}K", (float)1700, (float)10.0, (float)10000, 55212, usePopup);

	// Peak_Detection
	// cl parameters range conversion..., not just this group
	AddToggle(groupPeakDetect, SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_ENABLED, 55213, SettingLevel::Basic, videoSettings.m_PlaceboPeakDetectEnabled);
	AddButton(groupPeakDetect, SETTING_LIB_PLACEBO_PEAK_DETECT_LOAD_PRESET_DEFAULT, 55234, SettingLevel::Basic);
	AddButton(groupPeakDetect, SETTING_LIB_PLACEBO_PEAK_DETECT_LOAD_PRESET_HIGH_QUALITY, 55235, SettingLevel::Basic);
	AddSlider(groupPeakDetect, SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_SMOOTHING_PERIOD, 55214, SettingLevel::Basic, videoSettings.m_PlaceboPeakDetectSmoothingPeriod, "{0:4.0f}", (float)0.0, (float)1.0, (float)1000.0, 55214, usePopup);
	AddSlider(groupPeakDetect, SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_SCENE_THRESHOLD_LOW, 55215, SettingLevel::Basic, videoSettings.m_PlaceboPeakDetectSceneThresholdLow, "{0:5.1f}", (float)0.0, (float)0.1, (float)100.0, 55215, usePopup);
	AddSlider(groupPeakDetect, SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_SCENE_THRESHOLD_HIGH, 55216, SettingLevel::Basic, videoSettings.m_PlaceboPeakDetectSceneThresholdHigh, "{0:5.1f}", (float)0.0, (float)0.1, (float)100.0, 55216, usePopup);
	AddSlider(groupPeakDetect, SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_PERCENTILE, 55218, SettingLevel::Basic, videoSettings.m_PlaceboPeakDetectPercentile, "{0:6.3f}", (float)95.0, (float)0.001, (float)100.0, 55218, usePopup);
	AddSlider(groupPeakDetect, SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_BLACK_CUTOFF, 55219, SettingLevel::Basic, videoSettings.m_PlaceboPeakDetectBlackCutoff, "{0:5.1f}", (float)0.0, (float)0.1, (float)100.0, 55219, usePopup);
	// deprecated AddToggle(groupPeakDetect, SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_ALLOW_DELAYED,                     55220, SettingLevel::Basic, videoSettings.m_PlaceboPeakDetectAllowDelayed);

	// Scalers
	AddList(groupScaler, SETTING_LIB_PLACEBO_UPSCALER, 55223, SettingLevel::Basic, videoSettings.m_PlaceboUpscaler, CPLHelper::PlUpscalerOptionFiller, 55223);
	AddList(groupScaler, SETTING_LIB_PLACEBO_DOWNSCALER, 55224, SettingLevel::Basic, videoSettings.m_PlaceboDownscaler, CPLHelper::PlDownscalerOptionFiller, 55224);
	AddList(groupScaler, SETTING_LIB_PLACEBO_PLANE_UPSCALER, 55225, SettingLevel::Basic, videoSettings.m_PlaceboPlaneUpscaler, CPLHelper::PlUpscalerOptionFiller, 55225);
	AddList(groupScaler, SETTING_LIB_PLACEBO_PLANE_DOWNSCALER, 55226, SettingLevel::Basic, videoSettings.m_PlaceboPlaneDownscaler, CPLHelper::PlDownscalerOptionFiller, 55226);
	
	// Mixer
	AddList(groupMixer, SETTING_LIB_PLACEBO_FRAME_MIXER, 55227, SettingLevel::Basic, videoSettings.m_PlaceboFrameMixer, CPLHelper::PlFrameMixerOptionFiller, 55227);
	AddToggle(groupMixer, SETTING_LIB_PLACEBO_FRAME_MIXER_BYPASS_QUEUE, 55366, SettingLevel::Basic, videoSettings.m_PlaceboFrameMixerBypassQueue);
	//AddSlider(groupMixer, SETTING_LIB_PLACEBO_FRAME_MIXER_RADIUS_FACTOR, 55354, SettingLevel::Basic, videoSettings.m_PlaceboFrameMixerRadiusFactor, "{0:3.1f}", (float) 0.1, (float) 0.1, (float) 2.0, 55354, usePopup);
	//AddSlider(groupMixer, SETTING_LIB_PLACEBO_TEST, 55354, SettingLevel::Basic, videoSettings.m_PlaceboTest, "{0:4.0f}", (float) -100, (float) 5, (float) 100, 55354, usePopup);

	// Color map
	AddToggle(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_ENABLED, 55248, SettingLevel::Basic, videoSettings.m_PlaceboColorMapEnabled);
	AddButton(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_LOAD_PRESET_DEFAULT, 55246, SettingLevel::Basic);
	AddButton(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_LOAD_PRESET_HIGH_QUALITY, 55247, SettingLevel::Basic);
	AddList(groupColorMap,   SETTING_LIB_PLACEBO_COLOR_MAP_INTENT,55298,SettingLevel::Basic,videoSettings.m_PlaceboColorMapIntent,CPLHelper::PlColorMapIntentOptionFiller,55298);
	
	
	AddList(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_GAMUT_MAPPING, 55228, SettingLevel::Basic, videoSettings.m_PlaceboColorMapGamutMapping, CPLHelper::PlColorMapGamutMapFunctionOptionFiller, 55228);
	if(videoSettings.m_PlaceboColorMapGamutMapping != -1)
	{
	  std::string funcDesc = {};

	  if(pl_gamut_map_functions [videoSettings.m_PlaceboColorMapGamutMapping]->description)
		funcDesc = pl_gamut_map_functions [videoSettings.m_PlaceboColorMapGamutMapping]->description;

	  if((funcDesc == "Absolute colorimetric clip") || (funcDesc == "Darken and clip") || (funcDesc == "Colorimetric clip"))
	  {
		AddSlider(groupColorMap, SETTING_LIB_PLACEBO_GAMUT_CONSTANTS_COLORIMETRIC_GAMMA, 55286, SettingLevel::Basic, videoSettings.m_PlaceboGamutConstantsColorimetricGamma, "{0:4.1f}", (float) 0.0, (float) 0.1, (float) 10.0, 55286, usePopup);
	  }
	  else if((funcDesc == "Perceptual mapping"))
	  {
		AddSlider(groupColorMap, SETTING_LIB_PLACEBO_GAMUT_CONSTANTS_PERCEPTUAL_DEADZONE, 55287, SettingLevel::Basic, videoSettings.m_PlaceboGamutConstantsPerceptualDeadzone, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0, 55287, usePopup);
		AddSlider(groupColorMap, SETTING_LIB_PLACEBO_GAMUT_CONSTANTS_PERCEPTUAL_STRENGTH, 55349, SettingLevel::Basic, videoSettings.m_PlaceboGamutConstantsPerceptualStrength, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0, 55349, usePopup);
	  }
	  else if((funcDesc == "Soft clipping"))
	  {
		AddSlider(groupColorMap, SETTING_LIB_PLACEBO_GAMUT_CONSTANTS_SOFTCLIP_DESAT, 55288, SettingLevel::Basic, videoSettings.m_PlaceboGamutConstantsSoftclipDesat, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0, 55288, usePopup);
		AddSlider(groupColorMap, SETTING_LIB_PLACEBO_GAMUT_CONSTANTS_SOFTCLIP_KNEE, 55289, SettingLevel::Basic, videoSettings.m_PlaceboGamutConstantsSoftclipKnee, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0, 55289, usePopup);
	  }
	}
	
	InitializeToneMappingMenuHdr(videoSettings, groupColorMap);

	//AddInfoLabelButton(groupSdr, SETTING_LIB_PLACEBO_SDR_TO_HDR_LABEL, 55352, SettingLevel::Basic, " ");
	AddToggle(groupSdr, SETTING_LIB_PLACEBO_USE_HDR_FOR_SDR, 55351, SettingLevel::Basic, videoSettings.m_PlaceboUseHdrForSdr);
	AddButton(groupSdr, SETTING_LIB_PLACEBO_SDR_TO_HDR_LOAD_PRESET, 55353, SettingLevel::Basic);
	AddSlider(groupSdr, SETTING_LIB_PLACEBO_DISPLAY_SDR_PEAK_LUMINANCE, 55347, SettingLevel::Basic, videoSettings.m_PlaceboDisplaySdrPeakLuminance, "{0:5.0f}", (float) 0.0, (float) 10, (float) 10000.0, 55347, usePopup);
	AddSlider(groupSdr, SETTING_LIB_PLACEBO_SDR_SATURATION, 55210, SettingLevel::Basic, videoSettings.m_PlaceboSdrSaturation, "{0:4.1f}", (float) 0.0, (float) 0.5, (float) 100.0, 55210, usePopup);
	AddToggle(groupSdr, SETTING_LIB_PLACEBO_SDR_COLOR_MAP_INVERSE_TONE_MAPPING, 55291, SettingLevel::Basic, videoSettings.m_PlaceboSdrColorMapInverseToneMapping);
	AddToggle(groupSdr, SETTING_LIB_PLACEBO_SDR_COLOR_MAP_GAMUT_EXPANSION, 55290, SettingLevel::Basic, videoSettings.m_PlaceboSdrColorMapGamutExpansion);
	AddList(groupSdr, SETTING_LIB_PLACEBO_SDR_COLOR_MAP_INTENT, 55298, SettingLevel::Basic, videoSettings.m_PlaceboSdrColorMapIntent, CPLHelper::PlColorMapIntentOptionFiller, 55298);
	AddList(groupSdr, SETTING_LIB_PLACEBO_SDR_COLOR_MAP_GAMUT_MAPPING, 55228, SettingLevel::Basic, videoSettings.m_PlaceboSdrColorMapGamutMapping, CPLHelper::PlColorMapGamutMapFunctionOptionFiller, 55228);
	if(videoSettings.m_PlaceboSdrColorMapGamutMapping != -1)
	{
	  std::string funcDesc = {};

	  if(pl_gamut_map_functions [videoSettings.m_PlaceboSdrColorMapGamutMapping]->description)
		funcDesc = pl_gamut_map_functions [videoSettings.m_PlaceboSdrColorMapGamutMapping]->description;

	  if((funcDesc == "Absolute colorimetric clip") || (funcDesc == "Darken and clip") || (funcDesc == "Colorimetric clip"))
	  {
		AddSlider(groupSdr, SETTING_LIB_PLACEBO_SDR_GAMUT_CONSTANTS_COLORIMETRIC_GAMMA, 55286, SettingLevel::Basic, videoSettings.m_PlaceboSdrGamutConstantsColorimetricGamma, "{0:4.1f}", (float) 0.0, (float) 0.1, (float) 10.0, 55286, usePopup);
	  }
	  else if((funcDesc == "Perceptual mapping"))
	  {
		AddSlider(groupSdr, SETTING_LIB_PLACEBO_SDR_GAMUT_CONSTANTS_PERCEPTUAL_DEADZONE, 55287, SettingLevel::Basic, videoSettings.m_PlaceboSdrGamutConstantsPerceptualDeadzone, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0, 55287, usePopup);
		AddSlider(groupSdr, SETTING_LIB_PLACEBO_SDR_GAMUT_CONSTANTS_PERCEPTUAL_STRENGTH, 55349, SettingLevel::Basic, videoSettings.m_PlaceboSdrGamutConstantsPerceptualStrength, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0, 55349, usePopup);
	  }
	  else if((funcDesc == "Soft clipping"))
	  {
		AddSlider(groupSdr, SETTING_LIB_PLACEBO_SDR_GAMUT_CONSTANTS_SOFTCLIP_DESAT, 55288, SettingLevel::Basic, videoSettings.m_PlaceboSdrGamutConstantsSoftclipDesat, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0, 55288, usePopup);
		AddSlider(groupSdr, SETTING_LIB_PLACEBO_SDR_GAMUT_CONSTANTS_SOFTCLIP_KNEE, 55289, SettingLevel::Basic, videoSettings.m_PlaceboSdrGamutConstantsSoftclipKnee, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0, 55289, usePopup);
	  }
	}
	InitializeToneMappingMenuSdr(videoSettings, groupSdr);

	AddSlider(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_CONTRAST_RECOVERY, 55284, SettingLevel::Basic, videoSettings.m_PlaceboColorMapContrastRecovery, "{0:4.2f}", (float)0.0, (float)0.01, (float)2.0, 55284, usePopup);
	AddSlider(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_CONTRAST_SMOOTHNESS, 55285, SettingLevel::Basic, videoSettings.m_PlaceboColorMapContrastSmoothness, "{0:4.1f}", (float)1.0, (float)0.1, (float)32.0, 55285, usePopup);
	AddToggle(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_GAMUT_EXPANSION, 55290, SettingLevel::Basic, videoSettings.m_PlaceboColorMapGamutExpansion);

	AddToggle(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_INVERSE_TONE_MAPPING, 55291, SettingLevel::Basic, videoSettings.m_PlaceboColorMapInverseToneMapping);
	AddToggle(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_SHOW_CLIPPING, 55297, SettingLevel::Basic, videoSettings.m_PlaceboColorMapShowClipping);
	AddToggle(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_FORCE_TONE_MAPPING_LUT, 55299, SettingLevel::Basic, videoSettings.m_PlaceboColorMapForceToneMappingLut);
	AddToggle(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_LUT, 55277, SettingLevel::Basic, videoSettings.m_PlaceboColorMapVisualizeLut);
	AddSlider(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_RECT_X0, 55278, SettingLevel::Basic, videoSettings.m_PlaceboColorMapVisualizeRectX0, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0, 55278, usePopup);
	AddSlider(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_RECT_X1, 55279, SettingLevel::Basic, videoSettings.m_PlaceboColorMapVisualizeRectX1, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0, 55279, usePopup);
	AddSlider(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_RECT_Y0, 55280, SettingLevel::Basic, videoSettings.m_PlaceboColorMapVisualizeRectY0, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0, 55280, usePopup);
	AddSlider(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_RECT_Y1, 55281, SettingLevel::Basic, videoSettings.m_PlaceboColorMapVisualizeRectY1, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0, 55281, usePopup);
	AddSlider(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_HUE, 55282, SettingLevel::Basic, videoSettings.m_PlaceboColorMapVisualizeHue, "{0:3.0f}", (float) 0.0, (float) 1.0, (float) 360.0, 55282, usePopup);
	AddSlider(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_VISUALIZE_THETA, 55283, SettingLevel::Basic, videoSettings.m_PlaceboColorMapVisualizeTheta, "{0:3.0f}", (float) 0.0, (float) 1.0, (float) 360.0, 55283, usePopup);
	AddSlider(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_LUT3D_SIZE_I, 55292, SettingLevel::Basic, videoSettings.m_PlaceboColorMapLut3dSizeI, -1, 0, 1, 1024, 55292, usePopup);
	AddSlider(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_LUT3D_SIZE_C, 55293, SettingLevel::Basic, videoSettings.m_PlaceboColorMapLut3dSizeC, -1, 0, 1, 1024, 55293, usePopup);
	AddSlider(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_LUT3D_SIZE_H, 55294, SettingLevel::Basic, videoSettings.m_PlaceboColorMapLut3dSizeH, -1, 0, 1, 1024, 55294, usePopup);
	AddToggle(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_LUT3D_TRICUBIC, 55295, SettingLevel::Basic, videoSettings.m_PlaceboColorMapLut3dTricubic);
	AddSlider(groupColorMap, SETTING_LIB_PLACEBO_COLOR_MAP_LUT_SIZE, 55296, SettingLevel::Basic, videoSettings.m_PlaceboColorMapLutSize, -1, 0, 1, 1024, 55296, usePopup);

	// Deband
	AddToggle(groupDeband, SETTING_LIB_PLACEBO_DEBAND_ENABLED, 55237, SettingLevel::Basic, videoSettings.m_PlaceboDebandEnabled);
	AddButton(groupDeband, SETTING_LIB_PLACEBO_DEBAND_LOAD_PRESET, 55245, SettingLevel::Basic);
	AddSlider(groupDeband, SETTING_LIB_PLACEBO_DEBAND_GRAIN, 55238, SettingLevel::Basic, videoSettings.m_PlaceboDebandGrain, "{0:4.0f}", (float)0.0, (float)1.0, (float)1000.0, 55238, usePopup);
	AddSlider(groupDeband, SETTING_LIB_PLACEBO_DEBAND_GRAIN_NEUTRAL0, 55239, SettingLevel::Basic, videoSettings.m_PlaceboDebandGrainNeutral0, "{0:4.0f}", (float)0.0, (float)1.0, (float)1000.0, 55239, usePopup);
	AddSlider(groupDeband, SETTING_LIB_PLACEBO_DEBAND_GRAIN_NEUTRAL1, 55240, SettingLevel::Basic, videoSettings.m_PlaceboDebandGrainNeutral1, "{0:4.0f}", (float)0.0, (float)1.0, (float)1000.0, 55240, usePopup);
	AddSlider(groupDeband, SETTING_LIB_PLACEBO_DEBAND_GRAIN_NEUTRAL2, 55241, SettingLevel::Basic, videoSettings.m_PlaceboDebandGrainNeutral2, "{0:4.0f}", (float)0.0, (float)1.0, (float)1000.0, 55241, usePopup);
	AddSlider(groupDeband, SETTING_LIB_PLACEBO_DEBAND_ITERATIONS, 55242, SettingLevel::Basic, videoSettings.m_PlaceboDebandIterations, -1, 0, 1, 16, 55242, usePopup);
	AddSlider(groupDeband, SETTING_LIB_PLACEBO_DEBAND_RADIUS, 55243, SettingLevel::Basic, videoSettings.m_PlaceboDebandRadius, "{0:4.0f}", (float)0.0, (float)1.0, (float)1000.0, 55243, usePopup);
	AddSlider(groupDeband, SETTING_LIB_PLACEBO_DEBAND_THRESHOLD, 55244, SettingLevel::Basic, videoSettings.m_PlaceboDebandThreshold, "{0:4.0f}", (float)0.0, (float)1.0, (float)1000.0, 55244, usePopup);

	// Deinterlace
	AddToggle(groupDeinterlace, SETTING_LIB_PLACEBO_DEINTERLACE_ENABLED, 55249, SettingLevel::Basic, videoSettings.m_PlaceboDeinterlaceEnabled);
	AddList(groupDeinterlace,   SETTING_LIB_PLACEBO_DEINTERLACE_ALGO, 55250, SettingLevel::Basic, videoSettings.m_PlaceboDeinterlaceAlgo, CPLHelper::PlDeinterlaceAlgoOptionFiller, 55250);
	AddToggle(groupDeinterlace, SETTING_LIB_PLACEBO_DEINTERLACE_SKIP_SPATIAL_CHECK, 55251, SettingLevel::Basic, videoSettings.m_PlaceboDeinterlaceSkipSpatialCheck);

	// Sigmoid 
	AddToggle(groupSigmoid, SETTING_LIB_PLACEBO_SIGMOID_ENABLED, 55252, SettingLevel::Basic, videoSettings.m_PlaceboSigmoidEnabled);
	AddButton(groupSigmoid, SETTING_LIB_PLACEBO_SIGMOID_LOAD_PRESET_DEFAULT, 55255, SettingLevel::Basic);
	AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_SIGMOID_CENTER, 55253, SettingLevel::Basic, videoSettings.m_PlaceboSigmoidCenter, "{0:4.2f}", (float)0.0, (float)0.01, (float)1.0, 55253, usePopup);
	AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_SIGMOID_SLOPE, 55254, SettingLevel::Basic, videoSettings.m_PlaceboSigmoidSlope, "{0:4.1f}", (float)1.0, (float)1.0, (float)20.0, 55254, usePopup);

	// Cones 
	AddToggle(groupCone, SETTING_LIB_PLACEBO_CONE_ENABLED, 55256, SettingLevel::Basic, videoSettings.m_PlaceboConeEnabled);
	AddList(groupCone,   SETTING_LIB_PLACEBO_CONE_CONES, 55258, SettingLevel::Basic, videoSettings.m_PlaceboConeCones, CPLHelper::PlConeConesOptionFiller, 55258);
	AddSlider(groupCone, SETTING_LIB_PLACEBO_CONE_STRENGTH, 55259, SettingLevel::Basic, videoSettings.m_PlaceboConeStrength, "{0:5.2f}", (float)0.0, (float)0.01, (float)10.0, 55259, usePopup);

	// Dither
	AddToggle(groupDither, SETTING_LIB_PLACEBO_DITHER_ENABLED, 55260, SettingLevel::Basic, videoSettings.m_PlaceboDitherEnabled);
	AddButton(groupDither, SETTING_LIB_PLACEBO_DITHER_LOAD_PRESET_DEFAULT, 55261, SettingLevel::Basic);
	AddList(groupDither,   SETTING_LIB_PLACEBO_DITHER_METHOD, 55262, SettingLevel::Basic, videoSettings.m_PlaceboDitherMethod, CPLHelper::PlDitherMethodOptionFiller, 55262);
	AddSlider(groupDither, SETTING_LIB_PLACEBO_DITHER_DEPTH, 55350, SettingLevel::Basic, videoSettings.m_PlaceboDitherDepth, -1, 0, 1, CRendererPL::getColorDepth(), 555350, usePopup); //cl min/max
	AddSlider(groupDither, SETTING_LIB_PLACEBO_DITHER_LUT_SIZE, 55263, SettingLevel::Basic, videoSettings.m_PlaceboDitherLutSize, -1, 1, 1, 8, 55263, usePopup);
	AddToggle(groupDither, SETTING_LIB_PLACEBO_DITHER_TEMPORAL, 55264, SettingLevel::Basic, videoSettings.m_PlaceboDitherTemporal);
	AddList(groupDither,   SETTING_LIB_PLACEBO_DITHER_TRANSFER, 55265, SettingLevel::Basic, videoSettings.m_PlaceboDitherTransfer, CPLHelper::PlDitherTransferOptionFiller, 55265);

	AddList(groupLut,    SETTING_LIB_PLACEBO_LUT_TYPE, 55330, SettingLevel::Basic, videoSettings.m_PlaceboLutType, CPLHelper::PlLutTypeOptionFiller, 55330);
	AddList(groupLut,    SETTING_LIB_PLACEBO_LUT_FILENAME, 55332, SettingLevel::Basic, videoSettings.m_PlaceboLutFilename, CPLHelper::PlLutOptionFiller, 55332);
	AddSlider(groupMisc, SETTING_LIB_PLACEBO_ANTIRINGING_STRENGTH, 55300, SettingLevel::Basic, videoSettings.m_PlaceboAntiringingStrength, "{0:3.2f}", (float)0.0, (float)0.01, (float)1.0, 55300, usePopup);
	AddToggle(groupMisc, SETTING_LIB_PLACEBO_CORRECT_SUBPIXEL_OFFSET, 55301, SettingLevel::Basic, videoSettings.m_PlaceboCorrectSubpixelOffset);
	AddToggle(groupMisc, SETTING_LIB_PLACEBO_DISABLE_BUILTIN_SCALERS, 55302, SettingLevel::Basic, videoSettings.m_PlaceboDisableBuiltinScalers);
	AddToggle(groupMisc, SETTING_LIB_PLACEBO_DISABLE_DITHER_GAMMA_CORRECTION, 55303, SettingLevel::Basic, videoSettings.m_PlaceboDisableDitherGammaCorrection);
	AddToggle(groupMisc, SETTING_LIB_PLACEBO_DISABLE_LINEAR_SCALING, 55304, SettingLevel::Basic, videoSettings.m_PlaceboDisableLinearScaling);
	AddToggle(groupMisc, SETTING_LIB_PLACEBO_DYNAMIC_CONSTANTS, 55305, SettingLevel::Basic, videoSettings.m_PlaceboDynamicConstant);
	AddList(groupMisc,   SETTING_LIB_PLACEBO_ERROR_DIFFUSION, 55306, SettingLevel::Basic, videoSettings.m_PlaceboErrorDiffusion, CPLHelper::PlDiffusionKernelOptionFiller, 55306);
	AddToggle(groupMisc, SETTING_LIB_PLACEBO_FORCE_DITHER, 55307, SettingLevel::Basic, videoSettings.m_PlaceboForceDither);
	AddToggle(groupMisc, SETTING_LIB_PLACEBO_FORCE_LOW_BIT_DEPTH_FBOS, 55308, SettingLevel::Basic, videoSettings.m_PlaceboForceLowBitDepthFbos);
	// AddToggle(groupMisc, SETTING_LIB_PLACEBO_IGNORE_ICC_PROFILES,             55309, SettingLevel::Basic, videoSettings.m_PlaceboIgnoreIccProfiles); ignore_icc_profiles; // non-functional, just set pl_frame.icc to NULL
	AddToggle(groupMisc, SETTING_LIB_PLACEBO_PRESERVE_MIXING_CACHE, 55310, SettingLevel::Basic, videoSettings.m_PlaceboPreserveMixingCache);
	AddToggle(groupMisc, SETTING_LIB_PLACEBO_SKIP_ANTI_ALIASING, 55311, SettingLevel::Basic, videoSettings.m_PlaceboSkipAntiAliasing);
	AddToggle(groupMisc, SETTING_LIB_PLACEBO_SKIP_CACHING_SINGLE_FRAME, 55312, SettingLevel::Basic, videoSettings.m_PlaceboSkipCachingSingleFrame);
	AddToggle(groupMisc, SETTING_LIB_PLACEBO_SKIP_TARGET_CLEARING, 55368, SettingLevel::Basic, videoSettings.m_PlaceboSkipTargetClearing);
	AddToggle(groupMisc, SETTING_LIB_PLACEBO_DEBUG_OSD, 55355, SettingLevel::Basic, videoSettings.m_PlaceboDebugOsd);
	//AddToggle(groupMisc, SETTING_LIB_PLACEBO_VIDEO_DEBUG_OSD, 55356, SettingLevel::Basic, videoSettings.m_PlaceboVideoDebugOsd);
	AddToggle(groupMisc, SETTING_LIB_PLACEBO_DEBUG_HIDE, 55370, SettingLevel::Basic, videoSettings.m_PlaceboDebugHide);

	InitializeShaderMenu(videoSettings, category);
	CreateGroup(groupShaderLoad, category);
	AddToggle(groupShaderLoad, SETTING_LIB_PLACEBO_SHADER_APPLY, 55338, SettingLevel::Basic, videoSettings.m_PlaceboShaderApply);
	AddButton(groupShaderLoad, SETTING_LIB_PLACEBO_SHADER_ADD, 55334, SettingLevel::Basic);


  }
}

void CGUIDialogVideoSettings::InitializeToneMappingMenuHdr(CVideoSettings& videoSettings, const std::shared_ptr<CSettingGroup> group)
{
  // Tone mapping
  AddList(group, SETTING_LIB_PLACEBO_COLOR_MAP_TONE_MAPPING, 55236, SettingLevel::Basic, videoSettings.m_PlaceboColorMapToneMapping, CPLHelper::PlColorMapToneMapFunctionOptionFiller, 55236);

  std::string desc = {};
  std::string funcDesc = {};
  videoSettings.m_PlaceboColorMapToneMapParameter = 0.0;
  float paramMin = 0.0;
  float paramMax = 1.0;
  float paramStep = 0.01;
  bool bHasParam = false;
  if(videoSettings.m_PlaceboColorMapToneMapping != -1)
  {
	if(pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->param_desc)
	  desc = pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->param_desc;
	if(pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->description)
	  funcDesc = pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->description;

	if(desc == "Knee point target")
	{
	  videoSettings.m_PlaceboColorMapToneMapParameter = videoSettings.m_PlaceboToneConstantKneeAdaptation;
	  paramMin = pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->param_min;
	  paramMax = pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->param_max;
	  paramStep = (paramMax - paramMin) / 100.0;
	  bHasParam = true;
	}
	else if(desc == "Knee offset")
	{
	  videoSettings.m_PlaceboColorMapToneMapParameter = videoSettings.m_PlaceboToneConstantKneeOffset;
	  paramMin = pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->param_min;
	  paramMax = pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->param_max;
	  paramStep = (paramMax - paramMin) / 100.0;
	  bHasParam = true;
	}
	else if((desc == "Contrast") && (funcDesc == "Single-pivot polynomial spline"))
	{
	  videoSettings.m_PlaceboColorMapToneMapParameter = videoSettings.m_PlaceboToneConstantSplineContrast;
	  paramMin = pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->param_min;
	  paramMax = pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->param_max;
	  paramStep = (paramMax - paramMin) / 100.0;
	  bHasParam = true;
	}
	else if((desc == "Contrast") && (funcDesc == "Reinhard"))
	{
	  videoSettings.m_PlaceboColorMapToneMapParameter = videoSettings.m_PlaceboToneConstantReinhardContrast;
	  paramMin = pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->param_min;
	  paramMax = pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->param_max;
	  paramStep = (paramMax - paramMin) / 100.0;
	  bHasParam = true;
	}
	else if(desc == "Exposure")
	{
	  videoSettings.m_PlaceboColorMapToneMapParameter = videoSettings.m_PlaceboToneConstantExposure;
	  paramMin = pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->param_min;
	  paramMax = pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->param_max;
	  paramStep = (paramMax - paramMin) / 100.0;
	  bHasParam = true;
	}
	else if(desc == "Knee point")
	{
	  videoSettings.m_PlaceboColorMapToneMapParameter = videoSettings.m_PlaceboToneConstantLinearKnee;
	  paramMin = pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->param_min;
	  paramMax = pl_tone_map_functions [videoSettings.m_PlaceboColorMapToneMapping]->param_max;
	  paramStep = (paramMax - paramMin) / 100.0;
	  bHasParam = true;
	}
  }
  if(bHasParam)
  {
	AddSlider(group, SETTING_LIB_PLACEBO_COLOR_MAP_TONE_MAPPING_PARAMETER, "   " + desc, SettingLevel::Basic, videoSettings.m_PlaceboColorMapToneMapParameter, "{0:4.2f}", paramMin, paramStep, paramMax); //cl strings.po...
  }
  if(funcDesc == "Single-pivot polynomial spline" || (funcDesc == "SMPTE ST 2094-10 Annex B.2") || (funcDesc == "SMPTE ST 2094-40 Annex B"))
  {
	AddSlider(group, SETTING_LIB_PLACEBO_TONE_CONSTANTS_KNEE_MAXIMUM, 55269, SettingLevel::Basic, videoSettings.m_PlaceboToneConstantKneeMaximum, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0);
	AddSlider(group, SETTING_LIB_PLACEBO_TONE_CONSTANTS_KNEE_MINIMUM, 55270, SettingLevel::Basic, videoSettings.m_PlaceboToneConstantKneeMinimum, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 0.5);
  }

  if(funcDesc == "Single-pivot polynomial spline")
  {
	AddSlider(group, SETTING_LIB_PLACEBO_TONE_CONSTANTS_SLOPE_OFFSET, "   Slope offset", SettingLevel::Basic, videoSettings.m_PlaceboToneConstantSlopeOffset, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0);  //cl strings.po...
	AddSlider(group, SETTING_LIB_PLACEBO_TONE_CONSTANTS_SLOPE_TUNING, "   Slope Tuning", SettingLevel::Basic, videoSettings.m_PlaceboToneConstantSlopeTuning, "{0:4.2f}", (float) 0.0, (float) 0.1, (float) 10.0);  //cl strings.po...
  }
}

void CGUIDialogVideoSettings::InitializeToneMappingMenuSdr(CVideoSettings& videoSettings, const std::shared_ptr<CSettingGroup> group)
{
  // Tone mapping
  AddList(group, SETTING_LIB_PLACEBO_SDR_COLOR_MAP_TONE_MAPPING, 55236, SettingLevel::Basic, videoSettings.m_PlaceboSdrColorMapToneMapping, CPLHelper::PlColorMapToneMapFunctionOptionFiller, 55236);

  std::string desc = {};
  std::string funcDesc = {};
  videoSettings.m_PlaceboSdrColorMapToneMapParameter = 0.0;
  float paramMin = 0.0;
  float paramMax = 1.0;
  float paramStep = 0.01;
  bool bHasParam = false;
  if(videoSettings.m_PlaceboSdrColorMapToneMapping != -1)
  {
	if(pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->param_desc)
	  desc = pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->param_desc;
	if(pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->description)
	  funcDesc = pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->description;

	if(desc == "Knee point target")
	{
	  videoSettings.m_PlaceboSdrColorMapToneMapParameter = videoSettings.m_PlaceboSdrToneConstantKneeAdaptation;
	  paramMin = pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->param_min;
	  paramMax = pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->param_max;
	  paramStep = (paramMax - paramMin) / 100.0;
	  bHasParam = true;
	}
	else if(desc == "Knee offset")
	{
	  videoSettings.m_PlaceboSdrColorMapToneMapParameter = videoSettings.m_PlaceboSdrToneConstantKneeOffset;
	  paramMin = pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->param_min;
	  paramMax = pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->param_max;
	  paramStep = (paramMax - paramMin) / 100.0;
	  bHasParam = true;
	}
	else if((desc == "Contrast") && (funcDesc == "Single-pivot polynomial spline"))
	{
	  videoSettings.m_PlaceboSdrColorMapToneMapParameter = videoSettings.m_PlaceboSdrToneConstantSplineContrast;
	  paramMin = pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->param_min;
	  paramMax = pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->param_max;
	  paramStep = (paramMax - paramMin) / 100.0;
	  bHasParam = true;
	}
	else if((desc == "Contrast") && (funcDesc == "Reinhard"))
	{
	  videoSettings.m_PlaceboSdrColorMapToneMapParameter = videoSettings.m_PlaceboSdrToneConstantReinhardContrast;
	  paramMin = pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->param_min;
	  paramMax = pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->param_max;
	  paramStep = (paramMax - paramMin) / 100.0;
	  bHasParam = true;
	}
	else if(desc == "Exposure")
	{
	  videoSettings.m_PlaceboSdrColorMapToneMapParameter = videoSettings.m_PlaceboSdrToneConstantExposure;
	  paramMin = pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->param_min;
	  paramMax = pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->param_max;
	  paramStep = (paramMax - paramMin) / 100.0;
	  bHasParam = true;
	}
	else if(desc == "Knee point")
	{
	  videoSettings.m_PlaceboSdrColorMapToneMapParameter = videoSettings.m_PlaceboSdrToneConstantLinearKnee;
	  paramMin = pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->param_min;
	  paramMax = pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping]->param_max;
	  paramStep = (paramMax - paramMin) / 100.0;
	  bHasParam = true;
	}
  }
  if(bHasParam)
  {
	AddSlider(group, SETTING_LIB_PLACEBO_SDR_COLOR_MAP_TONE_MAPPING_PARAMETER, "   " + desc, SettingLevel::Basic, videoSettings.m_PlaceboSdrColorMapToneMapParameter, "{0:4.2f}", paramMin, paramStep, paramMax); //cl strings.po...
  }
  if(funcDesc == "Single-pivot polynomial spline" || (funcDesc == "SMPTE ST 2094-10 Annex B.2") || (funcDesc == "SMPTE ST 2094-40 Annex B"))
  {
	AddSlider(group, SETTING_LIB_PLACEBO_SDR_TONE_CONSTANTS_KNEE_MAXIMUM, 55269, SettingLevel::Basic, videoSettings.m_PlaceboSdrToneConstantKneeMaximum, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0);
	AddSlider(group, SETTING_LIB_PLACEBO_SDR_TONE_CONSTANTS_KNEE_MINIMUM, 55270, SettingLevel::Basic, videoSettings.m_PlaceboSdrToneConstantKneeMinimum, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 0.5);
  }

  if(funcDesc == "Single-pivot polynomial spline")
  {
	AddSlider(group, SETTING_LIB_PLACEBO_SDR_TONE_CONSTANTS_SLOPE_OFFSET, "   Slope offset", SettingLevel::Basic, videoSettings.m_PlaceboSdrToneConstantSlopeOffset, "{0:4.2f}", (float) 0.0, (float) 0.01, (float) 1.0);  //cl strings.po...
	AddSlider(group, SETTING_LIB_PLACEBO_SDR_TONE_CONSTANTS_SLOPE_TUNING, "   Slope Tuning", SettingLevel::Basic, videoSettings.m_PlaceboSdrToneConstantSlopeTuning, "{0:4.2f}", (float) 0.0, (float) 0.1, (float) 10.0);  //cl strings.po...
  }
}



void CGUIDialogVideoSettings::InitializeShaderMenu(CVideoSettings& vs, const std::shared_ptr<CSettingCategory>& category)
{
  for (int i = 0; i < vs.m_PlaceboShadersFilename.size(); i++)
  {
	if(!vs.m_PlaceboShadersHooks.m_bInit)
	  return; //cl hooks not initialized yet, should not happen but it did, fix applied but in case we change the code...
	std::string settingId;
    CreateGroup(group, category);
	if (!vs.m_PlaceboShadersHooks.m_Valid[i])
	{
	  //cl Eventually need to give some feedback even if menu is not open, popup???
	  AddInfoLabelButton(group, SETTING_LIB_PLACEBO_SHADER_INVALID, 55341, SettingLevel::Basic, vs.m_PlaceboShadersHooks.m_FileNames[i]);
	}
	else
	{
	  settingId = SETTING_LIB_PLACEBO_SHADER_ENABLED + StringUtils::Format("_{:02}", i);
	  AddToggle(group, settingId, "Shader: " + vs.m_PlaceboShadersHooks.m_FileNames[i], SettingLevel::Basic, vs.m_PlaceboShadersEnabled[i]);
	}

	settingId = SETTING_LIB_PLACEBO_SHADER_REMOVE + StringUtils::Format("_{:02}", i);
	AddButton(group, settingId, 55335, SettingLevel::Basic);

	settingId = SETTING_LIB_PLACEBO_SHADER_MOVE_UP + StringUtils::Format("_{:02}", i);
	AddButton(group, settingId, 55336, SettingLevel::Basic);

	settingId = SETTING_LIB_PLACEBO_SHADER_MOVE_DOWN + StringUtils::Format("_{:02}", i);
	AddButton(group, settingId, 55337, SettingLevel::Basic);


    if(vs.m_PlaceboShadersHooks.m_Valid[i])
	{
	  const pl_hook* hook = vs.m_PlaceboShadersHooks.m_Hooks[i].get();
	  for (int j = 0; j < hook->num_parameters; j++)
	  {
		std::string paramSettingId = SETTING_LIB_PLACEBO_SHADER_PARAM + StringUtils::Format("_{:02}_{:02}", i, j);
		if (hook->parameters[j].type == PL_VAR_FLOAT)
		{
		  std::string name = hook->parameters[j].name == nullptr ? "" : hook->parameters[j].name;
		  float min = hook->parameters[j].minimum.f;
		  float max = hook->parameters[j].maximum.f;
		  std::isfinite(min) ? min : min = 0.0; //cl hust use some value, it can easily be changed in the .glsl/.hook file instead of guessing here.
		  std::isfinite(max) ? max : max = min + 100.0;
		  float step = (max - min) / 100.0;
		  AddSlider(group, paramSettingId, name, SettingLevel::Basic, hook->parameters[j].data->f, "{0:8.3f}", min, step, max, true); //cl format
		}
		else if (hook->parameters[j].type == PL_VAR_SINT)
		{
		  std::string name = hook->parameters[j].name == nullptr ? "" : hook->parameters[j].name;
		  int min = hook->parameters[j].minimum.i;
		  int max = hook->parameters[j].maximum.i;
		  std::isfinite(min) ? min : min = -50;
		  std::isfinite(max) ? max : max = min + 100;
		  int step = (max - min) / 100;
		  if (step < 1) step = 1;
		  AddSlider(group, paramSettingId, name, SettingLevel::Basic, hook->parameters[j].data->i, "", min, step, max, true);

		}
		else if (hook->parameters[j].type == PL_VAR_UINT)
		{
		  std::string name = hook->parameters[j].name == nullptr ? "" : hook->parameters[j].name;
		  unsigned int min = hook->parameters[j].minimum.u;
		  unsigned int max = hook->parameters[j].maximum.u;
		  std::isfinite(min) ? min : min = 0;
		  std::isfinite(max) ? max : max = min + 100;
		  unsigned int step = (max - min) / 100;
		  if (step < 1) step = 1;
		  AddSlider(group, paramSettingId, name, SettingLevel::Basic, hook->parameters[j].data->u, "", (int)min, (int)step, (int)max, true);
		}
	  }
	}
  }
}

void CGUIDialogVideoSettings::AddVideoStreams(const std::shared_ptr<CSettingGroup>& group,
  const std::string& settingId)
{
  if (group == NULL || settingId.empty())
	return;

  auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();

  m_videoStream = appPlayer->GetVideoStream();
  if (m_videoStream < 0)
	m_videoStream = 0;

  AddList(group, settingId, 38031, SettingLevel::Basic, m_videoStream, VideoStreamsOptionFiller, 38031);
}





void CGUIDialogVideoSettings::VideoStreamsOptionFiller(
  const std::shared_ptr<const CSetting>& setting,
  std::vector<IntegerSettingOption>& list,
  int& current)
{
  const auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();

  int videoStreamCount = appPlayer->GetVideoStreamCount();
  // cycle through each video stream and add it to our list control
  for (int i = 0; i < videoStreamCount; ++i)
  {
	std::string strItem;
	std::string strLanguage;

	VideoStreamInfo info;
	appPlayer->GetVideoStreamInfo(i, info);

	g_LangCodeExpander.Lookup(info.language, strLanguage);

	if (!info.name.empty())
	{
	  if (!strLanguage.empty())
		strItem = StringUtils::Format("{} - {}", strLanguage, info.name);
	  else
		strItem = info.name;
	}
	else if (!strLanguage.empty())
	{
	  strItem = strLanguage;
	}

	if (info.codecName.empty())
	  strItem += StringUtils::Format(" ({}x{}", info.width, info.height);
	else
	  strItem += StringUtils::Format(" ({}, {}x{}", info.codecName, info.width, info.height);

	if (info.bitrate)
	  strItem += StringUtils::Format(", {} bps)", info.bitrate);
	else
	  strItem += ")";

	strItem += FormatFlags(info.flags);
	strItem += StringUtils::Format(" ({}/{})", i + 1, videoStreamCount);
	list.emplace_back(strItem, i);
  }

  if (list.empty())
  {
	list.emplace_back(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(231), -1);
	current = -1;
  }
}

void CGUIDialogVideoSettings::VideoOrientationFiller(
  const std::shared_ptr<const CSetting>& /*setting*/,
  std::vector<IntegerSettingOption>& list,
  int& /*current*/)
{
  list.emplace_back(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(687), 0);
  list.emplace_back(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(35229), 90);
  list.emplace_back(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(35230), 180);
  list.emplace_back(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(35231), 270);
}

std::string CGUIDialogVideoSettings::FormatFlags(StreamFlags flags)
{
  std::vector<std::string> localizedFlags;
  if (flags & StreamFlags::FLAG_DEFAULT)
	localizedFlags.emplace_back(
	  CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(39105));
  if (flags & StreamFlags::FLAG_FORCED)
	localizedFlags.emplace_back(
	  CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(39106));
  if (flags & StreamFlags::FLAG_HEARING_IMPAIRED)
	localizedFlags.emplace_back(
	  CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(39107));
  if (flags & StreamFlags::FLAG_VISUAL_IMPAIRED)
	localizedFlags.emplace_back(
	  CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(39108));

  std::string formated = StringUtils::Join(localizedFlags, ", ");

  if (!formated.empty())
	formated = StringUtils::Format(" [{}]", formated);

  return formated;
}

