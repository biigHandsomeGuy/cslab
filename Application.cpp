#define _CRT_SECURE_NO_WARNINGS 1
#include "Application.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "d3dx12.h"
#include <d3dcompiler.h>
#include "CompiledShaders/PerlinNoiseCS.h"
#include "CompiledShaders/RayMarching.h"
#include "CompiledShaders/VoronoiNoiseCS.h"
#include "CompiledShaders/AmazingShader.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_impl_win32.h"

LRESULT CALLBACK
MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// Forward hwnd on because we can get messages (e.g., WM_CREATE)
	// before CreateWindow returns, and thus before mhMainWnd is valid.
	return App::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
}
App* App::mApp = nullptr;
App::App() { mApp = this; }

App::~App()
{
	if (m_Device != nullptr)
		FlushCommandQueue();

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}
// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT App::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
		// WM_ACTIVATE is sent when the window is activated or deactivated.  
		// We pause the game when the window is deactivated and unpause it 
		// when it becomes active.  
	case WM_ACTIVATE:
		if (LOWORD(wParam) == WA_INACTIVE)
		{
			m_AppPaused = true;
		}
		else
		{
			m_AppPaused = false;
		}
		return 0;

		// WM_SIZE is sent when the user resizes the window.  
	case WM_SIZE:
		// Save the new client area dimensions.
		m_ClientWidth = LOWORD(lParam);
		m_ClientHeight = HIWORD(lParam);
		if (m_Device)
		{
			if (wParam == SIZE_MINIMIZED)
			{
				m_AppPaused = true;
				m_Minimized = true;
				m_Maximized = false;
			}
			else if (wParam == SIZE_MAXIMIZED)
			{
				m_AppPaused = false;
				m_Minimized = false;
				m_Maximized = true;
				OnResize();
			}
			else if (wParam == SIZE_RESTORED)
			{

				// Restoring from minimized state?
				if (m_Minimized)
				{
					m_AppPaused = false;
					m_Minimized = false;
					OnResize();
				}

				// Restoring from maximized state?
				else if (m_Maximized)
				{
					m_AppPaused = false;
					m_Maximized = false;
					OnResize();
				}
				else if (m_Resizing)
				{
					// If user is dragging the resize bars, we do not resize 
					// the buffers here because as the user continuously 
					// drags the resize bars, a stream of WM_SIZE messages are
					// sent to the window, and it would be pointless (and slow)
					// to resize for each WM_SIZE message received from dragging
					// the resize bars.  So instead, we reset after the user is 
					// done resizing the window and releases the resize bars, which 
					// sends a WM_EXITSIZEMOVE message.
				}
				else // API call such as SetWindowPos or m_SwapChain->SetFullscreenState.
				{
					OnResize();
				}
			}
		}
		return 0;

		// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
	case WM_ENTERSIZEMOVE:
		m_AppPaused = true;
		m_Resizing = true;
		return 0;

		// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
		// Here we reset everything based on the new window dimensions.
	case WM_EXITSIZEMOVE:
		m_AppPaused = false;
		m_Resizing = false;
		OnResize();
		return 0;

		// WM_DESTROY is sent when the window is being destroyed.
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

		// The WM_MENUCHAR message is sent when a menu is active and the user presses 
		// a key that does not correspond to any mnemonic or accelerator key. 
	case WM_MENUCHAR:
		// Don't beep when we alt-enter.
		return MAKELRESULT(0, MNC_CLOSE);

		// Catch this message so to prevent the window from becoming too small.
	case WM_GETMINMAXINFO:
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
		return 0;

	case WM_KEYUP:
		if (wParam == VK_ESCAPE)
		{
			PostQuitMessage(0);
		}
		return 0;
	}



	return DefWindowProc(hwnd, msg, wParam, lParam);
}


