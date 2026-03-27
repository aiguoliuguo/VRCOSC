#include "../openhw_ipc.h"

#include <Windows.h>
#include <cstddef>
#include <vcclr.h>

#using <System.dll>
#using <System.Core.dll>
#using "OpenHardwareMonitorLib_x64.dll"

using namespace System;
using namespace OpenHardwareMonitor::Hardware;

namespace {

ref class OpenHardwareMonitorState abstract sealed
{
public:
    static Computer^ ComputerInstance = nullptr;
    static String^ LastStatus = String::Empty;
};

void setLastStatus(String^ status)
{
    OpenHardwareMonitorState::LastStatus = String::IsNullOrWhiteSpace(status)
        ? String::Empty
        : status;
}

String^ safeText(String^ value)
{
    return String::IsNullOrWhiteSpace(value) ? String::Empty : value;
}

template <std::size_t N>
void copyManagedStringToBuffer(String^ value, wchar_t (&buffer)[N])
{
    if (N == 0)
        return;

    buffer[0] = L'\0';
    String^ text = safeText(value);
    if (String::IsNullOrEmpty(text))
        return;

    pin_ptr<const wchar_t> pinned = PtrToStringChars(text);
    wcsncpy_s(buffer, N, pinned, _TRUNCATE);
}

bool containsIgnoreCase(String^ text, String^ token)
{
    if (String::IsNullOrEmpty(text) || String::IsNullOrEmpty(token))
        return false;

    return text->IndexOf(token, StringComparison::OrdinalIgnoreCase) >= 0;
}

bool isValidTemperature(double value)
{
    return value >= 1.0 && value <= 125.0;
}

bool isValidVoltage(double value)
{
    return value > 0.0 && value <= 24.0;
}

int toRoundedPercent(double value)
{
    return value >= 0.0 && value <= 100.0
        ? static_cast<int>(value + 0.5)
        : 0;
}

int toRoundedTemperature(double value)
{
    return isValidTemperature(value)
        ? static_cast<int>(value + 0.5)
        : 0;
}

int toRoundedPower(double value)
{
    return value > 0.0 && value <= 2000.0
        ? static_cast<int>(value + 0.5)
        : 0;
}

std::int64_t toRoundedMegabytes(double value, SensorType sensorType)
{
    if (value <= 0.0)
        return 0;

    const double mbValue = sensorType == SensorType::Data ? value * 1024.0 : value;
    return static_cast<std::int64_t>(mbValue + 0.5);
}

bool hasCpuMetrics(const OpenHardwareMonitorCpuMetrics& metrics)
{
    return metrics.usagePercent > 0
        || metrics.voltageV > 0.0
        || metrics.frequencyGHz > 0.0
        || metrics.temperatureC > 0
        || metrics.powerW > 0;
}

bool hasMemoryMetrics(const OpenHardwareMonitorMemoryMetrics& metrics)
{
    return metrics.totalMB > 0
        || metrics.frequencyMHz > 0;
}

bool hasGpuMetrics(const OpenHardwareMonitorGpuMetrics& metrics)
{
    return metrics.usagePercent > 0
        || metrics.voltageV > 0.0
        || metrics.frequencyMHz > 0
        || metrics.temperatureC > 0
        || metrics.powerW > 0;
}

bool hasVramMetrics(const OpenHardwareMonitorVramMetrics& metrics)
{
    return metrics.totalMB > 0;
}

bool hasAnySystemMetrics(const OpenHardwareMonitorSystemMetrics& metrics)
{
    return hasCpuMetrics(metrics.cpu)
        || hasMemoryMetrics(metrics.memory)
        || hasGpuMetrics(metrics.gpu)
        || hasVramMetrics(metrics.vram);
}

bool isGpuHardwareType(HardwareType hardwareType)
{
    return hardwareType == HardwareType::GpuNvidia
        || hardwareType == HardwareType::GpuAmd
        || hardwareType == HardwareType::GpuIntel;
}

bool isDiscreteGpuType(HardwareType hardwareType)
{
    return hardwareType == HardwareType::GpuNvidia
        || hardwareType == HardwareType::GpuAmd;
}

bool isPreferredCpuTemperatureSensor(String^ name)
{
    return containsIgnoreCase(name, "package")
        || containsIgnoreCase(name, "tctl")
        || containsIgnoreCase(name, "tdie")
        || containsIgnoreCase(name, "ccd")
        || containsIgnoreCase(name, "die");
}

bool isFallbackCpuTemperatureSensor(String^ name)
{
    return isPreferredCpuTemperatureSensor(name)
        || containsIgnoreCase(name, "core")
        || containsIgnoreCase(name, "cpu")
        || containsIgnoreCase(name, "socket");
}

bool isPreferredCpuLoadSensor(String^ name)
{
    return containsIgnoreCase(name, "total");
}

bool isPreferredCpuClockSensor(String^ name)
{
    return containsIgnoreCase(name, "average effective")
        || containsIgnoreCase(name, "cores (average effective)")
        || containsIgnoreCase(name, "average");
}

bool isCoreClockSensor(String^ name)
{
    return containsIgnoreCase(name, "core #")
        || containsIgnoreCase(name, "cpu core #")
        || containsIgnoreCase(name, "core ");
}

bool isPreferredCpuPowerSensor(String^ name)
{
    return containsIgnoreCase(name, "package");
}

bool isPreferredCpuVoltageSensor(String^ name)
{
    return containsIgnoreCase(name, "package")
        || containsIgnoreCase(name, "cpu")
        || containsIgnoreCase(name, "soc")
        || containsIgnoreCase(name, "vdd")
        || containsIgnoreCase(name, "svi2");
}

bool isCpuCoreVoltageSensor(String^ name)
{
    return containsIgnoreCase(name, "vcore")
        || containsIgnoreCase(name, "core")
        || containsIgnoreCase(name, "vid");
}

bool isCpuRelatedVoltageSensor(String^ name)
{
    return containsIgnoreCase(name, "cpu")
        || containsIgnoreCase(name, "soc")
        || containsIgnoreCase(name, "vdd")
        || containsIgnoreCase(name, "svi2")
        || containsIgnoreCase(name, "package");
}

bool isPhysicalMemoryHardware(IHardware^ hardware)
{
    return hardware != nullptr
        && hardware->HardwareType == HardwareType::Memory
        && (containsIgnoreCase(hardware->Name, "physical")
            || containsIgnoreCase(hardware->Identifier->ToString(), "pram"));
}

bool isMemoryUsedSensor(String^ name)
{
    return containsIgnoreCase(name, "used");
}

bool isMemoryTotalSensor(String^ name)
{
    return containsIgnoreCase(name, "total");
}

bool isMemoryLoadSensor(String^ name)
{
    return containsIgnoreCase(name, "memory");
}

bool isGpuMemorySensor(String^ name)
{
    return containsIgnoreCase(name, "memory");
}

bool isPreferredGpuLoadSensor(String^ name)
{
    return containsIgnoreCase(name, "gpu core")
        || containsIgnoreCase(name, "gpu package")
        || containsIgnoreCase(name, "graphics");
}

bool isPreferredGpuClockSensor(String^ name)
{
    return containsIgnoreCase(name, "gpu core")
        || containsIgnoreCase(name, "graphics");
}

bool isPreferredGpuTemperatureSensor(String^ name)
{
    return containsIgnoreCase(name, "gpu core")
        || containsIgnoreCase(name, "gpu package");
}

bool isPreferredGpuPowerSensor(String^ name)
{
    return containsIgnoreCase(name, "gpu package")
        || containsIgnoreCase(name, "gpu total")
        || containsIgnoreCase(name, "gpu power");
}

bool isPreferredGpuVoltageSensor(String^ name)
{
    return containsIgnoreCase(name, "gpu")
        || containsIgnoreCase(name, "vddc")
        || containsIgnoreCase(name, "core");
}

bool isMemoryClockSensor(String^ name)
{
    return containsIgnoreCase(name, "memory")
        || containsIgnoreCase(name, "dram")
        || containsIgnoreCase(name, "ddr");
}

bool isPreferredDedicatedVramUsedSensor(String^ name)
{
    return containsIgnoreCase(name, "gpu memory used")
        || containsIgnoreCase(name, "d3d dedicated memory used");
}

bool isPreferredDedicatedVramTotalSensor(String^ name)
{
    return containsIgnoreCase(name, "gpu memory total")
        || containsIgnoreCase(name, "d3d dedicated memory total");
}

bool isFallbackVramUsedSensor(String^ name)
{
    return isPreferredDedicatedVramUsedSensor(name)
        || containsIgnoreCase(name, "d3d shared memory used")
        || containsIgnoreCase(name, "memory used");
}

bool isFallbackVramTotalSensor(String^ name)
{
    return isPreferredDedicatedVramTotalSensor(name)
        || containsIgnoreCase(name, "d3d shared memory total")
        || containsIgnoreCase(name, "memory total");
}

void updateHardwareTree(IHardware^ hardware)
{
    if (hardware == nullptr)
        return;

    hardware->Update();
    for each (IHardware ^ child in hardware->SubHardware)
        updateHardwareTree(child);
}

void collectCpuMetrics(IHardware^ hardware,
                       double% preferredLoad,
                       double% fallbackLoad,
                       double% preferredVoltage,
                       double% fallbackVoltage,
                       double% preferredClockMHz,
                       double% fallbackClockMHz,
                       int% fallbackClockCount,
                       double% preferredPower,
                       double% fallbackPower,
                       double% preferredTemp,
                       double% fallbackTemp,
                       String^% cpuSource)
{
    if (hardware == nullptr)
        return;

    const bool isCpuHardware = hardware->HardwareType == HardwareType::Cpu;
    const bool isCpuRelatedHardware =
        hardware->HardwareType == HardwareType::Motherboard
        || hardware->HardwareType == HardwareType::SuperIO
        || hardware->HardwareType == HardwareType::EmbeddedController;

    if (isCpuHardware && String::IsNullOrEmpty(cpuSource))
        cpuSource = safeText(hardware->Name);

    for each (ISensor ^ sensor in hardware->Sensors) {
        if (sensor == nullptr || !sensor->Value.HasValue)
            continue;

        const double value = sensor->Value.Value;
        String^ name = safeText(sensor->Name);

        if (sensor->SensorType == SensorType::Temperature && isValidTemperature(value)) {
            if (isCpuHardware) {
                if (isPreferredCpuTemperatureSensor(name))
                    preferredTemp = Math::Max(preferredTemp, value);
                else
                    fallbackTemp = Math::Max(fallbackTemp, value);
            } else if (isCpuRelatedHardware && isFallbackCpuTemperatureSensor(name)) {
                fallbackTemp = Math::Max(fallbackTemp, value);
            }
            continue;
        }

        if (sensor->SensorType == SensorType::Voltage
            && isValidVoltage(value)
            && isCpuRelatedHardware
            && isCpuRelatedVoltageSensor(name)) {
            if (isCpuCoreVoltageSensor(name)) {
                fallbackVoltage = Math::Max(fallbackVoltage, value);
            } else if (isPreferredCpuVoltageSensor(name)) {
                preferredVoltage = Math::Max(preferredVoltage, value);
            } else {
                fallbackVoltage = Math::Max(fallbackVoltage, value);
            }
            continue;
        }

        if (!isCpuHardware)
            continue;

        if (sensor->SensorType == SensorType::Load && value >= 0.0 && value <= 100.0) {
            if (isPreferredCpuLoadSensor(name))
                preferredLoad = Math::Max(preferredLoad, value);
            else
                fallbackLoad = Math::Max(fallbackLoad, value);
            continue;
        }

        if (sensor->SensorType == SensorType::Voltage && isValidVoltage(value)) {
            if (isCpuCoreVoltageSensor(name)) {
                fallbackVoltage = Math::Max(fallbackVoltage, value);
            } else if (isPreferredCpuVoltageSensor(name))
                preferredVoltage = Math::Max(preferredVoltage, value);
            else
                fallbackVoltage = Math::Max(fallbackVoltage, value);
            continue;
        }

        if (sensor->SensorType == SensorType::Clock && value > 0.0) {
            if (isPreferredCpuClockSensor(name)) {
                preferredClockMHz = Math::Max(preferredClockMHz, value);
            } else if (isCoreClockSensor(name)) {
                fallbackClockMHz += value;
                fallbackClockCount++;
            } else {
                fallbackClockMHz = Math::Max(fallbackClockMHz, value);
            }
            continue;
        }

        if (sensor->SensorType == SensorType::Power && value > 0.0) {
            if (isPreferredCpuPowerSensor(name))
                preferredPower = Math::Max(preferredPower, value);
            else
                fallbackPower = Math::Max(fallbackPower, value);
        }
    }

    for each (IHardware ^ child in hardware->SubHardware)
        collectCpuMetrics(child,
                          preferredLoad,
                          fallbackLoad,
                          preferredVoltage,
                          fallbackVoltage,
                          preferredClockMHz,
                          fallbackClockMHz,
                          fallbackClockCount,
                          preferredPower,
                          fallbackPower,
                          preferredTemp,
                          fallbackTemp,
                          cpuSource);
}

void collectMemoryMetrics(IHardware^ hardware,
                          OpenHardwareMonitorMemoryMetrics& bestMetrics,
                          int% bestScore,
                          String^% bestSource)
{
    if (hardware == nullptr)
        return;

    if (hardware->HardwareType == HardwareType::Memory) {
        OpenHardwareMonitorMemoryMetrics candidate{};
        double memoryLoad = 0.0;

        for each (ISensor ^ sensor in hardware->Sensors) {
            if (sensor == nullptr || !sensor->Value.HasValue)
                continue;

            const double value = sensor->Value.Value;
            String^ name = safeText(sensor->Name);

            if ((sensor->SensorType == SensorType::Data || sensor->SensorType == SensorType::SmallData)
                && isMemoryUsedSensor(name)) {
                candidate.usedMB = toRoundedMegabytes(value, sensor->SensorType);
            } else if ((sensor->SensorType == SensorType::Data || sensor->SensorType == SensorType::SmallData)
                       && isMemoryTotalSensor(name)) {
                candidate.totalMB = toRoundedMegabytes(value, sensor->SensorType);
            } else if (sensor->SensorType == SensorType::Load
                       && isMemoryLoadSensor(name)
                       && value >= 0.0
                       && value <= 100.0) {
                memoryLoad = value;
            }
        }

        if (candidate.usedMB <= 0 && candidate.totalMB > 0 && memoryLoad > 0.0) {
            candidate.usedMB = static_cast<std::int64_t>(candidate.totalMB * (memoryLoad / 100.0) + 0.5);
        }

        int score = 0;
        if (isPhysicalMemoryHardware(hardware))
            score += 100;
        if (candidate.totalMB > 0)
            score += 20;
        if (candidate.usedMB > 0)
            score += 10;
        if (memoryLoad > 0.0)
            score += 5;

        if (score > bestScore
            || (score == bestScore && candidate.totalMB > bestMetrics.totalMB)) {
            bestMetrics = candidate;
            bestScore = score;
            bestSource = safeText(hardware->Name);
        }
    }

    for each (IHardware ^ child in hardware->SubHardware)
        collectMemoryMetrics(child, bestMetrics, bestScore, bestSource);
}

void collectRamFrequencyMetrics(IHardware^ hardware, double% bestMemoryClockMHz)
{
    if (hardware == nullptr)
        return;

    if (!isGpuHardwareType(hardware->HardwareType)) {
        for each (ISensor ^ sensor in hardware->Sensors) {
            if (sensor == nullptr || !sensor->Value.HasValue || sensor->SensorType != SensorType::Clock)
                continue;

            const double value = sensor->Value.Value;
            if (value <= 0.0)
                continue;

            String^ name = safeText(sensor->Name);
            if (isMemoryClockSensor(name))
                bestMemoryClockMHz = Math::Max(bestMemoryClockMHz, value);
        }
    }

    for each (IHardware ^ child in hardware->SubHardware)
        collectRamFrequencyMetrics(child, bestMemoryClockMHz);
}

void collectGpuMetricsRecursive(IHardware^ hardware,
                                double% preferredLoad,
                                double% fallbackLoad,
                                double% preferredVoltage,
                                double% fallbackVoltage,
                                double% preferredClockMHz,
                                double% fallbackClockMHz,
                                double% preferredTemp,
                                double% fallbackTemp,
                                double% preferredPower,
                                double% fallbackPower,
                                std::int64_t% preferredVramUsedMB,
                                std::int64_t% fallbackVramUsedMB,
                                std::int64_t% preferredVramTotalMB,
                                std::int64_t% fallbackVramTotalMB)
{
    if (hardware == nullptr)
        return;

    for each (ISensor ^ sensor in hardware->Sensors) {
        if (sensor == nullptr || !sensor->Value.HasValue)
            continue;

        const double value = sensor->Value.Value;
        String^ name = safeText(sensor->Name);

        switch (sensor->SensorType) {
        case SensorType::Load:
            if (value >= 0.0 && value <= 100.0) {
                if (isGpuMemorySensor(name)) {
                    // VRAM load is not the main GPU usage we want to show.
                } else if (isPreferredGpuLoadSensor(name)) {
                    preferredLoad = Math::Max(preferredLoad, value);
                } else {
                    fallbackLoad = Math::Max(fallbackLoad, value);
                }
            }
            break;
        case SensorType::Voltage:
            if (isValidVoltage(value)) {
                if (isPreferredGpuVoltageSensor(name)) {
                    preferredVoltage = Math::Max(preferredVoltage, value);
                } else {
                    fallbackVoltage = Math::Max(fallbackVoltage, value);
                }
            }
            break;
        case SensorType::Clock:
            if (value > 0.0) {
                if (isGpuMemorySensor(name)) {
                    // Skip memory clocks for the main GPU frequency field.
                } else if (isPreferredGpuClockSensor(name)) {
                    preferredClockMHz = Math::Max(preferredClockMHz, value);
                } else {
                    fallbackClockMHz = Math::Max(fallbackClockMHz, value);
                }
            }
            break;
        case SensorType::Temperature:
            if (isValidTemperature(value)) {
                if (isPreferredGpuTemperatureSensor(name)) {
                    preferredTemp = Math::Max(preferredTemp, value);
                } else {
                    fallbackTemp = Math::Max(fallbackTemp, value);
                }
            }
            break;
        case SensorType::Power:
            if (value > 0.0) {
                if (isPreferredGpuPowerSensor(name)) {
                    preferredPower = Math::Max(preferredPower, value);
                } else {
                    fallbackPower = Math::Max(fallbackPower, value);
                }
            }
            break;
        case SensorType::Data:
        case SensorType::SmallData: {
            const std::int64_t mbValue = toRoundedMegabytes(value, sensor->SensorType);
            if (isPreferredDedicatedVramUsedSensor(name)) {
                preferredVramUsedMB = System::Math::Max(preferredVramUsedMB, mbValue);
            } else if (isPreferredDedicatedVramTotalSensor(name)) {
                preferredVramTotalMB = System::Math::Max(preferredVramTotalMB, mbValue);
            } else if (isFallbackVramUsedSensor(name)) {
                fallbackVramUsedMB = System::Math::Max(fallbackVramUsedMB, mbValue);
            } else if (isFallbackVramTotalSensor(name)) {
                fallbackVramTotalMB = System::Math::Max(fallbackVramTotalMB, mbValue);
            }
            break;
        }
        default:
            break;
        }
    }

    for each (IHardware ^ child in hardware->SubHardware)
        collectGpuMetricsRecursive(child,
                                   preferredLoad,
                                   fallbackLoad,
                                   preferredVoltage,
                                   fallbackVoltage,
                                   preferredClockMHz,
                                   fallbackClockMHz,
                                   preferredTemp,
                                   fallbackTemp,
                                   preferredPower,
                                   fallbackPower,
                                   preferredVramUsedMB,
                                   fallbackVramUsedMB,
                                   preferredVramTotalMB,
                                   fallbackVramTotalMB);
}

void collectBestGpuMetrics(IHardware^ hardware,
                           OpenHardwareMonitorGpuMetrics& bestGpuMetrics,
                           OpenHardwareMonitorVramMetrics& bestVramMetrics,
                           int% bestScore,
                           String^% bestSource)
{
    if (hardware == nullptr)
        return;

    if (isGpuHardwareType(hardware->HardwareType)) {
        double preferredLoad = 0.0;
        double fallbackLoad = 0.0;
        double preferredVoltage = 0.0;
        double fallbackVoltage = 0.0;
        double preferredClockMHz = 0.0;
        double fallbackClockMHz = 0.0;
        double preferredTemp = 0.0;
        double fallbackTemp = 0.0;
        double preferredPower = 0.0;
        double fallbackPower = 0.0;
        std::int64_t preferredVramUsedMB = 0;
        std::int64_t fallbackVramUsedMB = 0;
        std::int64_t preferredVramTotalMB = 0;
        std::int64_t fallbackVramTotalMB = 0;

        collectGpuMetricsRecursive(hardware,
                                   preferredLoad,
                                   fallbackLoad,
                                   preferredVoltage,
                                   fallbackVoltage,
                                   preferredClockMHz,
                                   fallbackClockMHz,
                                   preferredTemp,
                                   fallbackTemp,
                                   preferredPower,
                                   fallbackPower,
                                   preferredVramUsedMB,
                                   fallbackVramUsedMB,
                                   preferredVramTotalMB,
                                   fallbackVramTotalMB);

        OpenHardwareMonitorGpuMetrics candidateGpu{};
        candidateGpu.usagePercent = toRoundedPercent(preferredLoad > 0.0 ? preferredLoad : fallbackLoad);
        candidateGpu.voltageV = preferredVoltage > 0.0 ? preferredVoltage : fallbackVoltage;
        candidateGpu.frequencyMHz = static_cast<int>((preferredClockMHz > 0.0 ? preferredClockMHz : fallbackClockMHz) + 0.5);
        candidateGpu.temperatureC = toRoundedTemperature(preferredTemp > 0.0 ? preferredTemp : fallbackTemp);
        candidateGpu.powerW = toRoundedPower(preferredPower > 0.0 ? preferredPower : fallbackPower);

        OpenHardwareMonitorVramMetrics candidateVram{};
        candidateVram.usedMB = preferredVramUsedMB > 0 ? preferredVramUsedMB : fallbackVramUsedMB;
        candidateVram.totalMB = preferredVramTotalMB > 0 ? preferredVramTotalMB : fallbackVramTotalMB;

        int score = 0;
        if (isDiscreteGpuType(hardware->HardwareType))
            score += 50;
        if (candidateVram.totalMB > 0)
            score += 30;
        if (candidateVram.usedMB > 0)
            score += 10;
        if (candidateGpu.temperatureC > 0)
            score += 10;
        if (candidateGpu.frequencyMHz > 0)
            score += 10;
        if (candidateGpu.powerW > 0)
            score += 10;
        if (candidateGpu.usagePercent > 0)
            score += 10;

        if (score > bestScore
            || (score == bestScore && candidateVram.totalMB > bestVramMetrics.totalMB)) {
            bestGpuMetrics = candidateGpu;
            bestVramMetrics = candidateVram;
            bestScore = score;
            bestSource = safeText(hardware->Name);
        }
    }

    for each (IHardware ^ child in hardware->SubHardware)
        collectBestGpuMetrics(child, bestGpuMetrics, bestVramMetrics, bestScore, bestSource);
}

bool ensureComputer()
{
    if (OpenHardwareMonitorState::ComputerInstance != nullptr)
        return true;

    Computer^ computer = gcnew Computer();
    computer->IsCpuEnabled = true;
    computer->IsMotherboardEnabled = true;
    computer->IsMemoryEnabled = true;
    computer->IsGpuEnabled = true;
    computer->Open(false);

    OpenHardwareMonitorState::ComputerInstance = computer;
    setLastStatus("computer_opened");
    return true;
}

} // namespace

