#include "homepage.h"

#include "ElaProgressBar.h"
#include "ElaScrollPageArea.h"
#include "ElaText.h"
#include "ElaToggleButton.h"

#include <shellapi.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QHostInfo>
#include <QRegularExpression>
#include <QUdpSocket>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QStringConverter>

namespace {

constexpr wchar_t kOpenHardwareMonitorHelperExeName[] = L"VRCOSC.OpenHardwareMonitorHelper.exe";
constexpr ULONGLONG kOpenHardwareMonitorHelperLaunchCooldownMs = 5000;
constexpr ULONGLONG kOpenHardwareMonitorHelperLaunchCancelCooldownMs = 30000;
constexpr ULONGLONG kOpenHardwareMonitorHelperLaunchPendingTimeoutMs = 15000;
constexpr DWORD kOpenHardwareMonitorHelperStartupWaitMs = 2000;
constexpr ULONGLONG kOpenHardwareMonitorHelperRepeatedLogIntervalMs = 5000;
constexpr quint16 kOscTargetPort = 9000;
constexpr int kOscChatSendIntervalMs = 2000;

void logOpenHardwareMonitorHelperEvent(const QString& message)
{
    const QString line = QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
        + QStringLiteral(" pid=")
        + QString::number(QCoreApplication::applicationPid())
        + QStringLiteral(" ")
        + message;

    qInfo().noquote() << line;

    QFile logFile(QDir::tempPath() + QStringLiteral("/VRCOSC.OpenHardwareMonitor.log"));
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    QTextStream stream(&logFile);
    stream.setEncoding(QStringConverter::Utf8);
    stream << line << '\n';
}

QString openHardwareMonitorDetailCodeToString(std::uint32_t detailCode)
{
    switch (detailCode) {
    case OpenHardwareMonitorHelperDetailCode_None:
        return QStringLiteral("none");
    case OpenHardwareMonitorHelperDetailCode_ProtocolMismatch:
        return QStringLiteral("protocol_mismatch");
    case OpenHardwareMonitorHelperDetailCode_BridgeLoadFailed:
        return QStringLiteral("bridge_load_failed");
    case OpenHardwareMonitorHelperDetailCode_NoMetricsAvailable:
        return QStringLiteral("no_metrics_available");
    case OpenHardwareMonitorHelperDetailCode_UnsupportedCommand:
        return QStringLiteral("unsupported_command");
    default:
        return QStringLiteral("unknown_%1").arg(detailCode);
    }
}

QString openHardwareMonitorDetailMessageToString(const OpenHardwareMonitorHelperResponse& response)
{
    if (response.detailMessage[0] == L'\0')
        return {};

    return QString::fromWCharArray(response.detailMessage);
}

bool readExactHandle(HANDLE handle, void* buffer, DWORD size)
{
    auto* bytes = static_cast<unsigned char*>(buffer);
    DWORD totalRead = 0;
    while (totalRead < size) {
        DWORD chunkRead = 0;
        if (!ReadFile(handle, bytes + totalRead, size - totalRead, &chunkRead, nullptr)
            || chunkRead == 0) {
            return false;
        }
        totalRead += chunkRead;
    }
    return true;
}

bool writeExactHandle(HANDLE handle, const void* buffer, DWORD size)
{
    const auto* bytes = static_cast<const unsigned char*>(buffer);
    DWORD totalWritten = 0;
    while (totalWritten < size) {
        DWORD chunkWritten = 0;
        if (!WriteFile(handle, bytes + totalWritten, size - totalWritten, &chunkWritten, nullptr)
            || chunkWritten == 0) {
            return false;
        }
        totalWritten += chunkWritten;
    }
    return true;
}

bool hasCpuMetrics(const OpenHardwareMonitorSystemMetrics& metrics)
{
    return metrics.cpu.usagePercent > 0
        || metrics.cpu.voltageV > 0.0
        || metrics.cpu.frequencyGHz > 0.0
        || metrics.cpu.temperatureC > 0
        || metrics.cpu.powerW > 0;
}

bool hasMemoryMetrics(const OpenHardwareMonitorSystemMetrics& metrics)
{
    return metrics.memory.totalMB > 0
        || metrics.memory.frequencyMHz > 0;
}

bool hasGpuMetrics(const OpenHardwareMonitorSystemMetrics& metrics)
{
    return metrics.gpu.usagePercent > 0
        || metrics.gpu.voltageV > 0.0
        || metrics.gpu.frequencyMHz > 0
        || metrics.gpu.temperatureC > 0
        || metrics.gpu.powerW > 0;
}

bool hasVramMetrics(const OpenHardwareMonitorSystemMetrics& metrics)
{
    return metrics.vram.totalMB > 0;
}

QString trimTrailingZeroDecimal(const QString& numberText)
{
    QString text = numberText;
    const int dotPos = text.indexOf(QLatin1Char('.'));
    if (dotPos < 0)
        return text;

    while (text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text;
}

QString formatDecimalValue(double value, int precision)
{
    return trimTrailingZeroDecimal(QString::number(value, 'f', precision));
}

QString formatUsageText(bool available, int value)
{
    return available ? QStringLiteral("%1 %").arg(value) : QStringLiteral("-- %");
}

QString formatFrequencyGHzText(double value)
{
    return value > 0.0 ? QStringLiteral("%1 GHz").arg(formatDecimalValue(value, 2)) : QStringLiteral("--");
}

QString formatTemperatureText(int value)
{
    return value > 0 ? QStringLiteral("%1 C").arg(value) : QStringLiteral("--");
}

QString formatPowerText(int value)
{
    return value > 0 ? QStringLiteral("%1 W").arg(value) : QStringLiteral("--");
}

QString formatCapacityText(qint64 usedMB, qint64 totalMB)
{
    if (totalMB <= 0)
        return QStringLiteral("--");

    return QStringLiteral("%1 G / %2 G")
        .arg(formatDecimalValue(usedMB / 1024.0, 1))
        .arg(formatDecimalValue(totalMB / 1024.0, 1));
}

QString valueOrDash(const QString& text)
{
    return text.isEmpty() ? QStringLiteral("--") : text;
}

QString formatPercentValue(int value)
{
    return value >= 0 ? QStringLiteral("%1%").arg(value) : QStringLiteral("--");
}

QString formatVoltageValue(double value, int precision = 3)
{
    return value > 0.0 ? QStringLiteral("%1V").arg(formatDecimalValue(value, precision)) : QStringLiteral("--");
}

QString formatFrequencyGValue(double value, int precision = 2)
{
    return value > 0.0 ? QStringLiteral("%1G").arg(formatDecimalValue(value, precision)) : QStringLiteral("--");
}

QString formatFrequencyMHzValue(int value)
{
    return value > 0 ? QStringLiteral("%1MHz").arg(value) : QStringLiteral("--");
}

QString formatTemperatureValue(int value)
{
    return value > 0 ? QStringLiteral("%1C").arg(value) : QStringLiteral("--");
}

QString formatPowerValue(int value)
{
    return value > 0 ? QStringLiteral("%1W").arg(value) : QStringLiteral("--");
}

QString formatMbFullValue(qint64 usedMB, qint64 totalMB)
{
    if (totalMB <= 0)
        return QStringLiteral("--");

    return QStringLiteral("%1/%2MB").arg(usedMB).arg(totalMB);
}

QString formatGbShortValue(qint64 usedMB, qint64 totalMB)
{
    if (totalMB <= 0)
        return QStringLiteral("--");

    return QStringLiteral("%1G/%2G")
        .arg(formatDecimalValue(usedMB / 1024.0, 1))
        .arg(formatDecimalValue(totalMB / 1024.0, 1));
}

QString formatGbCompactValue(qint64 usedMB, qint64 totalMB)
{
    if (totalMB <= 0)
        return QStringLiteral("--");

    return QStringLiteral("%1/%2G")
        .arg(formatDecimalValue(usedMB / 1024.0, 1))
        .arg(formatDecimalValue(totalMB / 1024.0, 1));
}

QString formatUsedGbValue(qint64 usedMB)
{
    if (usedMB <= 0)
        return QStringLiteral("--");

    return QStringLiteral("%1G").arg(formatDecimalValue(usedMB / 1024.0, 1));
}

QString formatPercentFromUsage(qint64 usedMB, qint64 totalMB)
{
    if (totalMB <= 0)
        return QStringLiteral("--");

    const int percent = static_cast<int>(usedMB * 100 / totalMB);
    return QStringLiteral("%1%").arg(percent);
}

QString fromWideBuffer(const wchar_t* value)
{
    if (!value || value[0] == L'\0')
        return {};
    return QString::fromWCharArray(value);
}

QString shortHardwareModel(const QString& fullName)
{
    const QString trimmed = fullName.trimmed();
    if (trimmed.isEmpty())
        return {};

    const QStringList parts = trimmed.split(QRegularExpression(QStringLiteral("\\s+")),
                                            Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return {};

    int startIndex = -1;
    static const QRegularExpression hasDigit(QStringLiteral(".*\\d.*"));
    for (int i = parts.size() - 1; i >= 0; --i) {
        if (hasDigit.match(parts[i]).hasMatch()) {
            startIndex = i;
            break;
        }
    }

    if (startIndex < 0)
        return parts.last();

    return parts.mid(startIndex).join(QStringLiteral(" "));
}

void appendOscPaddedString(QByteArray& packet, const QByteArray& value)
{
    packet.append(value);
    packet.append('\0');

    const int padding = (4 - (packet.size() % 4)) % 4;
    packet.append(QByteArray(padding, '\0'));
}

QByteArray buildChatboxInputPacket(const QString& message)
{
    QByteArray packet;
    appendOscPaddedString(packet, QByteArrayLiteral("/chatbox/input"));
    appendOscPaddedString(packet, QByteArrayLiteral(",sTF"));
    appendOscPaddedString(packet, message.toUtf8());
    return packet;
}

QHostAddress resolveOscTargetAddress()
{
    const QHostInfo hostInfo = QHostInfo::fromName(QStringLiteral("localhost"));
    for (const QHostAddress& address : hostInfo.addresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol)
            return address;
    }

    for (const QHostAddress& address : hostInfo.addresses()) {
        if (address.protocol() == QAbstractSocket::IPv6Protocol)
            return address;
    }

    return QHostAddress::LocalHost;
}

} // namespace

HomePage::HomePage(QWidget* parent)
    : ElaScrollPage(parent)
{
    setWindowTitle("Home");
    setTitleVisible(false);

    QWidget* central = new QWidget(this);
    central->setWindowTitle("Home");

    QVBoxLayout* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 15, 0);
    layout->setSpacing(8);