bool App::Initialize()
{
	if (!InitMainWindow())
		return false;

	if (!InitDirect3D())
		return false;

	// Do the initial resize code.
	OnResize();


	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(m_CommandList->Reset(m_CommandAllocator.Get(), nullptr));

	
	// BuildRootSignature
	{
		CD3DX12_DESCRIPTOR_RANGE range = {};
		range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

		// Root parameter can be a table, root descriptor or root constants.
		CD3DX12_ROOT_PARAMETER slotRootParameter[(UINT)GraphicsRootParameters::kGraphicsRootParametersCount];

		slotRootParameter[(UINT)GraphicsRootParameters::kSrv].InitAsDescriptorTable(1, &range);


		CD3DX12_STATIC_SAMPLER_DESC staticSampler(
			0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER, // U 环绕
			D3D12_TEXTURE_ADDRESS_MODE_BORDER, // V 环绕
			D3D12_TEXTURE_ADDRESS_MODE_BORDER
		);

		// A root signature is an array of root parameters.
		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc((UINT)GraphicsRootParameters::kGraphicsRootParametersCount, slotRootParameter,
			1, &staticSampler,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
			serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

		if (errorBlob != nullptr)
		{
			::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
		}
		ThrowIfFailed(hr);

		ThrowIfFailed(m_Device->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(m_RootSignature.GetAddressOf())));
	}
	// create compute RS
	{

		CD3DX12_DESCRIPTOR_RANGE range;
		range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

		CD3DX12_ROOT_PARAMETER rootParameter[2];
		rootParameter[0].InitAsDescriptorTable(1, &range);
		rootParameter[1].InitAsConstantBufferView(0);

		// A root signature is an array of root parameters.
		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, rootParameter,
			0, nullptr,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		ThrowIfFailed(D3D12SerializeRootSignature(
			&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
			serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));

		if (errorBlob != nullptr)
		{
			::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
		}

		ThrowIfFailed(m_Device->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(&computeRootSignature)));

		//BuildDescriptorHeaps();
		{

			//
			// Create the SRV heap.
			//
			D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
			srvHeapDesc.NumDescriptors = 64;
			srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			ThrowIfFailed(m_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SrvDescriptorHeap)));

			// Fill out the heap with actual descriptors.
			//
			CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(m_SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

		}

		// Create Texture
		{
			D3D12_RESOURCE_DESC texDesc = {};
			texDesc.Width = 512;
			texDesc.Height = 512;
			texDesc.DepthOrArraySize = 1;
			texDesc.MipLevels = 1;
			texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			texDesc.SampleDesc.Count = 1;
			texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

			m_Device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&texDesc,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				nullptr,
				IID_PPV_ARGS(&m_PerlinTexture)
			);


		}

		// BuildPSOs();
		{
			ComPtr<ID3DBlob> vertexShader;
			ComPtr<ID3DBlob> pixelShader;
			ComPtr<ID3DBlob> error;

#if defined(_DEBUG)
			// Enable better shader debugging with the graphics debugging tools.
			UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
			UINT compileFlags = 0;
#endif

			ThrowIfFailed(D3DCompileFromFile(L"test.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &error));
			ThrowIfFailed(D3DCompileFromFile(L"test.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &error));

			D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }

			};

			D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc = {};
			basePsoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
			basePsoDesc.pRootSignature = m_RootSignature.Get();
			basePsoDesc.VS =
			{
				vertexShader->GetBufferPointer(),
				vertexShader->GetBufferSize()
			};
			basePsoDesc.PS =
			{
				pixelShader->GetBufferPointer(),
				pixelShader->GetBufferSize()
			};
			basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			basePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
			//basePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
			basePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			basePsoDesc.BlendState.RenderTarget[0].BlendEnable = false;
			basePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			basePsoDesc.SampleMask = UINT_MAX;
			basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			basePsoDesc.NumRenderTargets = 1;
			basePsoDesc.RTVFormats[0] = m_BackBufferFormat;
			basePsoDesc.SampleDesc.Count = m_4xMsaaState ? 4 : 1;
			basePsoDesc.SampleDesc.Quality = m_4xMsaaState ? (m_4xMsaaQuality - 1) : 0;
			basePsoDesc.DSVFormat = m_DepthStencilFormat;


			basePsoDesc.DepthStencilState.DepthEnable = true;
			basePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
			basePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
			ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&basePsoDesc, IID_PPV_ARGS(&m_PSOs["opaque"])));

			D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
			computeDesc.pRootSignature = computeRootSignature.Get();
			computeDesc.CS = { g_pRayMarching,sizeof(g_pRayMarching) };
			computeDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
			ThrowIfFailed(m_Device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(&m_PSOs["white"])));

			
			computeDesc.pRootSignature = computeRootSignature.Get();
			computeDesc.CS = { g_pPerlinNoiseCS,sizeof(g_pPerlinNoiseCS) };
			computeDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
			ThrowIfFailed(m_Device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(&m_PSOs["perlin"])));

			
			computeDesc.pRootSignature = computeRootSignature.Get();
			computeDesc.CS = { g_pVoronoiNoiseCS,sizeof(g_pVoronoiNoiseCS) };
			computeDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
			ThrowIfFailed(m_Device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(&m_PSOs["voronoi"])));

			computeDesc.pRootSignature = computeRootSignature.Get();
			computeDesc.CS = { g_pAmazingShader,sizeof(g_pAmazingShader) };
			computeDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
			ThrowIfFailed(m_Device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(&m_PSOs["amazing"])));
		}
		// CreateVertexBuffer();
		ComPtr<ID3D12Resource> rectangleVBUpload;
		{
			// Define the geometry for a triangle.

			Vertex rectangleVertices[] =
			{
					{ XMFLOAT4(-1.0, -1.0, 0.0, 1.0),	XMFLOAT2(0.0, 1.0) }, // 左下
					{ XMFLOAT4(1.0, -1.0, 0.0, 1.0),	XMFLOAT2(1.0, 1.0) }, // 右下
					{ XMFLOAT4(-1.0,  1.0, 0.0, 1.0),	XMFLOAT2(0.0, 0.0) }, // 左上
					{ XMFLOAT4(1.0,  1.0, 0.0, 1.0),	XMFLOAT2(1.0, 0.0) }, // 右上
			};

			const UINT rectangleVBSize = sizeof(rectangleVertices);


			ThrowIfFailed(m_Device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(rectangleVBSize),
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&m_RectangleVertexBuffer)));


			ThrowIfFailed(m_Device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(rectangleVBSize),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&rectangleVBUpload)));


			// Copy data to the intermediate upload heap and then schedule a copy
			// from the upload heap to the vertex buffer.

			D3D12_SUBRESOURCE_DATA rectangleVertexData = {};
			rectangleVertexData.pData = rectangleVertices;
			rectangleVertexData.RowPitch = rectangleVBSize;
			rectangleVertexData.SlicePitch = rectangleVertexData.RowPitch;

			UpdateSubresources<1>(m_CommandList.Get(), m_RectangleVertexBuffer.Get(), rectangleVBUpload.Get(), 0, 0, 1, &rectangleVertexData);
			m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_RectangleVertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));


			// Initialize the vertex buffer view.

			m_RectangleVertexBufferView.BufferLocation = m_RectangleVertexBuffer->GetGPUVirtualAddress();
			m_RectangleVertexBufferView.StrideInBytes = sizeof(Vertex);
			m_RectangleVertexBufferView.SizeInBytes = sizeof(rectangleVertices);
		}

		{
			ThrowIfFailed(m_Device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(sizeof(PerlinNoiseConstants)),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&m_PerlinNoiseConstantBuffer)));

			// Initialize the const buffers
			XMFLOAT2 noise = { 1,1 };
			m_PerlinNoiseData.NoiseScale = noise;

			ThrowIfFailed(m_PerlinNoiseConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_pComputeCbvDataBegin)));
			memcpy(m_pComputeCbvDataBegin, &m_PerlinNoiseData, sizeof(PerlinNoiseConstants));
		}

		// Create Srv uav
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = m_PerlinTexture->GetDesc().Format;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
			srvDesc.Texture2D.MipLevels = -1;

			m_Device->CreateShaderResourceView(
				m_PerlinTexture.Get(),
				&srvDesc,
				m_SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart()
			);

			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = m_PerlinTexture->GetDesc().Format;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			uavDesc.Texture2D.PlaneSlice = 0;

			auto handle = m_SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			handle.ptr += m_SrvUavCbvDescriptorSize;
			m_Device->CreateUnorderedAccessView(m_PerlinTexture.Get(),
				nullptr,
				&uavDesc,
				handle
			);
		}

		// Execute the initialization commands.
		ThrowIfFailed(m_CommandList->Close());
		ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
		m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

		// Wait until initialization is complete.
		FlushCommandQueue();


	}


	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls


	auto size = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), 63, size);
	auto GpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 63, size);


	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(m_hMainWnd);
	ImGui_ImplDX12_Init(m_Device.Get(), 2,
		m_BackBufferFormat, m_SrvDescriptorHeap.Get(),
		handle,
		GpuHandle);
	return true;
}

