#include "settingspage.h"

#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include "ElaScrollPageArea.h"
#include "ElaText.h"

#include <Windows.h>
#include <iphlpapi.h>

#include <QByteArray>
#include <QFileInfo>
#include <QFrame>
#include <QHash>
#include <QHostAddress>
#include <QHostInfo>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLineEdit>
#include <QSet>
#include <QSizePolicy>
#include <QStringList>
#include <QUdpSocket>
#include <QVBoxLayout>
#include <QWidget>
#include <QtEndian>

#include <algorithm>

namespace {

struct PortOccupancyInfo
{
    QString protocol;
    quint32 pid{0};
    QString processName;
};

quint16 localPortFromNetworkOrder(DWORD networkOrderPort)
{
    return qFromBigEndian(static_cast<quint16>(networkOrderPort & 0xFFFF));
}

QString processNameFromPid(DWORD pid)
{
    HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!processHandle)
        return QStringLiteral("鏈煡杩涚▼");

    wchar_t buffer[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    QString processName = QStringLiteral("鏈煡杩涚▼");
    if (QueryFullProcessImageNameW(processHandle, 0, buffer, &size))
        processName = QFileInfo(QString::fromWCharArray(buffer)).fileName();

    CloseHandle(processHandle);
    return processName;
}

QVector<PortOccupancyInfo> queryUdpOccupancy(quint16 port)
{
    DWORD bufferSize = 0;
    QVector<PortOccupancyInfo> results;

    DWORD ret = GetExtendedUdpTable(nullptr, &bufferSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    if (ret != ERROR_INSUFFICIENT_BUFFER)
        return results;

    QByteArray buffer(static_cast<int>(bufferSize), 0);
    auto* table = reinterpret_cast<PMIB_UDPTABLE_OWNER_PID>(buffer.data());
    ret = GetExtendedUdpTable(table, &bufferSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    if (ret != NO_ERROR)
        return results;

    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_UDPROW_OWNER_PID& row = table->table[i];
        if (localPortFromNetworkOrder(row.dwLocalPort) != port)
            continue;

        results.push_back({QStringLiteral("UDP"), row.dwOwningPid, processNameFromPid(row.dwOwningPid)});
    }

    return results;
}

QVector<PortOccupancyInfo> queryTcpOccupancy(quint16 port)
{
    DWORD bufferSize = 0;
    QVector<PortOccupancyInfo> results;

    DWORD ret = GetExtendedTcpTable(nullptr, &bufferSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (ret != ERROR_INSUFFICIENT_BUFFER)
        return results;

    QByteArray buffer(static_cast<int>(bufferSize), 0);
    auto* table = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buffer.data());
    ret = GetExtendedTcpTable(table, &bufferSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (ret != NO_ERROR)
        return results;

    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_TCPROW_OWNER_PID& row = table->table[i];
        if (localPortFromNetworkOrder(row.dwLocalPort) != port)
            continue;

        results.push_back({QStringLiteral("TCP"), row.dwOwningPid, processNameFromPid(row.dwOwningPid)});
    }

    return results;
}

QVector<PortOccupancyInfo> queryPortOccupancy(quint16 port)
{
    QVector<PortOccupancyInfo> results = queryUdpOccupancy(port);
    results += queryTcpOccupancy(port);
    return results;
}

QString formatOccupancyMessage(quint16 port, const QVector<PortOccupancyInfo>& occupancies, QVector<quint32>* pids)
{
    if (pids)
        pids->clear();

    if (occupancies.isEmpty())
        return QStringLiteral("绔彛 %1 鏈鍗犵敤").arg(port);

    struct DisplayInfo
    {
        QString processName;
        QSet<QString> protocols;
    };

    QHash<quint32, DisplayInfo> displayInfos;
    for (const PortOccupancyInfo& info : occupancies) {
        auto& displayInfo = displayInfos[info.pid];
        if (displayInfo.processName.isEmpty())
            displayInfo.processName = info.processName;
        displayInfo.protocols.insert(info.protocol);
    }

    QList<quint32> orderedPids = displayInfos.keys();
    std::sort(orderedPids.begin(), orderedPids.end());

    QStringList details;
    QVector<quint32> uniquePids;
    uniquePids.reserve(orderedPids.size());
    for (quint32 pid : orderedPids) {
        const DisplayInfo& displayInfo = displayInfos[pid];
        QStringList protocols = displayInfo.protocols.values();
        protocols.sort();
        details.push_back(QStringLiteral("%1 (PID: %2锛屽崗璁? %3)")
                              .arg(displayInfo.processName)
                              .arg(pid)
                              .arg(protocols.join('/')));
        uniquePids.push_back(pid);
    }

    if (pids)
        *pids = uniquePids;

    return QStringLiteral("绔彛 %1 宸茶鍗犵敤锛歕n%2").arg(port).arg(details.join('\n'));
}

bool terminateProcessByPid(quint32 pid, QString* errorMessage)
{
    HANDLE processHandle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!processHandle) {
        if (errorMessage)
            *errorMessage = QStringLiteral("鏃犳硶鎵撳紑杩涚▼锛岄敊璇爜锛?1").arg(GetLastError());
        return false;
    }

    const bool terminated = TerminateProcess(processHandle, 1) != FALSE;
    const DWORD errorCode = terminated ? ERROR_SUCCESS : GetLastError();
    const DWORD waitResult = terminated ? WaitForSingleObject(processHandle, 3000) : WAIT_FAILED;
    CloseHandle(processHandle);

    if (!terminated) {
        if (errorMessage)
            *errorMessage = QStringLiteral("缁撴潫澶辫触锛岄敊璇爜锛?1").arg(errorCode);
        return false;
    }

    if (waitResult != WAIT_OBJECT_0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("绛夊緟杩涚▼閫€鍑哄け璐ワ紝缁撴灉鐮侊細%1").arg(waitResult);
        return false;
    }

    return true;
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

SettingsPage::SettingsPage(QWidget* parent)
    : ElaScrollPage(parent)
{
    setTitleVisible(false);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 15, 0);
    layout->setSpacing(6);

    layout->addWidget(createOscPortCard(central));
    layout->addWidget(createOscChatboxTestCard(central));
    layout->addStretch();

    addCentralWidget(central, true, false, 0);
    detectPortOccupancy();
}

void SettingsPage::detectPortOccupancy()
{
    bool isValid = false;
    const quint16 port = currentPort(&isValid);
    if (!isValid) {
        _occupiedPids.clear();
        _terminateButton->setEnabled(false);
        setStatusMessage(QStringLiteral("璇疯緭鍏?1 - 65535 涔嬮棿鐨勭鍙ｅ彿"), true);
        return;
    }

    setStatusMessage(formatOccupancyMessage(port, queryPortOccupancy(port), &_occupiedPids));
    _terminateButton->setEnabled(!_occupiedPids.isEmpty());
}

void SettingsPage::sendOscChatboxTest()
{
    bool isValid = false;
    const quint16 port = currentPort(&isValid);
    if (!isValid) {
        setOscTestStatusMessage(QStringLiteral("Please enter an OSC port between 1 and 65535"), true);
        return;
    }

    const QString message = _oscTestInputEdit->text().trimmed();
    if (message.isEmpty()) {
        setOscTestStatusMessage(QStringLiteral("璇疯緭鍏ヨ鍙戦€佺殑鑱婂ぉ娴嬭瘯鏂囨湰"), true);
        return;
    }

    const QByteArray packet = buildChatboxInputPacket(message);
    const QHostAddress targetAddress = resolveOscTargetAddress();
    QUdpSocket socket;
    const qint64 writtenBytes = socket.writeDatagram(packet, targetAddress, port);
    if (writtenBytes != packet.size()) {
        const QString errorText = socket.errorString().isEmpty() ? QStringLiteral("鏈煡閿欒") : socket.errorString();
        setOscTestStatusMessage(QStringLiteral("鍙戦€佸け璐ワ細%1").arg(errorText), true);
        return;
    }

    setOscTestStatusMessage(QStringLiteral("Sent to localhost:%1: /chatbox/input (immediate, silent)").arg(port));
}

void SettingsPage::terminatePortOccupancy()
{
    bool isValid = false;
    const quint16 port = currentPort(&isValid);
    if (!isValid) {
        setStatusMessage(QStringLiteral("璇疯緭鍏?1 - 65535 涔嬮棿鐨勭鍙ｅ彿"), true);
        return;
    }

    QVector<quint32> pids;
    const QString occupancyMessage = formatOccupancyMessage(port, queryPortOccupancy(port), &pids);
    if (pids.isEmpty()) {
        _occupiedPids.clear();
        _terminateButton->setEnabled(false);
        setStatusMessage(occupancyMessage);
        return;
    }

    QStringList successMessages;
    QStringList failureMessages;
    for (quint32 pid : pids) {
        QString errorMessage;
        if (terminateProcessByPid(pid, &errorMessage))
            successMessages.push_back(QStringLiteral("PID %1 terminated").arg(pid));
        else
            failureMessages.push_back(QStringLiteral("PID %1: %2").arg(pid).arg(errorMessage));
    }

    QStringList lines;
    lines << formatOccupancyMessage(port, queryPortOccupancy(port), &_occupiedPids);
    if (!successMessages.isEmpty())
        lines << QStringLiteral("Terminate result: ") + successMessages.join(QStringLiteral("; "));
    if (!failureMessages.isEmpty())
        lines << QStringLiteral("Failure reason: ") + failureMessages.join(QStringLiteral("; "));

    _terminateButton->setEnabled(!_occupiedPids.isEmpty());
    setStatusMessage(lines.join('\n'), !failureMessages.isEmpty());
}

ElaScrollPageArea* SettingsPage::createOscChatboxTestCard(QWidget* parent)
{
    auto* card = new ElaScrollPageArea(parent);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    card->setFixedHeight(220);

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 16, 16, 16);
    cardLayout->setSpacing(12);

