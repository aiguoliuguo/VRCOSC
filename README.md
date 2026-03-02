# VRCOSC

一个基于 Qt 的 Windows 系统性能监控工具，提供实时的 CPU、GPU、内存和显存使用情况监控。

## 功能特性

- **CPU 监控**
  - 实时使用率
  - 当前频率（支持睿频检测）
  - 核心温度
  - 功耗监测

- **GPU 监控**
  - 实时使用率
  - 核心频率
  - 温度监测
  - 功耗监测（支持 NVIDIA GPU）

- **内存监控**
  - 物理内存使用情况
  - 实时使用率显示

- **显存监控**
  - GPU 显存使用情况
  - 实时使用率显示

## 技术栈

- **框架**: Qt 5/6
- **语言**: C++17
- **构建系统**: CMake 3.16+
- **UI 库**: [ElaWidgetTools](https://github.com/Liniyous/ElaWidgetTools)
- **系统 API**:
  - Windows PDH (Performance Data Helper)
  - WMI (Windows Management Instrumentation)
  - NVML (NVIDIA Management Library) - 用于 NVIDIA GPU 监控

## 系统要求

- **操作系统**: Windows 10/11
- **编译器**: 支持 C++17 的编译器（MSVC 2019+、MinGW-w64 等）
- **Qt 版本**: Qt 5.15+ 或 Qt 6.x
- **可选**: NVIDIA 显卡驱动（用于完整的 GPU 监控功能）

## 构建说明

### 前置依赖

1. 安装 Qt 5.15+ 或 Qt 6.x
2. 安装 CMake 3.16+
3. 下载并配置 [ElaWidgetTools](https://github.com/Liniyous/ElaWidgetTools)

### 配置 ElaWidgetTools

在 `CMakeLists.txt` 中修改 ElaWidgetTools 路径：

```cmake
set(ELA_ROOT "D:/QTProject/ElaWidgetTools/ElaWidgetTools")
```

将路径改为你的 ElaWidgetTools 安装位置。

### 编译步骤

```bash
# 创建构建目录
mkdir build
cd build

# 配置项目
cmake ..

# 编译
cmake --build . --config Release
```

### 使用 Qt Creator

1. 打开 `CMakeLists.txt`
2. 配置 Qt Kit
3. 点击"构建"按钮

## 项目结构

```
VRCOSC/
├── CMakeLists.txt      # CMake 构建配置
├── main.cpp            # 程序入口
├── mainwindow.h/cpp    # 主窗口
├── homepage.h/cpp      # 主页（性能监控页面）
└── build/              # 构建输出目录
```

## 许可证

[待添加]

## 贡献

欢迎提交 Issue 和 Pull Request！

## 作者

[待添加]