void App::CreateRtvAndDsvDescriptorHeaps()
{
	//
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(m_Device->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(m_RtvHeap.GetAddressOf())));

	// Add +1 DSV for shadow map.
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(m_Device->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(m_DsvHeap.GetAddressOf())));

}

void App::OnResize()
{
	assert(m_Device);
	assert(m_SwapChain);
	assert(m_CommandAllocator);

	// Flush before changing any resources.
	FlushCommandQueue();

	ThrowIfFailed(m_CommandList->Reset(m_CommandAllocator.Get(), nullptr));

	// Release the previous resources we will be recreating.
	for (int i = 0; i < SwapChainBufferCount; ++i)
		m_SwapChainBuffer[i].Reset();
	m_DepthStencilBuffer.Reset();

	// Resize the swap chain.
	ThrowIfFailed(m_SwapChain->ResizeBuffers(
		SwapChainBufferCount,
		m_ClientWidth, m_ClientHeight,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	m_CurrBackBuffer = 0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(m_RtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_SwapChainBuffer[i])));
		m_Device->CreateRenderTargetView(m_SwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.Offset(1, m_RtvDescriptorSize);
	}

	// Create the depth/stencil buffer and view.
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = m_ClientWidth;
	depthStencilDesc.Height = m_ClientHeight;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = m_DepthStencilFormat;
	depthStencilDesc.SampleDesc.Count = m_4xMsaaState ? 4 : 1;
	depthStencilDesc.SampleDesc.Quality = m_4xMsaaState ? (m_4xMsaaQuality - 1) : 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = m_DepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;
	ThrowIfFailed(m_Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(m_DepthStencilBuffer.GetAddressOf())));

	// Create descriptor to mip level 0 of entire resource using the format of the resource.
	m_Device->CreateDepthStencilView(m_DepthStencilBuffer.Get(), nullptr, DepthStencilView());

	// Transition the resource from its initial state to be used as a depth buffer.
	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_DepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	// Execute the resize commands.
	ThrowIfFailed(m_CommandList->Close());
	ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
	m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until resize is complete.
	FlushCommandQueue();

	// Update the viewport transform to cover the client area.
	m_ScreenViewport.TopLeftX = 0;
	m_ScreenViewport.TopLeftY = 0;
	m_ScreenViewport.Width = static_cast<float>(m_ClientWidth);
	m_ScreenViewport.Height = static_cast<float>(m_ClientHeight);
	m_ScreenViewport.MinDepth = 0.0f;
	m_ScreenViewport.MaxDepth = 1.0f;

	m_ScissorRect = { 0, 0, m_ClientWidth, m_ClientHeight };

}

