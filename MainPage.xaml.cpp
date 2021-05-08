//
// MainPage.xaml.cpp
// Implementation of the MainPage class.
//

#include "pch.h"
#include "MainPage.xaml.h"

using namespace UWP_DXR;


using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;

using namespace Windows::System::Threading;

#include <wrl.h>
using namespace Microsoft::WRL;

#include <dxgi1_6.h>
#include <d3d12.h>
#include <windows.ui.xaml.media.dxinterop.h>
#include <DirectXMath.h>

#include "Shader/DxrShader.hlsl.h"


void ThrowIfFailed(HRESULT hr) {
	if (FAILED(hr))
	{
		throw Platform::Exception::CreateException(hr);
	}
}

// 向上对齐
#define MEMORY_UP_ALIGNMENT(A,B) ((UINT64)(((A)+((B)-1))&~(B - 1)))

UINT								iWidth = 1500;
UINT								iHeight = 1000;

DXGI_FORMAT							emRenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
D3D_FEATURE_LEVEL					emD3DFeatureLevel = D3D_FEATURE_LEVEL_12_1;

ComPtr<IDXGIFactory7>				pIDXGIFactory7 = {};
ComPtr<IDXGIAdapter4>				pIAdapter4 = {};
ComPtr<ID3D12Device8>				pID3D12Device8 = {};

ComPtr<ID3D12Fence1>				pIFence1 = {};
HANDLE								FenceEvent = {};
UINT64								nFenceValue = {};

ComPtr<ID3D12CommandQueue>			pICMDQueue = {};
ComPtr<ID3D12CommandAllocator>		pICMDAlloc = {};
ComPtr<ID3D12GraphicsCommandList6>	pICMDList6 = {};

ComPtr<ID3D12Heap>					pIUploadHeap = {};

const UINT64						nFrameBackBufCount = 3u;
ComPtr<IDXGISwapChain1>				pISwapChain1 = {};
ComPtr<IDXGISwapChain3>				pISwapChain3 = {};
ComPtr<ID3D12Resource>				pISwapChainBuffer[nFrameBackBufCount] = {};
ComPtr<ISwapChainPanelNative>		pIPanelNative;

ComPtr<ID3D12DescriptorHeap>		pIDXRDescriptorHeap = {};



ComPtr<ID3D12Resource>				pIUAVOutputResource = {};



UINT64								nCurFrameIndex = 0;


UINT64								nDXRDescriptorSize = {};

static const WCHAR* DxilFoBinFileName = L"Shader\\DxrShader.Fo.bin";
static const WCHAR* kRayGenShader = L"RayGenerationShader";
static const WCHAR* kMissShader = L"MissShader";
static const WCHAR* kClosestHitShader = L"ClosestHitShader";
static const WCHAR* kHitGroup = L"HitGroup";
ComPtr<ID3D12StateObject>		pIDXRSO;

ComPtr<ID3D12Resource>	pIDxrShaderTable = {};
UINT64					nShaderTableEntrySize = 0;


ComPtr<ID3D12Resource>	pIUAVBufs;

