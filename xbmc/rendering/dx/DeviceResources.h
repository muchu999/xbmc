/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once
#include "DirectXHelper.h"
#include "HDRStatus.h"
#include "guilib/D3DResource.h"

#include <functional>
#include <memory>

#include <concrt.h>
#include <dxgi1_6.h> //cl<dxgi1_5.h>
#include <winrt/windows.foundation.h>
#include <wrl.h>
#include <wrl/client.h>
#include <settings/lib/SettingDefinitions.h>
#include <queue>

struct RESOLUTION_INFO;
struct DEBUG_INFO_RENDER;
struct VideoDriverInfo;

namespace DX
{
  interface IDeviceNotify
  {
    virtual void OnDXDeviceLost() = 0;
    virtual void OnDXDeviceRestored() = 0;
  };

  // Controls all the DirectX device resources.
  class DeviceResources
  {
    //cl
    #ifndef SUCCEEDED
      #define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
    #endif
    #ifndef FAILED
      #define FAILED(hr) ((HRESULT)(hr) < 0)
    #endif
    #ifndef SAFE_RELEASE
      #define SAFE_RELEASE(_p) { if(_p) { _p->Release();  _p=NULL; } }
    #endif
    typedef HRESULT(WINAPI* PFN_CREATE_DXGI_FACTORY)(REFIID riid, void** ppFactory);


  public:
    static std::shared_ptr<DX::DeviceResources> Get();
	HANDLE dxgiWaitHandle = nullptr;
    DeviceResources();
    virtual ~DeviceResources();
    void Release();
	Microsoft::WRL::ComPtr<IDXGIOutput> m_pActiveOutput;

	using EventCallback = std::function<void(const std::string&)>;
	class listener {
	public:
	  size_t id;
	  EventCallback callback;
	};

	size_t RegisterSwapchainListener(EventCallback callback);
    void UnregisterSwapchainListener(size_t id);
	void NotifySwapchainListeners(const std::string& message);

    void ValidateDevice();
    void HandleDeviceLost(bool removed);
    bool Begin();
    void Present();
	void AppendListOfAdapters(std::vector<IntegerSettingOption>& list);
	int GetDxvaDecoderAdapter() {return(m_dxva2DecoderAdapter);}

    // The size of the render target, in pixels.
    winrt::Windows::Foundation::Size GetOutputSize() const { return m_outputSize; }
    // The size of the render target, in dips.
    winrt::Windows::Foundation::Size GetLogicalSize() const { return m_logicalSize; }
    void SetLogicalSize(float width, float height);
    float GetDpi() const { return m_effectiveDpi; }
    void SetDpi(float dpi);

    // D3D Accessors.
    bool HasValidDevice() const { return m_bDeviceCreated; }
	bool HasValidDecoderDevice() const {return m_bDecoderDeviceCreated;}
    ID3D11Device1* GetD3DDevice() const { return m_d3dDevice.Get(); }
	ID3D11Device1* GetD3DDeviceDecoder() const { return m_d3dDeviceDecoder.Get(); }
	ID3D11DeviceContext1* GetD3DContext() const { return m_deferrContext.Get(); }
	ID3D11DeviceContext1* GetD3DContextDecoder() const { return m_deferrContextDecoder.Get(); }
	ID3D11DeviceContext1* GetImmediateContext() const { return m_d3dContext.Get(); }
	ID3D11DeviceContext1* GetImmediateContextDecoder() const { return m_d3dContextDecoder.Get(); }
	IDXGISwapChain1* GetSwapChain() const { return m_swapChain.Get(); }
    IDXGIFactory2* GetIDXGIFactory2() const { return m_dxgiFactory.Get(); }
    IDXGIAdapter1* GetAdapter() const { return m_adapter.Get(); }
	IDXGIAdapter1* GetAdapterDecoder() const { return m_adapterDecoder.Get(); }
	ID3D11DepthStencilView* GetDSV() const { return m_d3dDepthStencilView.Get(); }
    D3D_FEATURE_LEVEL GetDeviceFeatureLevel() const { return m_d3dFeatureLevel; }
    CD3DTexture& GetBackBuffer() { return m_backBufferTex; }


    void GetOutput(IDXGIOutput** ppOutput) const;
    /*!
     * \brief Retrieve current output and output description. Use cached data first to avoid delays due
     * to dxgi internal multithreading synchronization.
     * \param output The output
     * \param outputDesc The output description
    */
    void GetCachedOutputAndDesc(IDXGIOutput** output, DXGI_OUTPUT_DESC* outputDesc) const;
    DXGI_ADAPTER_DESC GetAdapterDesc() const;
	DXGI_ADAPTER_DESC GetAdapterDecoderDesc() const;
	void GetDisplayMode(DXGI_MODE_DESC *mode) const;

    D3D11_VIEWPORT GetScreenViewport() const { return m_screenViewport; }
    void SetViewPort(D3D11_VIEWPORT& viewPort) const;

