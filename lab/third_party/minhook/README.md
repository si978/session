# MinHook 集成说明

## 下载

从 GitHub 下载 MinHook 源码：

```
https://github.com/TsudaKageWorker/minhook
```

或使用 Git：

```bash
git clone https://github.com/TsudaKageWorker/minhook.git
```

## 需要的文件

将以下文件复制到此目录：

```
third_party/minhook/
├── include/
│   └── MinHook.h        # 头文件
├── src/
│   ├── buffer.c
│   ├── buffer.h
│   ├── hook.c
│   ├── trampoline.c
│   ├── trampoline.h
│   └── hde/
│       ├── hde32.c
│       ├── hde32.h
│       ├── hde64.c
│       ├── hde64.h
│       ├── pstdint.h
│       └── table32.h
│       └── table64.h
├── CMakeLists.txt       # 已提供
└── README.md            # 本文件
```

## 许可证

MinHook 使用 BSD 2-Clause 许可证，可免费用于商业和非商业项目。
