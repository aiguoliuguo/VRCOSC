#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include "ElaScrollPage.h"

#include <Windows.h>
#include <pdh.h>
#include <wbemidl.h>

class ElaProgressBar;
class ElaText;
class ElaScrollPageArea;
class QTimer;

class HomePage : public ElaScrollPage
{
    Q_OBJECT
public:
    explicit HomePage(QWidget *parent = nullptr);
    ~HomePage() override;

private slots:
    void updateMetrics();

private:
    // CPU UI
    ElaProgressBar* _cpuBar{nullptr};
    ElaText*        _cpuVal{nullptr};
    ElaText*        _cpuFreqVal{nullptr};
    ElaText*        _cpuTempVal{nullptr};
    ElaText*        _cpuPowerVal{nullptr};
    // 内存 UI
    ElaProgressBar* _memBar{nullptr};
    ElaText*        _memVal{nullptr};
    // GPU UI
    ElaProgressBar* _gpuBar{nullptr};
    ElaText*        _gpuVal{nullptr};
    ElaText*        _gpuFreqVal{nullptr};
    ElaText*        _gpuTempVal{nullptr};
    ElaText*        _gpuPowerVal{nullptr};
    // 显存 UI
    ElaProgressBar* _vramBar{nullptr};
    ElaText*        _vramVal{nullptr};

    QTimer* _timer{nullptr};

    // PDH
    PDH_HQUERY   _pdhQuery{nullptr};
    PDH_HCOUNTER _pdhGpuCounter{nullptr};
    PDH_HCOUNTER _pdhCpuUtilCounter{nullptr};     // % Processor Time
    PDH_HCOUNTER _pdhCpuFreqMhzCounter{nullptr};  // Processor Frequency (MHz 基准)
    PDH_HCOUNTER _pdhCpuPerfCounter{nullptr};     // % Processor Performance (含睿频)
    PDH_HCOUNTER _pdhCpuPowerCounter{nullptr};    // Energy Meter Package Power (mW)
    bool         _pdhReady{false};

    // WMI (ROOT\\CIMV2)
    IWbemLocator*  _wbemLoc{nullptr};
    IWbemServices* _wbemSvc{nullptr};

    // NVML 动态加载（GPU 功耗 / 温度 / 频率）
    HMODULE  _nvmlLib{nullptr};
    void*    _nvmlDevice{nullptr};
    using pfnNvmlInit                    = int(*)();
    using pfnNvmlDeviceGetHandleByIdx    = int(*)(unsigned int, void**);
    using pfnNvmlDeviceGetPowerUsage     = int(*)(void*, unsigned int*);
    using pfnNvmlDeviceGetTemperature    = int(*)(void*, unsigned int, unsigned int*);
    using pfnNvmlDeviceGetClockInfo      = int(*)(void*, unsigned int, unsigned int*);
    // nvmlMemory_t: total / free / used (bytes) — 对齐三个 unsigned long long
    struct NvmlMemory { unsigned long long total, free, used; };
    using pfnNvmlDeviceGetMemoryInfo     = int(*)(void*, NvmlMemory*);
    // nvmlUtilization_t: gpu% / memory%
    struct NvmlUtilization { unsigned int gpu, memory; };
    using pfnNvmlDeviceGetUtilizationRates = int(*)(void*, NvmlUtilization*);
    pfnNvmlInit                      _nvmlInit{nullptr};
    pfnNvmlDeviceGetPowerUsage       _nvmlGetPower{nullptr};
    pfnNvmlDeviceGetTemperature      _nvmlGetTemp{nullptr};
    pfnNvmlDeviceGetClockInfo        _nvmlGetClock{nullptr};
    pfnNvmlDeviceGetMemoryInfo       _nvmlGetMemInfo{nullptr};
    pfnNvmlDeviceGetUtilizationRates _nvmlGetUtil{nullptr};

    int    cpuUsage();
    double cpuFreqGHz();
    int    cpuTempC();
    int    cpuPowerW();
    void   memUsage(qint64& usedMB, qint64& totalMB);
    int    gpuUsage();
    int    gpuUsageNvml();
    int    gpuPowerW();
    int    gpuTempC();
    int    gpuFreqMHz();
    void   vramUsage(qint64& usedMB, qint64& totalMB);
    void   vramUsageNvml(qint64& usedMB, qint64& totalMB);

    ElaScrollPageArea* makeMetricRow(const QString& label,
                                     ElaProgressBar*& bar,
                                     ElaText*& val);
    ElaScrollPageArea* makeDetailedRow(const QString& label,
                                       ElaProgressBar*& bar,
                                       ElaText*& val,
                                       ElaText*& freqVal,
                                       ElaText*& tempVal,
                                       ElaText*& powerVal);
};

#endif // HOMEPAGE_H
