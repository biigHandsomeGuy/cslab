#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <dxgi1_4.h>
#include <unordered_map>
#include <comdef.h>
#include <string>
#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

class Timer
{
public:
	Timer()
	{
		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&startTime);
	}

	double GetElapsedTime()
	{
		LARGE_INTEGER currentTime;
		QueryPerformanceCounter(&currentTime);
		return static_cast<double>(currentTime.QuadPart - startTime.QuadPart) / frequency.QuadPart;
	}

private:
	LARGE_INTEGER frequency;
	LARGE_INTEGER startTime;
};
struct Vertex
{
	XMFLOAT4 position;
	XMFLOAT2 texcoord;
};

__declspec(align(256)) struct PerlinNoiseConstants
{
	XMFLOAT2 NoiseScale;
	FLOAT scale;
	FLOAT time;
};

enum class NoiseType
{
	WhiteNoise, PerlinNosie, VoronoiNoise, Counts
};

enum class HeapLayout : UINT8
{
	kSrv,
	kUav,
	kHeapsCount
};

enum class GraphicsRootParameters : UINT
{
	kSrv,
	kGraphicsRootParametersCount
};


class App
{
public:
	static App* GetApp() { return mApp; }


	App();
	App(const App& rhs) = delete;
	App& operator=(const App& rhs) = delete;
	~App();
	bool Initialize();
	int Run();

	static App* mApp;
	LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
	void CreateRtvAndDsvDescriptorHeaps();
	void OnResize();
	void Update(float time);
	void Draw();

	
	bool InitMainWindow();
	bool InitDirect3D();
	void CreateCommandObjects();
	void CreateSwapChain();

	void FlushCommandQueue();

	ID3D12Resource* CurrentBackBuffer()const;
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView()const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView()const;

	void ExportTexture();
private:
	int m_CurrentNoiseType;

	ComPtr<ID3D12RootSignature> m_RootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> m_SrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, ComPtr<ID3DBlob>> m_Shaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> m_PSOs;

	ComPtr<ID3D12Resource> m_RectangleVertexBuffer;
	ComPtr<ID3D12Resource> m_ConstantBuffer;
	ComPtr<ID3D12Resource> m_PerlinNoiseConstantBuffer;

	ComPtr<ID3D12Resource> m_PerlinTexture;

	D3D12_VERTEX_BUFFER_VIEW m_RectangleVertexBufferView;

	PerlinNoiseConstants m_PerlinNoiseData;

	ComPtr<ID3D12RootSignature> computeRootSignature = nullptr;

	UINT8* m_pComputeCbvDataBegin = nullptr;

	HINSTANCE m_hAppInst = nullptr; // application instance handle
	HWND      m_hMainWnd = nullptr; // main window handle
	bool      m_AppPaused = false;  // is the application paused?
	bool      m_Minimized = false;  // is the application minimized?
	bool      m_Maximized = false;  // is the application maximized?
	bool      m_Resizing = false;   // are the resize bars being dragged?
	bool      m_FullscreenState = false;// fullscreen enabled

	// Set true to use 4X MSAA (?.1.8).  The default is false.
	bool      m_4xMsaaState = false;    // 4X MSAA enabled
	UINT      m_4xMsaaQuality = 0;      // quality level of 4X MSAA


	Microsoft::WRL::ComPtr<IDXGIFactory4> m_Factory;
	Microsoft::WRL::ComPtr<IDXGISwapChain> m_SwapChain;
	Microsoft::WRL::ComPtr<ID3D12Device> m_Device;

	Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
	UINT64 m_CurrentFence = 0;

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_ComputeCommandQueue;

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_ComputeCommandAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CommandList;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_ComputeCommandList;

	static const int SwapChainBufferCount = 2;
	int m_CurrBackBuffer = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_SwapChainBuffer[SwapChainBufferCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthStencilBuffer;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DsvHeap;

	D3D12_VIEWPORT m_ScreenViewport;
	D3D12_RECT m_ScissorRect;

	// Derived class should set these in derived constructor to customize starting values.
	std::wstring m_MainWndCaption = L"d3d App";
	D3D_DRIVER_TYPE m_d3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
	DXGI_FORMAT m_BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	DXGI_FORMAT m_DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	int m_ClientWidth = 600;
	int m_ClientHeight = 600;


	UINT m_RtvDescriptorSize;
	UINT m_SrvUavCbvDescriptorSize;
	UINT m_DsvDescriptorSize;

	XMMATRIX m_ProjectionMatrix;

	Microsoft::WRL::ComPtr<ID3D12Resource> pReadbackBuffer;
	bool exporting;
	std::string filename;
	Timer timer;
};



class DxException
{
public:
	DxException() = default;
	DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber);

	std::wstring ToString()const;

	HRESULT ErrorCode = S_OK;
	std::wstring FunctionName;
	std::wstring Filename;
	int LineNumber = -1;
};

inline std::wstring DxException::ToString()const
{
	// Get the string description of the error code.
	_com_error err(ErrorCode);
	std::wstring msg = err.ErrorMessage();

	return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}

inline DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber) :
	ErrorCode(hr),
	FunctionName(functionName),
	Filename(filename),
	LineNumber(lineNumber)
{
}

inline std::wstring AnsiToWString(const std::string& str)
{
	WCHAR buffer[512];
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
	return std::wstring(buffer);
}

#define ThrowIfFailed(x)                                              \
{                                                                     \
    HRESULT hr__ = (x);                                               \
    std::wstring wfn = AnsiToWString(__FILE__);                       \
    if(FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); } \
}
