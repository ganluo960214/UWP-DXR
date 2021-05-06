# UWP-DXR

在UWP中使用DXR1.0

为在没有任何D3D经验的情况下，入门DXR

入门DXR需要先了解的内容：[COM](https://docs.microsoft.com/en-us/windows/win32/com/component-object-model--com--portal),[DXGI](https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi)，[D3D12](https://docs.microsoft.com/en-us/windows/win32/direct3d12/what-is-directx-12-)，[DXR](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)

### COM

#### 概览

COM是微软用于软件跨软件的兼容性API技术，能用让软件在不引用其他软件源码的情况下使用其他软件实现COM API提供的功能，DXR接口均是使用COM实现，所以本项目在不含有DXR源码的情况下可使用DXR的接口实现功能。并且可以借助ComPtr对COM对象进行生命周期的管理。

### DXGI

#### 使用方式

`#include <dxgi1_6.h>`

#### 概览

DXGI的目的是让应用程序能沟通内核驱动和硬件。项目中使用了DXGI提供的两个接口：[EnumAdapterByGpuPreference](https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_6/nf-dxgi1_6-idxgifactory6-enumadapterbygpupreference)，[CreateSwapChainForComposition](https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition)。

##### 通过CreateSwapChainForComposition可以获得交换链（SwapChain）
> CreateSwapChainForComposition创建DirectComposition API的交换链
>> 可以将Direct3D内容发送到DirectComposition API或Windows.UI.Xaml框架
>>> 本项目使用的是Windows.UI.Xaml

交换链是一套逻辑：一个交换链中必须有两个或两个以上的交换链缓冲区（SwapChainBuffer），交替呈现交换链缓冲区内容。

> 缓冲区可以想象为一帧的画面

> 当交替呈现交换链缓冲区内容时一定会有一个交换链缓冲区内容不会被呈现，不会被呈现的交换链缓冲区内容称之为`后台缓冲区`，正在被呈现的交换链缓冲区称为`前台缓冲区`。

> 这样设计的目的是：当前台缓冲区被呈现的时候，我们可以往后台缓冲区写入内容，当后台缓冲区内容写入完成后再呈现，那么前台缓冲区就变成后台缓冲区，再往后台缓冲区写入内容再呈现，如此循环往复。
>> 如果仅使用一个交换链缓冲区可能会出现读写并发问题，可以想象成一副画上半部分

##### 通过EnumAdapterByGpuPreference可以获得设备适配器（adapter）



### D3D12

### DXR

DXR为D3D12的一个分支。

### 额外内容

#### ComPtr
[ComPtr](https://docs.microsoft.com/en-us/cpp/cppcx/wrl/comptr-class?view=msvc-160)是微软运行时库提供给用户辅助管理COM对象生命周期的。

#### HLSL

[hlsl](https://docs.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl)是微软的高级着色器语言

#### DXC

[dxc](https://github.com/microsoft/DirectXShaderCompiler)是微软新一代的高级着色器语言编译器，通过dxc编译后的文件给DirectX或Vaulkan使用。

由于UWP的特殊性，暂时无法使用官方提供的*.dll文件，导致UWP无法在运行时编译HLSL文件。仅能使用dxc编译后的二进制文件（-Fo）或者使用头文件（-Fh）。个人推荐使用二进制文件。

编译HLSL到二进制文件：
```cmd 
dxc -T lib_6_5 -Fo *\*.Fo.bin *\*.hlsl

-T lib_6_5     指定描述文件版本进行编译
-Fo *\*.Fo.bin 指定编译后的文件为二进制，并指定二进制文件输出目录
*\*.hlsl       指定编译文件
```

