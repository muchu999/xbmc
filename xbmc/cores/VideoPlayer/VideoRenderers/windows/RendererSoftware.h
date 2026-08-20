/*
 *  Copyright (C) 2017-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */
#pragma once

#include "RendererBase.h"

#include <map>
extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

class CRendererSoftware : public CRendererBase
{
  class CRenderBufferImpl;
public:
  ~CRendererSoftware();

  bool UploadBuffer(CRenderBuffer* buffer) override;
  bool Configure(const VideoPicture& picture, float fps, unsigned orientation) override;
  bool Supports(ESCALINGMETHOD method) const override;

  static CRendererBase* Create(CVideoSettings& videoSettings);
  static void GetWeight(std::map<RenderMethod, int>& weights, const VideoPicture& picture);

protected:
  explicit CRendererSoftware(CVideoSettings& videoSettings);
  CRenderBuffer* CreateBuffer() override;
  void RenderImpl(CD3DTexture& target, CRect& sourceRect, CPoint(&destPoints)[4], uint32_t flags, double renderPts = 0.0) override;
  void FinalOutput(CD3DTexture& source, CD3DTexture& target, const CRect& src, const CPoint(&destPoints)[4]) override;

private:
  SwsContext* m_sw_scale_ctx = nullptr;
  SwsFilter* m_srcFilter = nullptr;
  bool m_restoreMultithreadProtectedOff = false;
};

class CRendererSoftware::CRenderBufferImpl : public CRenderBuffer
{
public:
  explicit CRenderBufferImpl(AVPixelFormat av_pix_format, unsigned width, unsigned height);
  ~CRenderBufferImpl();

  void AppendPicture(const VideoPicture& picture, CVideoSettings* pVideoSettings = nullptr) override;
  bool GetDataPlanes(uint8_t*(&planes)[3], int(&strides)[3]) override;

  void ReleasePicture() override;
  D3D11_MAPPED_SUBRESOURCE* GetMappedResourcePtr() { return &m_msr; }


private:
  D3D11_MAPPED_SUBRESOURCE m_msr{};
};