	void ReleaseDeferredContext(bool bDeep);
	void ReleaseBackBuffer(bool bDeep = false);
    void CreateBackBuffer();
    void ResizeBuffers();

    bool SetFullScreen(bool fullscreen, RESOLUTION_INFO& res);

    // Apply display settings changes
    void ApplyDisplaySettings();

    // HDR display support
    HDR_STATUS ToggleHDR();
    void SetHdrMetaData(DXGI_HDR_METADATA_HDR10& hdr10) const;
    void SetHdrColorSpace(const DXGI_COLOR_SPACE_TYPE colorSpace);
    bool IsHDROutput() const { return m_IsHDROutput; }
	bool IsHDROutput1() const;

    bool IsTransferPQ() const { return m_IsTransferPQ; }

    // DX resources registration
    void Register(ID3DResource *resource);
    void Unregister(ID3DResource *resource);

    void FinishCommandList(bool bExecute = true) const;
    void ClearDepthStencil() const;
    void ClearRenderTarget(ID3D11RenderTargetView* pRTView, float color[4]) const;
    void RegisterDeviceNotify(IDeviceNotify* deviceNotify);

    bool IsStereoAvailable() const;
    bool IsStereoEnabled() const { return m_stereoEnabled; }
    void SetStereoIdx(byte idx) { m_backBufferTex.SetViewIdx(idx); }

    void SetMonitor(HMONITOR monitor);
    HMONITOR GetMonitor() const;
#if defined(TARGET_WINDOWS_DESKTOP)
    void SetWindow(HWND window);
#elif defined(TARGET_WINDOWS_STORE)
    void Trim() const;
    void SetWindow(const winrt::Windows::UI::Core::CoreWindow& window);
    void SetWindowPos(winrt::Windows::Foundation::Rect rect);
#endif // TARGET_WINDOWS_STORE
    bool IsNV12SharedTexturesSupported() const { return m_NV12SharedTexturesSupport; }
    bool IsDXVA2SharedDecoderSurfaces() const { return m_DXVA2SharedDecoderSurfaces; }
    bool IsSuperResolutionSupported() const { return m_DXVASuperResolutionSupport; }
	bool IsRtxVideoHdrSupported() const { return m_DXVARtxVideoHdrSupport; }
	bool UseFence() const { return m_DXVA2UseFence; }

    struct mp_dxgi_factory_ctx {
      IDXGIFactory1* factory;
      IDXGIOutput6* last_matched_output;
    };

      bool get_output_desc1_from_ctx(struct mp_dxgi_factory_ctx* ctx, DXGI_OUTPUT_DESC1* desc);


    // Gets debug info from swapchain
    DEBUG_INFO_RENDER GetDebugInfo();
    std::vector<DXGI_COLOR_SPACE_TYPE> GetSwapChainColorSpaces() const;
    bool SetMultithreadProtected(bool enabled) const;
    bool IsGCNOrOlder() const;
	bool InitializeDecoderResources(int dxva2Adapter);

  private:
	std::atomic<uint64_t> m_JudderVramStall = 0;
	std::atomic<uint64_t> m_JudderTokenBunching = 0;
	std::atomic<uint64_t> m_JudderCadenceDrop = 0;
	UINT64 m_lastTrackedPresent = 0;
	UINT64 m_lastTrackedRefresh = 0;
	INT64  m_lastExpectedRefreshes = -1;
	std::vector<listener> m_swapchainListeners;
	size_t m_nextSwapchainListenerId = 0;
	class CBackBuffer : public CD3DTexture
    {
    public:
      CBackBuffer() : CD3DTexture() {}
      void SetViewIdx(unsigned idx) { m_viewIdx = idx; }
      bool Acquire(ID3D11Texture2D* pTexture);
    };

    HRESULT CreateSwapChain(DXGI_SWAP_CHAIN_DESC1 &desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC &fsDesc, IDXGISwapChain1 **ppSwapChain) const;
    void DestroySwapChain();
    void CreateDeviceIndependentResources();
    void CreateDeviceResources();
	void CreateDecoderDeviceResources();
	void CreateWindowSizeDependentResources();
    void UpdateRenderTargetSize();
    void OnDeviceLost(bool removed);
    void OnDeviceRestored();
    void HandleOutputChange(const std::function<bool(DXGI_OUTPUT_DESC)>& cmpFunc);
	bool CreateFactory();
    void CheckNV12SharedTexturesSupport();
    VideoDriverInfo GetVideoDriverVersion() const;
    void CheckDXVA2SharedDecoderSurfaces();

    HWND m_window{ nullptr };
#if defined(TARGET_WINDOWS_STORE)
    winrt::Windows::UI::Core::CoreWindow m_coreWindow = nullptr;
#endif
    Microsoft::WRL::ComPtr<IDXGIFactory2> m_dxgiFactory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> m_adapter;
	Microsoft::WRL::ComPtr<IDXGIAdapter1> m_adapterDecoder;
	Microsoft::WRL::ComPtr<IDXGIOutput1> m_output;
    DXGI_OUTPUT_DESC m_outputDesc{};