    QGridLayout* metricsLayout = new QGridLayout();
    metricsLayout->setContentsMargins(0, 0, 0, 0);
    metricsLayout->setHorizontalSpacing(8);
    metricsLayout->setVerticalSpacing(8);
    metricsLayout->setColumnStretch(0, 1);
    metricsLayout->setColumnStretch(1, 1);

    metricsLayout->addWidget(makeDetailedRow(QStringLiteral("CPU Usage"),
                                             _cpuBar,
                                             _cpuVal,
                                             _cpuFreqVal,
                                             _cpuTempVal,
                                             _cpuPowerVal),
                             0,
                             0);
    metricsLayout->addWidget(makeMetricRow(QStringLiteral("Memory Usage"), _memBar, _memVal), 0, 1);
    metricsLayout->addWidget(makeDetailedRow(QStringLiteral("GPU Usage"),
                                             _gpuBar,
                                             _gpuVal,
                                             _gpuFreqVal,
                                             _gpuTempVal,
                                             _gpuPowerVal),
                             1,
                             0);
    metricsLayout->addWidget(makeMetricRow(QStringLiteral("VRAM Usage"), _vramBar, _vramVal), 1, 1);

    layout->addLayout(metricsLayout);

    ElaScrollPageArea* oscControlArea = new ElaScrollPageArea(this);
    oscControlArea->setFixedHeight(64);