bool App::InitMainWindow()
{
	WNDCLASS wc;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = MainWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = m_hAppInst;
	wc.hIcon = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName = 0;
	wc.lpszClassName = L"MainWnd";

	if (!RegisterClass(&wc))
	{
		MessageBox(0, L"RegisterClass Failed.", 0, 0);
		return false;
	}

	// Compute window rectangle dimensions based on requested client area dimensions.
	RECT R = { 0, 0, m_ClientWidth, m_ClientHeight };
	AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int width = R.right - R.left;
	int height = R.bottom - R.top;

	m_hMainWnd = CreateWindow(L"MainWnd", m_MainWndCaption.c_str(),
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, m_hAppInst, 0);
	if (!m_hMainWnd)
	{
		MessageBox(0, L"CreateWindow Failed.", 0, 0);
		return false;
	}

	ShowWindow(m_hMainWnd, SW_SHOW);
	UpdateWindow(m_hMainWnd);

}

bool App::InitDirect3D()
{
#if defined(DEBUG) || defined(_DEBUG) 
	// Enable the D3D12 debug layer.
	{
		ComPtr<ID3D12Debug> debugController;
		ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
		debugController->EnableDebugLayer();
	}
#endif

	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_Factory)));
	
	// Try to create hardware device.
	HRESULT hardwareResult = D3D12CreateDevice(
		nullptr,             // default adapter
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&m_Device));
	
	// Fallback to WARP device.
	if (FAILED(hardwareResult))
	{
		ComPtr<IDXGIAdapter> pWarpAdapter;
		ThrowIfFailed(m_Factory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));
	
		ThrowIfFailed(D3D12CreateDevice(
			pWarpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_Device)));
	}
	
	ThrowIfFailed(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&m_Fence)));
	
	m_RtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_DsvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_SrvUavCbvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	
	// Check 4X MSAA quality support for our back buffer format.
	// All Direct3D 11 capable devices support 4X MSAA for all render 
	// target formats, so we only need to check quality support.
	
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = m_BackBufferFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;
	ThrowIfFailed(m_Device->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&msQualityLevels,
		sizeof(msQualityLevels)));
	
	m_4xMsaaQuality = msQualityLevels.NumQualityLevels;
	assert(m_4xMsaaQuality > 0 && "Unexpected MSAA quality level.");
	
	
	CreateCommandObjects();
	CreateSwapChain();
	CreateRtvAndDsvDescriptorHeaps();
}

