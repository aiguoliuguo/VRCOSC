#include "homepage.h"

#include "ElaScrollPageArea.h"
#include "ElaProgressBar.h"
#include "ElaText.h"

#include <pdhmsg.h>
#include <dxgi1_4.h>
#include <d3dkmthk.h>
#include <wbemidl.h>
#include <comdef.h>
#include <SensorsApi.h>
#include <Sensors.h>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

// ──────────────────────────────────────────────
HomePage::HomePage(QWidget *parent)
    : ElaScrollPage(parent)
{
    setWindowTitle("主页");
    setContentsMargins(20, 5, 15, 5);

    // ── 初始化 PDH 查询 ────────────────────────────────────
    if (PdhOpenQuery(nullptr, 0, &_pdhQuery) == ERROR_SUCCESS) {
        PdhAddEnglishCounterW(_pdhQuery,
            L"\\GPU Engine(*)\\Utilization Percentage",
            0, &_pdhGpuCounter);        // CPU 利用率：% Processor Time 与任务管理器一致
        PdhAddEnglishCounterW(_pdhQuery,
            L"\\Processor Information(_Total)\\% Processor Time",
            0, &_pdhCpuUtilCounter);        PdhAddEnglishCounterW(_pdhQuery,
            L"\\Processor Information(_Total)\\Processor Frequency",
            0, &_pdhCpuFreqMhzCounter);
        PdhAddEnglishCounterW(_pdhQuery,
            L"\\Processor Information(_Total)\\% Processor Performance",
            0, &_pdhCpuPerfCounter);
        // CPU Package 功耗（RAPL Energy Meter，单位 mW）
        PdhAddEnglishCounterW(_pdhQuery,
            L"\\Energy Meter(RAPL_Package0_PKG)\\Power",
            0, &_pdhCpuPowerCounter);
        PdhCollectQueryData(_pdhQuery);
    }

    // ── 初始化 WMI (ROOT\\CIMV2)：CPU 频率 + 温度 ─────────────────────
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(CoCreateInstance(CLSID_WbemLocator, nullptr,
            CLSCTX_INPROC_SERVER, IID_IWbemLocator,
            reinterpret_cast<void**>(&_wbemLoc))) && _wbemLoc) {
        _wbemLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"),
                nullptr, nullptr, nullptr, 0, nullptr, nullptr, &_wbemSvc);
        if (_wbemSvc)
            CoSetProxyBlanket(_wbemSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                              nullptr, RPC_C_AUTHN_LEVEL_CALL,
                              RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    }

    // ── 动态加载 NVML（GPU 功耗）──────────────────────────────────────
    _nvmlLib = LoadLibraryW(L"nvml.dll");
    if (_nvmlLib) {
        _nvmlInit       = (pfnNvmlInit)GetProcAddress(_nvmlLib, "nvmlInit_v2");
        _nvmlGetPower   = (pfnNvmlDeviceGetPowerUsage)GetProcAddress(_nvmlLib, "nvmlDeviceGetPowerUsage");
        _nvmlGetTemp    = (pfnNvmlDeviceGetTemperature)GetProcAddress(_nvmlLib, "nvmlDeviceGetTemperature");
        _nvmlGetClock   = (pfnNvmlDeviceGetClockInfo)GetProcAddress(_nvmlLib, "nvmlDeviceGetClockInfo");
        _nvmlGetMemInfo = (pfnNvmlDeviceGetMemoryInfo)GetProcAddress(_nvmlLib, "nvmlDeviceGetMemoryInfo");
        _nvmlGetUtil    = (pfnNvmlDeviceGetUtilizationRates)GetProcAddress(_nvmlLib, "nvmlDeviceGetUtilizationRates");
        auto getHandle  = (pfnNvmlDeviceGetHandleByIdx)GetProcAddress(_nvmlLib, "nvmlDeviceGetHandleByIndex_v2");
        if (_nvmlInit && _nvmlInit() == 0 && getHandle)
            getHandle(0, &_nvmlDevice);
    }

    // ── 构建 UI ──────────────────────────────
    QWidget* central = new QWidget(this);
    central->setWindowTitle("主页");
    QVBoxLayout* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 15, 0);
    layout->setSpacing(8);

    layout->addWidget(makeDetailedRow("CPU 占用", _cpuBar, _cpuVal, _cpuFreqVal, _cpuTempVal, _cpuPowerVal));
    layout->addWidget(makeMetricRow(  "内存占用", _memBar, _memVal));
    layout->addWidget(makeDetailedRow("GPU 占用", _gpuBar, _gpuVal, _gpuFreqVal, _gpuTempVal, _gpuPowerVal));
    layout->addWidget(makeMetricRow(  "显存占用", _vramBar, _vramVal));
    layout->addStretch();

    addCentralWidget(central, true, true, 0);

    // ── 定时刷新 ─────────────────────────────
    _timer = new QTimer(this);
    connect(_timer, &QTimer::timeout, this, &HomePage::updateMetrics);
    _timer->start(700);
    updateMetrics(); // 立即刷新一次
}

