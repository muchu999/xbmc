/*
 *  Copyright (C) 2025 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */
#pragma once

#include "RendererHQ.h"
#include "VideoRenderers/HwDecRender/DXVAHD.h"
#include "VideoRenderers/RenderFlags.h"

#include <map>

#include <d3d11_4.h>

//#pragma comment(lib, "libplacebo.a")
#pragma comment(lib, "libplacebo.dll.a")

extern "C"
{
#include <libavutil/pixfmt.h>
}

#include "libplacebo/log.h"
#include "libplacebo/renderer.h"
#include "libplacebo/d3d11.h"
#include <libplacebo/options.h>
#include "libplacebo/utils/frame_queue.h"
#include "libplacebo/utils/upload.h"
#include "libplacebo/colorspace.h"
#define PL_LIBAV_IMPLEMENTATION 0
#include <libplacebo/utils/libav.h>
#include "VideoRenderers/Libplacebo/PLHelper.h"
#include <dxgi1_6.h>

#define MAX_FRAME_PASSES 256
#define MAX_BLEND_PASSES 8
#define MAX_BLEND_FRAMES 8

enum class SettinglibPlaceboNvRtxPipelineEnabled
{
  AUTO = -1,
  NO = 0,
  YES = 1
};

enum class SettinglibPlaceboTargetColorspaceHint
{
  AUTO = -1,
  NO = 0,
  YES = 1
};

enum class SettinglibPlaceboTargetColorspaceHintMode
{
  TARGET = 0,
  SOURCE = 1,
  SOURCE_DYNAMIC = 2
};

enum RenderMethod;

class CRendererPL;
struct SRequestContext
{
  CRendererPL* renderer;
  CRenderBuffer* buffer;
};

struct ProcessorFormats
{
  std::vector<DXGI_FORMAT> m_input;
  std::vector<DXGI_FORMAT> m_output;
  bool m_valid {false};
};


class CRTXVideoProcessor
{
public:
  Microsoft::WRL::ComPtr<ID3D11VideoDevice>           m_pVideoDevice;
  Microsoft::WRL::ComPtr<ID3D11VideoContext>          m_pVideoContext;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessor>        m_pVideoProcessor;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> m_pVideoEnumerator;

  UINT m_currentInputWidth = 0;
  UINT m_currentInputHeight = 0;
  UINT m_currentOutputWidth = 0;
  UINT m_currentOutputHeight = 0;
  
  bool IsInitialized() const { return m_bInitialized; }
  ProcessorFormats GetProcessorFormats(bool inputFormats, bool outputFormats) const;

  bool InitializePipeline(unsigned int width, unsigned int height, unsigned int iFlags, unsigned int m_viewWidth, unsigned int m_viewHeight);
  void ProcessVideoFrame(ID3D11VideoProcessorInputView* inputView, ID3D11VideoProcessorOutputView* outputView);
  void UninitializePipeline();
  bool ExecuteBlit(ID3D11VideoProcessorInputView* pInputView, CRenderBuffer* pBuffer, ID3D11VideoProcessorOutputView* pOutputView, unsigned int pictFlags, uint32_t flags, uint64_t frameIdx);
  void DebugBypassToDisplay(CRect& srcRect, ID3D11Texture2D* pOutputWindowTexture, ID3D11Texture2D* pTempTarget, CPoint(&destPoints) [4]);
  bool ConfigureColorSpaces(CRenderBuffer* pBuffer, ID3D11VideoProcessor* pProcessor, bool bUseNvRtxHdr, DXGI_FORMAT TempTargetDxgiFormat);
  bool EnsureProcessorSize(UINT inW, UINT inH, unsigned int iFlags, UINT outW, UINT outH);
  void FlushHistoryQueue();
  void EvaluateRtxCapability(const VideoPicture& picture);
  bool IsVsrViable() const {return m_isVsrViable;}
  bool IsRtxHdrViable() const { return m_isRtxHdrViable; }
  bool IsRtxPipelineViable() const { return m_isRtxPipelineViable; }
  bool IsRtxPipelineEnabled() const { return m_isRtxPipelineEnabled; }
  void EnablePipeline() { m_isRtxPipelineEnabled = true; }
  void DisablePipeline() { m_isRtxPipelineEnabled = false; }
  bool IsStreamHdr() const { return m_bStreamIsHDR; }
  void EnableNvidiaVideoSuperResolution(bool bEnable);
  void EnableNvidiaRtxVideoHdr(bool bEnable);

private:
  bool m_bStreamIsHDR = false;
  bool m_bInitialized = false;
  bool m_isVsrViable = false;
  bool m_isRtxHdrViable = false;
  bool m_isRtxPipelineViable = false;
  bool m_isRtxPipelineEnabled = false;
  bool m_wasVsrEnabled = false;
  bool m_wasHdrEnabled = false;
  int m_numPastFrames = 0;
  int m_numFutureFrames = 0;
  uint64_t m_lastFrameIdx = 0;
  uint64_t m_lastFieldIdx = 0;