    auto* title = new ElaText(QStringLiteral("OSC Chat Send Test"), card);
    title->setTextPixelSize(18);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));

    auto* description = new ElaText(
        QStringLiteral("Send /chatbox/input to current port with b=True and n=False for quick verification."),
        card);
    description->setWordWrap(true);
    description->setTextPixelSize(13);
    description->setStyleSheet(QStringLiteral("color: rgb(140, 140, 140);"));

    auto* separator = new QFrame(card);
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QStringLiteral("color: rgba(120, 120, 120, 0.35);"));

    auto* inputRow = new QWidget(card);
    auto* inputLayout = new QHBoxLayout(inputRow);
    inputLayout->setContentsMargins(0, 0, 0, 0);
    inputLayout->setSpacing(8);

    _oscTestInputEdit = new ElaLineEdit(inputRow);
    _oscTestInputEdit->setPlaceholderText(QStringLiteral("Enter test text, then click Send"));
    _oscTestInputEdit->setClearButtonEnabled(true);

    _sendTestButton = new ElaPushButton(QStringLiteral("Send"), inputRow);
    _sendTestButton->setFixedWidth(96);

    inputLayout->addWidget(_oscTestInputEdit);
    inputLayout->addWidget(_sendTestButton);

    _oscTestStatusText = new ElaText(QStringLiteral("Waiting to send test message"), card);
    _oscTestStatusText->setWordWrap(true);
    _oscTestStatusText->setMinimumHeight(48);
    _oscTestStatusText->setTextPixelSize(14);

    cardLayout->addWidget(title);
    cardLayout->addWidget(description);
    cardLayout->addWidget(separator);
    cardLayout->addWidget(inputRow);
    cardLayout->addWidget(_oscTestStatusText);
    cardLayout->addStretch();

    connect(_sendTestButton, &ElaPushButton::clicked, this, &SettingsPage::sendOscChatboxTest);
    connect(_oscTestInputEdit, &QLineEdit::returnPressed, this, &SettingsPage::sendOscChatboxTest);

    return card;
}

