#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include "ElaScrollPage.h"
#include "openhw_ipc.h"

#include <Windows.h>
#include <QString>
#include <QStringList>

class ElaProgressBar;
class ElaToggleButton;
class ElaScrollPageArea;
class ElaText;
class QTimer;

class HomePage : public ElaScrollPage
{
    Q_OBJECT

public:
    explicit HomePage(QWidget* parent = nullptr);
    ~HomePage() override;
    void setOscSelectedParameters(const QStringList& parameters);

private slots:
    void updateMetrics();
    void onOscToggleChanged(bool checked);
    void sendOscChatboxMessage();
    void _onHelperLaunchFinished(bool launched, bool pipeReady, DWORD launchError, ULONGLONG now);

private:
    ElaProgressBar* _cpuBar{nullptr};
    ElaText* _cpuVal{nullptr};
    ElaText* _cpuFreqVal{nullptr};
    ElaText* _cpuTempVal{nullptr};
    ElaText* _cpuPowerVal{nullptr};

    ElaProgressBar* _memBar{nullptr};
    ElaText* _memVal{nullptr};

    ElaProgressBar* _gpuBar{nullptr};
    ElaText* _gpuVal{nullptr};
    ElaText* _gpuFreqVal{nullptr};
    ElaText* _gpuTempVal{nullptr};
    ElaText* _gpuPowerVal{nullptr};

    ElaProgressBar* _vramBar{nullptr};
    ElaText* _vramVal{nullptr};
    ElaToggleButton* _oscSendButton{nullptr};
    QTimer* _oscSendTimer{nullptr};
    QStringList _oscSelectedParameters;
    OpenHardwareMonitorSystemMetrics _latestMetrics{};
    bool _hasLatestMetrics{false};

    QTimer* _timer{nullptr};

    ULONGLONG _nextOpenHwHelperLaunchTick{0};
    ULONGLONG _openHwHelperLaunchDeadlineTick{0};
    bool _openHwHelperLaunchPending{false};
    bool _openHwHelperLaunchAttempted{false};
    bool _openHwHelperLaunchInFlight{false};
    ULONGLONG _lastOpenHwHelperFailureLogTick{0};
    QString _lastOpenHwHelperFailureSignature;
    ULONGLONG _lastOpenHwHelperStateLogTick{0};
    QString _lastOpenHwHelperStateSignature;
    ULONGLONG _lastOpenHwHelperSuccessLogTick{0};
    QString _lastOpenHwHelperSuccessSignature;

    bool systemMetricsFromOpenHardwareMonitor(OpenHardwareMonitorSystemMetrics& metrics);
    ElaScrollPageArea* makeMetricRow(const QString& label,
                                     ElaProgressBar*& bar,
                                     ElaText*& val);
    ElaScrollPageArea* makeDetailedRow(const QString& label,
                                       ElaProgressBar*& bar,
                                       ElaText*& val,
                                       ElaText*& freqVal,
                                       ElaText*& tempVal,
                                       ElaText*& powerVal);
    bool requestOpenHardwareMonitorHelper(OpenHardwareMonitorHelperCommand command,
                                          OpenHardwareMonitorSystemMetrics* metrics = nullptr);
    void ensureOpenHardwareMonitorHelper();
    void shutdownOpenHardwareMonitorHelper();
    void logOpenHardwareMonitorHelperFailureThrottled(const QString& signature, const QString& message);
    void logOpenHardwareMonitorHelperStateThrottled(const QString& signature, const QString& message);
    void logOpenHardwareMonitorHelperSuccessThrottled(const QString& signature, const QString& message);
    QString buildOscChatboxMessageFromSelection() const;

signals:
    void _helperLaunchFinished(bool launched, bool pipeReady, DWORD launchError, ULONGLONG launchTime);
};

#endif // HOMEPAGE_H