    QHBoxLayout* oscControlLayout = new QHBoxLayout(oscControlArea);
    oscControlLayout->setContentsMargins(14, 8, 14, 8);
    oscControlLayout->setSpacing(8);

    ElaText* oscControlTitle = new ElaText(QStringLiteral("OSC Control"), oscControlArea);
    oscControlTitle->setTextPixelSize(14);

    _oscSendButton = new ElaToggleButton(QStringLiteral("Send OSC"), oscControlArea);
    _oscSendButton->setMinimumWidth(100);
    connect(_oscSendButton, &ElaToggleButton::toggled, this, &HomePage::onOscToggleChanged);

    oscControlLayout->addWidget(oscControlTitle);
    oscControlLayout->addStretch();
    oscControlLayout->addWidget(_oscSendButton);

    layout->addWidget(oscControlArea);
    layout->addStretch();

    addCentralWidget(central, true, false, 0);

    // 连接工作线程回调信号，确保状态更新在主线程执行
    connect(this, &HomePage::_helperLaunchFinished,
            this, &HomePage::_onHelperLaunchFinished,
            Qt::QueuedConnection);

    _oscSendTimer = new QTimer(this);
    _oscSendTimer->setInterval(kOscChatSendIntervalMs);
    connect(_oscSendTimer, &QTimer::timeout, this, &HomePage::sendOscChatboxMessage);

    _timer = new QTimer(this);
    connect(_timer, &QTimer::timeout, this, &HomePage::updateMetrics);
    _timer->start(700);
    updateMetrics();
}

HomePage::~HomePage()
{
    if (_oscSendTimer)
        _oscSendTimer->stop();
    shutdownOpenHardwareMonitorHelper();
}

void HomePage::logOpenHardwareMonitorHelperFailureThrottled(const QString& signature, const QString& message)
{
    const ULONGLONG now = GetTickCount64();
    if (_lastOpenHwHelperFailureSignature == signature
        && now - _lastOpenHwHelperFailureLogTick < kOpenHardwareMonitorHelperRepeatedLogIntervalMs) {
        return;
    }

    _lastOpenHwHelperFailureSignature = signature;
    _lastOpenHwHelperFailureLogTick = now;
    logOpenHardwareMonitorHelperEvent(message);
}

void HomePage::logOpenHardwareMonitorHelperStateThrottled(const QString& signature, const QString& message)
{
    const ULONGLONG now = GetTickCount64();
    if (_lastOpenHwHelperStateSignature == signature
        && now - _lastOpenHwHelperStateLogTick < kOpenHardwareMonitorHelperRepeatedLogIntervalMs) {
        return;
    }

    _lastOpenHwHelperStateSignature = signature;
    _lastOpenHwHelperStateLogTick = now;
    logOpenHardwareMonitorHelperEvent(message);
}

