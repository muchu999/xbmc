/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/VideoPlayer/Interface/StreamInfo.h"
#include "cores/VideoSettings.h"
#include "settings/dialogs/GUIDialogSettingsManualBase.h"

#include <guilib/GUIMessage.h>
#include <memory>
#include <settings/dialogs/GUIDialogSettingsBase.h>
#include <settings/lib/Setting.h>
#include <string>
#include <vector>


struct IntegerSettingOption;

class CGUIDialogVideoSettings : public CGUIDialogSettingsManualBase
{
public:
  CGUIDialogVideoSettings();
  ~CGUIDialogVideoSettings() override;




protected:
  // implementations of ISettingCallback
  void OnSettingChanged(const std::shared_ptr<const CSetting>& setting) override;
  void OnSettingAction(const std::shared_ptr<const CSetting>& setting) override;
  void InitializeToneMappingMenuHdr(CVideoSettings& videoSettings, const std::shared_ptr<CSettingGroup> group);
  void InitializeToneMappingMenuSdr(CVideoSettings& videoSettings, const std::shared_ptr<CSettingGroup> group);
  void InitializeShaderMenu(CVideoSettings& vs, const std::shared_ptr<CSettingCategory>& category);
  void AddVideoStreams(const std::shared_ptr<CSettingGroup>& group, const std::string& settingId);
  void SaveLibplaceboSettings(const CVideoSettings& vs);
  bool ResetToDefault(CVideoSettings& vs);

  static void VideoStreamsOptionFiller(const std::shared_ptr<const CSetting>& setting,
	std::vector<IntegerSettingOption>& list,
	int& current);

  static void VideoOrientationFiller(const std::shared_ptr<const CSetting>& setting,
	std::vector<IntegerSettingOption>& list,
	int& current);

  static std::string FormatFlags(StreamFlags flags);

  // specialization of CGUIDialogSettingsBase
  bool AllowResettingSettings() const override { return false; }
  bool Save() override;
  void SetupView() override;

  bool OnMessage(CGUIMessage& message) override;
  bool OnBack(int actionID) override;

  // specialization of CGUIDialogSettingsManualBase
  void InitializeSettings() override;

private:
  int m_videoStream;
  bool m_viewModeChanged = false;
};
