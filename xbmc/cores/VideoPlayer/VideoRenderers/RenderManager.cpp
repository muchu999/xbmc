/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RenderManager.h"

/* to use the same as player */
#include "../VideoPlayer/DVDClock.h"
#include "RenderFactory.h"
#include "RenderFlags.h"
#include "ServiceBroker.h"
#include "application/Application.h"
#include "cores/VideoPlayer/Interface/TimingConstants.h"
#include "messaging/ApplicationMessenger.h"
#include "rendering/capture/CaptureBlit.h"
#include "rendering/capture/CaptureMetadata.h"
#include "rendering/capture/CaptureService.h"
#include "settings/AdvancedSettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "threads/SingleLock.h"
#include "utils/StringUtils.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"
#include "windowing/WinSystem.h"

#include <memory>
#include <mutex>
#include <dxgitype.h>
#include <rendering/dx/DeviceResources.h>
#include <utils/TimeUtils.h>
#include "LibPlacebo/PlHelper.h"

using namespace std::chrono_literals;

void CRenderManager::CClockSync::Reset()
{
  m_error = 0;
  m_errCount = 0;
  m_syncOffset = 0;
  m_enabled = false;
}

CRenderManager::CRenderManager(CDVDClock &clock, IRenderMsg *player) :
  m_dvdClock(clock),
  m_playerPort(player)
{
#ifdef _WIN32
  int num_buffers = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_VIDEOPLAYER_VIDEOBUFFERS);
  m_Queue = std::make_unique<SPresent []>(num_buffers);
#endif
}

CRenderManager::~CRenderManager()
{
  delete m_pRenderer;
}

void CRenderManager::GetVideoRect(CRect& source, CRect& dest, CRect& view) const
{
  std::unique_lock lock(m_statelock);
  if (m_pRenderer)
    m_pRenderer->GetVideoRect(source, dest, view);
}

float CRenderManager::GetAspectRatio() const
{
  std::unique_lock lock(m_statelock);
  if (m_pRenderer)
    return m_pRenderer->GetAspectRatio();
  else
    return 1.0f;
}

unsigned int CRenderManager::GetOrientation() const
{
  std::unique_lock lock(m_statelock);
  if (m_pRenderer)
    return m_pRenderer->GetOrientation();
  else
    return 0;
}

void CRenderManager::SetVideoSettings(const CVideoSettings& settings)
{
  std::unique_lock lock(m_statelock);
  if (m_pRenderer)
  {
    m_pRenderer->SetVideoSettings(settings);
  }
}

bool CRenderManager::Configure(const VideoPicture& picture, float fps, unsigned int orientation, int buffers)
{

  // check if something has changed
  {
    std::unique_lock lock(m_statelock);

    if (!m_bRenderGUI)
      return true;

    if (m_pRenderer != nullptr && m_picture.IsSameParams(picture) && m_orientation == orientation &&
        m_NumberBuffers == buffers && !m_pRenderer->ConfigChanged(picture))
    {
      if (m_fps != fps)
      {
        CLog::Log(LOGDEBUG, "CRenderManager::Configure - framerate changed from {:4.2f} to {:4.2f}",
                  m_fps, fps);
        m_fps = fps;
        m_pRenderer->SetFps(fps);
        m_bTriggerUpdateResolution = true;
        // Clear stale vsync/late-frame state from the old framerate; CheckEnableClockSync() will recalibrate on the next FrameMove on the main thread.
        m_clockSync.Reset();
        m_dvdClock.SetVsyncAdjust(0);
        m_lateframes = -1;
      }
      return true;
    }
  }

  CLog::Log(LOGDEBUG,
            "CRenderManager::Configure - change configuration. {}x{}. display: {}x{}. framerate: "
            "{:4.2f}.",
            picture.iWidth, picture.iHeight, picture.iDisplayWidth, picture.iDisplayHeight, fps);

  // make sure any queued frame was fully presented
  {
    std::unique_lock lock(m_presentlock);
    XbmcThreads::EndTime<> endtime(5000ms);
    m_forceNext = true;
    while (m_presentstep != PRESENT_IDLE)
    {
      if(endtime.IsTimePast())
      {
        CLog::Log(LOGWARNING, "CRenderManager::Configure - timeout waiting for state");
        m_forceNext = false;
        return false;
      }
      m_presentevent.wait(lock, endtime.GetTimeLeft());
    }
    m_forceNext = false;
  }

  {
    std::unique_lock lock(m_statelock);
    m_picture.SetParams(picture);
    m_fps = fps;
    m_orientation = orientation;
    m_NumberBuffers  = buffers;
    m_renderState = STATE_CONFIGURING;
    m_stateEvent.Reset();
    m_clockSync.Reset();
    m_dvdClock.SetVsyncAdjust(0);
    m_pConfigPicture = std::make_unique<VideoPicture>();
    m_pConfigPicture->CopyRef(picture);

    std::unique_lock lock2(m_presentlock);
    m_presentstep = PRESENT_READY;
	CLog::LogFC(LOGDEBUG, LOGAVTIMING,  "PRESENT_READY");
	m_presentevent.notifyAll();
  }

  if (!m_stateEvent.Wait(1000ms))
  {
    CLog::Log(LOGWARNING, "CRenderManager::Configure - timeout waiting for configure");
    std::unique_lock lock(m_statelock);
    return false;
  }

  std::unique_lock lock(m_statelock);
  if (m_renderState != STATE_CONFIGURED)
  {
    CLog::Log(LOGWARNING, "CRenderManager::Configure - failed to configure");
    return false;
  }

  return true;
}