void HomePage::logOpenHardwareMonitorHelperSuccessThrottled(const QString& signature, const QString& message)
{
    const ULONGLONG now = GetTickCount64();
    if (_lastOpenHwHelperSuccessSignature == signature
        && now - _lastOpenHwHelperSuccessLogTick < kOpenHardwareMonitorHelperRepeatedLogIntervalMs) {
        return;
    }

    _lastOpenHwHelperSuccessSignature = signature;
    _lastOpenHwHelperSuccessLogTick = now;
    logOpenHardwareMonitorHelperEvent(message);
}

bool HomePage::requestOpenHardwareMonitorHelper(OpenHardwareMonitorHelperCommand command,
                                                OpenHardwareMonitorSystemMetrics* metrics)
{
    HANDLE pipe = CreateFileW(kOpenHardwareMonitorPipeName,
                              GENERIC_READ | GENERIC_WRITE,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              0,
                              nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        logOpenHardwareMonitorHelperFailureThrottled(
            QStringLiteral("connect_%1_%2").arg(command).arg(error),
            QStringLiteral("request helper failed to connect command=%1 error=%2")
                .arg(command)
                .arg(error));
        return false;
    }

    DWORD readMode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &readMode, nullptr, nullptr);

    OpenHardwareMonitorHelperRequest request{};
    request.version = kOpenHardwareMonitorProtocolVersion;
    request.command = command;

    OpenHardwareMonitorHelperResponse response{};
    const bool ok = writeExactHandle(pipe, &request, sizeof(request))
        && readExactHandle(pipe, &response, sizeof(response))
        && response.version == kOpenHardwareMonitorProtocolVersion;

    CloseHandle(pipe);

    if (!ok) {
        logOpenHardwareMonitorHelperFailureThrottled(
            QStringLiteral("invalid_response_%1").arg(command),
            QStringLiteral("request helper invalid response command=%1").arg(command));
        return false;
    }

    _openHwHelperLaunchPending = false;
    _openHwHelperLaunchDeadlineTick = 0;
    _openHwHelperLaunchAttempted = true;

    if (response.success != 0 && metrics)
        *metrics = response.systemMetrics;

    const QString detailMessage = openHardwareMonitorDetailMessageToString(response);
    const QString logMessage = QStringLiteral("request helper success command=%1 success=%2 detail=%3 msg=%4")
                                   .arg(command)
                                   .arg(response.success)
                                   .arg(openHardwareMonitorDetailCodeToString(response.detailCode))
                                   .arg(detailMessage.isEmpty() ? QStringLiteral("<empty>") : detailMessage);
    if (response.success != 0) {
        logOpenHardwareMonitorHelperSuccessThrottled(
            QStringLiteral("success_%1_%2_%3")
                .arg(command)
                .arg(response.success)
                .arg(response.detailCode),
            logMessage);
    } else {
        logOpenHardwareMonitorHelperFailureThrottled(
            QStringLiteral("response_%1_%2_%3_%4")
                .arg(command)
                .arg(response.success)
                .arg(response.detailCode)
                .arg(detailMessage),
            logMessage);
    }

    return response.success != 0;
}

