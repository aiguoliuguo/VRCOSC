#ifndef LIGHTHOUSE_SERVICE_H
#define LIGHTHOUSE_SERVICE_H

#include "lighthouse_types.h"

#include <QSet>
#include <QString>
#include <QVector>
#include <QObject>
#include <QtGlobal>

class QThread;
class QTimer;

enum class LighthouseControlOperation
{
    PowerOn,
    Sleep,
    Standby,
    Identify,
};

class LighthouseService : public QObject
{
    Q_OBJECT

public:
    explicit LighthouseService(QObject* parent = nullptr);
    ~LighthouseService() override;

    [[nodiscard]] bool hasBluetoothAdapter() const;
    [[nodiscard]] bool isDiscovering() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] const QVector<LighthouseDevice>& devices() const;

public slots:
    void refreshBluetoothAvailability();
    void startDiscovery();
    void stopDiscovery();
    void controlDevice(int index, LighthouseControlOperation operation);
    void controlDevices(const QVector<int>& indices, LighthouseControlOperation operation);
    void controlAllDevices(LighthouseControlOperation operation);
    void setDeviceControlId(int index, const QString& lighthouseId);
    void removeDevice(const QString& bluetoothAddress);

signals:
    void availabilityChanged();
    void discoveryStateChanged();
    void devicesChanged();
    void statusChanged();
    void operationFinished(bool success, QString title, QString text);

    // 内部信号：从工作线程向主线程传递蓝牙可用性结果
    void _bluetoothAvailabilityResult(bool available, QString status);

private slots:
    void _onBluetoothAvailabilityResult(bool available, QString status);
    void _onDiscoveryTimeout();

private:
    [[nodiscard]] QString configFilePath() const;
    void loadPersistedDevices();
    void savePersistedDevices() const;
    void setStatusText(const QString& statusText);
    void handleDeviceAdded(const QString& id, const QString& name, const QString& bluetoothAddress);
    void handleEnumerationCompleted();
    void clearWatchers();

    bool _hasBluetoothAdapter{false};
    bool _isDiscovering{false};
    QString _statusText{QStringLiteral("未开始扫描")};
    QVector<LighthouseDevice> _devices;
    QSet<QString> _knownDeviceIds;
    QSet<QString> _knownBluetoothAddresses;
    quint64 _discoverySessionId{0};
    QTimer* _discoveryTimeoutTimer{nullptr};
    void* _watcherState{nullptr};
};

#endif // LIGHTHOUSE_SERVICE_H