    Microsoft::WRL::ComPtr<ID3D11Device1> m_d3dDevice;
	Microsoft::WRL::ComPtr<ID3D11Device1> m_d3dDeviceDecoder;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext1> m_d3dContext;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext1> m_d3dContextDecoder;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext1> m_deferrContext;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext1> m_deferrContextDecoder;
	Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D11Debug> m_d3dDebug;
#endif

    CBackBuffer m_backBufferTex;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_d3dDepthStencilView;
    D3D11_VIEWPORT m_screenViewport;

    // Cached device properties.
    D3D_FEATURE_LEVEL m_d3dFeatureLevel;
	D3D_FEATURE_LEVEL m_d3dFeatureLevelDecoder;
	winrt::Windows::Foundation::Size m_outputSize;
    winrt::Windows::Foundation::Size m_logicalSize;
    float m_dpi;

    // This is the DPI that will be reported back to the app. It takes into account whether the app supports high resolution screens or not.
    float m_effectiveDpi;
    // The IDeviceNotify can be held directly as it owns the DeviceResources.
    IDeviceNotify* m_deviceNotify;

    // scritical section
    Concurrency::critical_section m_criticalSection;
    Concurrency::critical_section m_resourceSection;
    std::vector<ID3DResource*> m_resources;

	int m_dxva2DecoderAdapter;
    bool m_stereoEnabled;
    bool m_bDeviceCreated;
	bool m_bDecoderDeviceCreated;
    bool m_IsHDROutput;
    bool m_IsTransferPQ;
    bool m_NV12SharedTexturesSupport{false};
    bool m_DXVA2SharedDecoderSurfaces{false};
    bool m_DXVASuperResolutionSupport{false};
	bool m_DXVARtxVideoHdrSupport {false};
	bool m_DXVA2UseFence{false};
private:
	// Multi-threaded Present Engine Primitives
	std::thread m_presentThread;
	std::mutex m_presentMutex;
	std::condition_variable m_presentCv;
	std::condition_variable m_renderCv;  // Used to wake up the Rendering Thread
	std::atomic<uint64_t> m_framesRendered {0};
	std::atomic<uint64_t> m_framesPresented {0};
	std::atomic<bool> m_presentRunning {false};
	std::atomic<int64_t> m_lastVsyncTimestamp {0};
	HANDLE m_latencyWaitableObject = nullptr;
	std::atomic<HRESULT> m_presentResult {S_OK};
	std::mutex                                m_lifelineMutex;
	std::vector<Microsoft::WRL::ComPtr<IUnknown>> m_currentFrameLifelines;
	//std::queue<Microsoft::WRL::ComPtr<ID3D11CommandList>> m_frameQueue;
	struct FramePackage
	{
	  Microsoft::WRL::ComPtr<ID3D11CommandList> CommandList;
	  std::vector<Microsoft::WRL::ComPtr<IUnknown>> ResourceLifelines; // Keeps assets alive!
	};

	// Change your queue to use this wrapper
	std::queue<FramePackage> m_frameQueue;
	std::mutex m_queueMutex;

	void PresentThreadLoop();
	void StartPresentThread();
	void StopPresentThread();

	static constexpr int PRESENT_QUERY_LATENCY = 8;
	void InitProfiling();
	void ReleaseProfilingQueries();
	void LogThreadState(const std::string& location);
	std::thread             m_watchdogThread;
	std::atomic<bool>       m_watchdogRunning {false};
	uint64_t                m_lastCheckPresented {0};
	int                     m_stallCount {0};
	bool                    m_bIsTearingDown = false;

	void WatchdogThreadLoop();



public:
  // Latency matching DXGI default max frame latency

  struct PresentQuery
  {
	ID3D11Query* disjoint = nullptr;
	ID3D11Query* start = nullptr;
	ID3D11Query* end = nullptr;
	bool is_active = false;
  };
  std::vector<PresentQuery> m_presentQueryRing = std::vector<PresentQuery> (PRESENT_QUERY_LATENCY);
  int m_currentWriteSlot = 0;
  int m_presentWriteSlot = 0;
  std::atomic<float> m_guiComposeTime {0.0f};

  void KeepResourceAliveThisFrame(const Microsoft::WRL::ComPtr<IUnknown>& resource);
  HRESULT SignalFrameReady();
  void DrainPresentationQueue();
  int64_t GetLatestVsyncTime() const { return m_lastVsyncTimestamp.load(std::memory_order_acquire); }

  // Public check so the rendering loop can audit the device state
  HRESULT GetLastPresentResult() const { return m_presentResult.load(std::memory_order_acquire); }
  void ResetPresentResult() { m_presentResult.store(S_OK, std::memory_order_release); }
  void StartWatchdog();
  void StopWatchdog();
  void RestartPresentThreadAsynchronously();
  static bool supports_nvidia_true_hdr(struct mp_filter* vf);

  

  };
}