void HomePage::ensureOpenHardwareMonitorHelper()
{
    const ULONGLONG now = GetTickCount64();
    if (WaitNamedPipeW(kOpenHardwareMonitorPipeName, 0)) {
        _openHwHelperLaunchPending = false;
        _openHwHelperLaunchDeadlineTick = 0;
        _openHwHelperLaunchAttempted = true;
        _nextOpenHwHelperLaunchTick = now + kOpenHardwareMonitorHelperLaunchCooldownMs;
        logOpenHardwareMonitorHelperStateThrottled(
            QStringLiteral("pipe_ready"),
            QStringLiteral("helper pipe already ready"));
        return;
    }

    if (_openHwHelperLaunchPending) {
        if (WaitNamedPipeW(kOpenHardwareMonitorPipeName, kOpenHardwareMonitorHelperStartupWaitMs)) {
            _openHwHelperLaunchPending = false;
            _openHwHelperLaunchDeadlineTick = 0;
            _openHwHelperLaunchAttempted = true;
            _nextOpenHwHelperLaunchTick = GetTickCount64() + kOpenHardwareMonitorHelperLaunchCooldownMs;
            logOpenHardwareMonitorHelperStateThrottled(
                QStringLiteral("pipe_ready_during_wait"),
                QStringLiteral("helper pipe became ready during startup wait"));
            return;
        }

        if (now < _openHwHelperLaunchDeadlineTick) {
            logOpenHardwareMonitorHelperStateThrottled(
                QStringLiteral("launch_pending"),
                QStringLiteral("helper launch still pending deadline=%1 now=%2")
                    .arg(_openHwHelperLaunchDeadlineTick)
                    .arg(now));
            return;
        }

        _openHwHelperLaunchPending = false;
        _openHwHelperLaunchDeadlineTick = 0;
        logOpenHardwareMonitorHelperEvent(QStringLiteral("helper launch pending timed out"));
    }

    if (_openHwHelperLaunchAttempted) {
        logOpenHardwareMonitorHelperEvent(QStringLiteral("skip helper launch because it was already attempted"));
        return;
    }

    if (now < _nextOpenHwHelperLaunchTick) {
        logOpenHardwareMonitorHelperEvent(
            QStringLiteral("skip helper launch due to cooldown until=%1 now=%2")
                .arg(_nextOpenHwHelperLaunchTick)
                .arg(now));
        return;
    }

    const QString helperPath = QDir::toNativeSeparators(
        QCoreApplication::applicationDirPath()
        + QStringLiteral("/")
        + QString::fromWCharArray(kOpenHardwareMonitorHelperExeName));

    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.lpVerb = L"runas";
    executeInfo.lpFile = reinterpret_cast<LPCWSTR>(helperPath.utf16());

    const QString helperArguments = QString::number(GetCurrentProcessId());
    executeInfo.lpParameters = reinterpret_cast<LPCWSTR>(helperArguments.utf16());
    executeInfo.nShow = SW_HIDE;

    _openHwHelperLaunchPending = true;
    _openHwHelperLaunchAttempted = true;
    _openHwHelperLaunchDeadlineTick = now + kOpenHardwareMonitorHelperLaunchPendingTimeoutMs;
    _nextOpenHwHelperLaunchTick = now + kOpenHardwareMonitorHelperLaunchCooldownMs;

    logOpenHardwareMonitorHelperEvent(
        QStringLiteral("launch helper runas path=%1 args=%2").arg(helperPath, helperArguments));

    if (ShellExecuteExW(&executeInfo)) {
        logOpenHardwareMonitorHelperEvent(QStringLiteral("launch helper runas accepted"));
        if (WaitNamedPipeW(kOpenHardwareMonitorPipeName, kOpenHardwareMonitorHelperStartupWaitMs)) {
            _openHwHelperLaunchPending = false;
            _openHwHelperLaunchDeadlineTick = 0;
            _nextOpenHwHelperLaunchTick = GetTickCount64() + kOpenHardwareMonitorHelperLaunchCooldownMs;
            logOpenHardwareMonitorHelperStateThrottled(
                QStringLiteral("pipe_ready_after_launch"),
                QStringLiteral("helper pipe ready immediately after launch"));
        }
        if (executeInfo.hProcess)
            CloseHandle(executeInfo.hProcess);
        return;
    }

    _openHwHelperLaunchPending = false;
    _openHwHelperLaunchDeadlineTick = 0;

    const DWORD launchError = GetLastError();
    _openHwHelperLaunchAttempted = false;
    if (launchError == ERROR_CANCELLED)
        _openHwHelperLaunchAttempted = true;

    logOpenHardwareMonitorHelperEvent(
        QStringLiteral("launch helper runas failed error=%1").arg(launchError));

    _nextOpenHwHelperLaunchTick = now
        + (launchError == ERROR_CANCELLED
               ? kOpenHardwareMonitorHelperLaunchCancelCooldownMs
               : kOpenHardwareMonitorHelperLaunchCooldownMs);
}

void HomePage::shutdownOpenHardwareMonitorHelper()
{
    _openHwHelperLaunchPending = false;
    _openHwHelperLaunchDeadlineTick = 0;
    _openHwHelperLaunchAttempted = false;
    logOpenHardwareMonitorHelperEvent(QStringLiteral("shutdown helper requested"));
    requestOpenHardwareMonitorHelper(OpenHardwareMonitorHelperCommand_Shutdown);
}

void HomePage::_onHelperLaunchFinished(bool launched,
                                       bool pipeReady,
                                       DWORD launchError,
                                       ULONGLONG now)
{
    _openHwHelperLaunchInFlight = false;

    if (launched) {
        _openHwHelperLaunchAttempted = true;
        _openHwHelperLaunchPending = !pipeReady;
        _openHwHelperLaunchDeadlineTick = pipeReady
            ? 0
            : now + kOpenHardwareMonitorHelperLaunchPendingTimeoutMs;
        _nextOpenHwHelperLaunchTick = now + kOpenHardwareMonitorHelperLaunchCooldownMs;

        logOpenHardwareMonitorHelperEvent(
            QStringLiteral("helper launch finished launched=1 pipeReady=%1")
                .arg(pipeReady ? 1 : 0));
        return;
    }

    _openHwHelperLaunchPending = false;
    _openHwHelperLaunchDeadlineTick = 0;
    _openHwHelperLaunchAttempted = (launchError == ERROR_CANCELLED);
    _nextOpenHwHelperLaunchTick = now
        + (launchError == ERROR_CANCELLED
               ? kOpenHardwareMonitorHelperLaunchCancelCooldownMs
               : kOpenHardwareMonitorHelperLaunchCooldownMs);

    logOpenHardwareMonitorHelperEvent(
        QStringLiteral("helper launch finished launched=0 error=%1").arg(launchError));
}