bool CRenderManager::Configure()
{
  // lock all interfaces
  std::unique_lock lock(m_statelock);
  std::unique_lock lock2(m_presentlock);
  std::unique_lock lock3(m_datalock);

  if (m_pRenderer)
  {
    DeleteRenderer();
  }

  if (!m_pRenderer)
  {
    CreateRenderer();
    if (!m_pRenderer)
      return false;
  }

  m_pRenderer->SetVideoSettings(m_playerPort->GetVideoSettings());
  bool result = m_pRenderer->Configure(*m_pConfigPicture, m_fps, m_orientation);
  if (result)
  {
    CRenderInfo info = m_pRenderer->GetRenderInfo();
    int renderbuffers = info.max_buffer_size;
    m_QueueSize = renderbuffers;
    if (m_NumberBuffers > 0)
      m_QueueSize = std::min(m_NumberBuffers, renderbuffers);

    if(m_QueueSize < 2)
    {
      m_QueueSize = 2;
      CLog::Log(LOGWARNING, "CRenderManager::Configure - queue size too small ({}, {}, {})",
                m_QueueSize, renderbuffers, m_NumberBuffers);
    }

    m_pRenderer->SetBufferSize(m_QueueSize);
    m_pRenderer->Update();

    m_playerPort->UpdateRenderInfo(info);
    m_playerPort->UpdateGuiRender(true);
    m_playerPort->UpdateVideoRender(m_pRenderer->VideoBypassesFramebuffer());

    m_queued.clear();
    m_discard.clear();
    m_free.clear();
    m_presentsource = 0;
    m_presentsourcePast = -1;
    for (int i=1; i < m_QueueSize; i++)
      m_free.push_back(i);

    m_bRenderGUI = true;
    m_bTriggerUpdateResolution = true;
    m_presentstep = PRESENT_IDLE;
    m_presentpts = DVD_NOPTS_VALUE;
    m_lateframes = -1;
    m_presentevent.notifyAll();
    m_renderedDebugOverlay = false;
    m_renderDebug = false;
    m_clockSync.Reset();
    m_dvdClock.SetVsyncAdjust(0);
    m_overlays.Reset();
    m_overlays.SetStereoMode(m_picture.stereoMode);

    m_renderState = STATE_CONFIGURED;

    CLog::Log(LOGDEBUG, "CRenderManager::Configure - {}", m_QueueSize);
  }
  else
    m_renderState = STATE_UNCONFIGURED;

  m_pConfigPicture.reset();

  m_stateEvent.Set();
  m_playerPort->VideoParamsChange();
  return result;
}

bool CRenderManager::IsConfigured() const
{
  std::unique_lock lock(m_statelock);
  if (m_renderState == STATE_CONFIGURED)
    return true;
  else
    return false;
}

class FramePLL2 {
private:
  double current_fps;
  double target_period;
  double current_phase;
  double current_freq;
  bool is_initialized;
  double K_p;
  double K_i;
  double max_drift;
  int lock_progress;
  const int FRAMES_TO_FULL_LOCK = 60;

  void updateGains() {
	double progress_ratio = std::min(1.0, static_cast<double>(lock_progress) / FRAMES_TO_FULL_LOCK);
	double fast_settle = 0.3;
	double slow_settle = 1.5;

	// Scale settle time based on fps so 60Hz isn't overly aggressive
	double current_settle_time = fast_settle + (slow_settle - fast_settle) * progress_ratio;

	// Relax the response slightly specifically for higher framerates
	if(current_fps >= 49.0) {
	  current_settle_time *= 1.5;
	}

	double omega_n = 4.0 / (0.707 * current_settle_time);
	double T_s = target_period / 1000000.0;

	K_p = 2.0 * 0.707 * omega_n * T_s;
	K_i = std::pow(omega_n * T_s, 2.0);

	// Perceptual Jitter budget
	double jitter_budget_percent = 0.04;
	max_drift = target_period * jitter_budget_percent;
  }
  void reconfigure(double new_fps, double initial_pts) {
	current_fps = new_fps;
	target_period = 1000000.0 / current_fps;
	current_freq = 0.0;
	current_phase = initial_pts;
	is_initialized = true;
	lock_progress = 0;
	updateGains();
  }

public:
  FramePLL2() : current_fps(0.0), target_period(0.0), current_phase(0.0), current_freq(0.0),
	is_initialized(false), K_p(0.0), K_i(0.0), max_drift(0.0), lock_progress(0) {
  }

  void forceReset() { is_initialized = false; }

  double process(double target_fps, double input_pts) {
	if(target_fps <= 0.0) return input_pts;

	if(!is_initialized || target_fps != current_fps) {
	  reconfigure(target_fps, input_pts);
	  return current_phase;
	}

	// 1. DISCONTINUITY DETECTOR
	double raw_deviation = std::abs(input_pts - (current_phase + target_period));
	double discontinuity_threshold = target_period * 3.0;

	if(raw_deviation > discontinuity_threshold) {
	  reconfigure(target_fps, input_pts);
	  return current_phase;
	}

	// 2. FIXED PHASE UNWRAPPING
	// Calculate the error relative to the next expected timeline step
	double expected_next_pts = current_phase + target_period;
	double error = input_pts - expected_next_pts;

	// Use modern remainder mapping instead of structural destructive loops
	// This calculates how far the frame is from the closest multi-frame boundary step
	error = error - target_period * std::round(error / target_period);

	// 3. LOOP FILTER WITH INTEGRAL ANTI-WINDUP PROTECTION
	// Only increment frequency if the error is outside the strict perceptual jitter budget 
	// (Eliminates micro-oscillations near 60Hz)
	if(std::abs(error) > max_drift * 0.5) {
	  current_freq += K_i * error;
	}

	// ANTI-WINDUP: Prevent the frequency memory from outgrowing our clamp limits
	double max_freq_drift = target_period * 0.2;
	current_freq = std::clamp(current_freq, -max_freq_drift, max_freq_drift);

	// Calculate raw adjustment step
	lock_progress++;
	updateGains();

	double delta_t = target_period + current_freq + (K_p * error);

	// 4. JITTER CLAMP
	delta_t = std::clamp(delta_t, target_period - max_drift, target_period + max_drift);
	current_phase += delta_t;
	return current_phase;
  }
};

FramePLL2 synchPLL;
CPLHelper::CMonitor jitterMonitor1(120);
CPLHelper::CMonitor jitterMonitor2(120);