HomePage::~HomePage()
{
    if (_pdhQuery) PdhCloseQuery(_pdhQuery);
    if (_wbemSvc) { _wbemSvc->Release(); _wbemSvc = nullptr; }
    if (_wbemLoc) { _wbemLoc->Release(); _wbemLoc = nullptr; }
    if (_nvmlLib) {
        auto shutdown = (pfnNvmlInit)GetProcAddress(_nvmlLib, "nvmlShutdown");
        if (shutdown) shutdown();
        FreeLibrary(_nvmlLib);
    }
    CoUninitialize();
}

// ──────────────────────────────────────────────
ElaScrollPageArea* HomePage::makeMetricRow(const QString& label,
                                            ElaProgressBar*& bar,
                                            ElaText*& val)
{
    ElaScrollPageArea* area = new ElaScrollPageArea(this);
    area->setFixedHeight(80);

    QVBoxLayout* vl = new QVBoxLayout(area);
    vl->setContentsMargins(14, 8, 14, 8);
    vl->setSpacing(4);

    // 第一行：标签 + 数值
    QHBoxLayout* hl = new QHBoxLayout();
    ElaText* lbl = new ElaText(label, this);
    lbl->setTextPixelSize(14);
    val = new ElaText("-- %", this);
    val->setTextPixelSize(14);
    hl->addWidget(lbl);
    hl->addStretch();
    hl->addWidget(val);

    // 第二行：进度条
    bar = new ElaProgressBar(this);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setTextVisible(false);

    vl->addLayout(hl);
    vl->addWidget(bar);

    return area;
}

// ──────────────────────────────────────────────
ElaScrollPageArea* HomePage::makeDetailedRow(const QString& label,
                                              ElaProgressBar*& bar,
                                              ElaText*& val,
                                              ElaText*& freqVal,
                                              ElaText*& tempVal,
                                              ElaText*& powerVal)
{
    ElaScrollPageArea* area = new ElaScrollPageArea(this);
    area->setFixedHeight(95);

    QVBoxLayout* vl = new QVBoxLayout(area);
    vl->setContentsMargins(14, 8, 14, 8);
    vl->setSpacing(4);

    QHBoxLayout* hl = new QHBoxLayout();
    ElaText* lbl = new ElaText(label, this);
    lbl->setTextPixelSize(14);

    freqVal = new ElaText("--", this);
    freqVal->setTextPixelSize(13);

    tempVal = new ElaText("--", this);
    tempVal->setTextPixelSize(13);

    powerVal = new ElaText("--", this);
    powerVal->setTextPixelSize(13);

    val = new ElaText("-- %", this);
    val->setTextPixelSize(14);

    hl->addWidget(lbl);
    hl->addStretch();
    hl->addWidget(freqVal);
    hl->addSpacing(12);
    hl->addWidget(tempVal);
    hl->addSpacing(12);
    hl->addWidget(powerVal);
    hl->addSpacing(12);
    hl->addWidget(val);

    bar = new ElaProgressBar(this);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setTextVisible(false);

    vl->addLayout(hl);
    vl->addWidget(bar);

    return area;
}