ElaScrollPageArea* HomePage::makeMetricRow(const QString& label,
                                           ElaProgressBar*& bar,
                                           ElaText*& val)
{
    ElaScrollPageArea* area = new ElaScrollPageArea(this);
    area->setFixedHeight(95);

    QVBoxLayout* layout = new QVBoxLayout(area);
    layout->setContentsMargins(14, 8, 14, 8);
    layout->setSpacing(4);

    QHBoxLayout* header = new QHBoxLayout();
    ElaText* title = new ElaText(label, this);
    title->setTextPixelSize(14);

    val = new ElaText("--", this);
    val->setTextPixelSize(14);

    header->addWidget(title);
    header->addStretch();
    header->addWidget(val);

    bar = new ElaProgressBar(this);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setTextVisible(false);

    layout->addLayout(header);
    layout->addWidget(bar);
    return area;
}

ElaScrollPageArea* HomePage::makeDetailedRow(const QString& label,
                                             ElaProgressBar*& bar,
                                             ElaText*& val,
                                             ElaText*& freqVal,
                                             ElaText*& tempVal,
                                             ElaText*& powerVal)
{
    ElaScrollPageArea* area = new ElaScrollPageArea(this);
    area->setFixedHeight(95);

    QVBoxLayout* layout = new QVBoxLayout(area);
    layout->setContentsMargins(14, 8, 14, 8);
    layout->setSpacing(4);

    QHBoxLayout* header = new QHBoxLayout();
    ElaText* title = new ElaText(label, this);
    title->setTextPixelSize(14);

    freqVal = new ElaText("--", this);
    freqVal->setTextPixelSize(13);

    tempVal = new ElaText("--", this);
    tempVal->setTextPixelSize(13);

    powerVal = new ElaText("--", this);
    powerVal->setTextPixelSize(13);

    val = new ElaText("-- %", this);
    val->setTextPixelSize(14);

    header->addWidget(title);
    header->addStretch();
    header->addWidget(freqVal);
    header->addSpacing(12);
    header->addWidget(tempVal);
    header->addSpacing(12);
    header->addWidget(powerVal);
    header->addSpacing(12);
    header->addWidget(val);

    bar = new ElaProgressBar(this);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setTextVisible(false);

    layout->addLayout(header);
    layout->addWidget(bar);
    return area;
}

bool HomePage::systemMetricsFromOpenHardwareMonitor(OpenHardwareMonitorSystemMetrics& metrics)
{
    if (requestOpenHardwareMonitorHelper(OpenHardwareMonitorHelperCommand_GetSystemMetrics, &metrics))
        return true;

    ensureOpenHardwareMonitorHelper();
    return false;
}

void HomePage::setOscSelectedParameters(const QStringList& parameters)
{
    _oscSelectedParameters = parameters;
}