void CRenderManager::RecordFlipEndTime() 
{
  double fps = CServiceBroker::GetWinSystem()->GetGfxContext().GetFPS(); //cl Changed GetFPS implementation because it was always returning 60 on my PC, irrespective of the real value.
  static double oldFlipEndTime = 0;
  static double oldDiff = 0;
  static double oldFlipEndTime2 = 0;
  static double oldDiff2 = 0;

  const int CHECK_INTERVAL = 48; 
  double freq = CurrentHostFrequency(); //cl opt all
  double ticksPerFrame = (double) DVD_TIME_BASE / fps;
  static bool bInit = false;
  static double currentPts = 0;
  static int frameCounter = 0;
  static double qpcDriftCorrection = 0.0; 
  if(!bInit)
  {
	currentPts = m_dvdClock.SystemToPlaying(DX::DeviceResources::Get()->GetLatestVsyncTime());
	frameCounter = 0;
	qpcDriftCorrection = 0.0;
	bInit = true;
  }

  double realPts = m_dvdClock.SystemToPlaying(DX::DeviceResources::Get()->GetLatestVsyncTime());
  ++frameCounter;
  currentPts += ticksPerFrame;
  CLog::LogFC(LOGDEBUG, LOGAVTIMING, "currentPts: {}, realPts: {}, diff: {} ms, frameCounterL {}, qpcDriftCorrection: {}", currentPts, realPts, (currentPts- realPts)/ (double)DVD_TIME_BASE*1000.0, frameCounter, qpcDriftCorrection);
  if(std::abs(currentPts - realPts) > ticksPerFrame * 0.5)
  {
	currentPts = realPts;
	frameCounter = 0;
	qpcDriftCorrection = 0.0;
  }
  else
  {
	if(frameCounter >= CHECK_INTERVAL)
	{
	  frameCounter = 0;

	  double ptsError = currentPts - realPts;
	  qpcDriftCorrection = -((ptsError * 0.20) / CHECK_INTERVAL);
	  currentPts -= (ptsError * 0.10);
	}
  }
  m_flipEndTime = currentPts;
  m_filteredFlipEndTime = synchPLL.process(fps, m_flipEndTime); //cl still needed?
  
  if((m_flipEndTime- oldFlipEndTime) > 1000000.0 / fps * 1.6)
  {
	m_flipSkipped++;
	CLog::LogFC(LOGDEBUG, LOGAVTIMING, "Skipped");
  }


  double diff = m_flipEndTime - oldFlipEndTime;
  double diff2 = m_filteredFlipEndTime - oldFlipEndTime2;

  m_rawJitter = diff - oldDiff;
  m_rawJitter2 = diff2 - oldDiff2;

  jitterMonitor1.update(std::abs(m_rawJitter));
  if(std::abs(diff) > 200000)
	jitterMonitor1.reset();
  jitterMonitor2.update(std::abs(m_rawJitter2));
  if(std::abs(diff2) > 200000)
	jitterMonitor2.reset();

  CLog::LogFC(LOGDEBUG, LOGAVTIMING, "raw: {:.0f}, filtered: {:.0f}, raw jitter: {:.0f}, filtered jitter {:.0f}, raw-filtered delta: {}, flipSkipped: {}", m_flipEndTime, m_filteredFlipEndTime, m_rawJitter, m_rawJitter2, m_filteredFlipEndTime- m_flipEndTime, m_flipSkipped);
  oldFlipEndTime = m_flipEndTime;
  oldDiff = diff;
  oldFlipEndTime2 = m_filteredFlipEndTime;
  oldDiff2 = diff2;
}

void CRenderManager::ShowVideo(bool enable)
{
  m_showVideo = enable;
  if (!enable)
    DiscardBuffer();
}

void CRenderManager::FrameWait(std::chrono::milliseconds duration)
{
  XbmcThreads::EndTime<> timeout{duration};
  std::unique_lock lock(m_presentlock);
  while(m_presentstep == PRESENT_IDLE && !timeout.IsTimePast())
    m_presentevent.wait(lock, timeout.GetTimeLeft());
}

bool CRenderManager::IsPresenting()
{
  if (!IsConfigured())
    return false;

  std::unique_lock lock(m_presentlock);
  if (!m_presentTimer.IsTimePast())
    return true;
  else
    return false;
}

void CRenderManager::FrameMove()
{
  CLog::LogFC(LOGDEBUG, LOGAVTIMING, "Enter");
  bool firstFrame = false;
  UpdateResolution();

  {
    std::unique_lock lock(m_statelock);

    if (m_renderState == STATE_UNCONFIGURED)
      return;
    else if (m_renderState == STATE_CONFIGURING)
    {
      lock.unlock();
      if (!Configure())
        return;
      UpdateLatencyTweak();
      firstFrame = true;
      FrameWait(50ms);
    }

    CheckEnableClockSync();
  }
  {
    std::unique_lock lock2(m_presentlock);

    if (m_queued.empty())
    {
      m_presentstep = PRESENT_IDLE;
	  CLog::LogFC(LOGDEBUG, LOGAVTIMING, "PRESENT_IDLE");

    }
    else
    {
      m_presentTimer.Set(1000ms);
    }

    if (m_presentstep == PRESENT_READY)
      PrepareNextRender();

    if (m_presentstep == PRESENT_FLIP)
    {
      m_presentstep = PRESENT_FRAME;
	  CLog::LogFC(LOGDEBUG, LOGAVTIMING, "PRESENT_FRAME");
	  m_presentevent.notifyAll();
    }

    // release all previous
    for (std::deque<int>::iterator it = m_discard.begin(); it != m_discard.end(); )
    {
      // renderer may want to keep the frame for postprocessing
      if (!m_pRenderer->NeedBuffer(*it) || !m_bRenderGUI)   //cl ?m_bRenderGUI
      {
        m_pRenderer->ReleaseBuffer(*it);
        m_overlays.Release(*it);
        m_free.push_back(*it);
        it = m_discard.erase(it);
      }
      else
        ++it;
    }

    m_playerPort->UpdateRenderBuffers(m_queued.size(), m_discard.size(), m_free.size());
    m_bRenderGUI = true;
  }

  m_playerPort->UpdateGuiRender(IsGuiLayer() || !m_pRenderer->VideoBypassesFramebuffer() ||
                                firstFrame);

  // Run libass for the current PTS and cache the output for ConvertLibass
  // to use during the render pass. PrepareOverlays MarkDirty's on libass
  // changes and on PGS/DVB/SPU arrival/disappearance.
  m_overlays.PrepareOverlays(m_presentsource);
  CLog::LogFC(LOGDEBUG, LOGAVTIMING, "Exit");
}

void CRenderManager::PreInit()
{
  {
    std::unique_lock lock(m_statelock);
    if (m_renderState != STATE_UNCONFIGURED)
      return;
  }

  if (!CServiceBroker::GetAppMessenger()->IsProcessThread())
  {
    m_initEvent.Reset();
    CServiceBroker::GetAppMessenger()->PostMsg(TMSG_RENDERER_PREINIT);
    if (!m_initEvent.Wait(2000ms))
    {
      CLog::Log(LOGERROR, "{} - timed out waiting for renderer to preinit", __FUNCTION__);
    }
  }

  std::unique_lock lock(m_statelock);

  if (!m_pRenderer)
  {
    CreateRenderer();
  }

  UpdateLatencyTweak();

  m_QueueSize   = 2;
  m_QueueSkip   = 0;
  m_presentstep = PRESENT_IDLE;
  m_bRenderGUI = true;

  m_initEvent.Set();
}

