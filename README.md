# UWP-DXR

在UWP中使用DXR


## 额外内容

### [dxc](https://github.com/microsoft/DirectXShaderCompiler) 

微软新一代的高级着色器语言编译器，通过dxc编译后的文件给DirectX或Vaulkan使用。

由于UWP的特殊性，暂时无法使用官方提供的*.dll文件，在UWP运行时编译HLSL文件。仅能使用dxc编译后的二进制文件（-Fo）或者使用头文件（-Fh）。个人推荐使用二进制文件。

编译HLSL到二进制文件：
```cmd
dxc -T lib_6_5 -Fo *\*.Fo.bin *\*.hlsl

-T lib_6_5     指定描述文件版本进行编译
-Fo *\*.Fo.bin 指定编译后的文件为二进制，并指定二进制文件输出目录
*\*.hlsl       指定编译文件
```