extern "C" __declspec(dllexport) bool __cdecl OHM_GetSystemMetrics(OpenHardwareMonitorSystemMetrics* metrics)
{
    try {
        if (!metrics) {
            setLastStatus("metrics_ptr_null");
            return false;
        }

        *metrics = OpenHardwareMonitorSystemMetrics{};

        if (!ensureComputer()) {
            setLastStatus("ensure_computer_failed");
            return false;
        }

        double preferredCpuLoad = 0.0;
        double fallbackCpuLoad = 0.0;
        double preferredCpuVoltage = 0.0;
        double fallbackCpuVoltage = 0.0;
        double preferredCpuClockMHz = 0.0;
        double fallbackCpuClockMHz = 0.0;
        int fallbackCpuClockCount = 0;
        double preferredCpuPower = 0.0;
        double fallbackCpuPower = 0.0;
        double preferredCpuTemp = 0.0;
        double fallbackCpuTemp = 0.0;
        String^ cpuSource = String::Empty;

        int bestMemoryScore = -1;
        String^ memorySource = String::Empty;

        int bestGpuScore = -1;
        String^ gpuSource = String::Empty;
        double bestRamClockMHz = 0.0;

        const auto hardwareList = OpenHardwareMonitorState::ComputerInstance->Hardware;
        for each (IHardware ^ hardware in hardwareList) {
            updateHardwareTree(hardware);
            collectCpuMetrics(hardware,
                              preferredCpuLoad,
                              fallbackCpuLoad,
                              preferredCpuVoltage,
                              fallbackCpuVoltage,
                              preferredCpuClockMHz,
                              fallbackCpuClockMHz,
                              fallbackCpuClockCount,
                              preferredCpuPower,
                              fallbackCpuPower,
                              preferredCpuTemp,
                              fallbackCpuTemp,
                              cpuSource);
            collectMemoryMetrics(hardware, metrics->memory, bestMemoryScore, memorySource);
            collectRamFrequencyMetrics(hardware, bestRamClockMHz);
            collectBestGpuMetrics(hardware, metrics->gpu, metrics->vram, bestGpuScore, gpuSource);
        }

        const double finalCpuClockMHz =
            preferredCpuClockMHz > 0.0 ? preferredCpuClockMHz
            : (fallbackCpuClockCount > 0 ? fallbackCpuClockMHz / fallbackCpuClockCount : fallbackCpuClockMHz);

        metrics->cpu.usagePercent = toRoundedPercent(preferredCpuLoad > 0.0 ? preferredCpuLoad : fallbackCpuLoad);
        metrics->cpu.voltageV = preferredCpuVoltage > 0.0 ? preferredCpuVoltage : fallbackCpuVoltage;
        metrics->cpu.frequencyGHz = finalCpuClockMHz > 0.0 ? finalCpuClockMHz / 1000.0 : 0.0;
        metrics->cpu.temperatureC = toRoundedTemperature(preferredCpuTemp > 0.0 ? preferredCpuTemp : fallbackCpuTemp);
        metrics->cpu.powerW = toRoundedPower(preferredCpuPower > 0.0 ? preferredCpuPower : fallbackCpuPower);
        metrics->memory.frequencyMHz = bestRamClockMHz > 0.0 ? static_cast<int>(bestRamClockMHz + 0.5) : 0;
        copyManagedStringToBuffer(cpuSource, metrics->cpuName);
        copyManagedStringToBuffer(gpuSource, metrics->gpuName);
        copyManagedStringToBuffer(memorySource, metrics->ramName);

        const bool hasMetrics = hasAnySystemMetrics(*metrics);

        String^ status = String::Format(
            "hardware_count={0} cpu={{name={1},usage={2},voltV={3:F3},freqGHz={4:F2},tempC={5},powerW={6}}} memory={{name={7},usedMB={8},totalMB={9},freqMHz={10},src={11}}} gpu={{name={12},usage={13},voltV={14:F3},freqMHz={15},tempC={16},powerW={17},src={18}}} vram={{usedMB={19},totalMB={20}}} hasAnyMetric={21}",
            hardwareList != nullptr ? hardwareList->Count : 0,
            safeText(cpuSource),
            metrics->cpu.usagePercent,
            metrics->cpu.voltageV,
            metrics->cpu.frequencyGHz,
            metrics->cpu.temperatureC,
            metrics->cpu.powerW,
            safeText(memorySource),
            metrics->memory.usedMB,
            metrics->memory.totalMB,
            metrics->memory.frequencyMHz,
            safeText(memorySource),
            safeText(gpuSource),
            metrics->gpu.usagePercent,
            metrics->gpu.voltageV,
            metrics->gpu.frequencyMHz,
            metrics->gpu.temperatureC,
            metrics->gpu.powerW,
            safeText(gpuSource),
            metrics->vram.usedMB,
            metrics->vram.totalMB,
            hasMetrics);
        setLastStatus(status);
        return hasMetrics;
    } catch (Exception^ ex) {
        setLastStatus(String::Format("exception={0}", safeText(ex->ToString())));
        return false;
    }
}