void CRenderManager::UnInit()
{
  if (!CServiceBroker::GetAppMessenger()->IsProcessThread())
  {
    m_initEvent.Reset();
    CServiceBroker::GetAppMessenger()->PostMsg(TMSG_RENDERER_UNINIT);
    if (!m_initEvent.Wait(2000ms))
    {
      CLog::Log(LOGERROR, "{} - timed out waiting for renderer to uninit", __FUNCTION__);
    }
  }

  std::unique_lock lock(m_statelock);

  m_overlays.UnInit();
  m_debugRenderer.Dispose();

  m_captureBlit.reset();
  DeleteRenderer();

  m_renderState = STATE_UNCONFIGURED;
  m_picture.Reset();
  m_bRenderGUI = false;

  m_initEvent.Set();
}

bool CRenderManager::Flush(bool wait, bool saveBuffers)
{
  if (!m_pRenderer)
    return true;

  if (CServiceBroker::GetAppMessenger()->IsProcessThread())
  {
    CLog::Log(LOGDEBUG, "{} - flushing renderer", __FUNCTION__);

// fix deadlock on Windows only when is enabled 'Sync playback to display'
#ifndef TARGET_WINDOWS
    CSingleExit exitlock(CServiceBroker::GetWinSystem()->GetGfxContext());
#endif

    std::unique_lock lock(m_statelock);
    std::unique_lock lock2(m_presentlock);
    std::unique_lock lock3(m_datalock);

    if (m_pRenderer)
    {
      m_overlays.Flush();
      m_debugRenderer.Flush();

      if (!m_pRenderer->Flush(saveBuffers))
      {
        m_queued.clear();
        m_discard.clear();
        m_free.clear();
        m_presentsource = 0;
        m_presentsourcePast = -1;
        m_presentstep = PRESENT_IDLE;
        for (int i = 1; i < m_QueueSize; i++)
          m_free.push_back(i);
      }

      m_flushEvent.Set();
    }
  }
  else
  {
    m_flushEvent.Reset();
    CServiceBroker::GetAppMessenger()->PostMsg(TMSG_RENDERER_FLUSH);
    if (wait)
    {
      if (!m_flushEvent.Wait(1000ms))
      {
        CLog::Log(LOGERROR, "{} - timed out waiting for renderer to flush", __FUNCTION__);
        return false;
      }
      else
        return true;
    }
  }
  return true;
}

void CRenderManager::CreateRenderer()
{
  if (!m_pRenderer)
  {
    CVideoBuffer *buffer = nullptr;
    if (m_pConfigPicture)
      buffer = m_pConfigPicture->videoBuffer;

    auto renderers = VIDEOPLAYER::CRendererFactory::GetRenderers();
    for (auto &id : renderers)
    {
      if (id == "default")
        continue;

      m_pRenderer = VIDEOPLAYER::CRendererFactory::CreateRenderer(id, buffer);
      if (m_pRenderer)
      {
        return;
      }
    }
    m_pRenderer = VIDEOPLAYER::CRendererFactory::CreateRenderer("default", buffer);
  }
}

void CRenderManager::DeleteRenderer()
{
  if (m_pRenderer)
  {
    CLog::Log(LOGDEBUG, "{} - deleting renderer", __FUNCTION__);

    delete m_pRenderer;
    m_pRenderer = NULL;
  }
}

void CRenderManager::ServiceVideoCaptures()
{
  using namespace KODI::RENDERING::CAPTURE;

  const auto captureService = CServiceBroker::GetCaptureService();
  if (!captureService)
    return;

  const auto requests = captureService->TakeActive(CaptureContent::VIDEO);
  if (requests.empty())
    return;

  // the renderer does not draw video into the framebuffer, so the copy-back
  // below would capture the GUI (if any) instead of the video; fail the request
  if (m_pRenderer->VideoBypassesFramebuffer())
  {
    for (const auto& request : requests)
    {
      CaptureResult result;
      if (m_pRenderer->CaptureVideoFrame(request->spec, result))
        captureService->Complete(request, std::move(result));
      else
        captureService->Fail(request);
    }
    return;
  }

  CRect src;
  CRect dst;
  CRect view;
  m_pRenderer->GetVideoRect(src, dst, view);

  // zoom modes push destRect past the output; the copy is clamped to the
  // visible region, so native-size targets must be sized from it too
  const CGraphicContext& gfx = CServiceBroker::GetWinSystem()->GetGfxContext();
  CRect visible{dst};
  visible.Intersect(
      CRect(0.0f, 0.0f, static_cast<float>(gfx.GetWidth()), static_cast<float>(gfx.GetHeight())));

  if (!m_captureBlit)
    m_captureBlit = std::make_unique<CCaptureBlit>();

  for (const auto& request : requests)
  {
    const unsigned int width =
        request->spec.width ? request->spec.width : static_cast<unsigned int>(visible.Width());
    const unsigned int height =
        request->spec.height ? request->spec.height : static_cast<unsigned int>(visible.Height());

    CaptureResult result;
    if (m_captureBlit->Blit(visible, width, height, request->spec.format) &&
        m_captureBlit->Read(result))
    {
      result.color = GetOutputColorMetadata(*CServiceBroker::GetWinSystem());
      // the presented PQ is the content's during passthrough, so carry its
      // mastering metadata verbatim: the source peak an SDR tonemap needs.
      // NOTE: m_picture, not m_pConfigPicture, which Configure() has already
      // reset by the time this frame reaches CONFIGURED.
      result.hasDisplayMetadata = m_picture.hasDisplayMetadata;
      result.displayMetadata = m_picture.displayMetadata;
      result.hasLightMetadata = m_picture.hasLightMetadata;
      result.lightMetadata = m_picture.lightMetadata;
      result.content = CaptureContent::VIDEO; // this tap delivers video-only
      captureService->Complete(request, std::move(result));
    }
    else
      captureService->Fail(request);
  }
}

void CRenderManager::SetViewMode(int iViewMode)
{
  std::unique_lock lock(m_statelock);
  if (m_pRenderer)
    m_pRenderer->SetViewMode(iViewMode);
  m_playerPort->VideoParamsChange();
}

RESOLUTION CRenderManager::GetResolution()
{
  RESOLUTION res = CServiceBroker::GetWinSystem()->GetGfxContext().GetVideoResolution();

  std::unique_lock lock(m_statelock);
  if (m_renderState == STATE_UNCONFIGURED)
    return res;

  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_VIDEOPLAYER_ADJUSTREFRESHRATE) != ADJUST_REFRESHRATE_OFF)
    res = CResolutionUtils::ChooseBestResolution(m_fps, m_picture.iWidth, m_picture.iHeight,
                                                 !m_picture.stereoMode.empty());

  return res;
}