void App::CreateCommandObjects()
{
	// Command Queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue)));

	D3D12_COMMAND_QUEUE_DESC computeQueueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(m_Device->CreateCommandQueue(&computeQueueDesc, IID_PPV_ARGS(&m_ComputeCommandQueue)));

	// Command Allocator
	ThrowIfFailed(m_Device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(m_CommandAllocator.GetAddressOf())));

	ThrowIfFailed(m_Device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_COMPUTE,
		IID_PPV_ARGS(m_ComputeCommandAllocator.GetAddressOf())));

	// Command List
	ThrowIfFailed(m_Device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_CommandAllocator.Get(), // Associated command allocator
		nullptr,                   // Initial PipelineStateObject
		IID_PPV_ARGS(m_CommandList.GetAddressOf())));

	ThrowIfFailed(m_Device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_COMPUTE,
		m_ComputeCommandAllocator.Get(), // Associated command allocator
		nullptr,                   // Initial PipelineStateObject
		IID_PPV_ARGS(m_ComputeCommandList.GetAddressOf())));


	// Start off in a closed state.  This is because the first time we refer 
	// to the command list we will Reset it, and it needs to be closed before
	// calling Reset.
	m_CommandList->Close();
	m_ComputeCommandList->Close();
}

void App::CreateSwapChain()
{
	// Release the previous swapchain we will be recreating.
	m_SwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = m_ClientWidth;
	sd.BufferDesc.Height = m_ClientHeight;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = m_4xMsaaState ? 4 : 1;
	sd.SampleDesc.Quality = m_4xMsaaState ? (m_4xMsaaQuality - 1) : 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = SwapChainBufferCount;
	sd.OutputWindow = m_hMainWnd;
	sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	// Note: Swap chain uses queue to perform flush.
	ThrowIfFailed(m_Factory->CreateSwapChain(
		m_CommandQueue.Get(),
		&sd,
		m_SwapChain.GetAddressOf()));
}

void App::FlushCommandQueue()
{
	// Advance the fence value to mark commands up to this fence point.
	m_CurrentFence++;

	// Add an instruction to the command queue to set a new fence point.  Because we 
	// are on the GPU timeline, the new fence point won't be set until the GPU finishes
	// processing all the commands prior to this Signal().
	ThrowIfFailed(m_CommandQueue->Signal(m_Fence.Get(), m_CurrentFence));

	// Wait until the GPU has completed commands up to this fence point.
	if (m_Fence->GetCompletedValue() < m_CurrentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

		// Fire event when GPU hits current fence.  
		ThrowIfFailed(m_Fence->SetEventOnCompletion(m_CurrentFence, eventHandle));

		// Wait until the GPU hits current fence event is fired.
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

ID3D12Resource* App::CurrentBackBuffer()const
{
	return m_SwapChainBuffer[m_CurrBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE App::CurrentBackBufferView()const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		m_RtvHeap->GetCPUDescriptorHandleForHeapStart(),
		m_CurrBackBuffer,
		m_RtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE App::DepthStencilView()const
{
	return m_DsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void App::ExportTexture()
{

	D3D12_RESOURCE_DESC desc = m_PerlinTexture->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
	UINT64 bufferSize = 0;
	m_Device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, nullptr, nullptr, &bufferSize);

	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = bufferSize;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = m_Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&pReadbackBuffer)
	);


	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		m_PerlinTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));

	D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
	srcLoc.pResource = m_PerlinTexture.Get();
	srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	srcLoc.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
	dstLoc.pResource = pReadbackBuffer.Get();
	dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dstLoc.PlacedFootprint = layout;

	m_CommandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

	// 提交命令并等待完成
	m_CommandList->Close();
	ID3D12CommandList* ppCommandLists[] = { m_CommandList.Get()};
	m_CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	FlushCommandQueue();
	ThrowIfFailed(m_CommandAllocator->Reset());


	ThrowIfFailed(m_CommandList->Reset(m_CommandAllocator.Get(), m_PSOs["opaque"].Get()));

	BYTE* pData;
	ThrowIfFailed(pReadbackBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pData)));


	pReadbackBuffer->Unmap(0, nullptr);

	
	stbi_write_png(filename.c_str(), 512, 512, 4, pData, 512 * 4);

}