ElaScrollPageArea* SettingsPage::createOscPortCard(QWidget* parent)
{
    auto* card = new ElaScrollPageArea(parent);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    card->setFixedHeight(260);

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 16, 16, 16);
    cardLayout->setSpacing(12);

    auto* title = new ElaText(QStringLiteral("OSC Port Occupancy Check"), card);
    title->setTextPixelSize(18);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));

    auto* description = new ElaText(
        QStringLiteral("By default checks VRChat OSC receive port 9000. You can change the port and detect/terminate occupancy."),
        card);
    description->setWordWrap(true);
    description->setTextPixelSize(13);
    description->setStyleSheet(QStringLiteral("color: rgb(140, 140, 140);"));

    auto* separator = new QFrame(card);
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QStringLiteral("color: rgba(120, 120, 120, 0.35);"));

    auto* portRow = new QWidget(card);
    auto* portLayout = new QHBoxLayout(portRow);
    portLayout->setContentsMargins(0, 0, 0, 0);
    portLayout->setSpacing(8);

    auto* portLabel = new ElaText(QStringLiteral("Port"), portRow);
    portLabel->setFixedWidth(56);
    portLabel->setTextPixelSize(14);

    _portEdit = new ElaLineEdit(portRow);
    _portEdit->setText(QStringLiteral("9000"));
    _portEdit->setPlaceholderText(QStringLiteral("璇疯緭鍏ョ鍙ｅ彿"));
    _portEdit->setClearButtonEnabled(false);
    _portEdit->setValidator(new QIntValidator(1, 65535, _portEdit));
    _portEdit->setFixedWidth(160);

    portLayout->addWidget(portLabel);
    portLayout->addWidget(_portEdit);
    portLayout->addStretch();

    auto* buttonRow = new QWidget(card);
    auto* buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);

    _detectButton = new ElaPushButton(QStringLiteral("Detect"), buttonRow);
    _terminateButton = new ElaPushButton(QStringLiteral("缁撴潫鍗犵敤"), buttonRow);
    _terminateButton->setEnabled(false);

    buttonLayout->addWidget(_detectButton);
    buttonLayout->addWidget(_terminateButton);
    buttonLayout->addStretch();

    _statusText = new ElaText(QStringLiteral("姝ｅ湪妫€娴嬬鍙ｇ姸鎬?.."), card);
    _statusText->setWordWrap(true);
    _statusText->setMinimumHeight(72);
    _statusText->setTextPixelSize(14);

    cardLayout->addWidget(title);
    cardLayout->addWidget(description);
    cardLayout->addWidget(separator);
    cardLayout->addWidget(portRow);
    cardLayout->addWidget(buttonRow);
    cardLayout->addWidget(_statusText);
    cardLayout->addStretch();

    connect(_detectButton, &ElaPushButton::clicked, this, &SettingsPage::detectPortOccupancy);
    connect(_terminateButton, &ElaPushButton::clicked, this, &SettingsPage::terminatePortOccupancy);
    connect(_portEdit, &QLineEdit::returnPressed, this, &SettingsPage::detectPortOccupancy);

    return card;
}

quint16 SettingsPage::currentPort(bool* isValid) const
{
    bool ok = false;
    const int port = _portEdit->text().trimmed().toInt(&ok);
    const bool valid = ok && port >= 1 && port <= 65535;
    if (isValid)
        *isValid = valid;
    return valid ? static_cast<quint16>(port) : 0;
}

void SettingsPage::setOscTestStatusMessage(const QString& message, bool isError)
{
    _oscTestStatusText->setText(message);
    _oscTestStatusText->setStyleSheet(
        isError ? QStringLiteral("color: rgb(219, 68, 55);")
                : QStringLiteral("color: rgb(110, 190, 110);"));
}

void SettingsPage::setStatusMessage(const QString& message, bool isError)
{
    _statusText->setText(message);
    _statusText->setStyleSheet(
        isError ? QStringLiteral("color: rgb(219, 68, 55);")
                : QStringLiteral("color: rgb(110, 190, 110);"));
}