extern "C" __declspec(dllexport) bool __cdecl OHM_GetCpuMetrics(OpenHardwareMonitorCpuMetrics* metrics)
{
    try {
        if (!metrics) {
            setLastStatus("cpu_metrics_ptr_null");
            return false;
        }

        OpenHardwareMonitorSystemMetrics systemMetrics{};
        const bool hasSystemMetrics = OHM_GetSystemMetrics(&systemMetrics);
        *metrics = systemMetrics.cpu;
        return hasCpuMetrics(systemMetrics.cpu) || hasSystemMetrics;
    } catch (Exception^ ex) {
        setLastStatus(String::Format("cpu_exception={0}", safeText(ex->ToString())));
        return false;
    }
}

extern "C" __declspec(dllexport) int __cdecl OHM_GetLastStatus(wchar_t* buffer, int capacity)
{
    try {
        if (!buffer || capacity <= 0)
            return 0;

        String^ status = OpenHardwareMonitorState::LastStatus;
        if (String::IsNullOrEmpty(status)) {
            buffer[0] = L'\0';
            return 0;
        }

        const int copyLength = Math::Min(status->Length, capacity - 1);
        pin_ptr<const wchar_t> pinned = PtrToStringChars(status);
        wcsncpy_s(buffer, capacity, pinned, copyLength);
        return copyLength;
    } catch (Exception^) {
        if (buffer && capacity > 0)
            buffer[0] = L'\0';
        return 0;
    }
}

extern "C" __declspec(dllexport) void __cdecl OHM_Shutdown()
{
    try {
        if (OpenHardwareMonitorState::ComputerInstance != nullptr) {
            OpenHardwareMonitorState::ComputerInstance->Close();
            OpenHardwareMonitorState::ComputerInstance = nullptr;
        }
        setLastStatus("shutdown");
    } catch (Exception^ ex) {
        setLastStatus(String::Format("shutdown_exception={0}", safeText(ex->ToString())));
        OpenHardwareMonitorState::ComputerInstance = nullptr;
    }
}