void CRenderManager::Render(bool clear, DWORD flags, DWORD alpha, bool gui)
{
  CSingleExit exitLock(CServiceBroker::GetWinSystem()->GetGfxContext());

  {
    std::unique_lock lock(m_statelock);
    if (m_renderState != STATE_CONFIGURED)
      return;
  }

  if (!gui && m_pRenderer->IsGuiLayer())
    return;

  bool presented = false;
  if (!gui || m_pRenderer->IsGuiLayer())
  {
    SPresent& m = m_Queue[m_presentsource];

    if( m.presentmethod == PRESENT_METHOD_BOB )
      PresentFields(clear, flags, alpha);
    else if( m.presentmethod == PRESENT_METHOD_BLEND )
      PresentBlend(clear, flags, alpha);
    else
      PresentSingle(clear, flags, alpha);

    presented = true;
  }

  // the just-presented region is video-only: OSD, GUI and subtitles come later
  if (presented)
    ServiceVideoCaptures();

  if (gui)
  {
    if (!m_pRenderer->IsGuiLayer())
      m_pRenderer->Update();

    CRect src, dst, view;
    m_pRenderer->GetVideoRect(src, dst, view);
    m_overlays.SetVideoRect(src, dst, view);
    m_overlays.Render(m_presentsource);

    if (m_renderDebug)
    {
	  //if (!m_renderDebugVideo)
      {
		static DEBUG_INFO_PLAYER info = {};
		static DEBUG_INFO_VIDEO video = {};
		static DEBUG_INFO_RENDER render = {};

		static auto lastUpdateTime = std::chrono::steady_clock::now();
		static double cachedLibplaceboLoad = 0.0;
		static double cachedDxvaLoad = 0.0;

		auto currentTime = std::chrono::steady_clock::now();
		auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastUpdateTime).count();

		// Only query every so often
		if(elapsedTime >= 500) {
		  m_playerPort->GetDebugInfo(info.audio, info.video, info.player);
		  double refreshrate, clockspeed;
		  int missedvblanks;

		  info.vsync = StringUtils::Format("Flip skip: {}, VSyncOff: {:5.1f}, latency: {:6.3f}", m_flipSkipped, m_clockSync.m_syncOffset / 1000, DVD_TIME_TO_MSEC(m_displayLatency) / 1000.0f);
		  if(m_dvdClock.GetClockInfo(missedvblanks, clockspeed, refreshrate))
		  {
			info.vsync += StringUtils::Format("VSync: refresh:{:.3f} missed:{} speed:{:.3f}%",
			  refreshrate, missedvblanks, clockspeed * 100);
		  }
		  double dummy;
		  info.jitter1 = StringUtils::Format("R jitterMax: {:4.2f}, JitterStdDev: {:4.2f}, Jitter: {:6.2f}", jitterMonitor1.calculatePeak() / 1000.0, std::sqrt(jitterMonitor1.calculateVariance(dummy)) / 1000.0, m_rawJitter / 1000.0);
		  info.jitter2 = StringUtils::Format("F JitterMax: {:4.2f}, JitterStdDev: {:4.2f}, Jitter: {:6.2f}", jitterMonitor2.calculatePeak() / 1000.0, std::sqrt(jitterMonitor2.calculateVariance(dummy)) / 1000.0, m_rawJitter2 / 1000.0);
		  
		  video = m_pRenderer->GetDebugInfo(m_presentsource);
		  render = CServiceBroker::GetWinSystem()->GetDebugInfo();

		  lastUpdateTime = currentTime;
		}
        m_debugRenderer.SetInfo(info, true);
		m_debugRenderer.SetInfo(video, render, false);
	  }

      m_debugRenderer.Render(src, dst, view);

      m_debugTimer.Set(1000ms);
      m_renderedDebugOverlay = true;
    }
  }

  const SPresent& m = m_Queue[m_presentsource];

  {
	CLog::LogFC(LOGDEBUG, LOGAVTIMING, "BEFORE LOCK");
	std::unique_lock lock(m_presentlock);
	CLog::LogFC(LOGDEBUG, LOGAVTIMING, "AFTER LOCK");

    if (m_presentstep == PRESENT_FRAME)
    {
      if (m.presentmethod == PRESENT_METHOD_BOB)
	  {
        m_presentstep = PRESENT_FRAME2;
		CLog::LogFC(LOGDEBUG, LOGAVTIMING, "PRESENT_FRAME2");
	  }
      else
	  {
        m_presentstep = PRESENT_IDLE;
		CLog::LogFC(LOGDEBUG, LOGAVTIMING, "PRESENT_IDLE");
	  }
	}
    else if (m_presentstep == PRESENT_FRAME2)
	{
      m_presentstep = PRESENT_IDLE;
	  CLog::LogFC(LOGDEBUG, LOGAVTIMING, "PRESENT_IDLE");
	}

    if (m_presentstep == PRESENT_IDLE)
    {
      if (!m_queued.empty())
	  {
        m_presentstep = PRESENT_READY;
		CLog::LogFC(LOGDEBUG, LOGAVTIMING, "PRESENT_READY");
	  }
    }

    m_presentevent.notifyAll();
  }
}

bool CRenderManager::IsGuiLayer()
{
  {
    std::unique_lock lock(m_statelock);

    if (!m_pRenderer)
      return false;

    if ((m_pRenderer->IsGuiLayer() && IsPresenting()) || m_renderedDebugOverlay ||
        m_overlays.HasVisibleOverlay(m_presentsource))
      return true;

    if (m_renderDebug && m_debugTimer.IsTimePast())
      return true;
  }
  return false;
}

/* simple present method */
void CRenderManager::PresentSingle(bool clear, DWORD flags, DWORD alpha)
{
  const SPresent& m = m_Queue[m_presentsource];

  if (m.presentfield == FS_BOT)
    m_pRenderer->RenderUpdate(m_presentsource, m_presentsourcePast, clear, flags | RENDER_FLAG_BOT, alpha, m_renderPts);
  else if (m.presentfield == FS_TOP)
    m_pRenderer->RenderUpdate(m_presentsource, m_presentsourcePast, clear, flags | RENDER_FLAG_TOP, alpha, m_renderPts);
  else
    m_pRenderer->RenderUpdate(m_presentsource, m_presentsourcePast, clear, flags, alpha, m_renderPts);
}