const UINT64	nUploadHeapByteSize = MEMORY_UP_ALIGNMENT(
	1		// byte
	* 1024	// KB
	* 1024	// MB
	* 8		// 8MB
	, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
static UINT64	nUploadHeapUsedByteSize = 0;



const float							fAspectRatio = 3.0f;
const DirectX::XMFLOAT4				vertices[] = {
	DirectX::XMFLOAT4{
		-0.25f * fAspectRatio
		, -0.25f * fAspectRatio
		, 0.0f
		, 1.0f}
	,DirectX::XMFLOAT4{
		-0.25f * fAspectRatio
		, 0.25f * fAspectRatio
		, 0.0f
		, 1.0f}
	,DirectX::XMFLOAT4{
		0.25f * fAspectRatio
		, -0.25f * fAspectRatio
		, 0.0f
		, 1.0f}
	,DirectX::XMFLOAT4{
		0.25f * fAspectRatio
		, 0.25f * fAspectRatio
		, 0.0f
		, 1.0f}
};
const UINT64						verticesSize = sizeof(vertices);
ComPtr<ID3D12Resource>				pIVertexBuffer = {};
D3D12_VERTEX_BUFFER_VIEW			stVertexBufferView = {};
ComPtr<ID3D12Resource>				pIBottomLevelAS = {};
ComPtr<ID3D12Resource>				pITopLevelAS = {};

ComPtr<ID3D12RootSignature>	pIRSGlobal = {};

static const D3D12_HEAP_PROPERTIES stDefaultHeapProps =
{
	D3D12_HEAP_TYPE_DEFAULT
	, D3D12_CPU_PAGE_PROPERTY_UNKNOWN
	, D3D12_MEMORY_POOL_UNKNOWN
	, 0
	, 0
};

static const D3D12_HEAP_PROPERTIES stUploadHeapProps =
{
	D3D12_HEAP_TYPE_UPLOAD
	, D3D12_CPU_PAGE_PROPERTY_UNKNOWN
	, D3D12_MEMORY_POOL_UNKNOWN
	, 0
	, 0
};

WorkItemHandler^ WorkItemHandlerRenderLoop;
Windows::Foundation::IAsyncAction^ ActionRenderLoop;

MainPage::MainPage()
{
	InitializeComponent();



#if defined(_DEBUG)
#pragma region 在debug编译下，开启debug
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(
			D3D12GetDebugInterface(
				IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();
		}
	}
#pragma endregion
#endif



#pragma region 初始化 dxgi factory
	{
		UINT CreateDXGIFactory2Flag = 0;
#if defined(_DEBUG)
		CreateDXGIFactory2Flag = DXGI_CREATE_FACTORY_DEBUG;
#endif
		ThrowIfFailed(
			CreateDXGIFactory2(
				CreateDXGIFactory2Flag
				, IID_PPV_ARGS(&pIDXGIFactory7)));
	}
#pragma endregion



#pragma region 初始化 adapter
	{
		for (
			UINT adapterIndex = 0 // 设备序号
			; DXGI_ERROR_NOT_FOUND != pIDXGIFactory7->EnumAdapterByGpuPreference(
				adapterIndex
				, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
				, IID_PPV_ARGS(&pIAdapter4)) // 通过设备序号依次列举高性能设备
			; adapterIndex++
			)
		{
			DXGI_ADAPTER_DESC1 desc = {};
			pIAdapter4->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Don't select the Basic Render Driver adapter.
				continue;
			}

#pragma region 检测设备是否支持emD3DFeatureLevel的DX版本
			// 尝试创建设备
			if (FAILED(
				D3D12CreateDevice(
					pIAdapter4.Get()
					, emD3DFeatureLevel
					, _uuidof(ID3D12Device8), nullptr))) {
				continue;
			}
#pragma endregion



#pragma region 尝试创建设备并检查是否支持DXR1.0
			// 临时设备
			ComPtr<ID3D12Device8> pID3D12Device8Temp;

			// 尝试创建设备
			if (FAILED(
				D3D12CreateDevice(
					pIAdapter4.Get()
					, emD3DFeatureLevel
					, IID_PPV_ARGS(&pID3D12Device8Temp)))) {
				continue;
			}

			// 获取特性
			D3D12_FEATURE_DATA_D3D12_OPTIONS5 stFeatureSupportData = {};
			if (FAILED(pID3D12Device8Temp->CheckFeatureSupport(
				D3D12_FEATURE_D3D12_OPTIONS5
				, &stFeatureSupportData
				, sizeof(stFeatureSupportData))))
			{
				continue;
			}

			//检测硬件是否是直接支持DXR
			if (stFeatureSupportData.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
			{
				continue;
			}
#pragma endregion

			break;
		}
	}
#pragma endregion



#pragma region 通过adapter获取device
	{
		if (pIAdapter4 == nullptr)
		{
			throw ref new Platform::Exception(-1, "not find high gpu preference device");
		}

		ThrowIfFailed(D3D12CreateDevice(
			pIAdapter4.Get()
			, emD3DFeatureLevel
			, IID_PPV_ARGS(&pID3D12Device8)));
	}
#pragma endregion



#pragma region 通过device创建fence
	{
		ThrowIfFailed(pID3D12Device8->CreateFence(
			0
			, D3D12_FENCE_FLAG_NONE
			, IID_PPV_ARGS(&pIFence1)));
		FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	}
#pragma endregion



#pragma region 创建命令队列，命令分配器，命令列表
	{
		D3D12_COMMAND_QUEUE_DESC stCMDQueueDesc = {};
		stCMDQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		stCMDQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

		ThrowIfFailed(pID3D12Device8->CreateCommandQueue(
			&stCMDQueueDesc
			, IID_PPV_ARGS(&pICMDQueue)));

		ThrowIfFailed(pID3D12Device8->CreateCommandAllocator(
			stCMDQueueDesc.Type
			, IID_PPV_ARGS(&pICMDAlloc)));

		ThrowIfFailed(pID3D12Device8->CreateCommandList(
			0
			, stCMDQueueDesc.Type
			, pICMDAlloc.Get()
			, nullptr
			, IID_PPV_ARGS(&pICMDList6)));
	}
#pragma endregion



#pragma region 创建交换链
	{
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width					= iWidth;
		swapChainDesc.Height				= iHeight;
		swapChainDesc.Format				= emRenderTargetFormat;
		swapChainDesc.Stereo				= false;							// 未知
		swapChainDesc.SampleDesc.Count		= 1;
		swapChainDesc.SampleDesc.Quality	= 0;
		swapChainDesc.BufferUsage			= DXGI_USAGE_RENDER_TARGET_OUTPUT;	// 未知
		swapChainDesc.BufferCount			= nFrameBackBufCount;
		swapChainDesc.Scaling				= DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect			= DXGI_SWAP_EFFECT_FLIP_DISCARD;	// 未知
		swapChainDesc.AlphaMode				= DXGI_ALPHA_MODE_PREMULTIPLIED;
		swapChainDesc.Flags					= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		ThrowIfFailed(pIDXGIFactory7->CreateSwapChainForComposition(
			pICMDQueue.Get()
			, &swapChainDesc
			, nullptr
			, pISwapChain1.GetAddressOf()));
		ThrowIfFailed(pISwapChain1.As(&pISwapChain3));
		for (UINT i = 0; i < swapChainDesc.BufferCount; i++)
		{
			ThrowIfFailed(pISwapChain3->GetBuffer(i, IID_PPV_ARGS(&pISwapChainBuffer[i])));
		}

	}
#pragma endregion



#pragma region 设置SwapChainPanel
	{
		ThrowIfFailed(
			reinterpret_cast<IUnknown*>(swapChainPanel)->QueryInterface(IID_PPV_ARGS(&pIPanelNative))
		);
		ThrowIfFailed(pIPanelNative->SetSwapChain(pISwapChain3.Get()));
	}
#pragma endregion



#pragma region 通过device创建upload heap
	{
		D3D12_HEAP_DESC stUploadHeapDesc = {};
		stUploadHeapDesc.SizeInBytes = MEMORY_UP_ALIGNMENT(
			nUploadHeapByteSize
			, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
		stUploadHeapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
		stUploadHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		stUploadHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		stUploadHeapDesc.Properties.CreationNodeMask = 0; // 未知
		stUploadHeapDesc.Properties.VisibleNodeMask = 0; // 未知
		stUploadHeapDesc.Alignment = 0;
		stUploadHeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

		ThrowIfFailed(pID3D12Device8->CreateHeap(
			&stUploadHeapDesc
			, IID_PPV_ARGS(&pIUploadHeap)));
	}
#pragma endregion



#pragma region 使用定位方式创建顶点缓冲
	{
		D3D12_RESOURCE_DESC stVertexBufferResourceDesc = {};
		stVertexBufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		stVertexBufferResourceDesc.Alignment = 0;
		stVertexBufferResourceDesc.Width = verticesSize;
		stVertexBufferResourceDesc.Height = 1;
		stVertexBufferResourceDesc.DepthOrArraySize = 1;
		stVertexBufferResourceDesc.MipLevels = 1;
		stVertexBufferResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		stVertexBufferResourceDesc.SampleDesc.Count = 1;
		stVertexBufferResourceDesc.SampleDesc.Quality = 0;
		stVertexBufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		stVertexBufferResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		ThrowIfFailed(pID3D12Device8->CreatePlacedResource(
			pIUploadHeap.Get()
			, MEMORY_UP_ALIGNMENT(nUploadHeapUsedByteSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
			, &stVertexBufferResourceDesc
			, D3D12_RESOURCE_STATE_GENERIC_READ
			, nullptr
			, IID_PPV_ARGS(&pIVertexBuffer)));

		UINT8* pVertexDataBegin = nullptr;
		D3D12_RANGE	stVertexBufferRange = { 0,0 };

		ThrowIfFailed(pIVertexBuffer->Map(
			0
			, &stVertexBufferRange
			, reinterpret_cast<void**>(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, vertices, verticesSize);
		pIVertexBuffer->Unmap(0, nullptr);

		stVertexBufferView.BufferLocation = pIVertexBuffer->GetGPUVirtualAddress();
		stVertexBufferView.StrideInBytes = sizeof(DirectX::XMFLOAT4);
		stVertexBufferView.SizeInBytes = verticesSize;
	}
#pragma endregion



#pragma region 创建顶点缓冲加速结构
	{
		// bottom level as
		D3D12_RAYTRACING_GEOMETRY_DESC stBottomLevelGeomDesc = {};
		stBottomLevelGeomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		stBottomLevelGeomDesc.Triangles.VertexBuffer.StartAddress = pIVertexBuffer->GetGPUVirtualAddress();
		stBottomLevelGeomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(DirectX::XMFLOAT4);
		stBottomLevelGeomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		stBottomLevelGeomDesc.Triangles.VertexCount = 3;
		stBottomLevelGeomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS stBottomLevelInputs = {};
		stBottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		stBottomLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		stBottomLevelInputs.NumDescs = 1;
		stBottomLevelInputs.pGeometryDescs = &stBottomLevelGeomDesc;
		stBottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO stBottomLevelASPrebuildInfo = {};
		pID3D12Device8->GetRaytracingAccelerationStructurePrebuildInfo(
			&stBottomLevelInputs
			, &stBottomLevelASPrebuildInfo);

		ComPtr<ID3D12Resource> pIBottomLevelScratch = {};
		D3D12_RESOURCE_DESC stBottomLevelScratchResourceDesc = {};
		stBottomLevelScratchResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		stBottomLevelScratchResourceDesc.Alignment = 0;
		stBottomLevelScratchResourceDesc.Width = stBottomLevelASPrebuildInfo.ScratchDataSizeInBytes;
		stBottomLevelScratchResourceDesc.Height = 1;
		stBottomLevelScratchResourceDesc.DepthOrArraySize = 1;
		stBottomLevelScratchResourceDesc.MipLevels = 1;
		stBottomLevelScratchResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		stBottomLevelScratchResourceDesc.SampleDesc.Count = 1;
		stBottomLevelScratchResourceDesc.SampleDesc.Quality = 0;
		stBottomLevelScratchResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		stBottomLevelScratchResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		ThrowIfFailed(pID3D12Device8->CreateCommittedResource(
			&stDefaultHeapProps
			, D3D12_HEAP_FLAG_NONE
			, &stBottomLevelScratchResourceDesc
			, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			, nullptr
			, IID_PPV_ARGS(&pIBottomLevelScratch)
		));

		//ComPtr<ID3D12Resource> pIBottomLevelResult	= {};
		D3D12_RESOURCE_DESC stBottomLevelResultResourceDesc = {};
		stBottomLevelResultResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		stBottomLevelResultResourceDesc.Alignment = 0;
		stBottomLevelResultResourceDesc.Width = stBottomLevelASPrebuildInfo.ResultDataMaxSizeInBytes;
		stBottomLevelResultResourceDesc.Height = 1;
		stBottomLevelResultResourceDesc.DepthOrArraySize = 1;
		stBottomLevelResultResourceDesc.MipLevels = 1;
		stBottomLevelResultResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		stBottomLevelResultResourceDesc.SampleDesc.Count = 1;
		stBottomLevelResultResourceDesc.SampleDesc.Quality = 0;
		stBottomLevelResultResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		stBottomLevelResultResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		ThrowIfFailed(pID3D12Device8->CreateCommittedResource(
			&stDefaultHeapProps
			, D3D12_HEAP_FLAG_NONE
			, &stBottomLevelResultResourceDesc
			, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
			, nullptr
			, IID_PPV_ARGS(&pIBottomLevelAS)
		));



		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC stBottomLevelASDesc = {};
		stBottomLevelASDesc.DestAccelerationStructureData = pIBottomLevelAS->GetGPUVirtualAddress();
		stBottomLevelASDesc.Inputs = stBottomLevelInputs;
		stBottomLevelASDesc.ScratchAccelerationStructureData = pIBottomLevelScratch->GetGPUVirtualAddress();
		pICMDList6->BuildRaytracingAccelerationStructure(
			&stBottomLevelASDesc
			, 0
			, nullptr);

		D3D12_RESOURCE_BARRIER BottomLevelASUavBarrier = {};
		BottomLevelASUavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		BottomLevelASUavBarrier.UAV.pResource = pIBottomLevelAS.Get();
		pICMDList6->ResourceBarrier(
			1
			, &BottomLevelASUavBarrier);

		// top level as
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS stTopLevelInputs = {};
		stTopLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		stTopLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		stTopLevelInputs.NumDescs = 1;
		stTopLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO stTopLevelASPrebuildInfo = {};
		pID3D12Device8->GetRaytracingAccelerationStructurePrebuildInfo(
			&stTopLevelInputs
			, &stTopLevelASPrebuildInfo);

		UINT64 nTopLevelTlasSize = 0;
		nTopLevelTlasSize = stTopLevelASPrebuildInfo.ResultDataMaxSizeInBytes;

		ComPtr<ID3D12Resource> pITopLevelScratch = {};
		D3D12_RESOURCE_DESC stTopLevelScratchResourceDesc = {};
		stTopLevelScratchResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		stTopLevelScratchResourceDesc.Alignment = 0;
		stTopLevelScratchResourceDesc.Width = stTopLevelASPrebuildInfo.ScratchDataSizeInBytes;
		stTopLevelScratchResourceDesc.Height = 1;
		stTopLevelScratchResourceDesc.DepthOrArraySize = 1;
		stTopLevelScratchResourceDesc.MipLevels = 1;
		stTopLevelScratchResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		stTopLevelScratchResourceDesc.SampleDesc.Count = 1;
		stTopLevelScratchResourceDesc.SampleDesc.Quality = 0;
		stTopLevelScratchResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		stTopLevelScratchResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		ThrowIfFailed(pID3D12Device8->CreateCommittedResource(
			&stDefaultHeapProps
			, D3D12_HEAP_FLAG_NONE
			, &stTopLevelScratchResourceDesc
			, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			, nullptr
			, IID_PPV_ARGS(&pITopLevelScratch)
		));

		//ComPtr<ID3D12Resource> pITopLevelResult = {};
		D3D12_RESOURCE_DESC stTopLevelResultResourceDesc = {};
		stTopLevelResultResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		stTopLevelResultResourceDesc.Alignment = 0;
		stTopLevelResultResourceDesc.Width = stTopLevelASPrebuildInfo.ResultDataMaxSizeInBytes;
		stTopLevelResultResourceDesc.Height = 1;
		stTopLevelResultResourceDesc.DepthOrArraySize = 1;
		stTopLevelResultResourceDesc.MipLevels = 1;
		stTopLevelResultResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		stTopLevelResultResourceDesc.SampleDesc.Count = 1;
		stTopLevelResultResourceDesc.SampleDesc.Quality = 0;
		stTopLevelResultResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		stTopLevelResultResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		ThrowIfFailed(pID3D12Device8->CreateCommittedResource(
			&stDefaultHeapProps
			, D3D12_HEAP_FLAG_NONE
			, &stTopLevelResultResourceDesc
			, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
			, nullptr
			, IID_PPV_ARGS(&pITopLevelAS)
		));


		ComPtr<ID3D12Resource> pITopLevelInstance = {};
		D3D12_RESOURCE_DESC stTopLevelInstanceResourceDesc = {};
		stTopLevelInstanceResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		stTopLevelInstanceResourceDesc.Alignment = 0;
		stTopLevelInstanceResourceDesc.Width = sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
		stTopLevelInstanceResourceDesc.Height = 1;
		stTopLevelInstanceResourceDesc.DepthOrArraySize = 1;
		stTopLevelInstanceResourceDesc.MipLevels = 1;
		stTopLevelInstanceResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		stTopLevelInstanceResourceDesc.SampleDesc.Count = 1;
		stTopLevelInstanceResourceDesc.SampleDesc.Quality = 0;
		stTopLevelInstanceResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		stTopLevelInstanceResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		ThrowIfFailed(pID3D12Device8->CreateCommittedResource(
			&stUploadHeapProps
			, D3D12_HEAP_FLAG_NONE
			, &stTopLevelInstanceResourceDesc
			, D3D12_RESOURCE_STATE_GENERIC_READ
			, nullptr
			, IID_PPV_ARGS(&pITopLevelInstance)
		));

		D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDesc = {};
		pITopLevelInstance->Map(
			0
			, nullptr
			, reinterpret_cast<void**>(&pInstanceDesc));
		pInstanceDesc->InstanceID = 0;
		pInstanceDesc->InstanceContributionToHitGroupIndex = 0;
		pInstanceDesc->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		//mat4 m; // Identity matrix
		DirectX::XMFLOAT4 m[3] = {
			{1,0,0,0}
			,{0,1,0,0}
			,{0,0,1,0}
		};
		memcpy(
			pInstanceDesc->Transform
			, &m
			, sizeof(pInstanceDesc->Transform));
		pInstanceDesc->AccelerationStructure = pIBottomLevelAS->GetGPUVirtualAddress();
		pInstanceDesc->InstanceMask = 0xFF;
		pITopLevelInstance->Unmap(
			0
			, nullptr);



		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC stTopLevelASDesc = {};
		stTopLevelASDesc.DestAccelerationStructureData = pITopLevelAS->GetGPUVirtualAddress();
		stTopLevelASDesc.Inputs = stTopLevelInputs;
		stTopLevelASDesc.Inputs.InstanceDescs = pITopLevelInstance->GetGPUVirtualAddress();
		stTopLevelASDesc.ScratchAccelerationStructureData = pITopLevelScratch->GetGPUVirtualAddress();
		pICMDList6->BuildRaytracingAccelerationStructure(
			&stTopLevelASDesc
			, 0
			, nullptr);

		D3D12_RESOURCE_BARRIER TopLevelASUavBarrier = {};
		TopLevelASUavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		TopLevelASUavBarrier.UAV.pResource = pITopLevelAS.Get();
		pICMDList6->ResourceBarrier(
			1
			, &TopLevelASUavBarrier);


		pICMDList6->Close();
		ID3D12CommandList* pGraphicsList = pICMDList6.Get();
		pICMDQueue->ExecuteCommandLists(1, &pGraphicsList);
		nFenceValue++;
		pICMDQueue->Signal(
			pIFence1.Get()
			, nFenceValue);
		pIFence1->SetEventOnCompletion(
			nFenceValue
			, FenceEvent);
		WaitForSingleObject(
			FenceEvent
			, INFINITE);

		pICMDList6->Reset(pICMDAlloc.Get(), nullptr);

	}
#pragma endregion



#pragma region 创建 DXR描述符堆
	{
		D3D12_DESCRIPTOR_HEAP_DESC stDXRDescriptorHeapDesc = {};
		stDXRDescriptorHeapDesc.NumDescriptors	= 2;
		stDXRDescriptorHeapDesc.Type			= D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		stDXRDescriptorHeapDesc.Flags			= D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		ThrowIfFailed(pID3D12Device8->CreateDescriptorHeap(
			&stDXRDescriptorHeapDesc
			, IID_PPV_ARGS(&pIDXRDescriptorHeap)));

		nDXRDescriptorSize = pID3D12Device8->GetDescriptorHandleIncrementSize(stDXRDescriptorHeapDesc.Type);
	}
#pragma endregion



#pragma region 创建 UAV 并加入 DXR描述符堆
	{
		D3D12_RESOURCE_DESC stOutputResourceDesc = {};
		stOutputResourceDesc.Dimension			= D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		stOutputResourceDesc.Alignment			= 0;
		stOutputResourceDesc.Width				= iWidth;
		stOutputResourceDesc.Height				= iHeight;
		stOutputResourceDesc.DepthOrArraySize	= 1;
		stOutputResourceDesc.MipLevels			= 1;
		stOutputResourceDesc.Format				= emRenderTargetFormat; // The backbuffer is actually DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, but sRGB formats can't be used with UAVs. We will convert to sRGB ourselves in the shader
		stOutputResourceDesc.SampleDesc.Count	= 1;
		stOutputResourceDesc.SampleDesc.Quality	= 0;
		stOutputResourceDesc.Layout				= D3D12_TEXTURE_LAYOUT_UNKNOWN;
		stOutputResourceDesc.Flags				= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		ThrowIfFailed(pID3D12Device8->CreateCommittedResource(
			&stDefaultHeapProps
			, D3D12_HEAP_FLAG_NONE
			, &stOutputResourceDesc
			, D3D12_RESOURCE_STATE_COPY_SOURCE
			, nullptr
			, IID_PPV_ARGS(&pIUAVOutputResource)));

		D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
		UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		pID3D12Device8->CreateUnorderedAccessView(
			pIUAVOutputResource.Get()
			, nullptr
			, &UAVDesc
			, pIDXRDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	}
#pragma endregion



#pragma region 创建 SRV 并加入 DXR描述符堆
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.RaytracingAccelerationStructure.Location = pITopLevelAS->GetGPUVirtualAddress();

		D3D12_CPU_DESCRIPTOR_HANDLE DXRDescriptorHeapHandle = pIDXRDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		DXRDescriptorHeapHandle.ptr += nDXRDescriptorSize;
		pID3D12Device8->CreateShaderResourceView(nullptr, &srvDesc, DXRDescriptorHeapHandle);
	}
#pragma endregion



#pragma region 创建渲染管线
	{
		std::array<D3D12_STATE_SUBOBJECT, 10> Subobjects = {};
		static const UINT SubObjectsIndexDxil = 0;
		static const UINT SubObjectsIndexHitGroup = 1;
		static const UINT SubObjectsIndexUnknow1 = 2;
		static const UINT SubObjectsIndexUnknow11 = 3;
		static const UINT SubObjectsIndexUnknow2 = 4;
		static const UINT SubObjectsIndexUnknow21 = 5;
		static const UINT SubObjectsIndexShaderConfig = 6;
		static const UINT SubObjectsIndexUnknow3 = 7;
		static const UINT SubObjectsIndexPipelineConfig = 8;
		static const UINT SubObjectsIndexRootSignature = 9;

		// 0 dxil
#pragma region 创建DXIL库
		// 1
		const WCHAR* DxilLibExports[] = {
			kRayGenShader
			, kMissShader
			, kClosestHitShader };

		D3D12_EXPORT_DESC ExportDesc[ARRAYSIZE(DxilLibExports)] = {};

		for (uint32_t i = 0; i < ARRAYSIZE(DxilLibExports); i++)
		{
			ExportDesc[i].Name = DxilLibExports[i];
			ExportDesc[i].Flags = D3D12_EXPORT_FLAG_NONE;
			ExportDesc[i].ExportToRename = nullptr;
		}

		// 2
		D3D12_DXIL_LIBRARY_DESC stdxilLibDesc = {};
		stdxilLibDesc.DXILLibrary.pShaderBytecode = dxil;
		stdxilLibDesc.DXILLibrary.BytecodeLength = sizeof(dxil);
		stdxilLibDesc.NumExports = ARRAYSIZE(DxilLibExports);
		stdxilLibDesc.pExports = ExportDesc;

		// 3
		D3D12_STATE_SUBOBJECT stDXILLibSubobject = {};
		stDXILLibSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		stDXILLibSubobject.pDesc = &stdxilLibDesc;

		Subobjects[SubObjectsIndexDxil] = stDXILLibSubobject;
#pragma endregion

		// 1 hit group
#pragma region hit group
		D3D12_HIT_GROUP_DESC stHitGroupDesc = {};
		stHitGroupDesc.AnyHitShaderImport = nullptr;
		stHitGroupDesc.ClosestHitShaderImport = kClosestHitShader;
		stHitGroupDesc.HitGroupExport = kHitGroup;

		D3D12_STATE_SUBOBJECT stHitGroupStateSubobject = {};
		stHitGroupStateSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
		stHitGroupStateSubobject.pDesc = &stHitGroupDesc;

		Subobjects[SubObjectsIndexHitGroup] = stHitGroupStateSubobject;
#pragma endregion

		// 2 未知！！！有关于root signature
#pragma region
		std::array<D3D12_DESCRIPTOR_RANGE, 2> stRange = {};

		// gOutput todo 未知
		// 问题点
		// 1. 为什么指定为 D3D12_DESCRIPTOR_RANGE_TYPE_UAV
		// 2. 为什么OffsetInDescriptorsFromTableStart 为0
		// 在Consts.hlsl中存在gOutput
		stRange[0].BaseShaderRegister = 0;
		stRange[0].NumDescriptors = 1;
		stRange[0].RegisterSpace = 0;
		stRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		stRange[0].OffsetInDescriptorsFromTableStart = 0;

		// gRtScene todo 未知
		// 问题点
		// 1. 为什么指定为 D3D12_DESCRIPTOR_RANGE_TYPE_SRV
		// 2. 为什么OffsetInDescriptorsFromTableStart 为1
		// 在Consts.hlsl中存在gRtScene
		stRange[1].BaseShaderRegister = 0;
		stRange[1].NumDescriptors = 1;
		stRange[1].RegisterSpace = 0;
		stRange[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		stRange[1].OffsetInDescriptorsFromTableStart = 1;

		//
		std::array<D3D12_ROOT_PARAMETER, 1> stRootParameters = {};
		stRootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		stRootParameters[0].DescriptorTable.NumDescriptorRanges = UINT(stRange.size());
		stRootParameters[0].DescriptorTable.pDescriptorRanges = stRange.data();

		//
		D3D12_ROOT_SIGNATURE_DESC stRayGenerationLocalSignatureDesc = {};
		stRayGenerationLocalSignatureDesc.NumParameters = UINT(stRootParameters.size());
		stRayGenerationLocalSignatureDesc.pParameters = stRootParameters.data();
		stRayGenerationLocalSignatureDesc.NumStaticSamplers = 0;
		stRayGenerationLocalSignatureDesc.pStaticSamplers = nullptr;
		stRayGenerationLocalSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

		ComPtr<ID3DBlob> pSignatureBlobA;
		ComPtr<ID3DBlob> pErrorBlobA;
		ThrowIfFailed(D3D12SerializeRootSignature(
			&stRayGenerationLocalSignatureDesc
			, D3D_ROOT_SIGNATURE_VERSION_1
			, &pSignatureBlobA
			, &pErrorBlobA));

		ComPtr<ID3D12RootSignature> pRootSignature = {};
		ThrowIfFailed(pID3D12Device8->CreateRootSignature(
			0
			, pSignatureBlobA->GetBufferPointer()
			, pSignatureBlobA->GetBufferSize()
			, IID_PPV_ARGS(&pRootSignature)));

		D3D12_STATE_SUBOBJECT stRayGenerationLocalSignatureStateSubobjectA = {};
		stRayGenerationLocalSignatureStateSubobjectA.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
		stRayGenerationLocalSignatureStateSubobjectA.pDesc = pRootSignature.GetAddressOf();

		Subobjects[SubObjectsIndexUnknow1] = stRayGenerationLocalSignatureStateSubobjectA;
#pragma endregion

		// 3 未知！！！有关于 2 root signature的导出
#pragma region MyRegion
		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION stA = {};
		stA.NumExports = 1;
		stA.pExports = &kRayGenShader;
		stA.pSubobjectToAssociate = &(Subobjects[SubObjectsIndexUnknow1]);

		D3D12_STATE_SUBOBJECT stRayGenerationLocalSignatureStateSubobjectB = {};
		stRayGenerationLocalSignatureStateSubobjectB.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
		stRayGenerationLocalSignatureStateSubobjectB.pDesc = &stA;

		Subobjects[SubObjectsIndexUnknow11] = stRayGenerationLocalSignatureStateSubobjectB;
#pragma endregion

		// 4 未知！！！有关于root signature
#pragma region
		D3D12_ROOT_SIGNATURE_DESC emptyDesc = {};
		emptyDesc.NumParameters = 0;
		emptyDesc.pParameters = nullptr;
		emptyDesc.NumStaticSamplers = 0;
		emptyDesc.pStaticSamplers = nullptr;
		emptyDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

		ComPtr<ID3DBlob> pSignatureBlobB;
		ComPtr<ID3DBlob> pErrorBlobB;
		ThrowIfFailed(D3D12SerializeRootSignature(
			&emptyDesc
			, D3D_ROOT_SIGNATURE_VERSION_1
			, &pSignatureBlobB
			, &pErrorBlobB));

		ComPtr<ID3D12RootSignature> pIEmptyRootSignature;
		ThrowIfFailed(pID3D12Device8->CreateRootSignature(
			0
			, pSignatureBlobB->GetBufferPointer()
			, pSignatureBlobB->GetBufferSize()
			, IID_PPV_ARGS(&pIEmptyRootSignature)));

		D3D12_STATE_SUBOBJECT stEmptySignatureStateSubobject = {};
		stEmptySignatureStateSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
		stEmptySignatureStateSubobject.pDesc = pIEmptyRootSignature.GetAddressOf();

		Subobjects[SubObjectsIndexUnknow2] = stEmptySignatureStateSubobject;
#pragma endregion

		// 5 未知！！！有关于 4 root signature的导出
#pragma region
		const WCHAR* missHitExportName[] = {
			kMissShader
			, kClosestHitShader };

		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION stB = {};
		stB.pSubobjectToAssociate = &(Subobjects[SubObjectsIndexUnknow2]);
		stB.NumExports = ARRAYSIZE(missHitExportName);
		stB.pExports = missHitExportName;

		D3D12_STATE_SUBOBJECT stRayGenerationLocalSignatureStateSubobject = {};
		stRayGenerationLocalSignatureStateSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
		stRayGenerationLocalSignatureStateSubobject.pDesc = &stB;

		Subobjects[SubObjectsIndexUnknow21] = stRayGenerationLocalSignatureStateSubobject;
#pragma endregion

		// 6 shader config
#pragma region 创建share config
		D3D12_RAYTRACING_SHADER_CONFIG stRaytracingShaderConfig = {};
		stRaytracingShaderConfig.MaxPayloadSizeInBytes = sizeof(float) * 3;
		stRaytracingShaderConfig.MaxAttributeSizeInBytes = sizeof(float) * 2;

		D3D12_STATE_SUBOBJECT stRayTracingShaderConfigSubobject = {};
		stRayTracingShaderConfigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
		stRayTracingShaderConfigSubobject.pDesc = &stRaytracingShaderConfig;

		Subobjects[SubObjectsIndexShaderConfig] = stRayTracingShaderConfigSubobject;
#pragma endregion


		// 7 未知！！！
#pragma region
		const WCHAR* ShareConfigSubobjectExportsAssociationExportName[] = {
			kMissShader
			, kClosestHitShader
			, kRayGenShader };

		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION stShaderConfigSubobjectExportsAssociation = {};
		stShaderConfigSubobjectExportsAssociation.pSubobjectToAssociate = &(Subobjects[SubObjectsIndexShaderConfig]);
		stShaderConfigSubobjectExportsAssociation.NumExports = ARRAYSIZE(ShareConfigSubobjectExportsAssociationExportName);
		stShaderConfigSubobjectExportsAssociation.pExports = ShareConfigSubobjectExportsAssociationExportName;

		D3D12_STATE_SUBOBJECT stShaderConfigSubobjectExportsAssociationSubobject = {};
		stShaderConfigSubobjectExportsAssociationSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
		stShaderConfigSubobjectExportsAssociationSubobject.pDesc = &stShaderConfigSubobjectExportsAssociation;

		Subobjects[SubObjectsIndexUnknow3] = stShaderConfigSubobjectExportsAssociationSubobject;
#pragma endregion

		// 8 pipeline config
#pragma region 创建pipeline config
		D3D12_RAYTRACING_PIPELINE_CONFIG stRaytracingPipelineConfig = {};
		stRaytracingPipelineConfig.MaxTraceRecursionDepth = 1;

		D3D12_STATE_SUBOBJECT stPipelineShaderConfigSubobject = {};
		stPipelineShaderConfigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
		stPipelineShaderConfigSubobject.pDesc = &stRaytracingPipelineConfig;

		Subobjects[SubObjectsIndexPipelineConfig] = stPipelineShaderConfigSubobject;
#pragma endregion

		// 9 root signature
#pragma region 创建全局root signature
		D3D12_ROOT_SIGNATURE_DESC stGlobalRootSignatureDesc = {};

		ComPtr<ID3DBlob> pSignatureBlobC;
		ComPtr<ID3DBlob> pErrorBlobC;
		ThrowIfFailed(D3D12SerializeRootSignature(
			&stGlobalRootSignatureDesc
			, D3D_ROOT_SIGNATURE_VERSION_1
			, &pSignatureBlobC
			, &pErrorBlobC));

		ThrowIfFailed(pID3D12Device8->CreateRootSignature(
			0
			, pSignatureBlobC->GetBufferPointer()
			, pSignatureBlobC->GetBufferSize()
			, IID_PPV_ARGS(&pIRSGlobal)));

		D3D12_STATE_SUBOBJECT stGlobalRootSignatureSubobject = {};
		stGlobalRootSignatureSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
		stGlobalRootSignatureSubobject.pDesc = pIRSGlobal.GetAddressOf();

		Subobjects[SubObjectsIndexRootSignature] = stGlobalRootSignatureSubobject;
#pragma endregion

		D3D12_STATE_OBJECT_DESC stDXRPSODesc = {};
		stDXRPSODesc.NumSubobjects = UINT(Subobjects.size());
		stDXRPSODesc.pSubobjects = Subobjects.data();
		stDXRPSODesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

		ThrowIfFailed(pID3D12Device8->CreateStateObject(
			&stDXRPSODesc
			, IID_PPV_ARGS(&pIDXRSO)));
	}
#pragma endregion



#pragma region 创建shader table
	{
		nShaderTableEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		nShaderTableEntrySize += 8; // The ray-gen's descriptor table
		nShaderTableEntrySize = MEMORY_UP_ALIGNMENT(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, nShaderTableEntrySize);
		UINT64 UShaderTableSize = nShaderTableEntrySize * 3;

		D3D12_RESOURCE_DESC stShaderTableBufferDesc = {};
		stShaderTableBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		stShaderTableBufferDesc.Alignment = 0;
		stShaderTableBufferDesc.Width = UShaderTableSize;
		stShaderTableBufferDesc.Height = 1;
		stShaderTableBufferDesc.DepthOrArraySize = 1;
		stShaderTableBufferDesc.MipLevels = 1;
		stShaderTableBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
		stShaderTableBufferDesc.SampleDesc.Count = 1;
		stShaderTableBufferDesc.SampleDesc.Quality = 0;
		stShaderTableBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		stShaderTableBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		pID3D12Device8->CreateCommittedResource(
			&stUploadHeapProps
			, D3D12_HEAP_FLAG_NONE
			, &stShaderTableBufferDesc
			, D3D12_RESOURCE_STATE_GENERIC_READ
			, nullptr
			, IID_PPV_ARGS(&pIDxrShaderTable));

		uint8_t* pData;
		pIDxrShaderTable->Map(0, nullptr, (void**)&pData);

		ComPtr<ID3D12StateObjectProperties> pIDXRSOProperties;
		pIDXRSO->QueryInterface(IID_PPV_ARGS(&pIDXRSOProperties));

		// 1 ray generation
		memcpy(
			pData
			, pIDXRSOProperties->GetShaderIdentifier(kRayGenShader)
			, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);


		// 2 miss
		memcpy(
			pData + nShaderTableEntrySize
			, pIDXRSOProperties->GetShaderIdentifier(kMissShader)
			, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

		// 3 closest hit
		memcpy(
			pData + nShaderTableEntrySize * 2
			, pIDXRSOProperties->GetShaderIdentifier(kHitGroup)
			, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

		pIDxrShaderTable->Unmap(0, nullptr);
	}
#pragma endregion




#pragma region 渲染循环
	WorkItemHandlerRenderLoop = ref new WorkItemHandler([this](Windows::Foundation::IAsyncAction^ action)
		{
			while (true)
			{
				switch (action->Status)
				{
				case Windows::Foundation::AsyncStatus::Started:
				{
					//OutputDebugString(L"Windows::Foundation::AsyncStatus::Started\n");
					// Prepare the command list for the next frame
					UINT bufferIndex = pISwapChain3->GetCurrentBackBufferIndex();

					// 1
					ID3D12DescriptorHeap* heaps[] = { pIDXRDescriptorHeap.Get() };
					pICMDList6->SetDescriptorHeaps(ARRAYSIZE(heaps), heaps);
					UINT8 nCurrentBackBufferIndex = pISwapChain3->GetCurrentBackBufferIndex();

					// 2
					D3D12_RESOURCE_BARRIER barrierA = {};
					barrierA.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					barrierA.Transition.pResource = pIUAVOutputResource.Get();
					barrierA.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
					barrierA.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
					barrierA.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
					pICMDList6->ResourceBarrier(
						1
						, &barrierA);

					// 3
					D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
					raytraceDesc.Width = iWidth;
					raytraceDesc.Height = iHeight;
					raytraceDesc.Depth = 1;
					// RayGen is the first entry in the shader-table
					raytraceDesc.RayGenerationShaderRecord.StartAddress = pIDxrShaderTable->GetGPUVirtualAddress() + 0 * nShaderTableEntrySize;
					raytraceDesc.RayGenerationShaderRecord.SizeInBytes = nShaderTableEntrySize;
					// Miss is the second entry in the shader-table
					size_t missOffset = 1 * nShaderTableEntrySize;
					raytraceDesc.MissShaderTable.StartAddress = pIDxrShaderTable->GetGPUVirtualAddress() + missOffset;
					raytraceDesc.MissShaderTable.StrideInBytes = nShaderTableEntrySize;
					raytraceDesc.MissShaderTable.SizeInBytes = nShaderTableEntrySize;
					// Hit is the third entry in the shader-table
					size_t hitOffset = 2 * nShaderTableEntrySize;
					raytraceDesc.HitGroupTable.StartAddress = pIDxrShaderTable->GetGPUVirtualAddress() + hitOffset;
					raytraceDesc.HitGroupTable.StrideInBytes = nShaderTableEntrySize;
					raytraceDesc.HitGroupTable.SizeInBytes = nShaderTableEntrySize;

					// 4
					pICMDList6->SetComputeRootSignature(pIRSGlobal.Get());

					//5
					pICMDList6->SetPipelineState1(pIDXRSO.Get());
					pICMDList6->DispatchRays(&raytraceDesc);

					// 6
					D3D12_RESOURCE_BARRIER barrierB = {};
					barrierB.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					barrierB.Transition.pResource = pIUAVOutputResource.Get();
					barrierB.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
					barrierB.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
					barrierB.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
					pICMDList6->ResourceBarrier(
						1
						, &barrierB);



					// 7
					D3D12_RESOURCE_BARRIER barrierC = {};
					barrierC.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					barrierC.Transition.pResource = pISwapChainBuffer[bufferIndex].Get();
					barrierC.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
					barrierC.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
					barrierC.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
					pICMDList6->ResourceBarrier(
						1
						, &barrierC);
					pICMDList6->CopyResource(
						pISwapChainBuffer[bufferIndex].Get()
						, pIUAVOutputResource.Get());

					// 8
					D3D12_RESOURCE_BARRIER barrierD = {};
					barrierD.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					barrierD.Transition.pResource = pISwapChainBuffer[bufferIndex].Get();
					barrierD.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
					barrierD.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
					barrierD.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
					pICMDList6->ResourceBarrier(1, &barrierD);

					// 9
					pICMDList6->Close();
					ID3D12CommandList* pIGraphicsList[] = { pICMDList6.Get() };
					pICMDQueue->ExecuteCommandLists(
						1
						, pIGraphicsList);
					nFenceValue++;
					pICMDQueue->Signal(pIFence1.Get(), nFenceValue);
				
					// 10
					pISwapChain3->Present(1, 0);



					// 11
					pIFence1->SetEventOnCompletion(
						nFenceValue
						, FenceEvent);
					WaitForSingleObject(FenceEvent, INFINITE);

					pICMDAlloc->Reset();
					pICMDList6->Reset(
						pICMDAlloc.Get()
						, nullptr);

					break;
				}
				case Windows::Foundation::AsyncStatus::Completed:
				{
					//OutputDebugString(L"Windows::Foundation::AsyncStatus::Completed\n");
					break;
				}
				case Windows::Foundation::AsyncStatus::Canceled:
				{
					//OutputDebugString(L"Windows::Foundation::AsyncStatus::Canceled\n");
					break;
				}
				case Windows::Foundation::AsyncStatus::Error:
				{
					//OutputDebugString(L"Windows::Foundation::AsyncStatus::Error\n");
					break;
				}
				default:
				{
					break;
				}
				}
			}
		}
	);
#pragma endregion

	ActionRenderLoop = ThreadPool::RunAsync(
		WorkItemHandlerRenderLoop
		, WorkItemPriority::High
		, WorkItemOptions::TimeSliced);

}