// ──────────────────────────────────────────────
void HomePage::updateMetrics()
{
    // 统一采样一次，确保所有 PDH 计数器都基于同一快照
    if (_pdhQuery) {
        PdhCollectQueryData(_pdhQuery);
        if (!_pdhReady) { _pdhReady = true; }
    }

    // CPU 占用
    int cpu = cpuUsage();
    _cpuBar->setValue(cpu);
    _cpuVal->setText(QString("%1 %").arg(cpu));

    // CPU 频率
    double freq = cpuFreqGHz();
    _cpuFreqVal->setText(freq > 0
        ? QString("%1 GHz").arg(freq, 0, 'f', 2)
        : QStringLiteral("--"));

    // CPU 温度
    int temp = cpuTempC();
    _cpuTempVal->setText(temp > 0
        ? QString("%1 °C").arg(temp)
        : QStringLiteral("--"));

    // 内存
    qint64 memUsed = 0, memTotal = 0;
    memUsage(memUsed, memTotal);
    int memPct = memTotal > 0 ? (int)(memUsed * 100 / memTotal) : 0;
    _memBar->setValue(memPct);
    _memVal->setText(QString("%1 GB / %2 GB")
        .arg(memUsed  / 1024.0, 0, 'f', 1)
        .arg(memTotal / 1024.0, 0, 'f', 1));

    // CPU 功耗
    int cpuW = cpuPowerW();
    _cpuPowerVal->setText(cpuW > 0 ? QString("%1 W").arg(cpuW) : QStringLiteral("--"));

    // GPU 占用（优先 NVML，回退 PDH）
    int gpu = (_nvmlDevice && _nvmlGetUtil) ? gpuUsageNvml() : gpuUsage();
    _gpuBar->setValue(gpu);
    _gpuVal->setText(QString("%1 %").arg(gpu));

    // GPU 频率
    int gpuMhz = gpuFreqMHz();
    _gpuFreqVal->setText(gpuMhz > 0 ? QString("%1 GHz").arg(gpuMhz / 1000.0, 0, 'f', 2) : QStringLiteral("--"));

    // GPU 温度
    int gpuT = gpuTempC();
    _gpuTempVal->setText(gpuT > 0 ? QString("%1 °C").arg(gpuT) : QStringLiteral("--"));

    // GPU 功耗
    int gpuW = gpuPowerW();
    _gpuPowerVal->setText(gpuW > 0 ? QString("%1 W").arg(gpuW) : QStringLiteral("--"));

    // 显存
    qint64 vramUsed = 0, vramTotal = 0;
    if (_nvmlDevice && _nvmlGetMemInfo)
        vramUsageNvml(vramUsed, vramTotal);
    else
        vramUsage(vramUsed, vramTotal);
    int vramPct = vramTotal > 0 ? (int)(vramUsed * 100 / vramTotal) : 0;
    _vramBar->setValue(vramPct);
    _vramVal->setText(QString("%1 GB / %2 GB")
        .arg(vramUsed  / 1024.0, 0, 'f', 1)
        .arg(vramTotal / 1024.0, 0, 'f', 1));
}

// ──────────────────────────────────────────────
int HomePage::cpuUsage()
{
    if (!_pdhCpuUtilCounter) return 0;
    PDH_FMT_COUNTERVALUE fmtVal{};
    if (PdhGetFormattedCounterValue(_pdhCpuUtilCounter,
                                     PDH_FMT_DOUBLE, nullptr, &fmtVal) != ERROR_SUCCESS)
        return 0;
    if (fmtVal.CStatus != PDH_CSTATUS_VALID_DATA) return 0;
    return qBound(0, static_cast<int>(fmtVal.doubleValue + 0.5), 100);
}

double HomePage::cpuFreqGHz()
{
    // 实际频率 = Processor Frequency (MHz) × % Processor Performance / 100
    // 与任务管理器算法一致，含睐频状态
    if (!_pdhCpuFreqMhzCounter || !_pdhCpuPerfCounter) return 0.0;

    PDH_FMT_COUNTERVALUE fmtMhz{}, fmtPerf{};
    if (PdhGetFormattedCounterValue(_pdhCpuFreqMhzCounter,
                                     PDH_FMT_DOUBLE, nullptr, &fmtMhz) != ERROR_SUCCESS)
        return 0.0;
    if (PdhGetFormattedCounterValue(_pdhCpuPerfCounter,
                                     PDH_FMT_DOUBLE, nullptr, &fmtPerf) != ERROR_SUCCESS)
        return 0.0;
    if (fmtMhz.CStatus != PDH_CSTATUS_VALID_DATA ||
        fmtPerf.CStatus != PDH_CSTATUS_VALID_DATA)
        return 0.0;
    return (fmtMhz.doubleValue * fmtPerf.doubleValue / 100.0) / 1000.0;
}