  // Deque to cache incoming frame views. 
  // They must remain alive in VRAM until pushed out of the tracking history window.
  struct SViewElement
  {
	Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> inputView;
	CRenderBuffer* pBuffer;
  };

  std::deque<SViewElement> m_historyQueue;
};

class CRendererPL : public CRendererBase
{
    CRect ApplyTransforms(const CRect& destRect) const;
public:
  class CRenderBufferImpl;
  ~CRendererPL();
  void RenderStart(CRenderBuffer* rb, const CRect& sourceRect, const CRect& destRect);
  bool UploadBuffer(CRenderBuffer* buffer) override;
  void CheckNvRtxStatus(bool& bUseNvRtxHdr, bool& bUseNvSuperResolution);
  void ClearBuffer(CRenderBuffer* buffer);

  bool CreateSoftwareUploadTarget(CRenderBufferImpl* pBuf, unsigned int width, unsigned int height);
  bool CreateTempTarget(unsigned int width, unsigned int height);
  DXGI_FORMAT GetDXGIFormat(AVPixelFormat format);
  void UpdateVideoFilters() override;
  bool NeedBuffer(int idx) override;
  CRenderInfo GetRenderInfo() override;
  bool Supports(ESCALINGMETHOD method) const override;
  bool Supports(ERENDERFEATURE feature) const override;
  void GetRendererIOFormat(bool& isInputHDR, bool& isOutputHDR);
  bool WantsDoublePass() override { return true; }
  bool Configure(const VideoPicture& picture, float fps, unsigned orientation) override;
  //bool IsRtxPipelineActive() const { return m_RtxVideoProcessor.IsRtxPipelineEnabled(); }

  void AddVideoPicture(const VideoPicture& picture, int index) override; 
  static bool MapFrame(pl_gpu gpu, pl_tex* tex, const struct pl_source_frame* src, struct pl_frame* out_frame);
  static void UnmapFrame(pl_gpu gpu, struct pl_frame* frame, const struct pl_source_frame* src);
  static void DiscardFrame(const struct pl_source_frame* src);


  DEBUG_INFO_VIDEO GetDebugInfo(int idx) override;

  static CRendererBase* Create(CVideoSettings& videoSettings);
  static void GetWeight(std::map<RenderMethod, int>& weights, const VideoPicture& picture);
  static bool InitializeFrame(pl_swapchain sw, pl_frame& frameOut);
  static int getColorDepth(void);
  std::unique_ptr<DXVA::CProcessorHD> m_processor;
  std::shared_ptr<DXVA::CEnumeratorHD> m_enumerator;
  DXVA::ProcessorConversion m_conversion;
  DXVA::SupportedConversionsArgs m_conversionsArgs;
  bool m_tryVSR {false};
  void OnRtxSettingChanged();
  void HandleDeferredSync();


protected:
  explicit CRendererPL(CVideoSettings& videoSettings);

