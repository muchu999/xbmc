/*
 *  Copyright (C) 2025 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RendererPL.h"

#include <VideoRenderers/HwDecRender/DXVAHD.h>
#include <VideoRenderers/HwDecRender/DXVAEnumeratorHD.h>
#include "DVDCodecs/Video/DVDVideoCodec.h"
#include "rendering/dx/RenderContext.h"
#include "utils/TimeUtils.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"

#include "RendererBase.h"
#include <ServiceBroker.h>
#include <VideoRenderers/DebugInfo.h>
#include <VideoRenderers/LibPlacebo/PlHelper.h>
#include <VideoRenderers/RenderInfo.h>
#include <VideoRenderers/VideoShaders/WinVideoFilter.h>
#include <cmath>
#include <commons/ilog.h>
#include <cores/VideoSettings.h>
#include <dxgicommon.h>
#include <dxgiformat.h>
#include <libavutil/pixfmt.h>
#include <libplacebo/cache.h>
#include <libplacebo/colorspace.h>
#include <libplacebo/common.h>
#include <libplacebo/gpu.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/shaders/deinterlacing.h>
#include <libplacebo/swapchain.h>
#include <libplacebo/tone_mapping.h>
#include <libplacebo/utils/frame_queue.h>
#include <libplacebo/utils/libav.h>
#include <memory>
#include <rendering/dx/DeviceResources.h>
#include <rendering/dx/DirectXHelper.h>
#include <utils/Geometry.h>
#include <utils/StringUtils.h>
#include <vector>
#include <utility>
#include <d3dcompiler.h>

using namespace XFILE;
using namespace Microsoft::WRL;

CRendererPL::~CRendererPL()
{
  if(*PL::PLInstance::Get()->GetQueue()) 
  {
	pl_queue_destroy(PL::PLInstance::Get()->GetQueue()); //cl need to unmap the buffers before flushing them
	*PL::PLInstance::Get()->GetQueue() = NULL;
  }
  Flush(false); // Must free buffers before resetting libplacebo
  PL::PLInstance::Get()->Reset(); //cl reset the rest

  //cl Force restore default color space on exit, non-hdr content messes up hdr color space
  DX::DeviceResources::Get()->SetHdrColorSpace();  // Windowing class instead?

  ReleaseProfilingQueries();
}


CPLHelper::CMonitor renderTimeMonitor(120);
CPLHelper::CMonitor renderTimeMonitorGpu(120);

void CRendererPL::UpdateVideoFilters()
{
  if (!m_outputShader)
  {
	m_outputShader = std::make_shared<COutputShader>();
	//if (!m_outputShader->Create(m_cmsOn, m_useDithering, m_ditherDepth, m_toneMapping, m_toneMapMethod, m_useHLGtoPQ))
	if (!m_outputShader->Create(false, false, m_ditherDepth, false, VS_TONEMAPMETHOD_OFF, false))
	{
	  CLog::LogF(LOGERROR, "unable to create output shader.");
	  m_outputShader.reset();
	}
	else
	{
	  m_outputShader->SetFinalShader(true);
	  if (m_pLUTView && m_lutSize)
		m_outputShader->SetLUT(m_lutSize, m_pLUTView.Get());
	}
  }
}

bool CRendererPL::NeedBuffer(int idx)
{
  //return false;
  CRenderBuffer* buf = m_renderBuffers [idx];
  if(!buf)
	return false;
  CRenderBufferImpl* buffer = static_cast<CRenderBufferImpl*>(buf);

  return buffer->m_NeedFrame;
}


void CRendererPL::AddVideoPicture(const VideoPicture& picture, int index)
{
  if (m_renderBuffers[index])
  {
	m_renderBuffers[index]->AppendPicture(picture);
	//m_renderBuffers[index]->frameIdx = index;
	m_renderBuffers [index]->frameIdx = m_frameIdx;
	m_frameIdx += 2;
  }
  if(!((m_videoSettings.m_placeboOptions->getPlOptions()->params.frame_mixer == NULL) && m_videoSettings.m_PlaceboFrameMixerBypassQueue))
  {
	CRenderBuffer* rb = m_renderBuffers [index];
	struct pl_source_frame sframe {};
	sframe.pts = rb->pts / 1000000.0;
	sframe.duration = rb->duration / 1000000.0;
	sframe.map = CRendererPL::MapFrame;
	sframe.unmap = CRendererPL::UnmapFrame;
	SRequestContext* pCtx = new SRequestContext {this, rb};
	sframe.frame_data = pCtx;
	sframe.discard = CRendererPL::DiscardFrame;
	if(picture.iFlags & DVP_FLAG_INTERLACED)
	  if(picture.iFlags & DVP_FLAG_TOP_FIELD_FIRST)
		sframe.first_field = PL_FIELD_TOP;
	  else
		sframe.first_field = PL_FIELD_BOTTOM;
	else
	  sframe.first_field = PL_FIELD_NONE;

	CLog::LogFC(LOGDEBUG, LOGPLACEBO, "pl_queue_push idx: {} pts: {}", index, rb->pts);
	pl_queue_push(*PL::PLInstance::Get()->GetQueue(), &sframe);
  }
}
void CRendererPL::DiscardFrame(const struct pl_source_frame* src)
{
  SRequestContext* pCtx = static_cast<SRequestContext*>(src->frame_data);
  CRenderBuffer* rb = pCtx->buffer;
  CRenderBufferImpl* plbuffer = static_cast<CRenderBufferImpl*>(rb);

  delete(src->frame_data);
  plbuffer->m_NeedFrame = false; //cl Shouldn't be needed but it works better, maybe just the presense of the function itself
  CLog::LogFC(LOGDEBUG, LOGPLACEBO, "DiscardFrame idx: {} pts: {:.0f}", plbuffer->frameIdx, plbuffer->pts);
}

bool CRendererPL::MapFrame(pl_gpu gpu, pl_tex* tex, const struct pl_source_frame* src, struct pl_frame* frameIn)
{
  SRequestContext* pCtx = static_cast<SRequestContext*>(src->frame_data);
  CRenderBuffer* rb = pCtx->buffer;
  CRenderBufferImpl* plbuffer = static_cast<CRenderBufferImpl*>(rb);
  if(!plbuffer->IsLoaded())
  {
	if (!pCtx->renderer->UploadBuffer(plbuffer))
	{
	  CLog::LogF(LOGERROR, "Failed to upload buffer to GPU");
	  return false;
	}
  }
 
  InitializeFrameInFieldsMix(frameIn, static_cast<CRendererPL::CRenderBufferImpl*>(rb));
  frameIn->user_data = plbuffer;
  plbuffer->m_NeedFrame = true;
  CLog::LogFC(LOGDEBUG, LOGPLACEBO, "MapFrame idx: {} pts: {}", plbuffer->frameIdx, plbuffer->pts);
  return true;
}

void CRendererPL::UnmapFrame(pl_gpu gpu, struct pl_frame* frame, const struct pl_source_frame* src)
{
  SRequestContext* pCtx = static_cast<SRequestContext*>(src->frame_data);
  CRenderBuffer* rb = pCtx->buffer;
  CRenderBufferImpl* plbuffer = static_cast<CRenderBufferImpl*>(rb);

  delete(src->frame_data);
  plbuffer->m_NeedFrame = false;
  CLog::LogFC(LOGDEBUG, LOGPLACEBO, "UnmapFrame idx: {} pts: {:.0f}", plbuffer->frameIdx, plbuffer->pts);
}

CRendererBase* CRendererPL::Create(CVideoSettings& videoSettings)
{
  return new CRendererPL(videoSettings);
}

void CRendererPL::GetWeight(std::map<RenderMethod, int>& weights, const VideoPicture& picture)
{
  unsigned weight = 0;
  const AVPixelFormat av_pixel_format = picture.videoBuffer->GetFormat();

  // Support common YUV formats for libplacebo
  if (av_pixel_format == AV_PIX_FMT_YUV420P ||
	av_pixel_format == AV_PIX_FMT_YUV420P10 ||
	av_pixel_format == AV_PIX_FMT_YUV420P16 ||
	av_pixel_format == AV_PIX_FMT_NV12 ||
	av_pixel_format == AV_PIX_FMT_P010 ||
	av_pixel_format == AV_PIX_FMT_P016)
  {
	weight += 800; // High priority for libplacebo
  }
  else if (av_pixel_format == AV_PIX_FMT_D3D11VA_VLD)
  {
	weight += 700; // Still support hardware decoded content
  }

  if (weight > 0)
	weights[RENDER_LIBPLACEBO] = weight;
}

CRendererPL::CRendererPL(CVideoSettings& videoSettings) : CRendererBase(videoSettings)
{
  m_renderMethodName = "LibPlacebo";
  m_colorSpace = {};
  m_chromaLocation = PL_CHROMA_UNKNOWN;
}

CRenderInfo CRendererPL::GetRenderInfo()
{
  auto info = __super::GetRenderInfo();

  info.m_deintMethods.push_back(VS_INTERLACEMETHOD_AUTO);

  return  info;
}


bool CRTXVideoProcessor::ConfigureColorSpaces(CRenderBuffer* pBuffer, ID3D11VideoProcessor* pProcessor, bool bUseNvRtxHdr, DXGI_FORMAT TempTargetDxgiFormat)
{
  CRendererPL::CRenderBufferImpl* pBuf = static_cast<CRendererPL::CRenderBufferImpl*>(pBuffer);
  if(!m_pVideoContext)
	return false;

  // Get ID3D11VideoContext1
  Microsoft::WRL::ComPtr<ID3D11VideoContext1> pVideoContext1;
  HRESULT hr = m_pVideoContext.As(&pVideoContext1);
  if(FAILED(hr))
  {
	CLog::LogF(LOGERROR, "Failed to get ID3D11VideoContext1: {}", hr);
	return false;
  }
  
  // Input
  if((pBuf->m_ColorSpace.primaries == PL_COLOR_PRIM_BT_601_625) || (pBuf->m_ColorSpace.primaries == PL_COLOR_PRIM_BT_601_525))
  {
	if(pBuf->full_range)
	  pVideoContext1->VideoProcessorSetStreamColorSpace1(pProcessor, 0, DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601);
	else
	  pVideoContext1->VideoProcessorSetStreamColorSpace1(pProcessor, 0, DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601);
  }
  else
  {
	if(pBuf->full_range)
	  pVideoContext1->VideoProcessorSetStreamColorSpace1(pProcessor, 0, DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709);
	else
	  pVideoContext1->VideoProcessorSetStreamColorSpace1(pProcessor, 0, DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
  }

  // Output
  // To trigger the RTX HDR engine, the output target color space MUST be configured as an HDR container. 
  if(bUseNvRtxHdr)
	if(TempTargetDxgiFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)
	  pVideoContext1->VideoProcessorSetOutputColorSpace1(pProcessor, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709); //DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;  DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
	else if(TempTargetDxgiFormat == DXGI_FORMAT_R10G10B10A2_UNORM)
	  pVideoContext1->VideoProcessorSetOutputColorSpace1(pProcessor, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020); //DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;  DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
	else
	  pVideoContext1->VideoProcessorSetOutputColorSpace1(pProcessor, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020); //DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;  DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
  else
  {
	static DXGI_COLOR_SPACE_TYPE color = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;  //NOt making a difference in many cases, G10 G22...
	pVideoContext1->VideoProcessorSetOutputColorSpace1(pProcessor, color);
  }

  return true;
}

bool CRendererPL::Configure(const VideoPicture& picture, float fps, unsigned orientation)
{

  m_iNumBuffers = 0;
  m_iBufferIndex = 0;

  m_sourceWidth = picture.iWidth;
  m_sourceHeight = picture.iHeight;
  m_fps = fps;
  m_renderOrientation = orientation;

  std::tie(m_useDithering, m_ditherDepth) = DX::Windowing()->GetDitherSettings();

  m_lastHdr10 = {};
  m_HdrType = HDR_TYPE::HDR_INVALID;
  m_useHLGtoPQ = false;
  m_AutoSwitchHDR = DX::Windowing()->IsHDRDisplaySettingEnabled();

  // Auto switch HDR only if supported and "Settings/Player/Use HDR display capabilities" = ON
  if (m_AutoSwitchHDR)
  {
    m_initialHdrEnabled = DX::Windowing()->IsHDROutput();
    CLog::LogF(LOGDEBUG, "Storing Windows HDR state: {}", m_initialHdrEnabled ? "ON" : "OFF");

	const bool streamIsHDR = (picture.color_primaries == AVCOL_PRI_BT2020) &&
	  (picture.color_transfer == AVCOL_TRC_SMPTE2084 ||
		picture.color_transfer == AVCOL_TRC_ARIB_STD_B67);

    if ((streamIsHDR && !DX::Windowing()->IsHDROutput()) || (m_videoSettings.m_PlaceboUseHdrForSdr && !DX::Windowing()->IsHDROutput()))
      DX::Windowing()->ToggleHDR();
  }

  // Init logs and other stuff
  PL::PLInstance::Get()->Init();

  // Set up color space based on picture metadata
  m_colorSpace = pl_color_space{
	.primaries = pl_primaries_from_av(picture.color_primaries),
	.transfer = pl_transfer_from_av(picture.color_transfer),
  };
  m_chromaLocation = pl_chroma_from_av(picture.chroma_position);
  m_format = picture.videoBuffer->GetFormat();

  m_RtxVideoProcessor.EvaluateRtxCapability(picture);
  return true;
}

float Pq2nit(float pq)
{
  float m1 = 2610.0/4096.0 * 0.25;
  float m2 = 2523.0 / 4096.0 * 128.0;
  float c1 = 3424.0 / 4096.0;
  float c2 = 2413.0 / 4096.0 * 32.0;
  float c3 = 2392.0 / 4096.0 * 32.0;

  return(10000.0*pow(std::fmax(0.0,pow(pq,1.0/m2)-c1)/(c2-c3*pow(pq,1.0/m2)), 1.0/m1));

}

DEBUG_INFO_VIDEO CRendererPL::GetDebugInfo(int idx)
{

  CRenderBuffer* rb = m_renderBuffers[idx];
  CRenderBufferImpl* plbuffer = static_cast<CRenderBufferImpl*>(rb);

  DEBUG_INFO_VIDEO info;
  pl_hdr_metadata hdr = plbuffer->m_ColorSpace.hdr;

  info.videoSource = StringUtils::Format("Display: Format: {} Levels: full, ColorMatrix:rgb", DX::DXGIFormatToShortString(m_IntermediateTarget.GetFormat()));

  info.metaPrim = StringUtils::Format("Transfer: {} Primaries: {}", pl_color_transfer_name(m_displayTransfer), pl_color_primaries_name(m_displayPrimaries));


  info.metaLight = StringUtils::Format("Video: Matrix:{} Primaries:{} Transfer:{}", pl_color_primaries_name(m_colorSpace.primaries)
	, pl_color_transfer_name(m_colorSpace.transfer)
	, pl_color_system_name(m_videoMatrix));
  
  info.render1 = StringUtils::Format("Display maxLuma: {:.0f}, maxFALL: {:.0f}",plbuffer->m_OutputDesc1.MaxLuminance, plbuffer->m_OutputDesc1.MaxFullFrameLuminance);
  info.render2 = StringUtils::Format("HDR10PlusMetadata:");
  if(plbuffer->hasHDR10PlusMetadata)
  {
	info.render2 += StringUtils::Format(" light (meta): min luma: {:.4f}, max luma: {:.0f}", hdr.min_luma, hdr.max_luma);
	info.render2 += StringUtils::Format(", max CLL: {}, max FALL: {}", hdr.max_cll, hdr.max_fall);
  }

  info.render3 = StringUtils::Format("FrameIn maxLuma: {:5.0f}, maxPqy: {:5.0f}, avgPqy: {:5.0f}, Out minLuma: {:.4f}, maxLuma: {:5.0f}",
	plbuffer->m_FrameInColor.hdr.max_luma,
	Pq2nit(plbuffer->m_FrameInColor.hdr.max_pq_y),
	Pq2nit(plbuffer->m_FrameInColor.hdr.avg_pq_y),
	plbuffer->m_FrameOutColor.hdr.min_luma,
    plbuffer->m_FrameOutColor.hdr.max_luma);

  info.render4 = StringUtils::Format("PeakDetect:");
  if(plbuffer->m_bHasPeakDetectMetadata)
    info.render4 += StringUtils::Format(" maxPqy: {:5.0f}, avgPqy: {:5.0f}", Pq2nit(plbuffer->m_PeakDetectMetadata.max_pq_y), Pq2nit(plbuffer->m_PeakDetectMetadata.avg_pq_y));

  pl_queue q = *PL::PLInstance::Get()->GetQueue();
  info.render5 = StringUtils::Format("ScreenFps: {:.3f}, LPvps:{:.3f}, SourceFps: {:.3f}({:1}), LPfps: {:.3f}, ", m_ScreenFps, pl_queue_estimate_vps(q), m_fps, plbuffer->pictureFlags & DVP_FLAG_INTERLACED ? "i" : "p", pl_queue_estimate_fps(q));
  info.render6 += StringUtils::Format("Mixer numFrames: {:1}, renderErr: {}, queueMore: {}, queueErr: {}, queueResets: {}",
	m_FrameMixerNumFrames,
	m_FrameMixerRenderErrors,
	m_FrameMixerQueueMore,
	m_FrameMixerQueueErr,
	m_FrameMixerQueueResets);

  double meanv, varv, minv, maxv;
  renderTimeMonitorGpu.calculateAll(meanv, varv, minv, maxv);
  info.render7 = StringUtils::Format("Render time (T) Min/Max: {:0>5.2f} / {:0>5.2f}, mean: {:0>5.2f}, stdDev:{:0>5.2f}", minv * 1000.0, maxv * 1000.0, meanv * 1000.0, std::sqrt(varv) * 1000.0);

  renderTimeMonitor.calculateAll(meanv, varv, minv, maxv);
  info.render8 = StringUtils::Format("Render time (P) Min/Max: {:0>5.2f} / {:0>5.2f}, mean: {:0>5.2f}, stdDev:{:0>5.2f}", minv * 1000.0, maxv * 1000.0, meanv * 1000.0, std::sqrt(varv) * 1000.0);

  const AVPixFmtDescriptor* fmtdesc = av_pix_fmt_desc_get(plbuffer->videoBuffer->GetFormat());

  int srcW = 0;
  int srcH = 0;
  std::string srcName = "";
  if(fmtdesc)
  {
	srcW = plbuffer->GetWidth();
	srcH = plbuffer->GetHeight();
	srcName = fmtdesc->name;
  }
  int softW = 0;
  int softH = 0;
  std::string softName = "";
  if(m_SoftwareUploadTexture.Get() && plbuffer->videoBuffer->GetFormat() != AV_PIX_FMT_D3D11VA_VLD)
  {
	softW = m_SoftwareUploadTexture.GetWidth();
	softH = m_SoftwareUploadTexture.GetHeight();
	softName = DX::DXGIFormatToShortString(m_SoftwareUploadTexture.GetFormat());
  }
  int tempW = 0;
  int tempH = 0;
  std::string tempName = "";
  if(m_TempTarget.Get())
  {
	tempW = m_TempTarget.GetWidth();
	tempH = m_TempTarget.GetHeight();
	tempName = DX::DXGIFormatToShortString(m_TempTarget.GetFormat());
  }

  info.render9 = StringUtils::Format("RTX pipeline:{}, SR:{}, HDR:{}, source: {}x{}({}), swUpload: {}x{}({}), bltOut: {}x{}({})", m_RtxVideoProcessor.IsRtxPipelineEnabled()?"on":"off", m_bUseNvSuperResolution?"on":"off", m_bUseNvRtxHdr?"on":"off", srcW, srcH, srcName, softW, softH, softName, tempW, tempH, tempName);

  info.shader = "-"; 
  if (plbuffer->hasHDR10PlusMetadata)
  {
	info.shader = "Primaries (meta): ";
	info.shader += StringUtils::Format(
	  "R({:.3f} {:.3f}), G({:.3f} {:.3f}), B({:.3f} {:.3f}), WP({:.3f} {:.3f})", hdr.prim.red.x, hdr.prim.red.y,
	  hdr.prim.green.x, hdr.prim.green.y, hdr.prim.blue.x, hdr.prim.blue.y, hdr.prim.white.x, hdr.prim.white.y);
  }
 

  return info;
}

void CRendererPL::CheckVideoParameters()
{
  __super::CheckVideoParameters();

  CRenderBuffer* buf = m_renderBuffers [m_iBufferIndex];
  if(buf)
  {
	// Check if color space parameters have changed
	if(buf->color_space != m_lastColorSpace ||
	  buf->color_transfer != m_lastColorTransfer ||
	  buf->primaries != m_lastPrimaries)
	{
	  m_lastColorSpace = buf->color_space;
	  m_lastColorTransfer = buf->color_transfer;
	  m_lastPrimaries = buf->primaries;
	  m_colorSpace = pl_color_space {
		.primaries = pl_primaries_from_av(buf->primaries),
		.transfer = pl_transfer_from_av(buf->color_transfer),
	  };
	}

  }
  bool bUseUnordered = !m_videoSettings.m_placeboOptions->getPlOptions()->params.skip_target_clearing ? true : false;
  CreateIntermediateTarget(m_viewWidth, m_viewHeight, false, DXGI_FORMAT_UNKNOWN, bUseUnordered); //cl DXGI_FORMAT_R10G10B10A2_UNORM);
}

bool CRendererPL::CreateSoftwareUploadTarget(CRenderBufferImpl* pBuf, unsigned int width, unsigned int height)
{
  unsigned int w = width; // FFALIGN(width, 128);// align to 128 solved garbled output for 1792x1080
  unsigned int h = height;//FFALIGN(height, 128);
  bool bUseUnordered = !m_videoSettings.m_placeboOptions->getPlOptions()->params.skip_target_clearing ? true : false;

  if(m_SoftwareUploadTexture.Get() && m_SoftwareUploadTexture.GetWidth() == w && m_SoftwareUploadTexture.GetHeight() == h)
	return true;
  
  if(m_SoftwareUploadTexture.Get())
	m_SoftwareUploadTexture.Release();

  DXGI_FORMAT format;
  if(pBuf->pictureFlags & DVP_FLAG_INTERLACED)
  {
	// RTX pipeline doesn't deinterlace P010 
	format = DXGI_FORMAT_NV12;
  }
  else
  {
	format = DXGI_FORMAT_P010;
  }

  CD3D11_TEXTURE2D_DESC textureDesc;
  textureDesc.Width = w;
  textureDesc.Height = h;
  textureDesc.Format = format;
  textureDesc.CPUAccessFlags = 0;
  textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  textureDesc.MipLevels = 1;
  textureDesc.MiscFlags = 0;
  textureDesc.ArraySize = 1;
  textureDesc.Usage = D3D11_USAGE_DEFAULT;
  textureDesc.SampleDesc.Count = 1;
  textureDesc.SampleDesc.Quality = 0;
  if(!m_SoftwareUploadTexture.Create(textureDesc))
  {
	CLog::LogF(LOGERROR, "SoftwareUploadTexture creation failed.");
	return false;
  }
  CLog::LogF(LOGDEBUG, "Multi-planar P010 subresource UAV pointers linked successfully.");

  CLog::LogF(LOGDEBUG, "creating SoftwareUploadTexture {}x{} format {}.", w, h, DX::DXGIFormatToString(format));
  return true;
}

bool CRendererPL::CreateTempTarget(unsigned int width, unsigned int height)
{
  unsigned int w = width; //FFALIGN(width, 128);
  unsigned int h = height; //FFALIGN(height, 128);

  // don't create new one if it exists with requested size and format
  if(m_TempTarget.Get() && m_TempTarget.GetFormat() == m_TempTargetDxgiFormat &&
	m_TempTarget.GetWidth() == w && m_TempTarget.GetHeight() == h &&
	m_TempTarget.GetbUseUnordered() == false)
	return true;

  if(m_TempTarget.Get())
	m_TempTarget.Release();
  m_pTempTargetView.Reset();

  CLog::LogF(LOGDEBUG, "creating temp target {}x{} format {}.", w, h,
	DX::DXGIFormatToString(m_TempTargetDxgiFormat));

  if(!m_TempTarget.Create(w, h, 1,	D3D11_USAGE_DEFAULT, m_TempTargetDxgiFormat, nullptr, 0, false))
  {
	CLog::LogF(LOGERROR, "Temp target creation failed.");
	return false;
  }

  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc = {};
  outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
  outputDesc.Texture2D.MipSlice = 0;
  HRESULT hr = m_RtxVideoProcessor.m_pVideoDevice->CreateVideoProcessorOutputView(m_TempTarget.Get(), m_RtxVideoProcessor.m_pVideoEnumerator.Get(), &outputDesc, m_pTempTargetView.GetAddressOf());
  if(FAILED(hr))
  {
	CLog::LogF(LOGERROR, "CreateVideoProcessorOutputView failed");
	return false;
  }

  return true;
}

CRect CRendererPL::ApplyTransforms(const CRect& destRect) const
{
  CRect result;
  CPoint rotated[4];
  ReorderDrawPoints(destRect, rotated);

  switch (m_renderOrientation)
  {
  case 90:
	result = { rotated[3], rotated[1] };
	break;
  case 180:
	result = destRect;
	break;
  case 270:
	result = { rotated[1], rotated[3] };
	break;
  default:
	result = CServiceBroker::GetWinSystem()->GetGfxContext().StereoCorrection(destRect);
	break;
  }

  return result;
}

enum pl_color_primaries MpGetBestPrimContainer(const struct pl_raw_primaries* gamut)
{
  enum pl_color_primaries container = PL_COLOR_PRIM_UNKNOWN;

  if (!pl_primaries_valid(gamut))
	return container;

  const struct pl_raw_primaries* best = NULL;
  for (int i = 1; i < PL_COLOR_PRIM_COUNT; i++)
  {
	pl_color_primaries prim = static_cast<pl_color_primaries>(i);
	const struct pl_raw_primaries* raw = pl_raw_primaries_get(prim);
	if (pl_raw_primaries_similar(raw, gamut)) {
	  container = prim;
	  best = raw;
	  break;
	}

	if (pl_primaries_superset(raw, gamut) &&
	  (!best || pl_primaries_superset(best, raw)))
	{
	  container = prim;
	  best = raw;
	}
  }

  if (!best)
	container = PL_COLOR_PRIM_BT_2020;

  return container;
}

static void ApplyTargetContrast(struct pl_color_space* color, float min_luma, float target_contrast)
{
  // Auto mode, use target value if available
  if (!target_contrast) {
	color->hdr.min_luma = min_luma;
	return;
  }

  // Infinite contrast
  if (target_contrast == -1) {
	color->hdr.min_luma = 1e-7;
	//clmp_assert(color->hdr.min_luma > 0);
	return;
  }

  // Infer max_luma for current pl_color_space
  pl_nominal_luma_params pl{
		.color = color,
		// with HDR10 meta to respect value if already set
		.metadata = PL_HDR_METADATA_HDR10,
		.scaling = PL_HDR_NITS,
		.out_max = &color->hdr.max_luma
  };
  pl_color_space_nominal_luma_ex(&pl);

  color->hdr.min_luma = color->hdr.max_luma / target_contrast;
}


//------------------------------------------
//
//------------------------------------------
struct pl_color_space MpDxgiDesc1ToColorSpace(const DXGI_OUTPUT_DESC1* desc1)
{
  struct pl_color_space ret = { };
  if (!desc1)
	return ret;

  ret.hdr.max_luma = desc1->MaxLuminance;
  ret.hdr.min_luma = desc1->MinLuminance;
  ret.hdr.max_fall = desc1->MaxFullFrameLuminance;
  ret.hdr.prim.blue.x = desc1->BluePrimary[0];
  ret.hdr.prim.blue.y = desc1->BluePrimary[1];
  ret.hdr.prim.green.x = desc1->GreenPrimary[0];
  ret.hdr.prim.green.y = desc1->GreenPrimary[1];
  ret.hdr.prim.red.x = desc1->RedPrimary[0];
  ret.hdr.prim.red.y = desc1->RedPrimary[1];
  ret.hdr.prim.white.x = desc1->WhitePoint[0];
  ret.hdr.prim.white.y = desc1->WhitePoint[1];

  switch (desc1->ColorSpace) {
  case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
	ret.primaries = PL_COLOR_PRIM_BT_709;
	ret.transfer = PL_COLOR_TRC_SRGB;
	break;
  case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
	ret.primaries = PL_COLOR_PRIM_BT_709;
	ret.transfer = PL_COLOR_TRC_SCRGB;
	break;
  case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
	ret.primaries = PL_COLOR_PRIM_BT_2020;
	ret.transfer = PL_COLOR_TRC_PQ;
	break;
  case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:
	ret.primaries = PL_COLOR_PRIM_BT_2020;
	ret.transfer = PL_COLOR_TRC_SRGB;
	break;
  default:
	ret.primaries = PL_COLOR_PRIM_UNKNOWN;
	ret.transfer = PL_COLOR_TRC_UNKNOWN;
	break;
  }

  if (!pl_color_transfer_is_hdr(ret.transfer)) {
	// Don't use reported display peak in SDR mode, setting target peak in
	// SDR mode is very specific usecase, needs proper calibration, users
	// can set it manually.
	ret.hdr.max_luma = 0;
	ret.hdr.max_cll = 0;
	ret.hdr.max_fall = 0;
  }

  return ret;
}

void CRendererPL::ApplyTargetOptions(CVideoSettings& videoSettings, struct pl_frame* source, struct pl_frame* target, float min_luma, bool hint)
{

  //// Colorspace overrides
  //const struct gl_video_opts* opts = p->opts_cache->opts;
  //// If swapchain returned a value use this, override is used in hint
  //if (p->output_levels)
  //  target->repr.levels = p->output_levels;
  //if(opts->target_prim && (!target->color.primaries || !hint))
  //  target->color.primaries = opts->target_prim;
  //if(opts->target_trc && (!target->color.transfer || !hint))
  //  target->color.transfer = opts->target_trc;

  // need to decide here because of possible changes above
  bool bSdrToHdr = videoSettings.m_PlaceboUseHdrForSdr && !pl_color_transfer_is_hdr(source->color.transfer) && m_bHdrOut;
  int target_contrast = bSdrToHdr ? videoSettings.m_PlaceboSdrTargetContrast : videoSettings.m_PlaceboTargetContrast;
  if ((!target->color.hdr.min_luma || !hint))
  {
      ApplyTargetContrast(&target->color, min_luma, target_contrast);
  }
  //if (opts->target_gamut)
  //  mp_parse_raw_primaries(mp_null_log, opts->target_gamut, &target->color.hdr.prim);
  
  int dither_depth = videoSettings.m_PlaceboDitherDepth;
  if (dither_depth == 0)
  {
    dither_depth = getColorDepth();
  }
  if (dither_depth > 0) {
    struct pl_bit_encoding* tbits = &target->repr.bits;
    tbits->color_depth += dither_depth - tbits->sample_depth;
    tbits->sample_depth = dither_depth;
  }
}

#define PL_ALIGN2(x, align) (((x) + (align) - 1) & ~((align) - 1))
#define PL_ALIGN_MEM(size) PL_ALIGN2(size, alignof(max_align_t))
#define PL_PRIV(pub) (const void *) ((uintptr_t) (pub) + PL_ALIGN_MEM(sizeof(*(pub))))

struct d3d_format {
  DXGI_FORMAT dxfmt;
  int minor; // The D3D11 minor version number which supports this format
  struct pl_fmt_t fmt;
};

static inline DXGI_FORMAT fmt_to_dxgi(const pl_fmt fmt)
{
  const struct d3d_format** fmtp = (const d3d_format * *)PL_PRIV(fmt);
  return (*fmtp)->dxfmt;
}

//cl could change, not exposed in libplacebo
#define SW_PFN(name) void *name
struct pl_sw_fns {
  // This destructor follows the same rules as `pl_gpu_fns`
  void (*destroy)(pl_swapchain sw);

  SW_PFN(latency); // optional
  SW_PFN(resize); // optional
  SW_PFN(colorspace_hint); // optional
  SW_PFN(start_frame);
  SW_PFN(submit_frame);
  SW_PFN(swap_buffers);
};
struct d3d11_csp_mapping {
  DXGI_COLOR_SPACE_TYPE d3d11_csp;
  DXGI_FORMAT           d3d11_fmt;
  struct pl_color_space out_csp;
};

struct priv {
  struct pl_sw_fns impl;
  struct pl_d3d11_swapchain_params params;

  struct d3d11_ctx* ctx;
  IDXGISwapChain* swapchain;
  pl_tex backbuffer;

  // Currently requested or applied swap chain configuration.
  // Affected by received colorspace hints.
  struct d3d11_csp_mapping csp_map;
};


int CRendererPL::getColorDepth()
{
  pl_frame frame;
  if(PL::PLInstance::Get()->GetGpu())
  {
    InitializeFrame(PL::PLInstance::Get()->GetSwapchain(), frame);
    return(frame.repr.bits.color_depth);
  }
  else
	return 8;
}

bool CRendererPL::InitializeFrame(pl_swapchain sw, pl_frame &frameOut)
{
  const struct priv* p = (const priv*) PL_PRIV(sw);

  pl_gpu gpu = PL::PLInstance::Get()->GetGpu();

  DXGI_SWAP_CHAIN_DESC desc;
  HRESULT hr = DX::DeviceResources::Get()->GetSwapChain()->GetDesc(&desc);
  DXGI_FORMAT fmt = desc.BufferDesc.Format;

  pl_fmt outFormat = NULL;
  for(int i = 0; i < gpu->num_formats; i++) 
  {
	DXGI_FORMAT target_fmt = fmt_to_dxgi(gpu->formats[i]);

	if(fmt == target_fmt) 
	{
	  outFormat = gpu->formats[i];
	  break;
	}
  }
  if(!outFormat) {
	CLog::LogF(LOGERROR,"CRendererPL::Could not find a suitable pl_fmt ({}) for wrapped resource ", fmt);
	return false;
  }

  
  int bits = 0;
  for(int i = 0; i < outFormat->num_components; i++)
	bits = std::max(bits,outFormat->component_depth[i]);

  int num_comps = outFormat->num_components;
  frameOut = {
	.num_planes = 1,
	.planes = {{
		.texture = NULL,
		.flipped = false,
		.components = num_comps,
		.component_mapping = {0,1,2,3},}},
		.repr = {
	      .sys = PL_COLOR_SYSTEM_RGB,
	      .levels = PL_COLOR_LEVELS_FULL,
	      .alpha = PL_ALPHA_UNKNOWN,
	      .bits = {.sample_depth = bits,.color_depth = bits}},
	    .color = p->csp_map.out_csp,
	    //.crop = {0,0,fbo->params.w,fbo->params.h},
  };
  return true;
}

void CRendererPL::InitializeFrameInFieldsMix(pl_frame* frameIn, CRendererPL::CRenderBufferImpl* buffer)
{
  frameIn->color = buffer->m_ColorSpace;
  frameIn->repr.levels = buffer->full_range ? PL_COLOR_LEVELS_FULL : PL_COLOR_LEVELS_LIMITED;
  frameIn->repr.sys = pl_system_from_av(buffer->color_space);
  frameIn->repr.bits = buffer->plFormat.bits;
  frameIn->repr.alpha = PL_ALPHA_UNKNOWN;

  if(buffer->hasDoviMetadata)
  {
	frameIn->repr.dovi = &buffer->doviPlMetadata;
	frameIn->repr.sys = PL_COLOR_SYSTEM_DOLBYVISION;
  }
  else
  {
	if(frameIn->repr.sys == PL_COLOR_SYSTEM_UNKNOWN)
	  frameIn->repr.sys = PL_COLOR_SYSTEM_BT_709;
  }

  frameIn->num_planes = buffer->plFormat.num_planes;
  frameIn->planes [0] = buffer->plplanes [0];
  frameIn->planes [1] = buffer->plplanes [1];
  frameIn->planes [2] = buffer->plplanes [2];
  pl_frame_set_chroma_location(frameIn, buffer->m_chromaLocation);
}

void CRendererPL::InitializeFrameInFields(pl_frame* frameIn, CRendererPL::CRenderBufferImpl* buffer)
{
  if(m_RtxVideoProcessor.IsRtxPipelineEnabled())
  {
	if(!m_RtxVideoProcessor.IsStreamHdr() && !m_bUseNvRtxHdr)
	{
	  if(m_bUseNvSuperResolution)
	  {
		if(DX::DeviceResources::Get()->IsHDROutput1())
		{
		  if(m_TempTargetDxgiFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)
		  {
			frameIn->color.primaries = PL_COLOR_PRIM_BT_2020;
			frameIn->color.transfer = PL_COLOR_TRC_SCRGB;      //PL_COLOR_TRC_LINEAR for DXGI_FORMAT_R16G16B16A16_FLOAT, PL_COLOR_TRC_SRGB for DXGI_FORMAT_R10G10B10A2_UNORM PL_COLOR_TRC_SCRGB
			frameIn->color.hdr = {};
			frameIn->color.hdr.min_luma = 0.000001;  //cl ?
			frameIn->color.hdr.max_luma = 1000.0;  //cl ?
		  }
		  else if(m_TempTargetDxgiFormat == DXGI_FORMAT_R10G10B10A2_UNORM)
		  {
			// Using super resolution in HDR mode result in a brighter image.
			frameIn->color.primaries = PL_COLOR_PRIM_BT_2020;
			frameIn->color.transfer = PL_COLOR_TRC_PQ; //PL_COLOR_TRC_GAMMA22 ; //PL_COLOR_TRC_SRGB;      //PL_COLOR_TRC_LINEAR for DXGI_FORMAT_R16G16B16A16_FLOAT, PL_COLOR_TRC_SRGB for DXGI_FORMAT_R10G10B10A2_UNORM
			frameIn->color.hdr = {};
			frameIn->color.hdr.min_luma = 0.000001;  //cl ?
			frameIn->color.hdr.max_luma = 1000.0;  //cl ?
		  }
		  else
		  {
			// Using super resolution in HDR mode result in a brighter image.
			frameIn->color.primaries = PL_COLOR_PRIM_BT_709;
			frameIn->color.transfer = PL_COLOR_TRC_GAMMA24; //PL_COLOR_TRC_GAMMA22 ; //PL_COLOR_TRC_SRGB;      //PL_COLOR_TRC_LINEAR for DXGI_FORMAT_R16G16B16A16_FLOAT, PL_COLOR_TRC_SRGB for DXGI_FORMAT_R10G10B10A2_UNORM
			frameIn->color.hdr = {};
			frameIn->color.hdr.min_luma = 0.000001;  //cl ?
			frameIn->color.hdr.max_luma = 1000.0;  //cl ?
		  }
		}
		else
		{
		  frameIn->color.primaries = PL_COLOR_PRIM_BT_709;
		  if(m_TempTargetDxgiFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)
			frameIn->color.transfer = PL_COLOR_TRC_LINEAR;      //PL_COLOR_TRC_LINEAR for DXGI_FORMAT_R16G16B16A16_FLOAT, PL_COLOR_TRC_SRGB for DXGI_FORMAT_R10G10B10A2_UNORM
		  else if(m_TempTargetDxgiFormat == DXGI_FORMAT_R10G10B10A2_UNORM)
			frameIn->color.transfer = PL_COLOR_TRC_GAMMA24; //PL_COLOR_TRC_GAMMA22 ; //PL_COLOR_TRC_SRGB;      //PL_COLOR_TRC_LINEAR for DXGI_FORMAT_R16G16B16A16_FLOAT, PL_COLOR_TRC_SRGB for DXGI_FORMAT_R10G10B10A2_UNORM
		  else
			frameIn->color.transfer = PL_COLOR_TRC_GAMMA24; //PL_COLOR_TRC_GAMMA22 ; //PL_COLOR_TRC_SRGB;      //PL_COLOR_TRC_LINEAR for DXGI_FORMAT_R16G16B16A16_FLOAT, PL_COLOR_TRC_SRGB for DXGI_FORMAT_R10G10B10A2_UNORM
		}
	  }
	  else
	  {
	    frameIn->color.primaries = PL_COLOR_PRIM_BT_709;
	    if(m_TempTargetDxgiFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)
		  frameIn->color.transfer = PL_COLOR_TRC_SCRGB;      //PL_COLOR_TRC_LINEAR for DXGI_FORMAT_R16G16B16A16_FLOAT, PL_COLOR_TRC_SRGB for DXGI_FORMAT_R10G10B10A2_UNORM, PL_COLOR_TRC_SCRGB
		else if(m_TempTargetDxgiFormat == DXGI_FORMAT_R10G10B10A2_UNORM)
		  frameIn->color.transfer = PL_COLOR_TRC_GAMMA24; //PL_COLOR_TRC_GAMMA22 ; //PL_COLOR_TRC_SRGB;      //PL_COLOR_TRC_LINEAR for DXGI_FORMAT_R16G16B16A16_FLOAT, PL_COLOR_TRC_SRGB for DXGI_FORMAT_R10G10B10A2_UNORM
		else
		  frameIn->color.transfer = PL_COLOR_TRC_GAMMA24; //PL_COLOR_TRC_GAMMA22 ; //PL_COLOR_TRC_SRGB;      //PL_COLOR_TRC_LINEAR for DXGI_FORMAT_R16G16B16A16_FLOAT, PL_COLOR_TRC_SRGB for DXGI_FORMAT_R10G10B10A2_UNORM

		frameIn->color.hdr.min_luma = 0.000001;  //cl ?  // Force libplacebo to use min_luma value set on output frame when rendering 
		//frameIn->color.hdr.max_luma = 1000.0;  //cl ?   
	  }
	}
	else
	{
	  frameIn->color.primaries = PL_COLOR_PRIM_BT_2020; //PL_COLOR_PRIM_BT_709;

	  if(m_TempTargetDxgiFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)
		frameIn->color.transfer = PL_COLOR_TRC_LINEAR; //PL_COLOR_TRC_LINEAR; //PL_COLOR_TRC_HLG; // PL_COLOR_TRC_PQ;
	  else if(m_TempTargetDxgiFormat == DXGI_FORMAT_R10G10B10A2_UNORM)
		frameIn->color.transfer = PL_COLOR_TRC_PQ; //PL_COLOR_TRC_LINEAR; //PL_COLOR_TRC_HLG; // PL_COLOR_TRC_PQ;
	  else
		frameIn->color.transfer = PL_COLOR_TRC_PQ; //PL_COLOR_TRC_LINEAR; //PL_COLOR_TRC_HLG; // PL_COLOR_TRC_PQ;

	  frameIn->color.hdr = {};
	  frameIn->color.hdr.min_luma = 0.000001;
	  frameIn->color.hdr.max_luma = 1000.0;
	}

	frameIn->repr.levels = PL_COLOR_LEVELS_FULL;
	frameIn->repr.sys = PL_COLOR_SYSTEM_RGB;
	frameIn->repr.bits = buffer->plFormat.bits;
	frameIn->repr.alpha = PL_ALPHA_UNKNOWN;

	frameIn->num_planes = buffer->plFormat.num_planes;
	frameIn->planes [0] = buffer->plplanes [0];
	frameIn->planes [1] = buffer->plplanes [1];
	frameIn->planes [2] = buffer->plplanes [2];
  }
  else 
  {
	InitializeFrameInFieldsMix(frameIn, buffer);
  }
}

class PlQueueCheck {
private:
  double lastRenderPts = -1;

public:
  PlQueueCheck() = default;

  bool needReset(double duration, double renderPts) //pts in microseconds
  {
	if(lastRenderPts == -1)
	{
	  lastRenderPts = renderPts;
	  return false;
	}

	const double maxJitter = 2000; //cl 
	if((renderPts + maxJitter < lastRenderPts) || ((renderPts - lastRenderPts) > 2.0 * 1000000*duration))  //cl 4.0 * 1000000.0/screenFps)) //cl too small will result in constant reset on some files or after pause/resume and no video... might need better algo...
	{
	  lastRenderPts = renderPts;
	  return true;
	}
	lastRenderPts = renderPts;
	return false;
  }
};
PlQueueCheck queueCheck;

void CRendererPL::ApplyGeometry(CVideoSettings& vs, CRect& sourceRect, CRect& dst, pl_frame& frameIn, pl_frame& frameOut)
{
frameIn.crop.x0 = sourceRect.x1;
frameIn.crop.x1 = sourceRect.x2;
frameIn.crop.y0 = sourceRect.y1;
frameIn.crop.y1 = sourceRect.y2; - vs.m_PlaceboCropBottom; //cl fix and check

frameOut.crop.x0 = dst.x1;
frameOut.crop.x1 = dst.x2;
frameOut.crop.y0 = dst.y1;
frameOut.crop.y1 = dst.y2;

frameOut.rotation = m_renderOrientation == 90 ? PL_ROTATION_90 : m_renderOrientation == 180 ? PL_ROTATION_180 : m_renderOrientation == 270 ? PL_ROTATION_270 : PL_ROTATION_0;
}

// Isolated static helper function with NO C++ object unwinding constraints
static HRESULT SafeGetQueryData(ID3D11DeviceContext* pContext, ID3D11Query* pQuery, void* pData, UINT dataSize)
{
  __try
  {
	return pContext->GetData(pQuery, pData, dataSize, D3D11_ASYNC_GETDATA_DONOTFLUSH);
  }
  __except(GetExceptionCode() == 0x0000087D ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
  {
	return E_FAIL;
  }
}

//---------------------------------------------------
//
//
//---------------------------------------------------
void CRendererPL::RenderDx(CD3DTexture& target, CRect& sourceRect, CPoint(&destPoints) [4], uint32_t flags, bool canSkip)
{
  HRESULT hr;
  ComPtr<ID3D11Texture2D> pTexture = {};
  ComPtr<ID3D11Resource> pResource;
  pl_d3d11 d3d11 = PL::PLInstance::Get()->GetD3d11();
  ID3D11DeviceContext* pDeviceContext = nullptr;
  d3d11->device->GetImmediateContext(&pDeviceContext);

  CRenderBuffer* buf = m_renderBuffers [m_iBufferIndex];
  if(!buf) // || !buf->IsLoaded())
	return;
  CRenderBufferImpl* buffer = static_cast<CRenderBufferImpl*>(buf);

  RECT srcRect = {sourceRect.x1, sourceRect.y1, sourceRect.x2, sourceRect.y2}; //cl 
  RECT destRect = {0, 0, static_cast<LONG>(m_TempTarget.GetWidth()), static_cast<LONG>(m_TempTarget.GetHeight())};

  //cl Don't re-render repeat frames
  D3D11_VIDEO_FRAME_FORMAT fieldFFormat = buf->pictureFlags & DVP_FLAG_INTERLACED ? buf->pictureFlags & DVP_FLAG_TOP_FIELD_FIRST ? D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST : D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST : D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  bool bIsInterlaced = !(fieldFFormat == D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
  int fieldIndex = !bIsInterlaced ? 0 : (flags & RENDER_FLAG_FIELD0) ? 0 : 1;

  if(canSkip && buf->IsLoaded() && (buf->frameIdx == m_lastFrameIdx) && (fieldIndex == m_lastFieldIndex) && (m_bUseNvSuperResolution == m_bLastNvSuperResolution) && (m_bUseNvRtxHdr == m_bLastNvRtxHdr))
  {
	CLog::LogFC(LOGDEBUG, LOGPLACEBO, "Skipping blit for frameIdx: {}, fieldIndex: {}", buf->frameIdx, fieldIndex);

	// Update source rect to the current output image for the next stage
	sourceRect.x1 = destRect.left;
	sourceRect.y1 = destRect.top;
	sourceRect.x2 = destRect.right;
	sourceRect.y2 = destRect.bottom;

	return;
  }
   
  m_lastFrameIdx = buf->frameIdx;
  m_lastFieldIndex = fieldIndex;
  m_bLastNvSuperResolution = m_bUseNvSuperResolution;
  m_bLastNvRtxHdr = m_bUseNvRtxHdr;

  // Get the underlying D3D11 texture index from the DXVA video buffer
  unsigned arrayIdx;
  if(buf->videoBuffer->GetFormat() == AV_PIX_FMT_D3D11VA_VLD)
  {
	hr = buf->GetResource(&pResource, &arrayIdx); //cl get buffer from decoder...
	if(FAILED(hr))
	{
	  CLog::LogF(LOGERROR, "GetResource for arrayIdx failed");
	  return;
	}

	// Query for the ID3D11Texture2D interface from the resource
	hr = pResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**) &pTexture);
	if(FAILED(hr))
	{
	  CLog::LogF(LOGERROR, "Query for the ID3D11Texture2D interface failed");
	}
  }
  else
  {
	pTexture = m_SoftwareUploadTexture.Get();
	arrayIdx = 0;
  }

  if(m_RtxVideoProcessor.IsInitialized() && pTexture)
  {
	Microsoft::WRL::ComPtr<ID3D11Multithread> pMultithread;
	if(SUCCEEDED(pDeviceContext->QueryInterface(IID_PPV_ARGS(&pMultithread))))
	{
	  pMultithread->Enter();
	}

	// Update processor sizes if required
	D3D11_TEXTURE2D_DESC srcDesc, destDesc;
	pTexture->GetDesc(&srcDesc);
	//m_RtxVideoProcessor.EnsureProcessorSize(srcDesc.Width, srcDesc.Height, buffer->pictureFlags, m_viewWidth, m_viewHeight);
	m_RtxVideoProcessor.EnsureProcessorSize(srcRect.right- srcRect.left, srcRect.bottom - srcRect.top, buffer->pictureFlags, destRect.right - destRect.left, destRect.bottom - destRect.top);


	RECT streamDestRect = destRect;
	m_RtxVideoProcessor.m_pVideoContext->VideoProcessorSetStreamSourceRect(m_RtxVideoProcessor.m_pVideoProcessor.Get(), 0, TRUE, &srcRect);
	m_RtxVideoProcessor.m_pVideoContext->VideoProcessorSetStreamDestRect(m_RtxVideoProcessor.m_pVideoProcessor.Get(), 0, TRUE, &streamDestRect);
	m_RtxVideoProcessor.m_pVideoContext->VideoProcessorSetOutputTargetRect(m_RtxVideoProcessor.m_pVideoProcessor.Get(), TRUE, &destRect);
	m_RtxVideoProcessor.m_pVideoContext->VideoProcessorSetStreamRotation(m_RtxVideoProcessor.m_pVideoProcessor.Get(), 0, false, static_cast<D3D11_VIDEO_PROCESSOR_ROTATION>(0 / 90));
	m_RtxVideoProcessor.ConfigureColorSpaces(buf, m_RtxVideoProcessor.m_pVideoProcessor.Get(), m_bUseNvRtxHdr, m_TempTargetDxgiFormat);  //cl placement
	m_RtxVideoProcessor.EnableNvidiaVideoSuperResolution(m_bUseNvSuperResolution);
	m_RtxVideoProcessor.EnableNvidiaRtxVideoHdr(m_bUseNvRtxHdr);

	// Create the video processor input view for the decoder output
	D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = {};
	Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView>  pInputView;
	inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D; // Explicitly map to a 2D Texture
	inputViewDesc.Texture2D.ArraySlice = arrayIdx;           // Target the specific frame index
	inputViewDesc.Texture2D.MipSlice = 0;
	hr = m_RtxVideoProcessor.m_pVideoDevice->CreateVideoProcessorInputView(pTexture.Get(), m_RtxVideoProcessor.m_pVideoEnumerator.Get(), &inputViewDesc, pInputView.GetAddressOf());
	if(FAILED(hr))
	{
	  CLog::LogF(LOGERROR, "CreateVideoProcessorInputView failed");
	  return;
	}

	CLog::LogFC(LOGDEBUG, LOGPLACEBO, "srcRect: {}x{}, destRect: {}x{}", srcRect.right- srcRect.left, srcRect.bottom - srcRect.top, destRect.right - destRect.left, destRect.bottom - destRect.top);
	bool blitSuccess = m_RtxVideoProcessor.ExecuteBlit(pInputView.Get(), buf, m_pTempTargetView.Get(), buffer->pictureFlags, flags, buf->frameIdx);
	if(!blitSuccess)
	{
	  CLog::LogF(LOGERROR, "RTX Engine: ExecuteBlit failed");
	}
	if(pMultithread)
	{
	  pMultithread->Leave();
	}
    //#define BYPASS_LIBPLACEBO 1
    #ifdef BYPASS_LIBPLACEBO
	  return;
    #endif
  }
  // Update source rect to the current output image for the next stage
  sourceRect.x1 = destRect.left;
  sourceRect.y1 = destRect.top;
  sourceRect.x2 = destRect.right;
  sourceRect.y2 = destRect.bottom;

}

//---------------------------------------------------
//
//
//---------------------------------------------------
void CRendererPL::RenderImpl(CD3DTexture& target, CRect& sourceRect, CPoint(&destPoints) [4], uint32_t flags, double renderPts)
{
  CRect dst = ApplyTransforms(CRect(destPoints [0], destPoints [2])); //uses m_renderOrientation
  CRenderBuffer* buf = m_renderBuffers [m_iBufferIndex];
  if(!buf || !buf->IsLoaded())
	return;
  CRenderBufferImpl* buffer = static_cast<CRenderBufferImpl*>(buf);
  bool canSkip = !m_bPendingRtxToggleStateChange;
  if(m_bPendingRtxToggleStateChange)
  {
	// Force a Reset the swapchain color pipeline or it will be incorrect when switching mid-stream
	DX::DeviceResources::Get()->SetHdrColorSpace(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);  //cl Windowing class instead?
	DX::DeviceResources::Get()->SetHdrColorSpace(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);  //cl Windowing class instead?
	DX::DeviceResources::Get()->SetHdrColorSpace();  //cl Windowing class instead?

	// Reset libplacebo shader cache
	pl_renderer renderer = PL::PLInstance::Get()->GetRenderer();
	if(renderer)
	{
	  pl_renderer_flush_cache(renderer);
	}

	m_bPendingRtxToggleStateChange = false;
	CLog::LogF(LOGDEBUG, "RTX Engine: Render thread successfully executed deferred pipeline flush.");
  }
  if(m_RtxVideoProcessor.IsRtxPipelineEnabled())
  {
	RenderDx(target, sourceRect, destPoints, flags, canSkip);
    #ifdef BYPASS_LIBPLACEBO
	  // only works if temp target and intermediate texture have same format...
	  m_RtxVideoProcessor.DebugBypassToDisplay(sourceRect, target.Get(), m_TempTarget.Get(), destPoints);
	  //m_RtxVideoProcessor.DebugBypassToDisplay(sourceRect, target.Get(), m_SoftwareUploadTexture.Get(), destPoints);
	sourceRect = dst; //cl Pass dst to next render stage...
	  return;
    #endif
	Render(target, sourceRect, destPoints, flags, renderPts);

  }
  else
  {
	Render(target, sourceRect, destPoints, flags, renderPts);
  }
}

//---------------------------------------------------
//
//
//---------------------------------------------------
void CRendererPL::Render(CD3DTexture& target, CRect& sourceRect, CPoint(&destPoints)[4], uint32_t flags, double renderPts)
{
  //m_bHdrOut = DX::Windowing()->IsHDROutput(); //cl manually toggling HDR will not update this value...
  m_bHdrOut = DX::DeviceResources::Get()->IsHDROutput1();
  CRect dst = ApplyTransforms(CRect(destPoints [0], destPoints [2])); //uses m_renderOrientation
  pl_frame frameOut{};
  pl_frame frameIn{};
  CVideoSettings videoSettings = m_videoSettings;  //cl take a copy, we might change a few settings and don't want to have to revert
  pl_render_params* params = &videoSettings.m_placeboOptions->getPlOptions()->params;
  CPLHelper::InitializeShaders(PL::PLInstance::Get()->GetGpu());  //cl here for now, race condition with the loading of videoSettings...

  CRenderBuffer* buf = m_renderBuffers[m_iBufferIndex];
  if (!buf || !buf->IsLoaded())
	return;
  CRenderBufferImpl* buffer = static_cast<CRenderBufferImpl*>(buf);

  InitializeFrameInFields(&frameIn, buffer); //cl wastefull, need cleanup
  if((m_videoSettings.m_placeboOptions->getPlOptions()->params.frame_mixer == NULL) && m_videoSettings.m_PlaceboFrameMixerBypassQueue && !m_RtxVideoProcessor.IsRtxPipelineEnabled())
  {
	if(buffer->pictureFlags & DVP_FLAG_INTERLACED)
	{
	  if((flags & RENDER_FLAG_FIELD0) && (flags & RENDER_FLAG_TOP))
	  {
		frameIn.field = PL_FIELD_TOP;
		frameIn.first_field = PL_FIELD_TOP;
	  }
	  else if((flags & RENDER_FLAG_FIELD1) && (flags & RENDER_FLAG_BOT))
	  {
		frameIn.field = PL_FIELD_BOTTOM;
		frameIn.first_field = PL_FIELD_TOP;
	  }
	  else if((flags & RENDER_FLAG_FIELD0) && (flags & RENDER_FLAG_BOT))
	  {
		frameIn.field = PL_FIELD_BOTTOM;
		frameIn.first_field = PL_FIELD_BOTTOM;
	  }
	  else if((flags & RENDER_FLAG_FIELD1) && (flags & RENDER_FLAG_TOP))
	  {
		frameIn.field = PL_FIELD_TOP;
		frameIn.first_field = PL_FIELD_BOTTOM;
	  }
	}
	else
	{
	  frameIn.field = PL_FIELD_NONE;
	  frameIn.first_field = PL_FIELD_NONE;
	}
  }

  // Override if NV driver is already doing deinterlacing
  if(m_RtxVideoProcessor.IsRtxPipelineEnabled())
  {
	frameIn.field = PL_FIELD_NONE;
	frameIn.first_field = PL_FIELD_NONE;
  }

    pl_color_space target_csp{ };
    static DX::DeviceResources::mp_dxgi_factory_ctx ctx = {0}; //cl
  if (DX::DeviceResources::Get()->get_output_desc1_from_ctx(&ctx, &buffer->m_OutputDesc1))
	target_csp = MpDxgiDesc1ToColorSpace(&buffer->m_OutputDesc1);

  if (target_csp.primaries == PL_COLOR_PRIM_UNKNOWN)
	target_csp.primaries = MpGetBestPrimContainer(&target_csp.hdr.prim);
  if (!pl_color_transfer_is_hdr(target_csp.transfer)) {
	// limit min_luma to 1000:1 contrast ratio in SDR mode
	if (target_csp.hdr.min_luma > PL_COLOR_SDR_WHITE / PL_COLOR_SDR_CONTRAST)
	  target_csp.hdr.min_luma = 0;
  }

  // maxFALL in display metadata is in fact MaxFullFrameLuminance. Wayland
  // reports it as maxFALL directly, but this doesn't mean the same thing.
  target_csp.hdr.max_fall = 0;

  SettinglibPlaceboTargetColorspaceHintMode hintMode = (SettinglibPlaceboTargetColorspaceHintMode)videoSettings.m_PlaceboTargetColorspaceHintMode;
  SettinglibPlaceboTargetColorspaceHint hintFunc = (SettinglibPlaceboTargetColorspaceHint)videoSettings.m_PlaceboTargetColorspaceHint;
  hint = { };

  bool target_hint = hintFunc == SettinglibPlaceboTargetColorspaceHint::YES || (hintFunc == SettinglibPlaceboTargetColorspaceHint::AUTO && target_csp.transfer != PL_COLOR_TRC_UNKNOWN);

  bool target_unknown = target_csp.transfer == PL_COLOR_TRC_UNKNOWN;
  if(target_unknown)
  {
	if (false) //cl mpv check if options specify a transfer instead
	  target_csp.transfer = pl_color_space_hdr10.transfer;
	else
	  target_csp.transfer = pl_color_space_hdr10.transfer;
  }

  float peakLuminance = 0.0;
  if(pl_color_transfer_is_hdr(frameIn.color.transfer))
  {
	peakLuminance = videoSettings.m_PlaceboDisplayHdrPeakLuminance;
  }
  else
  {
	peakLuminance = videoSettings.m_PlaceboDisplaySdrPeakLuminance;
  }


  if (target_hint)
  {
	struct pl_color_space* sourceCS = &frameIn.color;
	struct pl_color_space* targetCS = &target_csp;
	hint = *sourceCS;
	// Apply target contrast to the hint, this is important for SDR, because
	// libplacebo defaults to 1000:1 contrast ratio otherwise.
	if (!hint.hdr.min_luma)
	  hint.hdr.min_luma = targetCS->hdr.min_luma;
	if (hintMode == SettinglibPlaceboTargetColorspaceHintMode::TARGET)
	{
	  hint = *targetCS;
	  if (pl_color_transfer_is_hdr(hint.transfer) && !pl_primaries_valid(&hint.hdr.prim))
		pl_color_space_merge(&hint, sourceCS);
	  if(target_unknown && !pl_color_transfer_is_hdr(sourceCS->transfer))
		hint = *sourceCS;
	  if (targetCS->hdr.max_luma)
	  {
		hint.hdr.max_luma = targetCS->hdr.max_luma;
		hint.hdr.min_luma = targetCS->hdr.min_luma;
		hint.hdr.max_cll = targetCS->hdr.max_cll;
		hint.hdr.max_fall = targetCS->hdr.max_fall;
	  }
	}
	if (hintMode == SettinglibPlaceboTargetColorspaceHintMode::SOURCE_DYNAMIC)
	{
	  pl_nominal_luma_params p =
	  {
		.color = &hint,
		.metadata = PL_HDR_METADATA_ANY,
		.scaling = PL_HDR_NITS,
		.out_min = !hint.hdr.min_luma ? &hint.hdr.min_luma : NULL,
		.out_max = &hint.hdr.max_luma,
	  };
	  pl_color_space_nominal_luma_ex(&p);

	  // Set maxCLL to dynamic max luminance. Note that libplacebo uses
	  // max luminace as maxCLL in practice.
	  hint.hdr.max_cll = hint.hdr.max_luma;
	  // Keep maxFALL from static metadata, unless its value is too high.
	  // Could be set to 0, but let's keep it for now.
	  if (hint.hdr.max_fall > hint.hdr.max_cll)
		hint.hdr.max_fall = 0;
	}
	// Infer missing bits now. This is important so that we don't lose
	// information after user option overrides. For example, if the user
	// sets target_trc to PQ, but the hint(source) is SDR, we want to fill
	// in SDR luminance values instead of the default PQ range.
	struct pl_color_space source_csp = *sourceCS;
	pl_color_space_infer_map(&source_csp, &hint);

	bool inverseToneMapping = false;;
	if(pl_color_transfer_is_hdr(sourceCS->transfer))
	{
	  inverseToneMapping = videoSettings.m_PlaceboColorMapInverseToneMapping;
	}
	else
	{
	  if(videoSettings.m_PlaceboUseHdrForSdr)
	    inverseToneMapping = videoSettings.m_PlaceboSdrColorMapInverseToneMapping;

	}

	//cl disable inverse map if no color map, mpv doesn't check that or?
	//bool inverseToneMapping = videoSettings.m_placeboOptions->getPlOptions()->params.color_map_params == NULL ? false : inverseToneMapping;

	// Always prefer target luminance and transfer for inverse tone mapping
	if (pl_color_transfer_is_hdr(targetCS->transfer) && inverseToneMapping)
	{
	  hint.transfer = targetCS->transfer;
	  hint.hdr.max_luma = targetCS->hdr.max_luma;
	  hint.hdr.min_luma = targetCS->hdr.min_luma;
	  hint.hdr.max_cll = targetCS->hdr.max_cll;
	  hint.hdr.max_fall = targetCS->hdr.max_fall;
	}

	//if (opts->target_prim)  //cl 
    //  hint.primaries = opts->target_prim;
    //if (opts->target_gamut)
    //  mp_parse_raw_primaries(mp_null_log, opts->target_gamut, &hint.hdr.prim);
    //if (opts->target_trc)
    //  hint.transfer = opts->target_trc;

	//if(peakLuminance)
	//  if(hint.hdr.max_luma > peakLuminance)
	//	hint.hdr.max_luma = peakLuminance;
	if(pl_color_transfer_is_hdr(hint.transfer) && videoSettings.m_PlaceboDisplayHdrPeakLuminance)
	  hint.hdr.max_luma = videoSettings.m_PlaceboDisplayHdrPeakLuminance;

	if (!pl_color_transfer_is_hdr(frameIn.color.transfer) && videoSettings.m_PlaceboDisplaySdrPeakLuminance)
	{
	  if(videoSettings.m_PlaceboUseHdrForSdr)
	    hint.hdr.max_luma = videoSettings.m_PlaceboDisplaySdrPeakLuminance; 
	  else
		hint.hdr.max_luma = hint.hdr.max_luma; //cl works only up to 203, after that, libplacebo will switch output colorspace to HDR
	}

	// Always set maxCLL, display uses this metadata and we shouldn't let it
	// fallback to default value.
	if (!hint.hdr.max_cll)
	  hint.hdr.max_cll = hint.hdr.max_luma;

	// If tone mapping is required, adjust maxCLL and maxFALL
	if (sourceCS->hdr.max_luma > hint.hdr.max_luma || inverseToneMapping)
	{
	  // Set maxCLL to the target luminance if it's not already lower
	  if (!hint.hdr.max_cll || hint.hdr.max_luma < hint.hdr.max_cll || inverseToneMapping)
		hint.hdr.max_cll = hint.hdr.max_luma;
	  // There's no reliable way to estimate maxFALL here
	  hint.hdr.max_fall = 0;
	}

	if (hint.hdr.max_cll && hint.hdr.max_fall > hint.hdr.max_cll)
	  hint.hdr.max_fall = 0;

	bool bSrdToHdr = videoSettings.m_PlaceboUseHdrForSdr && !pl_color_transfer_is_hdr(frameIn.color.transfer) && m_bHdrOut;
	int targetContrast = bSrdToHdr ? videoSettings.m_PlaceboSdrTargetContrast : videoSettings.m_PlaceboTargetContrast;
    ApplyTargetContrast(&hint, hint.hdr.min_luma, targetContrast);
  }
  else if (!target_hint)
  {
	if (!hint.hdr.min_luma)
	  hint.hdr.min_luma = target_csp.hdr.min_luma;
	//external_params = set_colorspace_hint(p, NULL); //cl not supported on D3D11, so false...
  }

  pl_swapchain_colorspace_hint(PL::PLInstance::Get()->GetSwapchain(), &hint); //cl if hdr max_luma is set higher than 203 (PL_COLOR_SDR_WHITE), the libplacebo swapchain functions will pick an HDR colorspace as default instead of SDR
  if(!CRendererPL::InitializeFrame(PL::PLInstance::Get()->GetSwapchain(), frameOut))
	return;

  // Calculate target

  bool strict_sw_params = target_hint && true; //cl p->next_opts->target_hint_strict, enabled by default in mpv;
  //if(videoSettings.m_PlaceboUseHdrForSdr && !pl_color_transfer_is_hdr(frameIn.color.transfer) && pl_color_transfer_is_hdr(hint.transfer))
    //peakLuminance = 

  ApplyTargetOptions(videoSettings, &frameIn, &frameOut, hint.hdr.min_luma, strict_sw_params);

  if(pl_color_transfer_is_hdr(frameOut.color.transfer) && videoSettings.m_PlaceboDisplayHdrPeakLuminance && (!frameOut.color.hdr.max_luma || !strict_sw_params))
	frameOut.color.hdr.max_luma = videoSettings.m_PlaceboDisplayHdrPeakLuminance;

  if(!pl_color_transfer_is_hdr(frameIn.color.transfer) && videoSettings.m_PlaceboDisplaySdrPeakLuminance && (!frameOut.color.hdr.max_luma || !strict_sw_params))
  {
	if(videoSettings.m_PlaceboUseHdrForSdr)
      frameOut.color.hdr.max_luma = videoSettings.m_PlaceboDisplaySdrPeakLuminance;
	else
	  frameOut.color.hdr.max_luma = frameOut.color.hdr.max_luma; //cl 
  }

  bool clip_gamut = pl_primaries_valid(&frameOut.color.hdr.prim);
  clip_gamut = clip_gamut && frameOut.color.transfer != PL_COLOR_TRC_SCRGB;
  if (clip_gamut) {
	// Ensure resulting gamut still fits inside container
	frameOut.color.hdr.prim = pl_primaries_clip(&frameOut.color.hdr.prim,
	  pl_raw_primaries_get(frameOut.color.primaries));
  }

  //if (frameOut.color.transfer == PL_COLOR_TRC_SRGB && frame->current &&
  //  ((opts->sdr_adjust_gamma == 0 && opts->target_trc == PL_COLOR_TRC_UNKNOWN) ||
  //    opts->sdr_adjust_gamma == -1))
  //{
  //  switch (frame->current->params.color.transfer) {
  //  case PL_COLOR_TRC_BT_1886:
  //  case PL_COLOR_TRC_GAMMA22:
  //  case PL_COLOR_TRC_SRGB:
  //    frameOut.color.transfer = frame->current->params.color.transfer;
  //  }
  //}
  //if (frameOut.color.transfer == PL_COLOR_TRC_SRGB) {
  //  // sRGB reference display is pure 2.2 power function, see IEC 61966-2-1-1999.
  //  if (opts->treat_srgb_as_power22 & 2)
  //    frameOut.color.transfer = PL_COLOR_TRC_GAMMA22;

  bool target_pq = !target_unknown && target_csp.transfer == PL_COLOR_TRC_PQ;
  //if (opts->treat_srgb_as_power22 & 4 && target_pq)
  if (false && target_pq)
	frameOut.color.transfer = PL_COLOR_TRC_SRGB;


  // Apply SDR to HDR specific settings
  pl_options opt = videoSettings.m_placeboOptions->getPlOptions();
  if(videoSettings.m_PlaceboUseHdrForSdr && !pl_color_transfer_is_hdr(frameIn.color.transfer) && pl_color_transfer_is_hdr(frameOut.color.transfer))
  {
	opt->color_adjustment.saturation = pow(10.0, (videoSettings.m_PlaceboSdrSaturation - 50.0) / 40.0);
	opt->color_map_params.inverse_tone_mapping = videoSettings.m_PlaceboSdrColorMapInverseToneMapping;
	opt->color_map_params.gamut_expansion = videoSettings.m_PlaceboSdrColorMapGamutExpansion;
	opt->color_map_params.gamut_mapping = videoSettings.m_PlaceboSdrColorMapGamutMapping == -1 ? NULL : pl_gamut_map_functions [videoSettings.m_PlaceboSdrColorMapGamutMapping];
	opt->color_map_params.tone_mapping_function = videoSettings.m_PlaceboSdrColorMapToneMapping == -1 ? NULL : pl_tone_map_functions [videoSettings.m_PlaceboSdrColorMapToneMapping];
	opt->color_map_params.intent = (pl_rendering_intent) videoSettings.m_PlaceboSdrColorMapIntent;
	opt->color_map_params.tone_constants.exposure = videoSettings.m_PlaceboSdrToneConstantExposure;
	opt->color_map_params.tone_constants.knee_adaptation = videoSettings.m_PlaceboSdrToneConstantKneeAdaptation;
	opt->color_map_params.tone_constants.knee_default = videoSettings.m_PlaceboSdrToneConstantKneeDefault;
	opt->color_map_params.tone_constants.knee_maximum = videoSettings.m_PlaceboSdrToneConstantKneeMaximum;
	opt->color_map_params.tone_constants.knee_minimum = videoSettings.m_PlaceboSdrToneConstantKneeMinimum;
	opt->color_map_params.tone_constants.knee_offset = videoSettings.m_PlaceboSdrToneConstantKneeOffset;
	opt->color_map_params.tone_constants.linear_knee = videoSettings.m_PlaceboSdrToneConstantLinearKnee;
	opt->color_map_params.tone_constants.reinhard_contrast = videoSettings.m_PlaceboSdrToneConstantReinhardContrast;
	opt->color_map_params.tone_constants.slope_offset = videoSettings.m_PlaceboSdrToneConstantSlopeOffset;
	opt->color_map_params.tone_constants.slope_tuning = videoSettings.m_PlaceboSdrToneConstantSlopeTuning;
	opt->color_map_params.tone_constants.spline_contrast = videoSettings.m_PlaceboSdrToneConstantSplineContrast;
	opt->color_map_params.tone_constants.param0 = videoSettings.m_PlaceboSdrToneConstantParam0;
	opt->color_map_params.tone_constants.param1 = videoSettings.m_PlaceboSdrToneConstantParam1;
	opt->color_map_params.tone_constants.param2 = videoSettings.m_PlaceboSdrToneConstantParam2;
	opt->color_map_params.tone_constants.param3 = videoSettings.m_PlaceboSdrToneConstantParam3;
	opt->color_map_params.tone_constants.param4 = videoSettings.m_PlaceboSdrToneConstantParam4;
	opt->color_map_params.tone_constants.param5 = videoSettings.m_PlaceboSdrToneConstantParam5;
	opt->color_map_params.tone_constants.param6 = videoSettings.m_PlaceboSdrToneConstantParam6;
	opt->color_map_params.gamut_constants.colorimetric_gamma = videoSettings.m_PlaceboSdrGamutConstantsColorimetricGamma;
	opt->color_map_params.gamut_constants.perceptual_deadzone = videoSettings.m_PlaceboSdrGamutConstantsPerceptualDeadzone;
	opt->color_map_params.gamut_constants.perceptual_strength = videoSettings.m_PlaceboSdrGamutConstantsPerceptualStrength;
	opt->color_map_params.gamut_constants.softclip_desat = videoSettings.m_PlaceboSdrGamutConstantsSoftclipDesat;
	opt->color_map_params.gamut_constants.softclip_knee = videoSettings.m_PlaceboSdrGamutConstantsSoftclipKnee;
  }

  // Process HDR/SDR brightness and contrast settings
  m_bHdrIn = pl_color_transfer_is_hdr(frameIn.color.transfer);
  if(m_bHdrIn && m_bHdrOut)
  {
	opt->color_adjustment.brightness = CPLHelper::BrightnessKodi2Pl(videoSettings.m_PlaceboBrightnessHdrHdr);
	opt->color_adjustment.contrast = CPLHelper::ContrastKodi2Pl(videoSettings.m_PlaceboContrastHdrHdr);

  } 
  else if(m_bHdrIn && !m_bHdrOut)
  {
	opt->color_adjustment.brightness = CPLHelper::BrightnessKodi2Pl(videoSettings.m_PlaceboBrightnessHdrSdr);
	opt->color_adjustment.contrast = CPLHelper::ContrastKodi2Pl(videoSettings.m_PlaceboContrastHdrSdr);
  } 
  else if(!m_bHdrIn && m_bHdrOut) 
  {
	opt->color_adjustment.brightness = CPLHelper::BrightnessKodi2Pl(videoSettings.m_PlaceboBrightnessSdrHdr);
	opt->color_adjustment.contrast = CPLHelper::ContrastKodi2Pl(videoSettings.m_PlaceboContrastSdrHdr);
  } 
  else 
  {
	opt->color_adjustment.brightness = CPLHelper::BrightnessKodi2Pl(videoSettings.m_PlaceboBrightnessSdrSdr);
	opt->color_adjustment.contrast = CPLHelper::ContrastKodi2Pl(videoSettings.m_PlaceboContrastSdrSdr);
  }

  // RTX pipeline overrides
  if(m_RtxVideoProcessor.IsRtxPipelineEnabled() && m_bUseNvSuperResolution && videoSettings.m_PlaceboNvRtxDisableScalers)
  {
	opt->params.upscaler = nullptr;
	opt->params.downscaler = nullptr;
	opt->params.plane_upscaler = nullptr;
	opt->params.plane_downscaler = nullptr;
  }

  // Target texture
  pl_d3d11_wrap_params d3dparams =
  {
  .tex = target.Get(),
  .array_slice = 0,
  .fmt = target.GetFormat(),
  .w = (int) target.GetWidth(),
  .h = (int) target.GetHeight()
  };

  frameOut.num_planes = 1;
  frameOut.planes [0].texture = pl_d3d11_wrap(PL::PLInstance::Get()->GetGpu(), &d3dparams);
  frameOut.planes [0].components = 4;
  frameOut.planes [0].component_mapping [0] = PL_CHANNEL_R;
  frameOut.planes [0].component_mapping [1] = PL_CHANNEL_G;
  frameOut.planes [0].component_mapping [2] = PL_CHANNEL_B;
  frameOut.planes [0].component_mapping [3] = PL_CHANNEL_A;

  // Geometry
  ApplyGeometry(videoSettings, sourceRect, dst, frameIn, frameOut);

  // Data used for the video debug renderer
  m_displayTransfer = frameOut.color.transfer;
  m_displayPrimaries = frameOut.color.primaries;
  m_videoMatrix = frameIn.repr.sys;
  buffer->m_FrameInColor = frameIn.color;
  buffer->m_FrameOutColor = frameOut.color;
  m_ScreenFps = static_cast<double>(CServiceBroker::GetWinSystem()->GetGfxContext().GetFPS());

  // Shaders
  static std::vector<const pl_hook*> hooks;
  hooks.clear();

  const pl_hook* pGammaHook = PL::PLInstance::Get()->GetGammaShaderHook().get();
  if(pGammaHook)
  {
	if(m_RtxVideoProcessor.IsRtxPipelineEnabled() && m_bHdrOut &&
	  !m_RtxVideoProcessor.IsStreamHdr() && !m_bUseNvRtxHdr && m_bUseNvSuperResolution &&
	  (m_TempTargetDxgiFormat == DXGI_FORMAT_R10G10B10A2_UNORM))
	{
	  //cl Adjust gamma with trial/error value for this particular case or image is too bright out of blit??>
	  //hooks.push_back(pGammaHook);
	}
  }

  if((videoSettings.m_PlaceboShadersHooks.size() > 0) && (videoSettings.m_PlaceboShaderApply))
  {
	for(int i = 0; i < videoSettings.m_PlaceboShadersHooks.size(); ++i)
	{
	  if(videoSettings.m_PlaceboShadersEnabled [i] && videoSettings.m_PlaceboShadersHooks.m_Valid [i])
	  {
		hooks.push_back(videoSettings.m_PlaceboShadersHooks.m_Hooks [i].get());
	  }
	}
  }

  if(hooks.size() > 0)
  {
	params->hooks = hooks.data();
	params->num_hooks = hooks.size();
  }
  else
  {
	params->num_hooks = 0;
  }


  // Init GPU profiling, //cl move?
  static bool bInit = false;
  if(!bInit)
  {
	InitProfiling();
    bInit = true;
  }
  else
  {
	// reset the profiling if queries are invalid, something destroyed the query objects
	FrameQuery& frame = m_queryRing [m_currentWriteSlot]; //cl just choose one ?
	if(!frame.disjoint || !frame.start || !frame.end)
	{
	  ReleaseProfilingQueries();
	  InitProfiling();
	}
  }

  // get GPU profiling stats for OSD
  pl_d3d11 d3d11 = PL::PLInstance::Get()->GetD3d11();
  ID3D11DeviceContext* pDeviceContext = nullptr;
  d3d11->device->GetImmediateContext(&pDeviceContext);
  int read_slot = (m_currentWriteSlot + 1) % QUERY_LATENCY;
  FrameQuery& old_frame = m_queryRing [read_slot];

  if(old_frame.is_active && old_frame.disjoint)
  {
	D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint_data;

	HRESULT hr = SafeGetQueryData(pDeviceContext, old_frame.disjoint, &disjoint_data, sizeof(disjoint_data));

	if(hr == S_OK)
	{ // GPU has finished writing data to this historical frame slot
	  UINT64 start_time = 0;
	  UINT64 end_time = 0;

	  HRESULT hrStart = SafeGetQueryData(pDeviceContext, old_frame.start, &start_time, sizeof(UINT64));
	  HRESULT hrEnd = SafeGetQueryData(pDeviceContext, old_frame.end, &end_time, sizeof(UINT64));

	  if(hrStart == S_OK && hrEnd == S_OK && !disjoint_data.Disjoint && start_time > 0 && end_time > start_time)
	  {
		double freq = static_cast<double>(disjoint_data.Frequency);
		buffer->m_RenderDurationGpu = (static_cast<float>(end_time - start_time) / freq);
		renderTimeMonitorGpu.update(buffer->m_RenderDurationGpu);
	  }
	  old_frame.is_active = false; // Reset slot for reuse
	}
  }
  // Render 
  if((m_videoSettings.m_placeboOptions->getPlOptions()->params.frame_mixer == NULL) && m_videoSettings.m_PlaceboFrameMixerBypassQueue)
  {
	RenderSingle(buffer, renderPts, videoSettings, sourceRect, dst, frameIn, frameOut, pDeviceContext);
  }
  else
  {
	RenderMix(buffer, renderPts, videoSettings, sourceRect, dst, frameOut, pDeviceContext);
  }
  pDeviceContext->Release();
  pl_tex_destroy(PL::PLInstance::Get()->GetGpu(), &frameOut.planes [0].texture);

  sourceRect = dst; //cl Pass dst to next render stage...
  //pDeviceContext->Flush();
}

void CRendererPL::RenderSingle(CRenderBufferImpl* buffer, double renderPts, CVideoSettings& videoSettings, CRect& sourceRect, CRect& dst, pl_frame frameIn, pl_frame frameOut, ID3D11DeviceContext* pDeviceContext)
{
  pl_render_params* params = &videoSettings.m_placeboOptions->getPlOptions()->params;
  static double oldRenderPts = 0.0;
  if(pDeviceContext)
  {
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	int64_t start = CurrentHostCounter();

	Microsoft::WRL::ComPtr<ID3D11Multithread> pMultithread;
	if(SUCCEEDED(pDeviceContext->QueryInterface(IID_PPV_ARGS(&pMultithread))))
	{
	  pMultithread->Enter();
	}

	// Null check protection guard to prevent early startup crashes
	FrameQuery& current_frame = m_queryRing [m_currentWriteSlot];

	//if(!this->m_RtxVideoProcessor.IsRtxPipelineEnabled())
	  //frameIn.color.transfer = PL_COLOR_TRC_GAMMA22;

	if(!current_frame.is_active && current_frame.disjoint && current_frame.start && current_frame.end)
	{
	  pDeviceContext->Begin(current_frame.disjoint);
	  pDeviceContext->End(current_frame.start);
 	  bool res = pl_render_image(PL::PLInstance::Get()->GetRenderer(), &frameIn, &frameOut, params);

	  pDeviceContext->End(current_frame.end);
	  pDeviceContext->End(current_frame.disjoint);
	  current_frame.is_active = true;
	  m_currentWriteSlot = (m_currentWriteSlot + 1) % QUERY_LATENCY;
	}
	else
	{
	  bool res = pl_render_image(PL::PLInstance::Get()->GetRenderer(), &frameIn, &frameOut, params);
	}

	if(pMultithread)
	{
	  pMultithread->Leave();
	}

  int64_t end = CurrentHostCounter();
  buffer->m_RenderDuration = (end - start) / (float) frequency.QuadPart;
  buffer->m_bHasPeakDetectMetadata = pl_renderer_get_hdr_metadata(PL::PLInstance::Get()->GetRenderer(), &buffer->m_PeakDetectMetadata);
  renderTimeMonitor.update(buffer->m_RenderDuration);

  CLog::LogFC(LOGDEBUG, LOGPLACEBO, "ScreenFps: {:.3f}, sourceFps:{:.3f}, renderTime: {:6.3f}, idx: {}, frameIdx: {}, bufferPts: {:.1f}, renderPts: {:.1f}, renderPtsDiff: {:.1f}",
	m_ScreenFps, m_fps, buffer->m_RenderDuration * 1000.0, m_iBufferIndex, buffer->frameIdx, buffer->pts / 1000.0, renderPts / 1000, (renderPts - oldRenderPts) / 1000.0);
  }
  oldRenderPts = renderPts;
}

void CRendererPL::RenderMix(CRenderBufferImpl* buffer, double renderPts, CVideoSettings& videoSettings, CRect& sourceRect, CRect& dst, pl_frame frameOut, ID3D11DeviceContext* pDeviceContext)
{
  if(pDeviceContext)
  {
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	int64_t start = CurrentHostCounter();

	Microsoft::WRL::ComPtr<ID3D11Multithread> pMultithread;
	if(SUCCEEDED(pDeviceContext->QueryInterface(IID_PPV_ARGS(&pMultithread))))
	{
	  pMultithread->Enter();
	}

	FrameQuery& current_frame = m_queryRing [m_currentWriteSlot];

	if(!current_frame.is_active && current_frame.disjoint && current_frame.start && current_frame.end)
	{
	  pDeviceContext->Begin(current_frame.disjoint);
	  pDeviceContext->End(current_frame.start);

	  RenderMixExec(buffer, renderPts, videoSettings, sourceRect, dst, frameOut, pDeviceContext);

	  pDeviceContext->End(current_frame.end);
	  pDeviceContext->End(current_frame.disjoint);
	  current_frame.is_active = true;
	  m_currentWriteSlot = (m_currentWriteSlot + 1) % QUERY_LATENCY;
	}
	else
	{
	  RenderMixExec(buffer, renderPts, videoSettings, sourceRect, dst, frameOut, pDeviceContext);
	}

	if(pMultithread) 
	{
	  pMultithread->Leave();
	}

	// Stop timer
	int64_t end = CurrentHostCounter();
	buffer->m_RenderDuration = (end - start) / (float) frequency.QuadPart;
	buffer->m_bHasPeakDetectMetadata = pl_renderer_get_hdr_metadata(PL::PLInstance::Get()->GetRenderer(), &buffer->m_PeakDetectMetadata);
	renderTimeMonitor.update(buffer->m_RenderDuration);
  }
}

void CRendererPL::RenderMixExec(CRenderBufferImpl* buffer, double renderPts, CVideoSettings& videoSettings, CRect& sourceRect, CRect& dst, pl_frame frameOut, ID3D11DeviceContext* pDeviceContext)
{
  pl_render_params* params = &videoSettings.m_placeboOptions->getPlOptions()->params;
  static double oldRenderPts = 0.0;
  if(queueCheck.needReset(buffer->duration, renderPts))
  {
	CLog::LogFC(LOGDEBUG, LOGPLACEBO, "pl_queue_reset");
	pl_queue_reset(*PL::PLInstance::Get()->GetQueue());
	m_FrameMixerQueueResets++;
  }
  pl_queue* pQueue = PL::PLInstance::Get()->GetQueue();
  if(!pl_queue_num_frames(*pQueue))
  {
	CLog::LogFC(LOGDEBUG, LOGPLACEBO, "pl_queue is empty");
  }
  else
  {
	// Prepare queue update params
	struct pl_frame_mix mix {};
	pl_queue_params qParams {};
	qParams.pts = renderPts / 1000000; // + videoSettings.m_PlaceboTest / 1000.0; // - 0.021; // + 3*buffer->duration / 1000000 ;   
	qParams.radius = pl_frame_mix_radius(params) * videoSettings.m_PlaceboFrameMixerRadiusFactor;
	qParams.vsync_duration = 1.0 / m_ScreenFps; //cl 
	qParams.timeout = 0; //UINT64_MAX;
	//qParams.interpolation_threshold = 0.01;
	//qParams.drift_compensation = true;

	//qParams.pts += videoSettings.m_PlaceboTest / 1000.0;
	// Find min max pts in queue for debug
	pl_source_frame out = {};
	double minPts = std::numeric_limits<double>::max();
	double maxPts = std::numeric_limits<double>::lowest();
	for(int i = 0; i < pl_queue_num_frames(*pQueue); ++i)
	{
	  pl_queue_peek(*pQueue, i, &out);
	  if(!out.pts && !out.duration) // for interleaved material, libplacebo internally inserts fake frames
		continue;
	  if(out.pts < minPts) minPts = out.pts;
	  if(out.pts > maxPts) maxPts = out.pts;
	}
	double renderPtsPos = (maxPts == minPts) ? 0.5 : (renderPts / 1000000.0 - minPts) / (maxPts - minPts);
	double renderPtsShiftedPos = (maxPts == minPts) ? 0.5 : (qParams.pts - minPts) / (maxPts - minPts);

	// Queue update
	pl_queue_status res = pl_queue_update(*pQueue, &mix, &qParams);
	if(res != PL_QUEUE_OK)
	{
	  CLog::LogFC(LOGDEBUG, LOGPLACEBO, "pl_queue_update failed with status {}", res);
	  if(res == PL_QUEUE_MORE)
		++m_FrameMixerQueueMore;
	  else if(res == PL_QUEUE_ERR)
		++m_FrameMixerQueueErr;
	  if(mix.num_frames == 0)
	  {
		// Nothing to present, there will be a timeout on FinalOutput down the road but it will recover. 
		// We could present something anyway but it will not help much. 
	  }
	}

	// Adjust crop parameters
	for(int j = 0; j < mix.num_frames; ++j)
	{
	  pl_frame* frameIn = const_cast<pl_frame*>(mix.frames [j]); //cl do it in mapping but value is passed here...
	  pl_frame frameOut; //dummy
	  ApplyGeometry(videoSettings, sourceRect, dst, *frameIn, frameOut);
	}

	// Render
	m_FrameMixerNumFrames = mix.num_frames;
	bool res2 = pl_render_image_mix(PL::PLInstance::Get()->GetRenderer(), &mix, &frameOut, params);
	if(!res2)
	{
	  CLog::LogFC(LOGDEBUG, LOGPLACEBO, "pl_render_image_mix failed");
	  ++m_FrameMixerRenderErrors;
	}
	CLog::LogFC(LOGDEBUG, LOGPLACEBO, "ScreenFps: {:.3f}, LPvps: {:.3f}, SourceFps: {:.3f}, LPfps: {:.3f}, renderTime: {:6.3f}, renderTimeGpu: {:6.3f}, idx: {} bufferPts: {:.0f}, renderPts: {:.0f}, "
	  "renderPtsDiff: {:.0f}, qParamsPts: {:.0f}, mixNumFrames: {}, radius: {:f}, QPtsOffset: {:f}, minPts: {:.0f}, maxPts: {:.0f}, renderPtsPos: {:.3f}, renderPtsShiftedPos: {:.3f}",
	  m_ScreenFps, pl_queue_estimate_vps(*pQueue), m_fps, pl_queue_estimate_fps(*pQueue), buffer->m_RenderDuration * 1000.0, buffer->m_RenderDurationGpu * 1000.0, m_iBufferIndex, buffer->pts, renderPts,
	  (renderPts - oldRenderPts), qParams.pts * 1000000, mix.num_frames, qParams.radius, pl_queue_pts_offset(*pQueue), minPts * 1000000, maxPts * 1000000, renderPtsPos, renderPtsShiftedPos);
	oldRenderPts = renderPts;
	for(int i = 0; i < mix.num_frames; ++i)
	{
	  CRenderBufferImpl* plbuffer = (CRenderBufferImpl*) (mix.frames [i]->user_data);
	  CLog::LogFC(LOGDEBUG, LOGPLACEBO, "frame {}: {:.3f}", i, plbuffer->getPts() / 1000000.0);
	}
  }
  CLog::LogFC(LOGDEBUG, LOGPLACEBO, "ScreenFps: {:.3f}, sourceFps:{:.3f}, renderTime: {:6.3f}, idx: {} bufferPts: {:.3f}, renderPts: {:.3f}, renderPtsDiff: {:.3f}",
	m_ScreenFps, m_fps, buffer->m_RenderDuration * 1000.0, m_iBufferIndex, buffer->pts / 1000.0, renderPts / 1000, (renderPts - oldRenderPts) / 1000.0);
}


//cl called on every frame before render
void CRendererPL::ProcessHDR(CRenderBuffer* rb)
{
  
  if(  m_AutoSwitchHDR && !DX::Windowing()->IsHDROutput() && (rb->color_transfer == AVCOL_TRC_SMPTE2084 || rb->color_transfer == AVCOL_TRC_ARIB_STD_B67 || m_videoSettings.m_PlaceboUseHdrForSdr) )
  {
	//PL::PLInstance::Get()->DestroySwapchain();
	DX::Windowing()->ToggleHDR(); // Toggle display HDR ON
	//PL::PLInstance::Get()->CreateSwapchain();
  }

  m_HdrType = HDR_TYPE::HDR_HDR10; //cl
  

  //cl  m_HdrType used a bit is base class...to look into
  if (!DX::Windowing()->IsHDROutput())
  {
	if (m_HdrType != HDR_TYPE::HDR_NONE_SDR)
	{
	  m_HdrType = HDR_TYPE::HDR_NONE_SDR;
	  m_lastHdr10 = {};
	}
	return;
  }
}

bool CRendererPL::Supports(ERENDERFEATURE feature) const
{
  //TODO
  if ( (feature == RENDERFEATURE_LIBPLACEBO) ||
	(feature == RENDERFEATURE_GAMMA) ||
	(feature == RENDERFEATURE_BRIGHTNESS) ||
	(feature == RENDERFEATURE_CONTRAST) ||
	(feature == RENDERFEATURE_ROTATION) ||
	(feature == RENDERFEATURE_STRETCH) ||
	(feature == RENDERFEATURE_ZOOM) ||
	(feature == RENDERFEATURE_VERTICAL_SHIFT) ||
	(feature == RENDERFEATURE_PIXEL_RATIO))
  {
	return true;
  }
  else
	return false;

}

bool CRendererPL::Supports(ESCALINGMETHOD method) const
{
  // cl varies depending on upscaler/downscaler
  return false;
  // return __super::Supports(method);
}

void CRendererPL::GetRendererIOFormat(bool& isInputHDR, bool& isOutputHDR)
{
  isInputHDR = m_bHdrIn;
  isOutputHDR = m_bHdrOut;
}


CRenderBuffer* CRendererPL::CreateBuffer()
{
  //can we get the dxva format at this moment?
  return new CRenderBufferImpl(m_format, m_sourceWidth, m_sourceHeight);
}

CRendererPL::CRenderBufferImpl::CRenderBufferImpl(AVPixelFormat av_pix_format, unsigned width, unsigned height)
  : CRenderBuffer(av_pix_format, width, height)
{
  //Is this needed??
  m_widthTex = FFALIGN(width, 32);
  m_heightTex = FFALIGN(height, 32);
  //will be set on first upload
  plFormat.num_planes = -1;

}

CRendererPL::CRenderBufferImpl::~CRenderBufferImpl()
{
  CRenderBufferImpl::ReleasePicture();
}

void CRendererPL::CRenderBufferImpl::ReleasePicture()
{
  CLog::LogFC(LOGDEBUG, LOGPLACEBO, "index:{}, pts:{:.0f}", this->frameIdx, this->pts);
  for (int i = 0; i < plFormat.num_planes; i++)
  {
	pl_tex_destroy(PL::PLInstance::Get()->GetGpu(), &pltex[i]);
  }
  CRenderBuffer::ReleasePicture();
}

void CRendererPL::CRenderBufferImpl::AppendPicture(const VideoPicture& picture)
{
  CLog::LogFC(LOGDEBUG, LOGPLACEBO, "index:{}, pts:{:.0f}", this->frameIdx, picture.pts);
  
  __super::AppendPicture(picture);
  hdrDoviRpu = picture.hdrDoviRpu;
  hdrMetadata = picture.hdrMetadata;
  doviMetadata = picture.doviMetadata;
  doviColorSpace = picture.doviColorSpace;
  doviColorRepr = picture.doviColorRepr;
  doviPlMetadata = picture.doviPlMetadata;
  disable_residual_flag = picture.disable_residual_flag;
  hasDoviMetadata = picture.hasDoviMetadata;
  hasDoviRpuMetadata = picture.hasDoviRpuMetadata;
  hasHDR10PlusMetadata = picture.hasHDR10PlusMetadata;
  doviColor = picture.doviColor;
  doviExt = picture.doviExt;
  hasDoviExt = picture.hasDoviExt;
  m_chromaLocation = pl_chroma_from_av(picture.chroma_position);

  if (videoBuffer->GetFormat() == AV_PIX_FMT_D3D11VA_VLD)
  {
	const auto hw = dynamic_cast<DXVA::CVideoBuffer*>(videoBuffer); //cl hw = height width from the video buffer
	m_widthTex = hw->width;
	m_heightTex = hw->height;

  }

  m_pictureWidth = picture.iWidth;
  m_pictureHeight = picture.iHeight;
  pts = picture.pts;
  duration = picture.iDuration;
  m_ColorSpace = pl_color_space {.primaries = pl_primaries_from_av(primaries), .transfer = pl_transfer_from_av(color_transfer), .hdr={}};

  if(hasHDR10PlusMetadata)
  {
	pl_av_hdr_metadata metadata = {};
	metadata.clm = &lightMetadata;
	metadata.mdm = &displayMetadata;
	metadata.dhp = &hdrMetadata;
	pl_map_hdr_metadata(&m_ColorSpace.hdr, &metadata);
  }
  if(hasDoviMetadata)
  {
	if(disable_residual_flag)
	  m_ColorSpace = doviColorSpace;
	//m_ColorSpace.primaries = PL_COLOR_PRIM_BT_2020; //cl ?
	//m_ColorSpace.transfer = PL_COLOR_TRC_PQ; //cl ?
	//m_ColorSpace.hdr.min_luma = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, doviColor.source_min_pq / 4095.0f); //cl ? 
	//m_ColorSpace.hdr.max_luma = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, doviColor.source_max_pq / 4095.0f); //cl ?

	//if(hasDoviExt) {
	 // m_ColorSpace.hdr.max_pq_y = doviExt.l1.max_pq / 4095.0f;
	  //m_ColorSpace.hdr.avg_pq_y = doviExt.l1.avg_pq / 4095.0f;
  }
}

void CRendererPL::CheckNvRtxStatus(bool& bUseNvRtxHdr, bool& bUseNvSuperResolution)
{
  bUseNvRtxHdr = false;
  bUseNvSuperResolution = false;
  if(m_RtxVideoProcessor.IsVsrViable())
  {
	if(m_videoSettings.m_PlaceboNvSuperResolutionEnabled
	  && m_videoSettings.m_PlaceboFrameMixer == -1
	  && m_videoSettings.m_PlaceboFrameMixerBypassQueue == true)
	{
	  bUseNvSuperResolution = true;
	}
  }
  if(m_RtxVideoProcessor.IsRtxHdrViable())
  {
	if(m_videoSettings.m_PlaceboNvRtxHdrEnabled
	  && DX::DeviceResources::Get()->IsHDROutput1()  //cl 
	  && m_videoSettings.m_PlaceboFrameMixer == -1
	  && m_videoSettings.m_PlaceboFrameMixerBypassQueue == true)
	{
	  bUseNvRtxHdr = true;
	}
  }
}

void CRendererPL::ClearBuffer(CRenderBuffer* buffer)
{
  CRenderBufferImpl* buf = static_cast<CRenderBufferImpl*>(buffer);
  if(buf->IsLoaded())
  {
	if(buf->pltex [0])
	{
	  //for(int i = 0; i < buf->plFormat.num_planes; i++)
	  for(int i = 0; i < 3; i++)
		{
		pl_tex_destroy(PL::PLInstance::Get()->GetGpu(), &buf->pltex [i]);
	  }
	}
	buffer->ResetLoaded ();
  }
}

void CRendererPL::RenderStart(CRenderBuffer* buffer, const CRect& sourceRect, const CRect& destRect)
{
  CRenderBufferImpl* buf = static_cast<CRenderBufferImpl*>(buffer);
  if(!buf || !buf->videoBuffer)
	return;

  bool bUseNvRtxHdr;
  bool bUseNvSuperResolution;
  CheckNvRtxStatus(bUseNvRtxHdr, bUseNvSuperResolution);
  bool bShouldEnable = ((m_videoSettings.m_PlaceboNvRtxPipelineEnabled == (int) SettinglibPlaceboNvRtxPipelineEnabled::YES) || ((m_videoSettings.m_PlaceboNvRtxPipelineEnabled == (int) SettinglibPlaceboNvRtxPipelineEnabled::AUTO) && (bUseNvRtxHdr || bUseNvSuperResolution)))
	                   && (m_videoSettings.m_PlaceboFrameMixer == -1)
					   && m_videoSettings.m_PlaceboFrameMixerBypassQueue;
  if(m_RtxVideoProcessor.IsRtxPipelineViable() && bShouldEnable)
  {
	// Pipeline should be enabled
	if(!m_RtxVideoProcessor.IsInitialized())
	{
	  // Pipeline not initialized
	  if(m_viewWidth && m_viewHeight) //cl on first call, view not set yet
	  {
		if(!m_RtxVideoProcessor.InitializePipeline(buf->m_pictureWidth, buf->m_pictureHeight, buf->pictureFlags, m_viewWidth, m_viewHeight))  // wrong size, will be re-created...
		{
		  CLog::LogF(LOGDEBUG, "Rtx InitializePipeline failed");
		  return;
		}
		
		m_RtxVideoProcessor.EnablePipeline();

		CRendererPL::OnRtxSettingChanged();
		m_RtxVideoProcessor.FlushHistoryQueue();
		ClearBuffer(buffer);

	  }
	}
	else if(!m_RtxVideoProcessor.IsRtxPipelineEnabled())
	{
	  // Pipeline already initialized but not enabled, now impossible after changes, to remove
	  {
		CLog::LogF(LOGDEBUG, "Rtx pipeline wrong state");
	  }
	}
  }
  else if(m_RtxVideoProcessor.IsRtxPipelineEnabled())
  {
	// Pipeline should not be enabled but it is, flush queue and check if we should destroy it
	m_RtxVideoProcessor.FlushHistoryQueue();
	if(m_videoSettings.m_PlaceboNvRtxPipelineEnabled != (int) SettinglibPlaceboNvRtxPipelineEnabled::YES)
	{
	  m_RtxVideoProcessor.DisablePipeline();

	  CRendererPL::OnRtxSettingChanged();
	  ClearBuffer(buffer);

	  m_TempTarget.Release();
	  m_SoftwareUploadTexture.Release();
	  m_RtxVideoProcessor.UninitializePipeline();
	}
  }

  m_bUseNvRtxHdr = false;
  m_bUseNvSuperResolution = false;
  if(m_RtxVideoProcessor.IsRtxPipelineEnabled())
  {
	m_bUseNvRtxHdr = bUseNvRtxHdr;
	m_bUseNvSuperResolution = bUseNvSuperResolution;
	// If super resolution setting changed, flag it to make change at the beginning of frame rendering.
	if(!m_bPreviousUseNvSuperResolution.has_value() || (m_bPreviousUseNvSuperResolution != m_bUseNvSuperResolution))
	{
	  CRendererPL::OnRtxSettingChanged();
	  m_RtxVideoProcessor.FlushHistoryQueue();
	  ClearBuffer(buffer);
	}
	m_bPreviousUseNvSuperResolution = m_bUseNvSuperResolution;

	// Decide temp target format
	if(false) // !m_RtxVideoProcessor.IsStreamHdr() && !m_bUseNvRtxHdr && m_bUseNvSuperResolution && m_bHdrOut)
	{
	  m_TempTargetDxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	}
	else
	{
	  //cl In this case the blit output is too bright, applying a gamma of 0.826 fixes 99.9%
	  m_TempTargetDxgiFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
	}

	// Create temp target
	if(m_bUseNvSuperResolution)
	{
	  if((sourceRect.Width() <= destRect.Width()) && (sourceRect.Height() <= destRect.Height()))
	    CreateTempTarget(destRect.Width(), destRect.Height());
	  else
		CreateTempTarget(sourceRect.Width(), sourceRect.Height());
	}
	else
	{
	  CreateTempTarget(sourceRect.Width(), sourceRect.Height());
	}

	//cl Create software upload target, could first check if we need it...
	CreateSoftwareUploadTarget(buf, buf->m_pictureWidth, buf->m_pictureHeight);
  }
  else
  {
	//cl clean up?
  }
}

bool CRendererPL::UploadBuffer(CRenderBuffer* buffer)
{
  if(!buffer)
	return false;

  CRenderBufferImpl* buf = static_cast<CRenderBufferImpl*>(buffer);
  if(!buf->videoBuffer)
	return false;
  
  if (buf->videoBuffer->GetFormat() == AV_PIX_FMT_D3D11VA_VLD)
  {
	if(m_RtxVideoProcessor.IsRtxPipelineEnabled())
	{
	  // Wrap the RTX pipeline output texture for libplacebo input
	  pl_d3d11_wrap_params params = {};
	  params.tex = m_TempTarget.Get();
	  params.w = m_TempTarget.GetWidth();
	  params.h = m_TempTarget.GetHeight();
	  params.fmt = m_TempTargetDxgiFormat;
	  params.array_slice = 0;

	  buf->pltex [0] = pl_d3d11_wrap(PL::PLInstance::Get()->GetGpu(), &params);
	  if(!buf->pltex [0])
	  {
		CLog::LogF(LOGERROR, "libplacebo failed to wrap the RTX HDR output texture.");
		return false;
	  }

	  buf->plplanes [0].texture = buf->pltex [0];
	  buf->plplanes [0].components = 3; // R, G, B channels
	  buf->plplanes [0].component_mapping [0] = PL_CHANNEL_R;
	  buf->plplanes [0].component_mapping [1] = PL_CHANNEL_G;
	  buf->plplanes [0].component_mapping [2] = PL_CHANNEL_B;
	  buf->plplanes [0].component_mapping [3] = PL_CHANNEL_NONE;

	  //if(buffer->plFormat.num_planes == -1)  //cl optimize
	  {
		PL::PLInstance::Get()->fill_d3d_format(&buf->plFormat, m_TempTargetDxgiFormat);
	  }
	  return buf->SetLoaded();

	}
	else
	{
	  return buf->UploadWrapPlanes();
	}
  }
  else
  {
	// We need to upload the texture to GPU for blit, tried NV12 upload but ran into problems..., simply render with libplacebo for now
	if(m_RtxVideoProcessor.IsRtxPipelineEnabled())
	{
	  ID3D11DeviceContext* pDeviceContext = nullptr;
	  DX::DeviceResources::Get()->GetD3DDevice()->GetImmediateContext(&pDeviceContext);

	  // Init libplacebo render params for "decoded frame to uploaded frame" conversion
	  static bool bInit = false;
	  static pl_options placeboOptions = pl_options_alloc(NULL);  // Initialized to fast by defaults
	  if(!bInit)
	  {
		bInit = true;
		// Registers the cleanup function to run automatically at exit
		std::atexit([]() { pl_options_free(&placeboOptions); });
	  }

	  if(!m_SoftwareUploadTexture.Get())
	  {
		return false;
	  }

	  // Upload planes to GPU
	  buf->UploadPlanes();

	  // Initialize frame IN and OUT for conversion
	  pl_frame frameIn = {0};
	  pl_frame frameOut = {0};
	  pl_render_params params;
	  InitializeFrameInFieldsMix(&frameIn, buf);
	  frameIn.color.primaries = buf->m_ColorSpace.primaries;
	  frameIn.color.transfer = buf->m_ColorSpace.transfer;
	  frameIn.field = PL_FIELD_NONE;
	  frameIn.first_field = PL_FIELD_NONE;

	  //CRendererPL::InitializeFrame(PL::PLInstance::Get()->GetSwapchain(), frameOut);
	  //frameOut.color.primaries = PL_COLOR_PRIM_BT_709;  
	  //frameOut.color.transfer = PL_COLOR_TRC_BT_1886;
	  //frameOut.repr.levels = PL_COLOR_LEVELS_LIMITED;
      //frameOut.repr.bits.color_depth = 10;
	  //frameOut.repr.bits.sample_depth = 16;
	  //frameOut.repr.bits.bit_shift = 6;   // The mandatory P010 6-bit left shift!
	  frameOut.color.primaries = buf->m_ColorSpace.primaries;
	  frameOut.color.transfer = buf->m_ColorSpace.transfer; //PL_COLOR_TRC_GAMMA24; //buf->m_ColorSpace.transfer;
	  frameOut.repr.sys = frameIn.repr.sys;
	  frameOut.repr.levels = frameIn.repr.levels;

	  frameOut.color = frameIn.color;
	  frameOut.repr = frameOut.repr;
	  // Get format of output m_SoftwareUploadTexture
	  D3D11_TEXTURE2D_DESC desc;
	  m_SoftwareUploadTexture.Get()->GetDesc(&desc);
	  PL::pl_d3d_format format;
	  pl_tex tex;
	  PL::PLInstance::Get()->fill_d3d_format(&format, desc.Format);

	  // Wrap the planes of m_SoftwareUploadTexture
	  frameOut.num_planes = format.num_planes;
	  for(int i = 0; i < format.num_planes; i++)
	  {
		pl_d3d11_wrap_params params = {};
		params.tex = m_SoftwareUploadTexture.Get();
		params.w = desc.Width / format.width_div [i];
		params.h = desc.Height / format.height_div [i];
		params.fmt = format.planes [i];
		params.array_slice = 0;
		tex = pl_d3d11_wrap(PL::PLInstance::Get()->GetGpu(), &params);

		if(!tex)
		  return false;

		frameOut.planes [i].texture = tex;
		frameOut.planes [i].components = format.components [i];
		for(int j = 0; j < 4; j++)
		  frameOut.planes [i].component_mapping [j] = format.component_mapping [i][j];
	  }

	  // Crop input to texture size
	  frameIn.crop.x0 = 0;
	  frameIn.crop.x1 = m_SoftwareUploadTexture.GetWidth();
	  frameIn.crop.y0 = 0;
	  frameIn.crop.y1 = m_SoftwareUploadTexture.GetHeight();
	  //frameOut.rotation = PL_ROTATION_0;

	  // Convert image
	  Microsoft::WRL::ComPtr<ID3D11Multithread> pMultithread;
	  if(SUCCEEDED(pDeviceContext->QueryInterface(IID_PPV_ARGS(&pMultithread)))) { pMultithread->Enter();}
	  bool res = pl_render_image(PL::PLInstance::Get()->GetRenderer(), &frameIn, &frameOut, &placeboOptions->params);
	  if(pMultithread) { pMultithread->Leave(); }

	  // Destroy placebo input picture wraps
	  int nPlanes = av_pix_fmt_count_planes(buf->videoBuffer->GetFormat());
	  for(int i = 0; i < nPlanes; i++)
	  {
		pl_tex_destroy(PL::PLInstance::Get()->GetGpu(), &buf->pltex [i]);
	  }

	  // Destroy placebo m_SoftwareUploadTexture wraps, no need for it since frame mixing not supported
	  for(int i = 0; i < format.num_planes; i++)
	  {
		pl_tex_destroy(PL::PLInstance::Get()->GetGpu(), &frameOut.planes [i].texture);
	  }

	  // Wrap the blit output texture
	  pl_d3d11_wrap_params params2 = {};
	  params2.tex = m_TempTarget.Get();
	  params2.w = m_TempTarget.GetWidth();
	  params2.h = m_TempTarget.GetHeight();
	  params2.fmt = m_TempTargetDxgiFormat;
	  params2.array_slice = 0;

	  buf->pltex [0] = pl_d3d11_wrap(PL::PLInstance::Get()->GetGpu(), &params2);
	  if(!buf->pltex [0])
	  {
		CLog::LogF(LOGERROR, "libplacebo failed to wrap the RTX HDR output texture.");
		return false;
	  }
	  buf->plplanes [0].texture = buf->pltex [0];
	  buf->plplanes [0].components = 3; // R, G, B channels
	  buf->plplanes [0].component_mapping [0] = PL_CHANNEL_R;
	  buf->plplanes [0].component_mapping [1] = PL_CHANNEL_G;
	  buf->plplanes [0].component_mapping [2] = PL_CHANNEL_B;
	  buf->plplanes [0].component_mapping [3] = PL_CHANNEL_NONE;

	  // Fill format for new input to libplacebo (m_TempTarget)
	  //if(buffer->plFormat.num_planes == -1)  //cl optimize
	  {
		PL::PLInstance::Get()->fill_d3d_format(&buf->plFormat, m_TempTargetDxgiFormat);
	  }


	  return buf->SetLoaded();
	}
	else
	{
	  return buf->UploadPlanes();
	}
  }
}

bool CRendererPL::CRenderBufferImpl::UploadPlanes()
{
  // source
  uint8_t* src[3];
  int srcStrides[3];

  const AVPixelFormat buffer_format = videoBuffer->GetFormat();

  //AV_PIX_FMT_YUV420P10LE

  int out_map[4];

  videoBuffer->GetPlanes(src);
  videoBuffer->GetStrides(srcStrides);
  pl_plane_data pdata[4] = { };

  for (int n = 0; n < pl_plane_data_from_pixfmt(pdata, &plFormat.bits, buffer_format); n++)
  {
	pdata[n].pixels = src[n];
	pdata[n].row_stride = srcStrides[n];
    pdata[n].width = n > 0 ? m_width >> 1: m_width;
	pdata[n].height = n > 0 ? m_height >> 1 : m_height;

	if (!pl_upload_plane(PL::PLInstance::Get()->GetGpu(), &plplanes[n], &pltex[n], &pdata[n]))
	{
	  CLog::LogF(LOGERROR, "pl_upload_plane failed");

	}
  }
  plFormat.num_planes = av_pix_fmt_count_planes(buffer_format); //cl cleanup? get the right number of plaenes for eventual pl_destroy
  m_bLoaded = true;
  return m_bLoaded;
}

bool CRendererPL::CRenderBufferImpl::UploadWrapPlanes()
{
  ComPtr<ID3D11Resource> pResource;
  ComPtr<ID3D11Texture2D> pTexture;
  D3D11_TEXTURE2D_DESC desc;
  HRESULT hr;
  unsigned arrayIdx;

  if (FAILED(GetResource(&pResource, &arrayIdx)))
  {
	CLog::LogF(LOGERROR, "unable to open d3d11va resource.");
	return false;
  }

  //if (plFormat.num_planes == -1) //cl optimize?
  {
	//fill the plane data information needed for the conversion only once
	const auto dxva_buf = dynamic_cast<DXVA::CVideoBuffer*>(videoBuffer);
	DXGI_FORMAT fmt = dxva_buf->format;
    PL::PLInstance::Get()->fill_d3d_format(&plFormat,fmt);
  }


  hr = pResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pTexture);
  pTexture->GetDesc(&desc);

  // Wrap the plane of the D3D11 texture
  for (int i = 0; i < plFormat.num_planes; i++)
  {
	pl_d3d11_wrap_params params = {};
	params.tex = pTexture.Get();
	params.w = desc.Width / plFormat.width_div[i];
	params.h = desc.Height / plFormat.height_div[i];
	params.fmt = plFormat.planes[i];
	params.array_slice = arrayIdx;
	pltex[i] = pl_d3d11_wrap(PL::PLInstance::Get()->GetGpu(), &params);

	if (!pltex[i])
	  return false;
	plplanes[i].texture = pltex[i];
	//number of components per plane example uv is 2 in d3d the alpha is always a component but not with libplacebo
	plplanes[i].components = plFormat.components[i];
	//mapping yuv planes to rgba channels
  	for (int j = 0; j < 4; j++)
	  plplanes[i].component_mapping[j] = plFormat.component_mapping[i][j];

  }
  m_bLoaded = true;
  return m_bLoaded;
}

bool CRendererPL::CRenderBufferImpl::HasHdrData()
{
  return (hasHDR10PlusMetadata || hasDoviMetadata || hasDoviRpuMetadata);
}

void CRendererPL::ReleaseProfilingQueries()
{
  for(int i = 0; i < QUERY_LATENCY; ++i)
  {
	if(m_queryRing [i].disjoint)
	{
	  m_queryRing [i].disjoint->Release();
	  m_queryRing [i].disjoint = nullptr;
	}
	if(m_queryRing [i].start)
	{
	  m_queryRing [i].start->Release();
	  m_queryRing [i].start = nullptr;
	}
	if(m_queryRing [i].end)
	{
	  m_queryRing [i].end->Release();
	  m_queryRing [i].end = nullptr;
	}
	m_queryRing [i].is_active = false;
  }

  m_currentWriteSlot = 0;
}


void CRendererPL::InitProfiling() {
  pl_d3d11 d3d11 = PL::PLInstance::Get()->GetD3d11();

  D3D11_QUERY_DESC desc_disjoint = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
  D3D11_QUERY_DESC desc_timestamp = {D3D11_QUERY_TIMESTAMP, 0};

  for(int i = 0; i < QUERY_LATENCY; i++) {
	d3d11->device->CreateQuery(&desc_disjoint, &m_queryRing [i].disjoint);
	d3d11->device->CreateQuery(&desc_timestamp, &m_queryRing [i].start);
	d3d11->device->CreateQuery(&desc_timestamp, &m_queryRing [i].end);
	m_queryRing [i].is_active = false;
  }
}
void CRTXVideoProcessor::UninitializePipeline()
{
  m_pVideoProcessor.Reset();
  m_pVideoEnumerator.Reset();
  m_pVideoContext.Reset();
  m_pVideoDevice.Reset();

  m_bInitialized = false;
}

bool CRTXVideoProcessor::EnsureProcessorSize(UINT inW, UINT inH, unsigned int iFlags, UINT outW, UINT outH)
{
  if(m_bInitialized &&
	m_currentInputWidth == inW && m_currentInputHeight == inH &&
	m_currentOutputWidth == outW && m_currentOutputHeight == outH) //cl interleave mode shouldn't change
  {
	return true;
  }

  CLog::LogF(LOGDEBUG, "RTX Processor: Re-creating pipeline. Input: {}x{}, Output: {}x{}", inW, inH, outW, outH);

  // Reset old interfaces to avoid memory leaks
  m_pVideoProcessor.Reset();
  m_pVideoEnumerator.Reset();
  m_pVideoContext.Reset();
  m_pVideoDevice.Reset();
  m_bInitialized = false;

  InitializePipeline(inW, inH, iFlags, outW, outH);

  m_bInitialized = true;
  return true;
}

ProcessorFormats CRTXVideoProcessor::GetProcessorFormats(bool inputFormats, bool outputFormats) const
{
  // Not initialized yet
  if(!m_pVideoEnumerator)
	return {};

  ProcessorFormats formats;
  HRESULT hr {};
  UINT uiFlags {0};
  for(int fmt = DXGI_FORMAT_UNKNOWN; fmt <= DXGI_FORMAT_V408; fmt++)
  {
	const DXGI_FORMAT dxgiFormat = static_cast<DXGI_FORMAT>(fmt);
	if(S_OK == (hr = m_pVideoEnumerator->CheckVideoProcessorFormat(dxgiFormat, &uiFlags)))
	{
	  if((uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) && inputFormats)
		formats.m_input.push_back(dxgiFormat);
	  if((uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) && outputFormats)
		formats.m_output.push_back(dxgiFormat);
	}
	else
	{
	  CLog::LogF(LOGWARNING,
		"Unable to retrieve support of the dxva processor for format {}, error {}",
		DX::DXGIFormatToString(dxgiFormat), CWIN32Util::FormatHRESULT(hr));
	  return formats;
	}
  }
  formats.m_valid = true;

  return formats;
}


bool CRTXVideoProcessor::InitializePipeline(unsigned int width, unsigned int height, unsigned int iFlags, unsigned int m_viewWidth, unsigned int m_viewHeight)
{
  // Get D3D device
  ID3D11Device* pRootDevice = DX::DeviceResources::Get()->GetD3DDevice();
  if(!pRootDevice) 
	return false;

  // Get video Device
  HRESULT hr = pRootDevice->QueryInterface(__uuidof(ID3D11VideoDevice), reinterpret_cast<void**>(m_pVideoDevice.GetAddressOf()));
  if(FAILED(hr)) 
	return false;

  // Get Immediate context, had problem getting it from DX::DeviceResources
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> pImmediateContext;
  pRootDevice->GetImmediateContext(&pImmediateContext);

  // Update internal tracker dimensions
  m_currentInputWidth = width; 
  m_currentInputHeight = height; 
  m_currentOutputWidth = m_viewWidth; 
  m_currentOutputHeight = m_viewHeight;

  // Create the Enumerator
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc = {};
  desc.InputFrameFormat = iFlags & DVP_FLAG_INTERLACED ? iFlags & DVP_FLAG_TOP_FIELD_FIRST ? D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST : D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST : D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  desc.InputWidth = m_currentInputWidth;
  desc.InputHeight = m_currentInputHeight;
  desc.OutputWidth = m_currentOutputWidth;
  desc.OutputHeight = m_currentOutputHeight;
  desc.InputFrameRate.Numerator = 60; //cl  Max expected
  desc.InputFrameRate.Denominator = 1;
  desc.OutputFrameRate.Numerator = 60;
  desc.OutputFrameRate.Denominator = 1;
  desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
  ID3D11VideoProcessorEnumerator* pVideoProcessorEnumerator = nullptr;
  hr = m_pVideoDevice->CreateVideoProcessorEnumerator(&desc, m_pVideoEnumerator.GetAddressOf());
  if(FAILED(hr)) 
	return false;
  
  //GetProcessorFormats(true, true);
  // 
  // Get video context
  hr = pImmediateContext->QueryInterface(__uuidof(ID3D11VideoContext), reinterpret_cast<void**>(m_pVideoContext.GetAddressOf()));
  if(FAILED(hr)) 
	return false;

  // Check how many past and future reference frames the NVIDIA driver needs
  D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS rateCaps = {};
  m_pVideoEnumerator->GetVideoProcessorRateConversionCaps(0, &rateCaps);
  m_numPastFrames = 0;
  m_numFutureFrames = 0;
  if(desc.InputFrameFormat != D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE)
  {
	m_numPastFrames = rateCaps.PastFrames;
	m_numFutureFrames = rateCaps.FutureFrames;
  }

  // Create/configure the Video Processor
  hr = m_pVideoDevice->CreateVideoProcessor(m_pVideoEnumerator.Get(), 0, m_pVideoProcessor.GetAddressOf());
  if(FAILED(hr)) 
	return false;

  m_pVideoContext->VideoProcessorSetStreamAutoProcessingMode(m_pVideoProcessor.Get(), 0, FALSE);
  m_pVideoContext->VideoProcessorSetStreamOutputRate(m_pVideoProcessor.Get(), 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, FALSE, 0);
  ComPtr<ID3D11VideoContext1> videoCtx1;
  if(SUCCEEDED(m_pVideoContext.As(&videoCtx1)))
  {
	videoCtx1->VideoProcessorSetOutputShaderUsage(m_pVideoProcessor.Get(), 1);
  }
  else
  {
	CLog::LogF(LOGWARNING, "unable to retrieve ID3D11VideoContext1 to allow usage of shaders on video processor output surfaces.");
  }
  D3D11_VIDEO_COLOR color;
  color.YCbCr = {0.0625f, 0.5f, 0.5f, 1.0f}; // black color
  m_pVideoContext->VideoProcessorSetOutputBackgroundColor(m_pVideoProcessor.Get(), TRUE, &color);


  m_bInitialized = true;
  return true;
}

void CRTXVideoProcessor::FlushHistoryQueue()
{
  pl_d3d11 d3d11 = PL::PLInstance::Get()->GetD3d11();
  ID3D11DeviceContext* pDeviceContext = nullptr;
  d3d11->device->GetImmediateContext(&pDeviceContext);
  Microsoft::WRL::ComPtr<ID3D11Multithread> pMultithread;
  if(SUCCEEDED(pDeviceContext->QueryInterface(IID_PPV_ARGS(&pMultithread))))
  {
	pMultithread->Enter();
  }

  if(!m_historyQueue.empty())
  {
	while(!m_historyQueue.empty()) 
	{
	  auto& entry = m_historyQueue.front();

	  CRendererPL::CRenderBufferImpl* pBuf = static_cast<CRendererPL::CRenderBufferImpl*>(entry.pBuffer);
	  pBuf->m_NeedFrame = false;
	  m_historyQueue.front().inputView.Reset();
	  m_historyQueue.pop_front();
	}
	CLog::LogF(LOGDEBUG, "RTX Video Processor: Flushed deinterlacing history queue due to seek/skip.");
  }
  if(pMultithread)
  {
	pMultithread->Leave();
  }

}

void CRTXVideoProcessor::DebugBypassToDisplay(CRect& srcRect, ID3D11Texture2D* pOutputWindowTexture, ID3D11Texture2D* pTempTarget, CPoint(&destPoints) [4])
{
  // Get the immediate context
  ID3D11DeviceContext* pGraphicsContext = nullptr;
  DX::DeviceResources::Get()->GetD3DDevice()->GetImmediateContext(&pGraphicsContext);
  
  if(!m_pVideoContext || !pTempTarget) return;

  //cl we should validate that the textures match types and formats before blasting memory...
  D3D11_TEXTURE2D_DESC destDesc;
  pOutputWindowTexture->GetDesc(&destDesc);

  D3D11_BOX sourceBox = {};
  sourceBox.left = 0;
  sourceBox.top = 0;
  sourceBox.right = (srcRect.Width() < destDesc.Width) ? srcRect.Width() : destDesc.Width;
  sourceBox.bottom = (srcRect.Height() < destDesc.Height) ? srcRect.Height() : destDesc.Height;
  sourceBox.front = 0;
  sourceBox.back = 1;
  
  pGraphicsContext->CopySubresourceRegion(pOutputWindowTexture, 0, destPoints[0].x, destPoints [0].y, 0, pTempTarget, 0, &sourceBox);
}

bool CRTXVideoProcessor::ExecuteBlit(ID3D11VideoProcessorInputView* pInputView, CRenderBuffer* pBuffer, ID3D11VideoProcessorOutputView* pOutputView, unsigned int pictFlags, uint32_t flags, uint64_t frameIdx)
{
  if(!m_bInitialized || !m_pVideoContext || !m_pVideoProcessor)
  {
	CLog::LogF(LOGERROR, "Uninitialized pipeline");
	return false;
  }

  if(!pInputView || !pOutputView)
  {
	CLog::LogF(LOGERROR, "RTX Processor: Missing valid input or output views for this frame.");
	return false;
  }

  D3D11_VIDEO_FRAME_FORMAT fieldFFormat = pictFlags & DVP_FLAG_INTERLACED ? pictFlags & DVP_FLAG_TOP_FIELD_FIRST ? D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST : D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST : D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  bool bIsInterlaced = !(fieldFFormat == D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
  int fieldIndex = !bIsInterlaced ? 0 : (flags & RENDER_FLAG_FIELD0) ? 0 : 1;
  unsigned int streamFrameIndex = bIsInterlaced ? frameIdx + fieldIndex : frameIdx / 2; //cl

  // flush queue in case of discontinuous frames
  int maxStepSize = bIsInterlaced ? 4 : 2;
  if(std::abs(int(frameIdx) - int(m_lastFrameIdx)) > maxStepSize)
  {
	FlushHistoryQueue();
  }

  // In case  of interlaced material in paused mode, avoid getting a black screen when toggling RTX features.
  bool bForceRepeat = false;
  if(bIsInterlaced && (frameIdx == m_lastFrameIdx) && (fieldIndex == m_lastFieldIdx))
  {
	bForceRepeat = true;
  }
  m_lastFieldIdx = fieldIndex;
  m_lastFrameIdx = frameIdx;

  // Only update queue for new complete frames
  int totalNeeded = 1 + m_numPastFrames + m_numFutureFrames;
  bool isNewTexturePass = (bIsInterlaced && fieldIndex == 1) ? false : true;
  if(isNewTexturePass || bForceRepeat)
  {
	CRendererPL::CRenderBufferImpl* pBuf = static_cast<CRendererPL::CRenderBufferImpl*>(pBuffer);
	pBuf->m_NeedFrame = true;
	m_historyQueue.push_back(SViewElement{pInputView, pBuffer});

	while(m_historyQueue.size() > totalNeeded)
	{
	  // Evict oldest frame
	  CRendererPL::CRenderBufferImpl* pBuf = static_cast<CRendererPL::CRenderBufferImpl*>(m_historyQueue.front().pBuffer);
	  pBuf->m_NeedFrame = false;
	  m_historyQueue.front().inputView.Reset();
	  m_historyQueue.pop_front();
	}
  }

  int currentSize = m_historyQueue.size();
  if(!currentSize)
  {
	return true;
  }
#if 0
  int actualCurrentFrameIndex = std::min(m_numPastFrames+1, currentSize) - 1; //Keep current frame as the last one received until future frames come in, There will be a couple of repeated frames on screen then...
#else
  int actualCurrentFrameIndex = std::max(m_numPastFrames - (totalNeeded- currentSize), 0); //Keep frame 0 as the last one received untill we can increase, there will be a couple of repeated frames at the start...
#endif
  int actualPastFrames = actualCurrentFrameIndex;
  int actualFutureFrames = currentSize - actualPastFrames - 1;

  // Construct the input arrays for the driver
  std::vector<ID3D11VideoProcessorInputView*> pastSurfaces(actualPastFrames, nullptr);
  std::vector<ID3D11VideoProcessorInputView*> futureSurfaces(actualFutureFrames, nullptr);
  for(UINT i = 0; i < actualPastFrames; i++) {
	pastSurfaces [i] = m_historyQueue [i].inputView.Get();
  }
  for(UINT i = 0; i < actualFutureFrames; i++) {
	futureSurfaces [i] = m_historyQueue [actualCurrentFrameIndex + 1 + i].inputView.Get();
  }
  D3D11_VIDEO_PROCESSOR_STREAM streamData = {};
  streamData.Enable = TRUE;
  streamData.pInputSurface = m_historyQueue [actualCurrentFrameIndex].inputView.Get(); // Current Frame Target
  streamData.PastFrames = actualPastFrames;
  streamData.ppPastSurfaces = pastSurfaces.data();
  streamData.FutureFrames = actualFutureFrames;
  streamData.ppFutureSurfaces = futureSurfaces.data();
  streamData.InputFrameOrField = streamFrameIndex;
  streamData.OutputIndex = fieldIndex;
  m_pVideoContext->VideoProcessorSetStreamFrameFormat(m_pVideoProcessor.Get(), 0, fieldFFormat);
  CLog::LogFC(LOGDEBUG, LOGPLACEBO, " frameIdx: {}, InputFrameOrField: {}, OutputIndex: {}", frameIdx, streamData.InputFrameOrField, streamData.OutputIndex);
  
  // Execute Blit
  HRESULT hr = m_pVideoContext->VideoProcessorBlt(m_pVideoProcessor.Get(), pOutputView, 0, 1, &streamData );

  if(FAILED(hr))
  {
	CLog::LogF(LOGERROR, "RTX Processor: VideoProcessorBlt failed! HRESULT = {}", hr);

	// If the GPU crashed or was removed, we must flag the pipeline to prevent spamming errors
	if(hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
	  CLog::LogF(LOGFATAL, "RTX Processor: Graphics hardware device lost. Disabling RTX processing.");
	  m_bInitialized = false;
	}
  }

  return true;
}

void CRTXVideoProcessor::EvaluateRtxCapability(const VideoPicture& picture)
{
  
  m_bStreamIsHDR = (picture.color_primaries == AVCOL_PRI_BT2020) && (picture.color_transfer == AVCOL_TRC_SMPTE2084 || picture.color_transfer == AVCOL_TRC_ARIB_STD_B67);
  
  //cl Super resolution with e.g. 1080p HDR material has washed out colors, need special correction, disable for now
  m_isVsrViable = DX::DeviceResources::Get()->IsSuperResolutionSupported() && (picture.iWidth < 3840) && (picture.iHeight < 2160) && !m_bStreamIsHDR;
  m_isRtxHdrViable = DX::DeviceResources::Get()->IsRtxVideoHdrSupported() && !m_bStreamIsHDR;
  m_isRtxPipelineViable = (m_isVsrViable || m_isRtxHdrViable);
}

void CRendererPL::OnRtxSettingChanged()
{
  m_bPendingRtxToggleStateChange = true;
  CLog::LogF(LOGDEBUG, "RTX Engine: UI setting toggle flagged for deferred render-thread execution.");
}

constexpr GUID GUID_NVIDIA_PPE_INTERFACE = { 0xd43ce1b3, 0x1f4b, 0x48ac, {0xba, 0xee, 0xc3, 0xc2, 0x53, 0x75, 0xe6, 0xf7}};
constexpr UINT kStreamExtensionVersionV1 = 0x1;
constexpr UINT kStreamExtensionMethodSuperResolution = 0x2;

void CRTXVideoProcessor::EnableNvidiaVideoSuperResolution(bool bEnable)
{
  struct NvidiaStreamExt
  {
	UINT version;
	UINT method;
	UINT enable;
  };

  NvidiaStreamExt ext = {kStreamExtensionVersionV1, kStreamExtensionMethodSuperResolution, bEnable ? 1u : 0u};

  HRESULT hr = m_pVideoContext->VideoProcessorSetStreamExtension(m_pVideoProcessor.Get(), 0, &GUID_NVIDIA_PPE_INTERFACE, sizeof(ext), &ext);
  if(FAILED(hr))
  {
	CLog::LogF(LOGERROR, "Failed to set the NVIDIA video process stream extension with error {}", hr);
	return;
  }


  //CLog::LogF(LOGDEBUG, "RTX Video Super Resolution request enable successfully");
}

// {FDD62BB4-620B-4FD7-9AB3-1E59D0D544B3}
constexpr GUID kNvidiaTrueHDRInterfaceGUID = { 0xfdd62bb4, 0x620b, 0x4fd7, {0x9a, 0xb3, 0x1e, 0x59, 0xd0, 0xd5, 0x44, 0xb3}};
constexpr UINT kStreamExtensionVersionV4 = 0x4;
constexpr UINT kStreamExtensionMethodTrueHDR = 0x3;

// 3. THE EXACT ALIGNED PACKING STRUCT REQUIRED BY THE DRIVER
struct NV_RTX_HDR_STREAM_EXT
{
  UINT version;
  UINT method;
  UINT enable : 1;  // Bit-field optimization!
  UINT reserved : 31;
};

void CRTXVideoProcessor::EnableNvidiaRtxVideoHdr(bool bEnable)
{
  if(!m_pVideoProcessor || !m_pVideoContext) return;

  // Build the Version 4 bit-field data packet structure
  NV_RTX_HDR_STREAM_EXT hdrExt = {};
  hdrExt.version = kStreamExtensionVersionV4;     // Must be 4
  hdrExt.method = kStreamExtensionMethodTrueHDR; // Must be 3
  hdrExt.enable = bEnable ? 1u : 0u;             // Toggle

  // Transmit to the driver context utilizing the unique HDR interface key
  HRESULT hr = m_pVideoContext->VideoProcessorSetStreamExtension(
	m_pVideoProcessor.Get(),
	0, // Stream Index 0
	&kNvidiaTrueHDRInterfaceGUID, // Use the new GUID!
	sizeof(hdrExt),
	&hdrExt
  );

  if(FAILED(hr))
  {
	CLog::LogF(LOGERROR, "RTX Engine: Failed to inject TrueHDR stream extension. Error: {}", hr);
  }
}