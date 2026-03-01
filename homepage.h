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
    // 内存 UI
    ElaProgressBar* _memBar{nullptr};
    ElaText*        _memVal{nullptr};
    // GPU UI
    ElaProgressBar* _gpuBar{nullptr};
    ElaText*        _gpuVal{nullptr};
    ElaText*        _gpuFreqVal{nullptr};
    ElaText*        _gpuTempVal{nullptr};
    // 显存 UI
    ElaProgressBar* _vramBar{nullptr};
    ElaText*        _vramVal{nullptr};

    QTimer* _timer{nullptr};

    // CPU 时间基准
    FILETIME _prevIdle{};
    FILETIME _prevKernel{};
    FILETIME _prevUser{};

    // PDH
    PDH_HQUERY   _pdhQuery{nullptr};
    PDH_HCOUNTER _pdhGpuCounter{nullptr};
    bool         _pdhReady{false};

    // WMI (ROOT\\CIMV2)
    IWbemLocator*  _wbemLoc{nullptr};
    IWbemServices* _wbemSvc{nullptr};

    int    cpuUsage();
    double cpuFreqGHz();
    int    cpuTempC();
    void   memUsage(qint64& usedMB, qint64& totalMB);
    int    gpuUsage();
    void   vramUsage(qint64& usedMB, qint64& totalMB);

    ElaScrollPageArea* makeMetricRow(const QString& label,
                                     ElaProgressBar*& bar,
                                     ElaText*& val);
    ElaScrollPageArea* makeDetailedRow(const QString& label,
                                       ElaProgressBar*& bar,
                                       ElaText*& val,
                                       ElaText*& freqVal,
                                       ElaText*& tempVal);
};

#endif // HOMEPAGE_H