  void CheckVideoParameters() override;
  void RenderImpl(CD3DTexture& target, CRect& sourceRect, CPoint(&destPoints)[4], uint32_t flags, double renderPts = 0.0) override;
  void Render(CD3DTexture& target, CRect& sourceRect, CPoint(&destPoints) [4], uint32_t flags, double renderPts = 0.0);
  void RenderDx(CD3DTexture& target, CRect& sourceRect, CPoint(&destPoints) [4], uint32_t flags, bool canSip);
  void ApplyGeometry(CVideoSettings& vs, CRect& sourceRect, CRect& dst, pl_frame& frameIn, pl_frame& frameOut);
  static void InitializeFrameInFieldsMix(CVideoSettings& vs, pl_frame* frameIn, CRendererPL::CRenderBufferImpl* buffer);
  void InitializeFrameInFields(pl_frame* frameIn, CRendererPL::CRenderBufferImpl* buffer);
  void ApplyTargetOptions(CVideoSettings& videoSettings, struct pl_frame* source, struct pl_frame* target, float min_luma, bool hint);
  CRenderBuffer* CreateBuffer() override;

  //important to override we need to let libplacebo set the swapchain csp or we would get issues with HDR
  void ProcessHDR(CRenderBuffer* rb) override;
  void RenderSingle(CRenderBufferImpl* buffer, double renderPts, CVideoSettings& videoSettings, CRect& sourceRect, CRect& dst, pl_frame frameIn, pl_frame frameOut, ID3D11DeviceContext* pDeviceContext);
  void RenderMix(CRenderBufferImpl* buffer, double renderPts, CVideoSettings& videoSettings, CRect& sourceRect, CRect& dst, pl_frame frameOut, ID3D11DeviceContext* pDeviceContext);
  void RenderMixExec(CRenderBufferImpl* buffer, double renderPts, CVideoSettings& videoSettings, CRect& sourceRect, CRect& dst, pl_frame frameOut, ID3D11DeviceContext* pDeviceContext);

private:
  CD3DTexture m_TempTarget;
  Microsoft::WRL::ComPtr < ID3D11VideoProcessorOutputView> m_pTempTargetView;
  CD3DTexture m_SoftwareUploadTexture;
  CD3DTexture m_SoftwareUploadStagingTexture;

  static inline bool m_bHdrIn;
  static inline bool m_bHdrOut;
  pl_color_space m_colorSpace;
  pl_color_space hint = {};
  uint64_t m_signatureCounter=1;
  int lastBufferIndex=-1;
  pl_frame prevFrame {0};

  pl_chroma_location m_chromaLocation;

  //For debug info
  pl_color_system m_videoMatrix;
  pl_color_transfer m_displayTransfer;
  pl_color_primaries m_displayPrimaries;
  int m_FrameMixerNumFrames = 0;
  int m_FrameMixerRenderErrors = 0;
  int m_FrameMixerQueueMore = 0;
  int m_FrameMixerQueueErr = 0;
  double m_ScreenFps = 0.0;
  int m_lastFrameIdx;
  int m_lastFieldIndex;
  bool m_bLastNvSuperResolution;
  bool m_bLastNvRtxHdr;
  //DXGI_FORMAT m_TempTargetDxgiFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;  //couldn't figure that one out yet, enabling super resolution result in over saturation, changing min_luma to 5 brings us closer but it is not a solution.
  //DXGI_FORMAT m_TempTargetDxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  DXGI_FORMAT m_TempTargetDxgiFormat = DXGI_FORMAT_R10G10B10A2_UNORM;

  int m_FrameMixerQueueResets = 0;


  // Tracking for parameter changes
  AVColorSpace m_lastColorSpace = AVCOL_SPC_UNSPECIFIED;

