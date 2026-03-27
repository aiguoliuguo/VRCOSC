#ifndef OPENHW_IPC_H
#define OPENHW_IPC_H

#include <cstdint>

inline constexpr wchar_t kOpenHardwareMonitorPipeName[] =
    L"\\\\.\\pipe\\VRCOSC.OpenHardwareMonitor";
inline constexpr std::uint32_t kOpenHardwareMonitorProtocolVersion = 1;
inline constexpr std::uint32_t kOpenHardwareMonitorNameCapacity = 128;
inline constexpr std::uint32_t kOpenHardwareMonitorDetailMessageCapacity = 512;

struct OpenHardwareMonitorCpuMetrics
{
    std::int32_t usagePercent{0};
    double voltageV{0.0};
    double frequencyGHz{0.0};
    std::int32_t temperatureC{0};
    std::int32_t powerW{0};
};

struct OpenHardwareMonitorMemoryMetrics
{
    std::int64_t usedMB{0};
    std::int64_t totalMB{0};
    std::int32_t frequencyMHz{0};
};

struct OpenHardwareMonitorGpuMetrics
{
    std::int32_t usagePercent{0};
    double voltageV{0.0};
    std::int32_t frequencyMHz{0};
    std::int32_t temperatureC{0};
    std::int32_t powerW{0};
};

struct OpenHardwareMonitorVramMetrics
{
    std::int64_t usedMB{0};
    std::int64_t totalMB{0};
};

struct OpenHardwareMonitorSystemMetrics
{
    OpenHardwareMonitorCpuMetrics cpu{};
    OpenHardwareMonitorMemoryMetrics memory{};
    OpenHardwareMonitorGpuMetrics gpu{};
    OpenHardwareMonitorVramMetrics vram{};
    wchar_t cpuName[kOpenHardwareMonitorNameCapacity]{};
    wchar_t gpuName[kOpenHardwareMonitorNameCapacity]{};
    wchar_t ramName[kOpenHardwareMonitorNameCapacity]{};
};

enum OpenHardwareMonitorHelperCommand : std::uint32_t
{
    OpenHardwareMonitorHelperCommand_GetSystemMetrics = 1,
    OpenHardwareMonitorHelperCommand_Shutdown = 2
};

enum OpenHardwareMonitorHelperDetailCode : std::uint32_t
{
    OpenHardwareMonitorHelperDetailCode_None = 0,
    OpenHardwareMonitorHelperDetailCode_ProtocolMismatch = 1,
    OpenHardwareMonitorHelperDetailCode_BridgeLoadFailed = 2,
    OpenHardwareMonitorHelperDetailCode_NoMetricsAvailable = 3,
    OpenHardwareMonitorHelperDetailCode_UnsupportedCommand = 4
};

struct OpenHardwareMonitorHelperRequest
{
    std::uint32_t version{kOpenHardwareMonitorProtocolVersion};
    std::uint32_t command{OpenHardwareMonitorHelperCommand_GetSystemMetrics};
};

struct OpenHardwareMonitorHelperResponse
{
    std::uint32_t version{kOpenHardwareMonitorProtocolVersion};
    std::uint32_t success{0};
    std::uint32_t detailCode{OpenHardwareMonitorHelperDetailCode_None};
    OpenHardwareMonitorSystemMetrics systemMetrics{};
    wchar_t detailMessage[kOpenHardwareMonitorDetailMessageCapacity]{};
};

#endif // OPENHW_IPC_H
