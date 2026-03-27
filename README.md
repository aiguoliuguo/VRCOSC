# VRCOSC

一个基于 Qt 的 Windows 监控与控制工具，集成：

- 系统性能采集（CPU / GPU / 内存 / 显存）
- Lighthouse 相关功能页
- SteamVR 监控服务
- OpenHardwareMonitor Bridge + Helper 进程通信

---

## 技术栈

- C++17
- Qt 5.15+ / Qt 6.x（Widgets + Network）
- CMake 3.16+
- Windows API / C++/WinRT
- OpenHardwareMonitor（外部依赖）

---

## 系统要求

- Windows 10 / 11
- MSVC 2019+（推荐 VS2022）
- 已安装 Qt 与 CMake

---

## 构建说明

### 1) 配置 ElaWidgetTools 路径

在 `CMakeLists.txt` 中确认：

```cmake
set(ELA_ROOT "D:/QTProject/ElaWidgetTools/ElaWidgetTools")
```

改为你本机实际路径。

### 2) 准备 OpenHardwareMonitor 依赖

项目默认从以下位置读取：

- `extern/OpenHardwareMonitor/prebuilt/OpenHardwareMonitorLib_x64.dll`
  或
- `extern/OpenHardwareMonitor/prebuilt/OpenHardwareMonitorLib.dll`

若缺失，会跳过桥接构建并给出 CMake 警告。

### 3) 本地构建

```bash
cmake -S . -B build
cmake --build build --config Release
```

---

## 仓库提交约定（重要）

- `extern/`：默认不纳入版本控制（已在 `.gitignore`）
- `build/`、`openhw_bridge/obj/`：编译产物，不提交
- 仅提交你维护的源码、头文件、工程配置与必要脚本

---

## 目录示例

```text
VRCOSC/
├─ CMakeLists.txt
├─ main.cpp
├─ homepage.cpp / homepage.h
├─ settingspage.cpp / settingspage.h
├─ steamvr_monitor_service.cpp / steamvr_monitor_service.h
├─ openhw_helper.cpp
├─ openhw_bridge/
│  ├─ OpenHardwareMonitorBridge.cpp
│  └─ OpenHardwareMonitorBridge.vcxproj
├─ extern/                  # 外部依赖（默认忽略）
└─ build/                   # 本地构建输出（忽略）
```