int HomePage::cpuTempC()
{
    // 使用 Windows Sensor Platform (ISensorManager)读取温度传感器
    ISensorManager* pMgr = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SensorManager, nullptr,
                                CLSCTX_INPROC_SERVER, IID_ISensorManager,
                                reinterpret_cast<void**>(&pMgr))) || !pMgr)
        return 0;

    ISensorCollection* pColl = nullptr;
    // SENSOR_CATEGORY_ENVIRONMENTAL 是温度传感器的正确类别
    HRESULT hr = pMgr->GetSensorsByCategory(SENSOR_CATEGORY_ENVIRONMENTAL, &pColl);
    pMgr->Release();
    if (FAILED(hr) || !pColl) return 0;

    ULONG count = 0;
    pColl->GetCount(&count);
    float maxTemp = 0.0f;

    for (ULONG i = 0; i < count; ++i) {
        ISensor* pSensor = nullptr;
        if (FAILED(pColl->GetAt(i, &pSensor)) || !pSensor) continue;

        SENSOR_TYPE_ID typeId{};
        pSensor->GetType(&typeId);
        // SENSOR_TYPE_TEMPERATURE_JUNCTION = CPU 核心温度 (if available)
        // SENSOR_DATA_TYPE_TEMPERATURE_CELSIUS 是所有温度传感器的标准別名
        ISensorDataReport* pReport = nullptr;
        if (SUCCEEDED(pSensor->GetData(&pReport)) && pReport) {
            PROPVARIANT pv{};
            PropVariantInit(&pv);
            if (SUCCEEDED(pReport->GetSensorValue(
                    SENSOR_DATA_TYPE_TEMPERATURE_CELSIUS, &pv))
                && pv.vt == VT_R4) {
                if (pv.fltVal > maxTemp) maxTemp = pv.fltVal;
            }
            PropVariantClear(&pv);
            pReport->Release();
        }
        pSensor->Release();
    }
    pColl->Release();

    return maxTemp > 0.0f ? static_cast<int>(maxTemp + 0.5f) : 0;
}

int HomePage::cpuPowerW()
{
    if (!_pdhCpuPowerCounter) return 0;
    PDH_FMT_COUNTERVALUE fmtVal{};
    if (PdhGetFormattedCounterValue(_pdhCpuPowerCounter,
                                     PDH_FMT_DOUBLE, nullptr, &fmtVal) != ERROR_SUCCESS)
        return 0;
    if (fmtVal.CStatus != PDH_CSTATUS_VALID_DATA) return 0;
    // 单位 mW 转 W
    return static_cast<int>(fmtVal.doubleValue / 1000.0 + 0.5);
}

int HomePage::gpuUsageNvml()
{
    NvmlUtilization util{};
    if (_nvmlGetUtil(_nvmlDevice, &util) != 0) return 0;
    return qBound(0, (int)util.gpu, 100);
}

void HomePage::vramUsageNvml(qint64& usedMB, qint64& totalMB)
{
    usedMB = totalMB = 0;
    NvmlMemory mem{};
    if (_nvmlGetMemInfo(_nvmlDevice, &mem) != 0) return;
    totalMB = static_cast<qint64>(mem.total >> 20);
    usedMB  = static_cast<qint64>(mem.used  >> 20);
}

int HomePage::gpuPowerW()
{
    if (!_nvmlDevice || !_nvmlGetPower) return 0;
    unsigned int mw = 0;
    if (_nvmlGetPower(_nvmlDevice, &mw) != 0) return 0;
    return static_cast<int>(mw / 1000.0 + 0.5);
}

int HomePage::gpuTempC()
{
    if (!_nvmlDevice || !_nvmlGetTemp) return 0;
    unsigned int temp = 0;
    // NVML_TEMPERATURE_GPU = 0
    if (_nvmlGetTemp(_nvmlDevice, 0, &temp) != 0) return 0;
    return static_cast<int>(temp);
}

