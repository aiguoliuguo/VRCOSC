#include "homepage.h"

#include "ElaScrollPageArea.h"
#include "ElaProgressBar.h"
#include "ElaText.h"

#include <pdhmsg.h>
#include <dxgi1_4.h>
#include <d3dkmthk.h>
#include <wbemidl.h>
#include <comdef.h>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

// ──────────────────────────────────────────────
// 辅助：lambda 将 FILETIME 转为 ULONGLONG
static ULONGLONG ftToU64(const FILETIME& ft)
{
    return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

// ──────────────────────────────────────────────
HomePage::HomePage(QWidget *parent)
    : ElaScrollPage(parent)
{
    setWindowTitle("主页");
    setContentsMargins(20, 5, 15, 5);

    // ── 初始化 CPU 基准值 ─────────────────────────────────────────────
    GetSystemTimes(&_prevIdle, &_prevKernel, &_prevUser);

    // ── 初始化 PDH 查询（仅 GPU 占用）──────────────────────────────────
    if (PdhOpenQuery(nullptr, 0, &_pdhQuery) == ERROR_SUCCESS) {
        PdhAddEnglishCounterW(_pdhQuery,
            L"\\GPU Engine(*)\\Utilization Percentage",
            0, &_pdhGpuCounter);
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

    // ── 构建 UI ──────────────────────────────
    QWidget* central = new QWidget(this);
    central->setWindowTitle("主页");
    QVBoxLayout* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 15, 0);
    layout->setSpacing(8);

    layout->addWidget(makeDetailedRow("CPU 占用", _cpuBar,  _cpuVal,  _cpuFreqVal,  _cpuTempVal));
    layout->addWidget(makeMetricRow(  "内存占用", _memBar,  _memVal));
    layout->addWidget(makeDetailedRow("GPU 占用", _gpuBar,  _gpuVal,  _gpuFreqVal,  _gpuTempVal));
    layout->addWidget(makeMetricRow(  "显存占用", _vramBar, _vramVal));
    layout->addStretch();

    addCentralWidget(central, true, true, 0);

    // ── 定时刷新 ─────────────────────────────
    _timer = new QTimer(this);
    connect(_timer, &QTimer::timeout, this, &HomePage::updateMetrics);
    _timer->start(1000);
    updateMetrics(); // 立即刷新一次
}

HomePage::~HomePage()
{
    if (_pdhQuery) PdhCloseQuery(_pdhQuery);
    if (_wbemSvc) { _wbemSvc->Release(); _wbemSvc = nullptr; }
    if (_wbemLoc) { _wbemLoc->Release(); _wbemLoc = nullptr; }
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
                                              ElaText*& tempVal)
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

    val = new ElaText("-- %", this);
    val->setTextPixelSize(14);

    hl->addWidget(lbl);
    hl->addStretch();
    hl->addWidget(freqVal);
    hl->addSpacing(12);
    hl->addWidget(tempVal);
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

    // GPU 占用
    int gpu = gpuUsage();
    _gpuBar->setValue(gpu);
    _gpuVal->setText(QString("%1 %").arg(gpu));
    // GPU 频率/温度：需厂商 SDK（NVAPI/ADL），暂不支持
    _gpuFreqVal->setText(QStringLiteral("--"));
    _gpuTempVal->setText(QStringLiteral("--"));

    // 显存
    qint64 vramUsed = 0, vramTotal = 0;
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
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user))
        return 0;

    ULONGLONG dIdle   = ftToU64(idle)   - ftToU64(_prevIdle);
    ULONGLONG dKernel = ftToU64(kernel) - ftToU64(_prevKernel);
    ULONGLONG dUser   = ftToU64(user)   - ftToU64(_prevUser);

    _prevIdle   = idle;
    _prevKernel = kernel;
    _prevUser   = user;

    ULONGLONG total = dKernel + dUser;
    if (total == 0) return 0;

    int pct = (int)((total - dIdle) * 100ULL / total);
    return qBound(0, pct, 100);
}

double HomePage::cpuFreqGHz()
{
    // WMI Win32_Processor.CurrentClockSpeed 返回当前实时频率（MHz）
    if (!_wbemSvc) return 0.0;
    IEnumWbemClassObject* pEnum = nullptr;
    if (FAILED(_wbemSvc->ExecQuery(
            _bstr_t("WQL"),
            _bstr_t("SELECT CurrentClockSpeed FROM Win32_Processor"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &pEnum)) || !pEnum)
        return 0.0;
    double ghz = 0.0;
    IWbemClassObject* pObj = nullptr;
    ULONG uRet = 0;
    if (pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet) == WBEM_S_NO_ERROR && pObj) {
        VARIANT vt{};
        if (SUCCEEDED(pObj->Get(L"CurrentClockSpeed", 0, &vt, nullptr, nullptr)))
            ghz = vt.lVal / 1000.0;
        VariantClear(&vt);
        pObj->Release();
    }
    pEnum->Release();
    return ghz;
}

int HomePage::cpuTempC()
{
    // WMI Win32_PerfFormattedData_Counters_ThermalZoneInformation.Temperature
    // 单位：十分之一开尔文
    if (!_wbemSvc) return 0;
    IEnumWbemClassObject* pEnum = nullptr;
    if (FAILED(_wbemSvc->ExecQuery(
            _bstr_t("WQL"),
            _bstr_t("SELECT Temperature FROM Win32_PerfFormattedData_Counters_ThermalZoneInformation"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &pEnum)) || !pEnum)
        return 0;
    int maxTemp = 0;
    IWbemClassObject* pObj = nullptr;
    ULONG uRet = 0;
    while (pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet) == WBEM_S_NO_ERROR && pObj) {
        VARIANT vt{};
        if (SUCCEEDED(pObj->Get(L"Temperature", 0, &vt, nullptr, nullptr))) {
            // 单位是十分之一开尔文
            int c = static_cast<int>(vt.lVal / 10.0 - 273.15 + 0.5);
            if (c > maxTemp) maxTemp = c;
        }
        VariantClear(&vt);
        pObj->Release();
    }
    pEnum->Release();
    return maxTemp;
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