QString HomePage::buildOscChatboxMessageFromSelection() const
{
    if (_oscSelectedParameters.isEmpty())
        return QStringLiteral("VRCOSC monitoring active");

    const QString cpuName = fromWideBuffer(_latestMetrics.cpuName);
    const QString gpuName = fromWideBuffer(_latestMetrics.gpuName);
    const QString ramName = fromWideBuffer(_latestMetrics.ramName);

    const int cpuPercent = _latestMetrics.cpu.usagePercent;
    const int gpuPercent = _latestMetrics.gpu.usagePercent;
    const qint64 memoryUsedMB = _latestMetrics.memory.usedMB;
    const qint64 memoryTotalMB = _latestMetrics.memory.totalMB;
    const qint64 vramUsedMB = _latestMetrics.vram.usedMB;
    const qint64 vramTotalMB = _latestMetrics.vram.totalMB;

    QStringList lines;
    QStringList rowParts;
    rowParts.reserve(_oscSelectedParameters.size());

    for (const QString& parameterRaw : _oscSelectedParameters) {
        if (parameterRaw == QStringLiteral("|||rowbreak")) {
            if (!rowParts.isEmpty()) {
                lines.push_back(rowParts.join(QString()));
                rowParts.clear();
            }
            continue;
        }

        const QStringList tokens = parameterRaw.split(QStringLiteral("|||"));
        const QString parameter = tokens.isEmpty() ? QString() : tokens.first();
        const bool noPrefix = tokens.contains(QStringLiteral("noprefix"));
        const bool shortMode = tokens.contains(QStringLiteral("short"));
        const bool appendSpace = tokens.contains(QStringLiteral("space"));
        auto appendValue = [&](const QString& prefix, const QString& value) {
            QString text = noPrefix ? value : QStringLiteral("%1:%2").arg(prefix, value);
            if (appendSpace)
                text += QStringLiteral(" ");
            rowParts.push_back(text);
        };

        if (parameter == QStringLiteral("CPU\u540d\u5b57")) {
            const QString cpuDisplay = shortMode ? shortHardwareModel(cpuName) : cpuName;
            appendValue(QStringLiteral("CPU"), valueOrDash(cpuDisplay));
        } else if (parameter == QStringLiteral("CPU%")) {
            appendValue(QStringLiteral("CPU"), formatPercentValue(cpuPercent));
        } else if (parameter == QStringLiteral("CPU\u7535\u538b")) {
            appendValue(QStringLiteral("CPUV"),
                        formatVoltageValue(_latestMetrics.cpu.voltageV, shortMode ? 2 : 3));
        } else if (parameter == QStringLiteral("CPU\u6e29\u5ea6")) {
            appendValue(QStringLiteral("CPUT"), formatTemperatureValue(_latestMetrics.cpu.temperatureC));
        } else if (parameter == QStringLiteral("CPU\u9891\u7387")) {
            appendValue(QStringLiteral("CPUF"),
                        formatFrequencyGValue(_latestMetrics.cpu.frequencyGHz, shortMode ? 1 : 2));
        } else if (parameter == QStringLiteral("CPU\u529f\u8017")) {
            appendValue(QStringLiteral("CPUP"), formatPowerValue(_latestMetrics.cpu.powerW));
        } else if (parameter == QStringLiteral("GPU\u540d\u5b57")) {
            const QString gpuDisplay = shortMode ? shortHardwareModel(gpuName) : gpuName;
            appendValue(QStringLiteral("GPU"), valueOrDash(gpuDisplay));
        } else if (parameter == QStringLiteral("GPU%")) {
            appendValue(QStringLiteral("GPU"), formatPercentValue(gpuPercent));
        } else if (parameter == QStringLiteral("GPU\u7535\u538b")) {
            appendValue(QStringLiteral("GPUV"),
                        formatVoltageValue(_latestMetrics.gpu.voltageV, shortMode ? 2 : 3));
        } else if (parameter == QStringLiteral("GPU\u6e29\u5ea6")) {
            appendValue(QStringLiteral("GPUT"), formatTemperatureValue(_latestMetrics.gpu.temperatureC));
        } else if (parameter == QStringLiteral("GPU\u9891\u7387")) {
            const int gpuFrequencyMHz = _latestMetrics.gpu.frequencyMHz;
            const QString gpuFrequencyText = shortMode
                                                 ? (gpuFrequencyMHz > 0 ? QString::number(gpuFrequencyMHz)
                                                                        : QStringLiteral("--"))
                                                 : formatFrequencyMHzValue(gpuFrequencyMHz);
            appendValue(QStringLiteral("GPUF"), gpuFrequencyText);
        } else if (parameter == QStringLiteral("\u663e\u5b58\u5360\u7528")) {
            appendValue(QStringLiteral("VRAM"),
                        shortMode ? formatUsedGbValue(vramUsedMB)
                                  : formatGbCompactValue(vramUsedMB, vramTotalMB));
        } else if (parameter == QStringLiteral("\u663e\u5b58\u5360\u7528\u5b8c\u6574")) {
            appendValue(QStringLiteral("VRAM"), formatMbFullValue(vramUsedMB, vramTotalMB));
        } else if (parameter == QStringLiteral("\u663e\u5b58\u5360\u7528\u7f29\u5199")) {
            appendValue(QStringLiteral("VRAM"), formatGbShortValue(vramUsedMB, vramTotalMB));
        } else if (parameter == QStringLiteral("GPU\u529f\u8017")) {
            appendValue(QStringLiteral("GPUP"), formatPowerValue(_latestMetrics.gpu.powerW));
        } else if (parameter == QStringLiteral("RAM\u540d\u5b57")) {
            appendValue(QStringLiteral("RAM"), valueOrDash(ramName));
        } else if (parameter == QStringLiteral("RAM%")) {
            appendValue(QStringLiteral("RAM"), formatPercentFromUsage(memoryUsedMB, memoryTotalMB));
        } else if (parameter == QStringLiteral("RAM\u9891\u7387")) {
            appendValue(QStringLiteral("RAMF"), formatFrequencyMHzValue(_latestMetrics.memory.frequencyMHz));
        } else if (parameter == QStringLiteral("\u5185\u5b58\u5360\u7528")) {
            appendValue(QStringLiteral("RAM"),
                        shortMode ? formatUsedGbValue(memoryUsedMB)
                                  : formatGbCompactValue(memoryUsedMB, memoryTotalMB));
        } else if (parameter == QStringLiteral("\u5185\u5b58\u5360\u7528\u5b8c\u6574")) {
            appendValue(QStringLiteral("RAM"), formatMbFullValue(memoryUsedMB, memoryTotalMB));
        } else if (parameter == QStringLiteral("\u5185\u5b58\u5360\u7528\u7f29\u5199")) {
            appendValue(QStringLiteral("RAM"), formatGbShortValue(memoryUsedMB, memoryTotalMB));
        } else if (parameter == QStringLiteral("\u529f\u8017\u5b8c\u6574")) {
            const QString cpuPower = formatPowerValue(_latestMetrics.cpu.powerW);
            const QString gpuPower = formatPowerValue(_latestMetrics.gpu.powerW);
            const QString value = shortMode
                                      ? QStringLiteral("%1/%2").arg(cpuPower, gpuPower)
                                      : QStringLiteral("CPU %1/GPU %2").arg(cpuPower, gpuPower);
            appendValue(QStringLiteral("PWR"), value);
        } else if (parameter == QStringLiteral("FPS")) {
            appendValue(QStringLiteral("FPS"), QStringLiteral("--"));
        } else if (parameter == QStringLiteral("Time")) {
            appendValue(QStringLiteral("TIME"),
                        QDateTime::currentDateTime().toString(
                            shortMode ? QStringLiteral("HH:mm")
                                      : QStringLiteral("HH:mm:ss")));
        } else if (parameter == QStringLiteral("Battery")) {
            appendValue(QStringLiteral("BAT"), QStringLiteral("--"));
        } else if (parameter == QStringLiteral("Heart")) {
            appendValue(QStringLiteral("HEART"), QStringLiteral("--"));
        }
    }

    if (!rowParts.isEmpty())
        lines.push_back(rowParts.join(QString()));

    if (lines.isEmpty())
        return QStringLiteral("VRCOSC monitoring active");

    return lines.join(QStringLiteral("\n"));
}