int HomePage::gpuFreqMHz()
{
    if (!_nvmlDevice || !_nvmlGetClock) return 0;
    unsigned int mhz = 0;
    // NVML_CLOCK_GRAPHICS = 0
    if (_nvmlGetClock(_nvmlDevice, 0, &mhz) != 0) return 0;
    return static_cast<int>(mhz);
}

void HomePage::memUsage(qint64& usedMB, qint64& totalMB)
{
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) { usedMB = totalMB = 0; return; }
    totalMB = (qint64)(ms.ullTotalPhys   >> 20);
    usedMB  = (qint64)((ms.ullTotalPhys - ms.ullAvailPhys) >> 20);
}

int HomePage::gpuUsage()
{
    if (!_pdhGpuCounter) return 0;
    if (!_pdhReady) return 0; // 需至少两次采样

    DWORD bufSize  = 0;
    DWORD itemCnt  = 0;
    PdhGetFormattedCounterArrayW(_pdhGpuCounter, PDH_FMT_DOUBLE,
                                  &bufSize, &itemCnt, nullptr);
    if (bufSize == 0) return 0;

    QByteArray buf(bufSize, 0);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    if (PdhGetFormattedCounterArrayW(_pdhGpuCounter, PDH_FMT_DOUBLE,
                                      &bufSize, &itemCnt, items) != ERROR_SUCCESS)
        return 0;

    // 累加所有引擎占用，总和即为 GPU 综合利用率（上限 100）
    double total = 0.0;
    for (DWORD i = 0; i < itemCnt; ++i)
        if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA)
            total += items[i].FmtValue.doubleValue;

    return qBound(0, (int)total, 100);
}

void HomePage::vramUsage(qint64& usedMB, qint64& totalMB)
{
    usedMB = totalMB = 0;

    // ── 第一步：通过 DXGI 获取第一块独立显卡的 LUID 和总显存 ──────────
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                   reinterpret_cast<void**>(&factory))))
        return;

    LUID adapterLuid{};
    bool found = false;

    IDXGIAdapter* adapter = nullptr;
    for (UINT i = 0;
         factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++i)
    {
        DXGI_ADAPTER_DESC desc{};
        adapter->GetDesc(&desc);
        if (desc.DedicatedVideoMemory > 0) {
            totalMB     = static_cast<qint64>(desc.DedicatedVideoMemory >> 20);
            adapterLuid = desc.AdapterLuid;
            found       = true;
            adapter->Release();
            break;
        }
        adapter->Release();
    }
    factory->Release();
    if (!found) return;

    // ── 第二步：通过 D3DKMT 内核接口查询系统级显存占用 ────────────────
    // D3DKMTQueryStatistics 是 Task Manager / GPU-Z 底层使用的接口
    D3DKMT_OPENADAPTERFROMLUID openInfo{};
    openInfo.AdapterLuid = adapterLuid;
    if (D3DKMTOpenAdapterFromLuid(&openInfo) != 0)
        return;

    // 查询显卡信息（含显存段数量）
    D3DKMT_QUERYSTATISTICS qs{};
    qs.Type        = D3DKMT_QUERYSTATISTICS_ADAPTER;
    qs.AdapterLuid = adapterLuid;
    if (D3DKMTQueryStatistics(&qs) == 0) {
        ULONG nbSegments = qs.QueryResult.AdapterInformation.NbSegments;

        // 遍历所有显存段，累加 Local（非 Aperture）段的已提交字节
        ULONGLONG usedBytes = 0;
        for (ULONG seg = 0; seg < nbSegments; ++seg) {
            D3DKMT_QUERYSTATISTICS segQs{};
            segQs.Type                        = D3DKMT_QUERYSTATISTICS_SEGMENT;
            segQs.AdapterLuid                 = adapterLuid;
            segQs.QuerySegment.SegmentId      = seg;
            if (D3DKMTQueryStatistics(&segQs) != 0) continue;

            // Aperture = 0 表示本地显存段（VRAM）
            if (segQs.QueryResult.SegmentInformation.Aperture == 0)
                usedBytes += segQs.QueryResult.SegmentInformation.BytesCommitted;
        }
        usedMB = static_cast<qint64>(usedBytes >> 20);
    }

    D3DKMT_CLOSEADAPTER ca{};
    ca.hAdapter = openInfo.hAdapter;
    D3DKMTCloseAdapter(&ca);
}