/* new simpler method of handling interlaced material, *
 * we just render the two fields right after each other */
void CRenderManager::PresentFields(bool clear, DWORD flags, DWORD alpha)
{
  const SPresent& m = m_Queue[m_presentsource];

  if(m_presentstep == PRESENT_FRAME)
  {
    if( m.presentfield == FS_BOT)
      m_pRenderer->RenderUpdate(m_presentsource, m_presentsourcePast, clear, flags | RENDER_FLAG_BOT | RENDER_FLAG_FIELD0, alpha, m_renderPts);
    else
      m_pRenderer->RenderUpdate(m_presentsource, m_presentsourcePast, clear, flags | RENDER_FLAG_TOP | RENDER_FLAG_FIELD0, alpha, m_renderPts);
  }
  else
  {
    if( m.presentfield == FS_TOP)
      m_pRenderer->RenderUpdate(m_presentsource, m_presentsourcePast, clear, flags | RENDER_FLAG_BOT | RENDER_FLAG_FIELD1, alpha, m_renderPts2);
    else
      m_pRenderer->RenderUpdate(m_presentsource, m_presentsourcePast, clear, flags | RENDER_FLAG_TOP | RENDER_FLAG_FIELD1, alpha, m_renderPts2);
  }
}

void CRenderManager::PresentBlend(bool clear, DWORD flags, DWORD alpha)
{
  const SPresent& m = m_Queue[m_presentsource];

  if( m.presentfield == FS_BOT )
  {
    m_pRenderer->RenderUpdate(m_presentsource, m_presentsourcePast, clear, flags | RENDER_FLAG_BOT | RENDER_FLAG_NOOSD, alpha, m_renderPts);
    m_pRenderer->RenderUpdate(m_presentsource, m_presentsourcePast, false, flags | RENDER_FLAG_TOP, alpha / 2, m_renderPts);
  }
  else
  {
    m_pRenderer->RenderUpdate(m_presentsource, m_presentsourcePast, clear, flags | RENDER_FLAG_TOP | RENDER_FLAG_NOOSD, alpha, m_renderPts);
    m_pRenderer->RenderUpdate(m_presentsource, m_presentsourcePast, false, flags | RENDER_FLAG_BOT, alpha / 2, m_renderPts);
  }
}

void CRenderManager::UpdateLatencyTweak()
{
  float fps = CServiceBroker::GetWinSystem()->GetGfxContext().GetFPS();
  const bool isHDREnabled = CServiceBroker::GetWinSystem()->GetOSHDRStatus() == HDR_STATUS::HDR_ON;

  float refresh = fps;
  if (CServiceBroker::GetWinSystem()->GetGfxContext().GetVideoResolution() == RES_WINDOW)
    refresh = 0; // No idea about refresh rate when windowed, just get the default latency
  m_latencyTweak = static_cast<double>(
      CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->GetLatencyTweak(refresh,
                                                                                     isHDREnabled));
}

void CRenderManager::UpdateResolution()
{
  if (m_bTriggerUpdateResolution)
  {
    if (CServiceBroker::GetWinSystem()->GetGfxContext().IsFullScreenVideo() && CServiceBroker::GetWinSystem()->GetGfxContext().IsFullScreenRoot())
    {
      if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_VIDEOPLAYER_ADJUSTREFRESHRATE) != ADJUST_REFRESHRATE_OFF && m_fps > 0.0f)
      {
        RESOLUTION res = CResolutionUtils::ChooseBestResolution(
            m_fps, m_picture.iWidth, m_picture.iHeight, !m_picture.stereoMode.empty());
        CServiceBroker::GetWinSystem()->GetGfxContext().SetVideoResolution(res, false);
        UpdateLatencyTweak();
        if (m_pRenderer)
          m_pRenderer->Update();
      }
      m_bTriggerUpdateResolution = false;
      m_playerPort->VideoParamsChange();
    }
  }
}

void CRenderManager::TriggerUpdateResolution(float fps, int width, int height, std::string &stereomode)
{
  if (width)
  {
    m_fps = fps;
    m_picture.iWidth = width;
    m_picture.iHeight = height;
    m_picture.stereoMode = stereomode;
  }
  m_bTriggerUpdateResolution = true;
}

void CRenderManager::ToggleDebug(std::optional<bool> enable)
{
  if(enable.has_value())
  {
	m_renderDebug = enable.value();
	if(m_renderDebug)
	  m_debugRenderer.Initialize();
	else
	  m_debugRenderer.Dispose();

	m_debugTimer.SetExpired();
  }
  else
  {
	bool isEnabled = !m_renderDebug;

	if(isEnabled)
	  m_debugRenderer.Initialize();
	else
	  m_debugRenderer.Dispose();

	m_renderDebug = isEnabled;
	m_debugTimer.SetExpired();
	m_renderDebugVideo = false;
  }
}

void CRenderManager::ToggleDebugVideo(std::optional<bool> enable)
{
  if(enable.has_value())
  {
	m_renderDebugVideo = enable.value();

	m_debugTimer.SetExpired();
  }
  else
  {
	bool isEnabled = !m_renderDebug;

	if(isEnabled)
	  m_debugRenderer.Initialize();
	else
	  m_debugRenderer.Dispose();

	m_renderDebug = isEnabled;
	m_debugTimer.SetExpired();
	m_renderDebugVideo = true;
  }
}

void CRenderManager::SetSubtitleVerticalPosition(int value, bool save)
{
  m_overlays.SetSubtitleVerticalPosition(value, save);
}