void App::Update(float time)
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	m_PerlinNoiseData.time = time;
	{
		ImGui::Begin("Debug");

		const char* items[] = { "RayMarching", "PerlinNosie", "VoronoiNoise", "AmazingShader"};
		ImGui::ListBox("type", &m_CurrentNoiseType, items, IM_ARRAYSIZE(items), 4);

		ImGui::SliderFloat2("noiseScale", &m_PerlinNoiseData.NoiseScale.x, 0, 20);
		// ImGui::SliderFloat2("noiseScale", &m_PerlinNoiseData.NoiseScale.x, 0, 100, "%.6f");
		memcpy(m_pComputeCbvDataBegin, &m_PerlinNoiseData, sizeof(PerlinNoiseConstants));

		ImGui::InputText("file name", &filename[0], 512);
		if (ImGui::Button("export"))
		{
			exporting = true;
		}

		ImGui::End();
	}
}



void App::Draw()
{
	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(m_CommandAllocator->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(m_CommandList->Reset(m_CommandAllocator.Get(), m_PSOs["opaque"].Get()));

	if (exporting)
	{
		ExportTexture();
		exporting = false;
	}

	ID3D12DescriptorHeap* descriptorHeaps[] = { m_SrvDescriptorHeap.Get() };
	m_CommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());


	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));


	// 设置 PipelineState
	if (m_CurrentNoiseType == 0)
	{
		m_CommandList->SetPipelineState(m_PSOs["white"].Get());
	}
	else if (m_CurrentNoiseType == 1)
	{
		m_CommandList->SetPipelineState(m_PSOs["perlin"].Get());
	}
	else if (m_CurrentNoiseType == 2)
	{
		m_CommandList->SetPipelineState(m_PSOs["voronoi"].Get());
	}
	else if (m_CurrentNoiseType == 3)
	{
		m_CommandList->SetPipelineState(m_PSOs["amazing"].Get());
	}
	m_CommandList->SetComputeRootSignature(computeRootSignature.Get());

	auto handle = m_SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += m_SrvUavCbvDescriptorSize;
	// 设置 UAV
	m_CommandList->SetComputeRootDescriptorTable(0, handle);
	m_CommandList->SetComputeRootConstantBufferView(1, m_PerlinNoiseConstantBuffer->GetGPUVirtualAddress());
	// 执行 Compute Shader
	m_CommandList->Dispatch(32, 32, 1); // 512x512 纹理，每个线程组 8x8



	static XMFLOAT4 color = XMFLOAT4(0, 0, 0, 1);
	// Clear the back buffer.
	m_CommandList->ClearRenderTargetView(CurrentBackBufferView(), &color.x, 0, nullptr);
	m_CommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	m_CommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	m_CommandList->RSSetViewports(1, &m_ScreenViewport);
	m_CommandList->RSSetScissorRects(1, &m_ScissorRect);
	m_CommandList->SetPipelineState(m_PSOs["opaque"].Get());

	m_CommandList->SetGraphicsRootDescriptorTable((UINT)GraphicsRootParameters::kSrv, m_SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	m_CommandList->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_CommandList->IASetVertexBuffers(0, 1, &m_RectangleVertexBufferView);

	m_CommandList->DrawInstanced(4, 1, 0, 0);


	// RenderingF
	ImGui::Render();
	// //m_CommandList->SetDescriptorHeaps(1, &m_SrvDescriptorHeap);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_CommandList.Get());


	// Indicate a state transition on the resource usage.
	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(m_CommandList->Close());


	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
	m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(m_SwapChain->Present(0, 0));
	m_CurrBackBuffer = (m_CurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	m_CurrentFence++;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().

	const UINT64 fence = m_CurrentFence;

	ThrowIfFailed(m_CommandQueue->Signal(m_Fence.Get(), fence));

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (m_Fence->GetCompletedValue() < fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(m_Fence->SetEventOnCompletion(fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

int App::Run()
{
	MSG msg = { 0 };

	while (msg.message != WM_QUIT)
	{
		// If there are Window messages then process them.
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		// Otherwise, do animation/game stuff.
		else
		{
			if (!m_AppPaused)
			{
				Update(timer.GetElapsedTime());
				Draw();
			}
			else
			{
				Sleep(100);
			}
		}
	}

	return (int)msg.wParam;
}