void HomePage::onOscToggleChanged(bool checked)
{
    _oscSendButton->setText(checked ? QStringLiteral("Stop OSC")
                                    : QStringLiteral("Send OSC"));

    if (checked) {
        sendOscChatboxMessage();
        _oscSendTimer->start();
        return;
    }

    _oscSendTimer->stop();
}

void HomePage::sendOscChatboxMessage()
{
    const QString message = buildOscChatboxMessageFromSelection();
    const QByteArray packet = buildChatboxInputPacket(message);
    QUdpSocket socket;
    const qint64 writtenBytes = socket.writeDatagram(packet, resolveOscTargetAddress(), kOscTargetPort);

    if (writtenBytes != packet.size()) {
        logOpenHardwareMonitorHelperFailureThrottled(
            QStringLiteral("osc_chat_send_%1").arg(socket.error()),
            QStringLiteral("osc chatbox send failed: %1").arg(socket.errorString()));
        return;
    }

    logOpenHardwareMonitorHelperSuccessThrottled(
        QStringLiteral("osc_chat_sent"),
        QStringLiteral("osc chatbox packet sent to localhost:%1 (/chatbox/input, b=true, n=false)")
            .arg(kOscTargetPort));
}

void HomePage::updateMetrics()
{
    OpenHardwareMonitorSystemMetrics metrics{};
    const bool hasSystemMetrics = systemMetricsFromOpenHardwareMonitor(metrics);
    _latestMetrics = metrics;
    _hasLatestMetrics = hasSystemMetrics;

    const bool cpuAvailable = hasSystemMetrics && hasCpuMetrics(metrics);
    _cpuBar->setValue(cpuAvailable ? metrics.cpu.usagePercent : 0);
    _cpuVal->setText(formatUsageText(cpuAvailable, metrics.cpu.usagePercent));
    _cpuFreqVal->setText(formatFrequencyGHzText(cpuAvailable ? metrics.cpu.frequencyGHz : 0.0));
    _cpuTempVal->setText(formatTemperatureText(cpuAvailable ? metrics.cpu.temperatureC : 0));
    _cpuPowerVal->setText(formatPowerText(cpuAvailable ? metrics.cpu.powerW : 0));

    const bool memoryAvailable = hasSystemMetrics && hasMemoryMetrics(metrics);
    const qint64 memoryUsed = memoryAvailable ? metrics.memory.usedMB : 0;
    const qint64 memoryTotal = memoryAvailable ? metrics.memory.totalMB : 0;
    const int memoryPercent = memoryTotal > 0 ? static_cast<int>(memoryUsed * 100 / memoryTotal) : 0;
    _memBar->setValue(memoryPercent);
    _memVal->setText(formatCapacityText(memoryUsed, memoryTotal));

    const bool gpuAvailable = hasSystemMetrics && hasGpuMetrics(metrics);
    _gpuBar->setValue(gpuAvailable ? metrics.gpu.usagePercent : 0);
    _gpuVal->setText(formatUsageText(gpuAvailable, metrics.gpu.usagePercent));
    _gpuFreqVal->setText(formatFrequencyGHzText(gpuAvailable ? metrics.gpu.frequencyMHz / 1000.0 : 0.0));
    _gpuTempVal->setText(formatTemperatureText(gpuAvailable ? metrics.gpu.temperatureC : 0));
    _gpuPowerVal->setText(formatPowerText(gpuAvailable ? metrics.gpu.powerW : 0));

    const bool vramAvailable = hasSystemMetrics && hasVramMetrics(metrics);
    const qint64 vramUsed = vramAvailable ? metrics.vram.usedMB : 0;
    const qint64 vramTotal = vramAvailable ? metrics.vram.totalMB : 0;
    const int vramPercent = vramTotal > 0 ? static_cast<int>(vramUsed * 100 / vramTotal) : 0;
    _vramBar->setValue(vramPercent);
    _vramVal->setText(formatCapacityText(vramUsed, vramTotal));
}
