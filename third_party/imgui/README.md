# ImGui 集成说明

## 下载

从 GitHub 下载 ImGui 源码：

```
https://github.com/ocornut/imgui
```

或使用 Git：

```bash
git clone https://github.com/ocornut/imgui.git
```

## 需要的文件

将以下文件复制到此目录：

```
third_party/imgui/
├── imgui.cpp
├── imgui.h
├── imgui_demo.cpp
├── imgui_draw.cpp
├── imgui_internal.h
├── imgui_tables.cpp
├── imgui_widgets.cpp
├── imconfig.h
├── imstb_rectpack.h
├── imstb_textedit.h
├── imstb_truetype.h
├── backends/
│   ├── imgui_impl_win32.cpp
│   ├── imgui_impl_win32.h
│   ├── imgui_impl_dx11.cpp
│   └── imgui_impl_dx11.h
├── CMakeLists.txt       # 已提供
└── README.md            # 本文件
```

## 版本要求

推荐使用 ImGui v1.89 或更高版本。

## 许可证

ImGui 使用 MIT 许可证，可免费用于商业和非商业项目。