bool CRenderManager::AddVideoPicture(const VideoPicture& picture, volatile std::atomic_bool& bStop, EINTERLACEMETHOD deintMethod, bool wait)
{
  std::unique_lock lock(m_presentlock);

  if (m_free.empty())
    return false;

  int index = m_free.front();

  {
    std::unique_lock lock(m_datalock);
    if (!m_pRenderer)
      return false;

    m_pRenderer->AddVideoPicture(picture, index);
  }


  // set fieldsync if picture is interlaced
  EFIELDSYNC displayField = FS_NONE; //cl
  if (picture.iFlags & DVP_FLAG_INTERLACED)
  {
    if (deintMethod != EINTERLACEMETHOD::VS_INTERLACEMETHOD_NONE)
    {
      if (picture.iFlags & DVP_FLAG_TOP_FIELD_FIRST)
        displayField = FS_TOP;
      else
        displayField = FS_BOT;
    }
  }

  EPRESENTMETHOD presentmethod = PRESENT_METHOD_SINGLE;
  if (deintMethod == VS_INTERLACEMETHOD_NONE)
  {
    presentmethod = PRESENT_METHOD_SINGLE;
    displayField = FS_NONE;
  }
  else
  {
    if (displayField == FS_NONE)
      presentmethod = PRESENT_METHOD_SINGLE;
    else
    {
      if (deintMethod == VS_INTERLACEMETHOD_RENDER_BLEND)
        presentmethod = PRESENT_METHOD_BLEND;
      else if (deintMethod == VS_INTERLACEMETHOD_RENDER_BOB)
        presentmethod = PRESENT_METHOD_BOB;
      else
      {
        if (!m_pRenderer->WantsDoublePass())
          presentmethod = PRESENT_METHOD_SINGLE;
        else
          presentmethod = PRESENT_METHOD_BOB;
      }
    }
  }


  SPresent& m = m_Queue[index];
  m.presentfield = displayField;
  m.presentmethod = presentmethod;
  m.pts = picture.pts;
  m_queued.push_back(m_free.front());
  m_free.pop_front();
  m_playerPort->UpdateRenderBuffers(m_queued.size(), m_discard.size(), m_free.size());

  // signal to any waiters to check state
  if (m_presentstep == PRESENT_IDLE)
  {
    m_presentstep = PRESENT_READY;
	CLog::LogFC(LOGDEBUG, LOGAVTIMING, "PRESENT_READY");
	m_presentevent.notifyAll();
  }

  if (wait)
  {
    m_forceNext = true;
    XbmcThreads::EndTime<> endtime(200ms);
    while (m_presentstep == PRESENT_READY)
    {
      m_presentevent.wait(lock, 20ms);
      if(endtime.IsTimePast() || bStop)
      {
        if (!bStop)
        {
          CLog::Log(LOGWARNING, "CRenderManager::AddVideoPicture - timeout waiting for render");
        }
        break;
      }
    }
    m_forceNext = false;
  }

  return true;
}

void CRenderManager::AddOverlay(std::shared_ptr<CDVDOverlay> o, double pts)
{
  int idx;
  {
    std::unique_lock lock(m_presentlock);
    if (m_free.empty())
      return;
    idx = m_free.front();
  }
  std::unique_lock lock(m_datalock);
  m_overlays.AddOverlay(std::move(o), pts, idx);
}

bool CRenderManager::Supports(ERENDERFEATURE feature) const
{
  std::unique_lock lock(m_statelock);
  if (m_pRenderer)
    return m_pRenderer->Supports(feature);
  else
    return false;
}

bool CRenderManager::Supports(ESCALINGMETHOD method) const
{
  std::unique_lock lock(m_statelock);
  if (m_pRenderer)
    return m_pRenderer->Supports(method);
  else
    return false;
}
void CRenderManager::GetRendererIOFormat(bool& isInputHDR, bool& isOutputHDR)
{
  std::unique_lock lock(m_statelock);
  if(m_pRenderer)
	m_pRenderer->GetRendererIOFormat(isInputHDR, isOutputHDR);
}

int CRenderManager::WaitForBuffer(volatile std::atomic_bool& bStop,
                                  std::chrono::milliseconds timeout)
{
  CLog::LogFC(LOGDEBUG, LOGAVTIMING, "Enter");
  std::unique_lock lock(m_presentlock);

  // check if gui is active and discard buffer if not
  // this keeps videoplayer going
  if (!m_bRenderGUI || !g_application.GetRenderGUI())
  {
    m_bRenderGUI = false;
    double presenttime = 0;
    double clock = m_dvdClock.GetClock();
    if (!m_queued.empty())
    {
      int idx = m_queued.front();
      presenttime = m_Queue[idx].pts;
    }
    else
      presenttime = clock + 0.02;

    auto sleeptime = std::chrono::milliseconds(static_cast<int>((presenttime - clock) * 1000));
    if (sleeptime < 0ms)
      sleeptime = 0ms;
    sleeptime = std::min(sleeptime, 20ms);
	m_presentevent.wait(lock, sleeptime);
    DiscardBuffer();
	CLog::LogFC(LOGDEBUG, LOGAVTIMING, "Exit");
	return 0;
  }

  XbmcThreads::EndTime<> endtime{timeout};
  while(m_free.empty())
  {
    m_presentevent.wait(lock, std::min(50ms, timeout));
    if (endtime.IsTimePast() || bStop)
    {
	  CLog::LogFC(LOGDEBUG, LOGAVTIMING, "Exit");
	  return -1;
	}
  }

  // make sure overlay buffer is released, this won't happen on AddOverlay
  m_overlays.Release(m_free.front());

  // return buffer level
  CLog::LogFC(LOGDEBUG, LOGAVTIMING, "Exit");
  return m_queued.size() + m_discard.size();
}