  //maybe remove those 2
  AVColorTransferCharacteristic m_lastColorTransfer = AVCOL_TRC_UNSPECIFIED;
  AVColorPrimaries m_lastPrimaries = AVCOL_PRI_UNSPECIFIED;

  AVPixelFormat m_format;
  static constexpr int QUERY_LATENCY = 12;
  struct FrameQuery
  {
	ID3D11Query* disjoint = nullptr;
	ID3D11Query* start = nullptr;
	ID3D11Query* end = nullptr;
	bool is_active = false;
  };
  std::vector<FrameQuery> m_queryRing = std::vector<FrameQuery> (QUERY_LATENCY);
  int m_currentWriteSlot = 0;
  int m_presentWriteSlot = 0;
  void InitProfiling();
  void ReleaseProfilingQueries();
	//ID3D11Texture2D* m_pOutputHdrTexture = nullptr;

  CRTXVideoProcessor m_RtxVideoProcessor;
  bool m_bUseNvRtxHdr = false;
  bool m_bUseNvSuperResolution = false;
  bool m_bPendingRtxToggleStateChange = false;
  std::optional<bool> m_bPreviousUseNvSuperResolution;
};

class CRendererPL::CRenderBufferImpl : public CRenderBuffer
{
public:
  void ReleasePicture() override;

  explicit CRenderBufferImpl(AVPixelFormat av_pix_format, unsigned width, unsigned height);
  ~CRenderBufferImpl();


  void AppendPicture(const VideoPicture& picture) override;
  bool GetLibplaceboFrame(pl_frame& frame);
  bool map_frame(pl_gpu gpu, pl_tex* tex, struct pl_source_frame* src, struct pl_frame* out_frame);
  double getPts() { return pts; }
  bool HasHdrData();
  pl_color_space doviColorSpace = {}; //< pl_color_space
  pl_color_space m_ColorSpace = {}; //< pl_color_space
  pl_color_repr doviColorRepr = {};
  pl_dovi_metadata doviPlMetadata = {};
  pl_hdr_metadata hdrDoviRpu; //< pl_hdr_metadata
  bool hasHDR10PlusMetadata = false;
  bool hasDoviMetadata = false;
  bool hasDoviRpuMetadata = false;
  AVDOVIColorMetadata doviColor { 0 };
  AVDOVIDmData doviExt{ 0 };
  bool hasDoviExt = false;
  bool m_NeedFrame = false;
  pl_chroma_location m_chromaLocation = PL_CHROMA_UNKNOWN;

  // For debugInfo
  DXGI_OUTPUT_DESC1 m_OutputDesc1 = {};
  pl_hdr_metadata m_PeakDetectMetadata = {};
  bool m_bHasPeakDetectMetadata = false;     //cl debug info move to CRendererPL?
  float m_RenderDuration = 0.0;
  float m_RenderDurationGpu = 0.0;
  pl_color_space m_FrameInColor = {};
  pl_color_space m_FrameOutColor = {};

  uint64_t m_signature = 0;
  AVDynamicHDRPlus hdrMetadata = {};
  bool disable_residual_flag = true;
  AVDOVIMetadata doviMetadata = {};
  PL::pl_d3d_format plFormat = {};
  //planes are used to create the frame
  pl_plane plplanes[3] = {};

  unsigned int m_pictureWidth = 0;
  unsigned int m_pictureHeight = 0;


  // they are only kept for plane reference
// we could put them to null according to libplacebo doc but it crash right away
  pl_tex pltex [3] = {};
  /* data info for dxva planes formating and bit encoding fill with plhelper
  *  this include the bit format for the color conversion
  * */

  //sw upload
  bool UploadPlanes();
  bool UploadToTexture(CD3DTexture& texture);

	//When decoded with d3d11va
  bool UploadWrapPlanes();
  //move those to the video codec if linux start to use libplacebo
private:
};

