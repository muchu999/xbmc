/*
 *      Copyright (C) 2005-2010 Team XBMC
 *      http://www.xbmc.org
 *
 *		Copyright (C) 2010-2013 Eduard Kytmanov
 *		http://www.avmedia.su
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#ifdef HAS_DS_PLAYER

#include "GraphFilters.h"
#include "settings/Settings.h"
#include "settings/MediaSettings.h"
#include "Filters/LavSettings.h"
#include "Filters/LAVAudioSettings.h"
#include "Filters/LAVVideoSettings.h"
#include "Filters/LAVSplitterSettings.h"
#include "DSPropertyPage.h"
#include "FGFilter.h"
#include "DSPlayerDatabase.h"
#include "filtercorefactory/filtercorefactory.h"
#include "Utils/DSFilterEnumerator.h"

CGraphFilters *CGraphFilters::m_pSingleton = NULL;

CGraphFilters::CGraphFilters() :
m_isDVD(false), m_UsingDXVADecoder(false), m_CurrentRenderer(DIRECTSHOW_RENDERER_UNDEF), m_hsubfilter(false)
{
  m_isKodiRealFS = false;
}

CGraphFilters::~CGraphFilters()
{
  if (m_isKodiRealFS)
  {
    CSettings::Get().SetBool("videoscreen.fakefullscreen", false);
    m_isKodiRealFS = false;
  }

  // Store into DSPlayer DB changes made for internal lavfilters directly from the trayicon
  if (Video.pBF && Video.internalLav)
  {
    GetLavSettings(LAVVIDEO, Video.pBF);
    SaveLavSettings(LAVVIDEO);
  }
  if (Audio.pBF && Audio.internalLav)
  {
    GetLavSettings(LAVAUDIO, Audio.pBF);
    SaveLavSettings(LAVAUDIO);
  }
  if (Source.pBF && Source.internalLav)
  {
    GetLavSettings(LAVSPLITTER, Source.pBF);
    SaveLavSettings(LAVSPLITTER);
  }
  if (Splitter.pBF && Splitter.internalLav)
  {
    GetLavSettings(LAVSPLITTER, Splitter.pBF);
    SaveLavSettings(LAVSPLITTER);
  }
}

CGraphFilters* CGraphFilters::Get()
{
  return (m_pSingleton) ? m_pSingleton : (m_pSingleton = new CGraphFilters());
}

void CGraphFilters::ShowLavFiltersPage(LAVFILTERS_TYPE type, bool showPropertyPage)
{
  m_pBF = NULL;

  // If there is a playback don't recreate the filter to show the GUI/Property page
  GetCurrentFilter(type, &m_pBF);

  if (m_pBF == NULL)
    CreateInternalFilter(type, &m_pBF);

  if (showPropertyPage)
  {
    CDSPropertyPage *pDSPropertyPage = DNew CDSPropertyPage(m_pBF, type);
    pDSPropertyPage->Initialize();
  }
  else
  {
    if (type == LAVVIDEO)
      g_windowManager.ActivateWindow(WINDOW_DIALOG_LAVVIDEO);
    if (type == LAVAUDIO)
      g_windowManager.ActivateWindow(WINDOW_DIALOG_LAVAUDIO);
    if (type == LAVSPLITTER)
      g_windowManager.ActivateWindow(WINDOW_DIALOG_LAVSPLITTER);
  }
}

void CGraphFilters::GetCurrentFilter(LAVFILTERS_TYPE type, IBaseFilter **ppBF)
{
  if (type == LAVVIDEO && Video.pBF && Video.internalLav)
    m_pBF = Video.pBF;

  if (type == LAVAUDIO && Audio.pBF && Audio.internalLav)
    m_pBF = Audio.pBF;

  if (type == LAVSPLITTER && Splitter.pBF && Splitter.internalLav)
    m_pBF = Splitter.pBF;

  if (type == LAVSPLITTER && Source.pBF && Source.internalLav)
    m_pBF = Source.pBF;

  *ppBF = m_pBF;
}

void CGraphFilters::CreateInternalFilter(LAVFILTERS_TYPE type, IBaseFilter **ppBF)
{
  std::string filterName;
  if (type == LAVVIDEO)
    filterName = "lavvideo_internal";
  if (type == LAVAUDIO)
    filterName = "lavaudio_internal";
  if (type == LAVSPLITTER)
    filterName = "lavsource_internal";
  if (type == XYSUBFILTER)
    IsRegisteredFilter(XYSUBFILTER_FILTERSTR) ? filterName = "xysubfilter" : filterName = "xysubfilter_internal";

  CFGLoader *pLoader = new CFGLoader();
  pLoader->LoadConfig();

  CFGFilter *pFilter = NULL;
  if (!(pFilter = CFilterCoreFactory::GetFilterFromName(filterName)))
    return;

  pFilter->Create(ppBF);

  // Init LavFilters settings
  SetupLavSettings(type, *ppBF);
}

bool CGraphFilters::IsInternalFilter(IBaseFilter *pBF)
{
  if (Video.pBF == pBF && Video.internalLav)
  {
    g_windowManager.ActivateWindow(WINDOW_DIALOG_LAVVIDEO);
    return true;
  }
  if (Audio.pBF == pBF && Audio.internalLav)
  {
    g_windowManager.ActivateWindow(WINDOW_DIALOG_LAVAUDIO);
    return true;
  }
  if ((Source.pBF == pBF && Source.internalLav) || (Splitter.pBF == pBF && Splitter.internalLav))
  {
    g_windowManager.ActivateWindow(WINDOW_DIALOG_LAVSPLITTER);
    return true;
  }
  return false;
}

LAVFILTERS_TYPE CGraphFilters::GetInternalType(IBaseFilter *pBF)
{
  if (Video.pBF == pBF && Video.internalLav)
    return LAVVIDEO;
  if (Audio.pBF == pBF && Audio.internalLav)
    return LAVAUDIO;
  if ((Source.pBF == pBF && Source.internalLav) || (Splitter.pBF == pBF && Splitter.internalLav))
    return LAVSPLITTER;

  return NULLFILTER;
}

void CGraphFilters::SetupLavSettings(LAVFILTERS_TYPE type, IBaseFilter* pBF)
{
  if (type != LAVVIDEO && type != LAVAUDIO && type != LAVSPLITTER)
    return;

  // Set LavFilters in RunTimeConfig to have personal settings only for DSPlayer
  // this will reset LavFilters to default settings
  SetLavInternal(type, pBF);

  // Use LavFilters settings stored in DSPlayer DB if they are present
  if (LoadLavSettings(type))
  {
    SetLavSettings(type, pBF);
  }
  else
  {    
    // If DSPlayer DB it's empty load default LavFilters settings and then save into DB
    GetLavSettings(type, pBF);
    SaveLavSettings(type);  
  }
}

bool CGraphFilters::SetLavInternal(LAVFILTERS_TYPE type, IBaseFilter *pBF)
{
  if (type != LAVVIDEO && type != LAVAUDIO && type != LAVSPLITTER)
    return false;

  if (type == LAVVIDEO)
  {
    Com::SmartQIPtr<ILAVVideoSettings> pLAVFSettings = pBF;
    pLAVFSettings->SetRuntimeConfig(TRUE);
  }
  else if (type == LAVAUDIO)
  {
    Com::SmartQIPtr<ILAVAudioSettings> pLAVFSettings = pBF;
    pLAVFSettings->SetRuntimeConfig(TRUE);
  }
  else if (type == LAVSPLITTER)
  {
    Com::SmartQIPtr<ILAVFSettings> pLAVFSettings = pBF;
    pLAVFSettings->SetRuntimeConfig(TRUE);
  }

  return true;
}

bool CGraphFilters::GetLavSettings(LAVFILTERS_TYPE type, IBaseFilter* pBF)
{
  if (type != LAVVIDEO && type != LAVAUDIO && type != LAVSPLITTER)
    return false;

  if (type == LAVVIDEO)
  {
    Com::SmartQIPtr<ILAVVideoSettings> pLAVFSettings = pBF;

    CLavSettings &LavSettings = CMediaSettings::Get().GetCurrentLavSettings();

    if (!pLAVFSettings)
      return false;

    LavSettings.video_bTrayIcon = pLAVFSettings->GetTrayIcon();
    LavSettings.video_dwStreamAR = pLAVFSettings->GetStreamAR();
    LavSettings.video_dwNumThreads = pLAVFSettings->GetNumThreads();
    LavSettings.video_dwDeintFieldOrder = pLAVFSettings->GetDeintFieldOrder();
    LavSettings.video_deintMode = pLAVFSettings->GetDeinterlacingMode();
    LavSettings.video_dwRGBRange = pLAVFSettings->GetRGBOutputRange();
    LavSettings.video_dwSWDeintMode = pLAVFSettings->GetSWDeintMode();
    LavSettings.video_dwSWDeintOutput = pLAVFSettings->GetSWDeintOutput();
    LavSettings.video_dwDitherMode = pLAVFSettings->GetDitherMode();
    for (int i = 0; i < LAVOutPixFmt_NB; ++i) {
      LavSettings.video_bPixFmts[i] = pLAVFSettings->GetPixelFormat((LAVOutPixFmts)i);
    }
    LavSettings.video_dwHWAccel = pLAVFSettings->GetHWAccel();
    for (int i = 0; i < HWCodec_NB; ++i) {
      LavSettings.video_bHWFormats[i] = pLAVFSettings->GetHWAccelCodec((LAVVideoHWCodec)i);
    }
    LavSettings.video_dwHWAccelResFlags = pLAVFSettings->GetHWAccelResolutionFlags();
    LavSettings.video_dwHWDeintMode = pLAVFSettings->GetHWAccelDeintMode();
    LavSettings.video_dwHWDeintOutput = pLAVFSettings->GetHWAccelDeintOutput();
    LavSettings.video_bHWDeintHQ = pLAVFSettings->GetHWAccelDeintHQ();
  } 
  if (type == LAVAUDIO)
  {
    Com::SmartQIPtr<ILAVAudioSettings> pLAVFSettings = pBF;

    CLavSettings &LavSettings = CMediaSettings::Get().GetCurrentLavSettings();

    if (!pLAVFSettings)
      return false;

    LavSettings.audio_bTrayIcon = pLAVFSettings->GetTrayIcon();
    pLAVFSettings->GetDRC(&LavSettings.audio_bDRCEnabled, &LavSettings.audio_iDRCLevel);
    LavSettings.audio_bDTSHDFraming = pLAVFSettings->GetDTSHDFraming();
    LavSettings.audio_bAutoAVSync = pLAVFSettings->GetAutoAVSync();
    LavSettings.audio_bExpandMono = pLAVFSettings->GetExpandMono();
    LavSettings.audio_bExpand61 = pLAVFSettings->GetExpand61();
    LavSettings.audio_bOutputStandardLayout = pLAVFSettings->GetOutputStandardLayout();
    LavSettings.audio_b51Legacy = pLAVFSettings->GetOutput51LegacyLayout();
    LavSettings.audio_bMixingEnabled = pLAVFSettings->GetMixingEnabled();
    LavSettings.audio_dwMixingLayout = pLAVFSettings->GetMixingLayout();
    LavSettings.audio_dwMixingFlags = pLAVFSettings->GetMixingFlags();
    LavSettings.audio_dwMixingMode = pLAVFSettings->GetMixingMode();
    pLAVFSettings->GetMixingLevels(&LavSettings.audio_dwMixingCenterLevel, &LavSettings.audio_dwMixingSurroundLevel, &LavSettings.audio_dwMixingLFELevel);
    //pLAVFSettings->GetAudioDelay(&LavSettings.audio_bAudioDelayEnabled, &LavSettings.audio_iAudioDelay);

    for (int i = 0; i < Bitstream_NB; ++i) {
      LavSettings.audio_bBitstream[i] = pLAVFSettings->GetBitstreamConfig((LAVBitstreamCodec)i);
    }
    for (int i = 0; i < SampleFormat_Bitstream; ++i) {
      LavSettings.audio_bSampleFormats[i] = pLAVFSettings->GetSampleFormat((LAVAudioSampleFormat)i);
    }
    LavSettings.audio_bSampleConvertDither = pLAVFSettings->GetSampleConvertDithering();
  }
  if (type == LAVSPLITTER)
  {
    Com::SmartQIPtr<ILAVFSettings> pLAVFSettings = pBF;

    CLavSettings &LavSettings = CMediaSettings::Get().GetCurrentLavSettings();

    if (!pLAVFSettings)
      return false;

    LavSettings.splitter_bTrayIcon = pLAVFSettings->GetTrayIcon();

    HRESULT hr;
    LPWSTR lpwstr = nullptr;
    hr = pLAVFSettings->GetPreferredLanguages(&lpwstr);
    if (SUCCEEDED(hr) && lpwstr) {
      LavSettings.splitter_prefAudioLangs = lpwstr;
      CoTaskMemFree(lpwstr);
    }
    lpwstr = nullptr;
    hr = pLAVFSettings->GetPreferredSubtitleLanguages(&lpwstr);
    if (SUCCEEDED(hr) && lpwstr) {
      LavSettings.splitter_prefSubLangs = lpwstr;
      CoTaskMemFree(lpwstr);
    }
    lpwstr = nullptr;
    hr = pLAVFSettings->GetAdvancedSubtitleConfig(&lpwstr);
    if (SUCCEEDED(hr) && lpwstr) {
      LavSettings.splitter_subtitleAdvanced = lpwstr;
      CoTaskMemFree(lpwstr);
    }

    LavSettings.splitter_subtitleMode = pLAVFSettings->GetSubtitleMode();
    LavSettings.splitter_bPGSForcedStream = pLAVFSettings->GetPGSForcedStream();
    LavSettings.splitter_bPGSOnlyForced = pLAVFSettings->GetPGSOnlyForced();
    LavSettings.splitter_iVC1Mode = pLAVFSettings->GetVC1TimestampMode();
    LavSettings.splitter_bSubstreams = pLAVFSettings->GetSubstreamsEnabled();
    LavSettings.splitter_bMatroskaExternalSegments = pLAVFSettings->GetLoadMatroskaExternalSegments();
    LavSettings.splitter_bStreamSwitchRemoveAudio = pLAVFSettings->GetStreamSwitchRemoveAudio();
    LavSettings.splitter_bImpairedAudio = pLAVFSettings->GetUseAudioForHearingVisuallyImpaired();
    LavSettings.splitter_bPreferHighQualityAudio = pLAVFSettings->GetPreferHighQualityAudioStreams();
    LavSettings.splitter_dwQueueMaxSize = pLAVFSettings->GetMaxQueueMemSize();
    LavSettings.splitter_dwQueueMaxPacketsSize = pLAVFSettings->GetMaxQueueSize();
    LavSettings.splitter_dwNetworkAnalysisDuration = pLAVFSettings->GetNetworkStreamAnalysisDuration();
  }

  return true;
}

bool CGraphFilters::SetLavSettings(LAVFILTERS_TYPE type, IBaseFilter* pBF)
{
  if (type != LAVVIDEO && type != LAVAUDIO && type != LAVSPLITTER)
    return false;

  if (type == LAVVIDEO)
  {
    Com::SmartQIPtr<ILAVVideoSettings> pLAVFSettings = pBF;

    CLavSettings &LavSettings = CMediaSettings::Get().GetCurrentLavSettings();

    if (!pLAVFSettings)
      return false;

    pLAVFSettings->SetTrayIcon(LavSettings.video_bTrayIcon);
    pLAVFSettings->SetStreamAR(LavSettings.video_dwStreamAR);
    pLAVFSettings->SetNumThreads(LavSettings.video_dwNumThreads);
    pLAVFSettings->SetDeintFieldOrder((LAVDeintFieldOrder)LavSettings.video_dwDeintFieldOrder);
    pLAVFSettings->SetDeinterlacingMode(LavSettings.video_deintMode);
    pLAVFSettings->SetRGBOutputRange(LavSettings.video_dwRGBRange);
    pLAVFSettings->SetSWDeintMode((LAVSWDeintModes)LavSettings.video_dwSWDeintMode);
    pLAVFSettings->SetSWDeintOutput((LAVDeintOutput)LavSettings.video_dwSWDeintOutput);
    pLAVFSettings->SetDitherMode((LAVDitherMode)LavSettings.video_dwDitherMode);
    for (int i = 0; i < LAVOutPixFmt_NB; ++i) {
      pLAVFSettings->SetPixelFormat((LAVOutPixFmts)i, LavSettings.video_bPixFmts[i]);
    }
    pLAVFSettings->SetHWAccel((LAVHWAccel)LavSettings.video_dwHWAccel);
    for (int i = 0; i < HWCodec_NB; ++i) {
      pLAVFSettings->SetHWAccelCodec((LAVVideoHWCodec)i, LavSettings.video_bHWFormats[i]);
    }
    pLAVFSettings->SetHWAccelResolutionFlags(LavSettings.video_dwHWAccelResFlags);
    pLAVFSettings->SetHWAccelDeintMode((LAVHWDeintModes)LavSettings.video_dwHWDeintMode);
    pLAVFSettings->SetHWAccelDeintOutput((LAVDeintOutput)LavSettings.video_dwHWDeintOutput);
    pLAVFSettings->SetHWAccelDeintHQ(LavSettings.video_bHWDeintHQ);

    // Custom interface
    if (Com::SmartQIPtr<ILAVVideoSettingsDSPlayerCustom> pLAVFSettingsDSPlayerCustom = pLAVFSettings)
      pLAVFSettingsDSPlayerCustom->SetPropertyPageCallback(PropertyPageCallback);
  }
  if (type == LAVAUDIO)
  {
    Com::SmartQIPtr<ILAVAudioSettings> pLAVFSettings = pBF;

    CLavSettings &LavSettings = CMediaSettings::Get().GetCurrentLavSettings();

    if (!pLAVFSettings)
      return false;

    pLAVFSettings->SetTrayIcon(LavSettings.audio_bTrayIcon);
    pLAVFSettings->SetDRC(LavSettings.audio_bDRCEnabled, LavSettings.audio_iDRCLevel);
    pLAVFSettings->SetDTSHDFraming(LavSettings.audio_bDTSHDFraming);
    pLAVFSettings->SetAutoAVSync(LavSettings.audio_bAutoAVSync);
    pLAVFSettings->SetExpandMono(LavSettings.audio_bExpandMono);
    pLAVFSettings->SetExpand61(LavSettings.audio_bExpand61);
    pLAVFSettings->SetOutputStandardLayout(LavSettings.audio_bOutputStandardLayout);
    pLAVFSettings->SetOutput51LegacyLayout(LavSettings.audio_b51Legacy);
    pLAVFSettings->SetMixingEnabled(LavSettings.audio_bMixingEnabled);
    pLAVFSettings->SetMixingLayout(LavSettings.audio_dwMixingLayout);
    pLAVFSettings->SetMixingFlags(LavSettings.audio_dwMixingFlags);
    pLAVFSettings->SetMixingMode((LAVAudioMixingMode)LavSettings.audio_dwMixingMode);
    pLAVFSettings->SetMixingLevels(LavSettings.audio_dwMixingCenterLevel, LavSettings.audio_dwMixingSurroundLevel, LavSettings.audio_dwMixingLFELevel);
    //pLAVFSettings->SetAudioDelay(LavSettings.audio_bAudioDelayEnabled, LavSettings.audio_iAudioDelay);
    for (int i = 0; i < Bitstream_NB; ++i) {
      pLAVFSettings->SetBitstreamConfig((LAVBitstreamCodec)i, LavSettings.audio_bBitstream[i]);
    }
    for (int i = 0; i < SampleFormat_Bitstream; ++i) {
      pLAVFSettings->SetSampleFormat((LAVAudioSampleFormat)i, LavSettings.audio_bSampleFormats[i]);
    }
    pLAVFSettings->SetSampleConvertDithering(LavSettings.audio_bSampleConvertDither);

    // The internal LAV Audio Decoder will not be registered to handle WMA formats
    // since the system decoder is preferred. However we can still enable those
    // formats internally so that they are used in low-merit mode.
    pLAVFSettings->SetFormatConfiguration(Codec_WMA2, TRUE);
    pLAVFSettings->SetFormatConfiguration(Codec_WMAPRO, TRUE);
    pLAVFSettings->SetFormatConfiguration(Codec_WMALL, TRUE);

    // Custom interface
    if (Com::SmartQIPtr<ILAVAudioSettingsDSPlayerCustom> pLAVFSettingsDSPlayerCustom = pLAVFSettings)
      pLAVFSettingsDSPlayerCustom->SetPropertyPageCallback(PropertyPageCallback);
  }
  if (type == LAVSPLITTER)
  {
    Com::SmartQIPtr<ILAVFSettings> pLAVFSettings = pBF;

    CLavSettings &LavSettings = CMediaSettings::Get().GetCurrentLavSettings();

    if (!pLAVFSettings)
      return false;

    pLAVFSettings->SetTrayIcon(LavSettings.splitter_bTrayIcon);
    pLAVFSettings->SetPreferredLanguages(LavSettings.splitter_prefAudioLangs.c_str());
    pLAVFSettings->SetPreferredSubtitleLanguages(LavSettings.splitter_prefSubLangs.c_str());
    pLAVFSettings->SetAdvancedSubtitleConfig(LavSettings.splitter_subtitleAdvanced.c_str());
    pLAVFSettings->SetSubtitleMode(LavSettings.splitter_subtitleMode);
    pLAVFSettings->SetPGSForcedStream(LavSettings.splitter_bPGSForcedStream);
    pLAVFSettings->SetPGSOnlyForced(LavSettings.splitter_bPGSOnlyForced);
    pLAVFSettings->SetVC1TimestampMode(LavSettings.splitter_iVC1Mode);
    pLAVFSettings->SetSubstreamsEnabled(LavSettings.splitter_bSubstreams);
    pLAVFSettings->SetLoadMatroskaExternalSegments(LavSettings.splitter_bMatroskaExternalSegments);
    pLAVFSettings->SetStreamSwitchRemoveAudio(LavSettings.splitter_bStreamSwitchRemoveAudio);
    pLAVFSettings->SetUseAudioForHearingVisuallyImpaired(LavSettings.splitter_bImpairedAudio);
    pLAVFSettings->SetPreferHighQualityAudioStreams(LavSettings.splitter_bPreferHighQualityAudio);
    pLAVFSettings->SetMaxQueueMemSize(LavSettings.splitter_dwQueueMaxSize);
    pLAVFSettings->SetMaxQueueSize(LavSettings.splitter_dwQueueMaxPacketsSize);
    pLAVFSettings->SetNetworkStreamAnalysisDuration(LavSettings.splitter_dwNetworkAnalysisDuration);

    // Custom interface
    if (Com::SmartQIPtr<ILAVFSettingsDSPlayerCustom> pLAVFSettingsDSPlayerCustom = pLAVFSettings)
      pLAVFSettingsDSPlayerCustom->SetPropertyPageCallback(PropertyPageCallback);
  }
  return true;
}

bool CGraphFilters::SaveLavSettings(LAVFILTERS_TYPE type)
{
  if (type != LAVVIDEO && type != LAVAUDIO && type != LAVSPLITTER)
    return false;

  CLavSettings &lavSettings = CMediaSettings::Get().GetCurrentLavSettings();

  CDSPlayerDatabase dsdbs;
  if (dsdbs.Open())
  {
    if (type == LAVVIDEO)
      dsdbs.SetLAVVideoSettings(lavSettings);
    if (type == LAVAUDIO)
      dsdbs.SetLAVAudioSettings(lavSettings);
    if (type == LAVSPLITTER)
      dsdbs.SetLAVSplitterSettings(lavSettings);
    dsdbs.Close();
  }

  return true;
}

bool CGraphFilters::LoadLavSettings(LAVFILTERS_TYPE type)
{
  if (type != LAVVIDEO && type != LAVAUDIO && type != LAVSPLITTER)
    return false;

  CLavSettings &lavSettings = CMediaSettings::Get().GetCurrentLavSettings();
  bool result = false;
  CDSPlayerDatabase dsdbs;
  if (dsdbs.Open())
  {
    if (type == LAVVIDEO)
      result = dsdbs.GetLAVVideoSettings(lavSettings);
    if (type == LAVAUDIO)
      result = dsdbs.GetLAVAudioSettings(lavSettings);
    if (type == LAVSPLITTER)
      result = dsdbs.GetLAVSplitterSettings(lavSettings);

    dsdbs.Close();
  }
  return result;
}

void CGraphFilters::EraseLavSetting(LAVFILTERS_TYPE type)
{
  if (type != LAVVIDEO && type != LAVAUDIO && type != LAVSPLITTER)
    return;

  CDSPlayerDatabase dsdbs;
  if (dsdbs.Open())
  {
    if (type == LAVVIDEO)
      dsdbs.EraseLAVVideo();
    if (type == LAVAUDIO)
      dsdbs.EraseLAVAudio();
    if (type == LAVSPLITTER)
      dsdbs.EraseLAVSplitter();

    dsdbs.Close();
  }
}

HRESULT CGraphFilters::PropertyPageCallback(IUnknown* pBF)
{
  LAVFILTERS_TYPE type = CGraphFilters::Get()->GetInternalType((IBaseFilter *)pBF);
  if (type != NULLFILTER)
    CGraphFilters::Get()->ShowLavFiltersPage(type, true);

  return S_OK;
}

bool CGraphFilters::IsRegisteredFilter(const std::string filter)
{
  CDSFilterEnumerator p_dsfilter;
  std::vector<DSFiltersInfo> dsfilterList;
  p_dsfilter.GetDSFilters(dsfilterList);
  std::vector<DSFiltersInfo>::const_iterator iter = dsfilterList.begin();

  for (int i = 1; iter != dsfilterList.end(); i++)
  {
    DSFiltersInfo dev = *iter;
    if (dev.lpstrName == filter)
    {
      return true;
      break;
    }
    ++iter;
  }
  return false;
}

#endif