void CRenderManager::PrepareNextRender()
{
  static double lastFramePts = 0;
  if (m_queued.empty())
  {
    CLog::Log(LOGERROR, "CRenderManager::PrepareNextRender - asked to prepare with nothing available");
    m_presentstep = PRESENT_IDLE;
	CLog::LogFC(LOGDEBUG, LOGAVTIMING, "PRESENT_IDLE");
	m_presentevent.notifyAll();
    return;
  }

  if (!m_showVideo && !m_forceNext)
    return;

  double fps = CServiceBroker::GetWinSystem()->GetGfxContext().GetFPS(); //cl Changed GetFPS implementation because it was always returning 60 on my PC, irrespective of the real value.
  double frametime = DVD_TIME_BASE / fps;
 
  static double lastFrameOnScreen = 0;
  static double lastClock = 0;
  double clock = m_dvdClock.GetClock();
  double frameOnScreen = m_filteredFlipEndTime;
  double diff = frameOnScreen - lastFrameOnScreen;

  double diffClock = clock - lastClock;
  double clockDiff = clock - m_filteredFlipEndTime;
  lastFrameOnScreen = frameOnScreen;
  lastClock = clock;

  m_displayLatency = DVD_MSEC_TO_TIME(
      m_latencyTweak +
      static_cast<double>(CServiceBroker::GetWinSystem()->GetGfxContext().GetDisplayLatency()) -
      m_videoDelay -
      static_cast<double>(CServiceBroker::GetWinSystem()->GetFrameLatencyAdjustment()));

  const bool isPaused = m_dvdClock.IsPaused();
  double renderPts = frameOnScreen;
  if(!isPaused)
	renderPts += m_displayLatency;

  const double nextFramePts =
	m_dvdClock.GetClockSpeed() < 0 ? renderPts : m_Queue [m_queued.front()].pts;

  double err = 0.0;

  static double average = 0.0;
  if (m_clockSync.m_enabled)
  {
	if(nextFramePts != lastFramePts)
	{
	  err = fmod(renderPts - nextFramePts, frametime);
	  m_clockSync.m_error += err;
	  m_clockSync.m_errCount ++;
	  if(m_clockSync.m_errCount > 30) //cl adjust average offset between frame timestamp and target render time every 30 presentation frames
	  {
		average = m_clockSync.m_error / m_clockSync.m_errCount;
		m_clockSync.m_syncOffset = average;
		m_clockSync.m_error = 0;
		m_clockSync.m_errCount = 0;
		m_dvdClock.SetVsyncAdjust(-average); //cl for audio sync, the actual value is not used, just a test if it's different from 0 which allows correction to take place with error calculated somewhere else
	  }
	  if(!isPaused)
		renderPts += frametime / 2 - m_clockSync.m_syncOffset;
	}
  }
  else
  {
    m_dvdClock.SetVsyncAdjust(0);
  }

  renderPts += frametime / 2;  

  m_renderPts = renderPts;
  m_renderPts2 = renderPts + frametime; // In case of interleaved material, the PrepareNextRender function is only called once.
  CLog::LogFC(LOGDEBUG, LOGAVTIMING,
              "frameOnScreen: {:.0f}, renderPts: {:.0f}, nextFramePts: {:.0f}, diff: {:.0f}, render: {}, "
              "forceNext: {}, Queued: {}, Discard: {}, Free: {}, diffClock: {:.0f}, OnscreenDiff: {:.0f}, VsyncAdjust: {:.0f}, err: {:.0f}, clockDiff: {:.0f}, displayLatency: {:.0f}",
              frameOnScreen, renderPts, nextFramePts, (renderPts - nextFramePts),
              renderPts >= nextFramePts, m_forceNext, m_queued.size(), m_discard.size(), m_free.size(), diffClock, diff, average, err, clockDiff, m_displayLatency);
  lastFramePts = nextFramePts;

  bool combined = false;
  if (m_presentsourcePast >= 0)
  {
    m_discard.push_back(m_presentsourcePast);
    m_presentsourcePast = -1;
    combined = true;
  }

  if (renderPts >= nextFramePts || m_forceNext)
  {
    // see if any future queued frames are already due
    auto iter = m_queued.begin();
    int idx = *iter;
    ++iter;
    while (iter != m_queued.end())
    {
      // the slot for rendering in time is [pts .. (pts +  x * frametime)]
      // renderer/drivers have internal queues, being slightly late here does not mean that
      // we are really late. The likelihood that we recover decreases the greater m_lateframes
      // get. Skipping a frame is easier than having decoder dropping one (lateframes > 10)
      double x = (m_lateframes <= 6) ? 0.98 : 0;
      if (renderPts < m_Queue[*iter].pts + x * frametime)
        break;
      idx = *iter;
      ++iter;
    }

    // skip late frames
    while (m_queued.front() != idx)
    {
      if (m_presentsourcePast >= 0)
      {
		CLog::LogFC(LOGDEBUG, LOGAVTIMING, "Skip");
		m_discard.push_back(m_presentsourcePast);
        m_QueueSkip++;
      }
      m_presentsourcePast = m_queued.front();
      m_queued.pop_front();
    }

    const int lateframes = static_cast<int>((renderPts - m_Queue[idx].pts) *
                                            static_cast<double>(m_fps / DVD_TIME_BASE));
    if (lateframes)
      m_lateframes += lateframes;
    else
      m_lateframes = 0;

    m_presentstep = PRESENT_FLIP;
	CLog::LogFC(LOGDEBUG, LOGAVTIMING, "PRESENT_FLIP");
	m_discard.push_back(m_presentsource);
    m_presentsource = idx;
    m_queued.pop_front();
    m_presentpts = m_Queue[idx].pts - m_displayLatency;
    m_presentevent.notifyAll();

    m_playerPort->UpdateRenderBuffers(m_queued.size(), m_discard.size(), m_free.size());
  }
  else if (!combined && renderPts > (nextFramePts - frametime))
  {
    m_lateframes = 0;
    m_presentstep = PRESENT_FLIP;
	CLog::LogFC(LOGDEBUG, LOGAVTIMING, "PRESENT_FLIP");
	m_presentsourcePast = m_presentsource;
    m_presentsource = m_queued.front();
    m_queued.pop_front();
    m_presentpts = m_Queue[m_presentsource].pts - m_displayLatency - frametime / 2;
	m_presentevent.notifyAll();
  }
}

void CRenderManager::DiscardBuffer()
{
  std::unique_lock lock2(m_presentlock);

  while(!m_queued.empty())
  {
    m_discard.push_back(m_queued.front());
    m_queued.pop_front();
  }

  if(m_presentstep == PRESENT_READY)
  {
    m_presentstep = PRESENT_IDLE;
	CLog::LogFC(LOGDEBUG, LOGAVTIMING, "PRESENT_IDLE");
  }
  m_presentevent.notifyAll();
}

bool CRenderManager::GetStats(int &lateframes, double &pts, int &queued, int &discard)
{
  std::unique_lock lock(m_presentlock);
  lateframes = m_lateframes / 10;
  pts = m_presentpts;
  queued = m_queued.size();
  discard  = m_discard.size();
  return true;
}

void CRenderManager::CheckEnableClockSync()
{
  // refresh rate can be a multiple of video fps
  double diff = 1.0;

  if (m_fps != 0)
  {
    double fps = static_cast<double>(m_fps);
    double refreshrate, clockspeed;
    int missedvblanks;
    if (m_dvdClock.GetClockInfo(missedvblanks, clockspeed, refreshrate))
    {
      fps *= clockspeed;
    }

    diff = static_cast<double>(CServiceBroker::GetWinSystem()->GetGfxContext().GetFPS()) / fps;
    if (diff < 1.0)
      diff = 1.0 / diff;

    // Calculate distance from nearest integer proportion
    diff = std::abs(std::round(diff) - diff);
  }

  if (diff < 0.0005)
  {
    m_clockSync.m_enabled = true;
  }
  else
  {
    m_clockSync.m_enabled = false;
    m_dvdClock.SetVsyncAdjust(0);
  }

  m_playerPort->UpdateClockSync(m_clockSync.m_enabled);
}
