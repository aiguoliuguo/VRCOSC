#include "lighthouse_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QSettings>
#include <QThread>
#include <QTimer>

#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage::Streams;

constexpr winrt::guid kV1ControlService{L"0000cb00-0000-1000-8000-00805f9b34fb"};
constexpr winrt::guid kV1PowerCharacteristic{L"0000cb01-0000-1000-8000-00805f9b34fb"};
constexpr winrt::guid kV2ControlService{L"00001523-1212-efde-1523-785feabcd124"};
constexpr winrt::guid kV2PowerCharacteristic{L"00001525-1212-efde-1523-785feabcd124"};
constexpr winrt::guid kV2IdentifyCharacteristic{L"00008421-1212-efde-1523-785feabcd124"};
constexpr int kDiscoveryTimeoutMs = 10000;

// 每个线程独立初始化 WinRT 公寓，避免跨线程 STA 死锁。
// 使用 MTA（多线程公寓），允许异步操作在任意线程完成回调，
// 无需 marshal 回发起线程，从根本上消除 .get() 阻塞死锁。
void ensureWinRtApartmentForCurrentThread()
{
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error& e) {
        // CO_E_NOTINITIALIZED 以外的错误忽略（公寓已初始化时正常抛出）
        if (e.code() != static_cast<HRESULT>(0x800401F0) /*CO_E_NOTINITIALIZED*/)
            (void)e;
    } catch (...) {
    }
}

QString toQString(const winrt::hstring& value)
{
    return QString::fromWCharArray(value.c_str());
}

QString normalizeBluetoothAddress(QString bluetoothAddress)
{
    QString normalized = bluetoothAddress.trimmed().toUpper();
    normalized.remove(QLatin1Char(':'));
    normalized.remove(QLatin1Char('-'));

    if (normalized.size() != 12)
        return bluetoothAddress.trimmed();

    QString formatted;
    formatted.reserve(17);
    for (int i = 0; i < normalized.size(); i += 2) {
        if (!formatted.isEmpty())
            formatted += QLatin1Char(':');
        formatted += normalized.mid(i, 2);
    }
    return formatted;
}

QString deviceAddressFromProperties(const DeviceInformation& info)
{
    constexpr wchar_t kDeviceAddressProperty[] = L"System.Devices.Aep.DeviceAddress";

    try {
        const auto properties = info.Properties();
        if (!properties.HasKey(kDeviceAddressProperty))
            return {};

        const auto value = properties.Lookup(kDeviceAddressProperty);
        if (value == nullptr)
            return {};

        const auto address = winrt::unbox_value_or<winrt::hstring>(value, winrt::hstring{});
        if (address.empty())
            return {};

        return normalizeBluetoothAddress(toQString(address));
    } catch (...) {
        return {};
    }
}

QString resolveBluetoothAddress(const DeviceInformation& info)
{
    const QString propertyAddress = deviceAddressFromProperties(info);
    if (!propertyAddress.isEmpty())
        return propertyAddress;

    try {
        const auto device = BluetoothLEDevice::FromIdAsync(info.Id()).get();
        if (device == nullptr)
            return {};

        return bluetoothAddressToString(device.BluetoothAddress());
    } catch (...) {
        return {};
    }
}

bool isLighthouseName(const QString& name)
{
    return name.startsWith(QStringLiteral("HTC BS"))
        || name.startsWith(QStringLiteral("LHB-"));
}

QString advertisementDeviceId(quint64 bluetoothAddress)
{
    return QStringLiteral("adv:%1").arg(bluetoothAddressToString(bluetoothAddress));
}

QString operationName(LighthouseControlOperation operation)
{
    switch (operation) {
    case LighthouseControlOperation::PowerOn:
        return QStringLiteral("开机");
    case LighthouseControlOperation::Sleep:
        return QStringLiteral("休眠");
    case LighthouseControlOperation::Standby:
        return QStringLiteral("待机");
    case LighthouseControlOperation::Identify:
        return QStringLiteral("识别");
    }

    return QStringLiteral("操作");
}

QString normalizeLighthouseId(const QString& lighthouseId)
{
    QString normalized = lighthouseId.trimmed().toUpper();
    normalized.remove(QLatin1Char(' '));
    normalized.remove(QLatin1Char(':'));
    normalized.remove(QLatin1Char('-'));
    return normalized;
}

QString versionToPersistentString(LighthouseVersion version)
{
    switch (version) {
    case LighthouseVersion::V1:
        return QStringLiteral("V1");
    case LighthouseVersion::V2:
        return QStringLiteral("V2");
    case LighthouseVersion::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

LighthouseVersion versionFromPersistentString(QString versionText)
{
    versionText = versionText.trimmed().toUpper();
    if (versionText == QStringLiteral("V1"))
        return LighthouseVersion::V1;
    if (versionText == QStringLiteral("V2"))
        return LighthouseVersion::V2;
    return LighthouseVersion::Unknown;
}

QString fallbackNameFromVersion(LighthouseVersion version)
{
    switch (version) {
    case LighthouseVersion::V1:
        return QStringLiteral("HTC BS");
    case LighthouseVersion::V2:
        return QStringLiteral("LHB-UNKNOWN");
    case LighthouseVersion::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

bool hasBluetoothLeAdapterForControl()
{
    const auto adapter = BluetoothAdapter::GetDefaultAsync().get();
    return adapter != nullptr && adapter.IsLowEnergySupported();
}

std::vector<std::uint8_t> buildV1ControlPayload(const QString& lighthouseId, bool powerOn)
{
    const QString normalizedId = normalizeLighthouseId(lighthouseId);
    if (normalizedId.size() != 8)
        throw std::runtime_error("V1 基站 ID 必须是 8 位十六进制");

    std::vector<std::uint8_t> payload = powerOn
        ? std::vector<std::uint8_t>{0x12, 0x00, 0x00, 0x00}
        : std::vector<std::uint8_t>{0x12, 0x02, 0x00, 0x01};

    std::vector<std::uint8_t> idBytes;
    idBytes.reserve(4);
    for (int i = 0; i < normalizedId.size(); i += 2) {
        bool ok = false;
        const auto value = static_cast<std::uint8_t>(normalizedId.mid(i, 2).toUInt(&ok, 16));
        if (!ok)
            throw std::runtime_error("V1 基站 ID 不是合法的十六进制");
        idBytes.push_back(value);
    }

    for (auto it = idBytes.rbegin(); it != idBytes.rend(); ++it)
        payload.push_back(*it);

    payload.resize(20, 0x00);
    return payload;
}

BluetoothLEDevice getBluetoothLeDevice(quint64 bluetoothAddress)
{
    constexpr int retryCount = 10;
    for (int attempt = 0; attempt < retryCount; ++attempt) {
        const auto device = BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddress).get();
        if (device != nullptr)
            return device;

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    throw std::runtime_error("找不到 Lighthouse 蓝牙设备");
}

GattDeviceService getGattService(const BluetoothLEDevice& device, const winrt::guid& serviceGuid)
{
    const auto result = device.GetGattServicesForUuidAsync(serviceGuid, BluetoothCacheMode::Cached).get();
    switch (result.Status()) {
    case GattCommunicationStatus::Success:
        if (result.Services().Size() > 0)
            return result.Services().GetAt(0);
        throw std::runtime_error("未找到目标 GATT Service");
    case GattCommunicationStatus::Unreachable:
    case GattCommunicationStatus::ProtocolError:
    case GattCommunicationStatus::AccessDenied:
        throw std::runtime_error("读取 GATT Service 失败");
    }

    throw std::runtime_error("未知的 GATT Service 状态");
}

GattCharacteristic getGattCharacteristic(const GattDeviceService& service, const winrt::guid& characteristicGuid)
{
    const auto result = service.GetCharacteristicsForUuidAsync(characteristicGuid, BluetoothCacheMode::Cached).get();
    switch (result.Status()) {
    case GattCommunicationStatus::Success:
        if (result.Characteristics().Size() > 0)
            return result.Characteristics().GetAt(0);
        throw std::runtime_error("未找到目标 GATT Characteristic");
    case GattCommunicationStatus::Unreachable:
    case GattCommunicationStatus::ProtocolError:
    case GattCommunicationStatus::AccessDenied:
        throw std::runtime_error("读取 GATT Characteristic 失败");
    }

    throw std::runtime_error("未知的 GATT Characteristic 状态");
}

void writeGattCharacteristic(const GattCharacteristic& characteristic, const std::vector<std::uint8_t>& data)
{
    DataWriter writer;
    writer.WriteBytes(winrt::array_view<const std::uint8_t>(data));

    const auto status = characteristic.WriteValueAsync(writer.DetachBuffer()).get();
    switch (status) {
    case GattCommunicationStatus::Success:
        return;
    case GattCommunicationStatus::Unreachable:
    case GattCommunicationStatus::ProtocolError:
    case GattCommunicationStatus::AccessDenied:
        throw std::runtime_error("写入 GATT Characteristic 失败");
    }

    throw std::runtime_error("未知的 GATT 写入状态");
}

void writePowerCharacteristic(const LighthouseDevice& device,
                              const winrt::guid& serviceGuid,
                              const winrt::guid& characteristicGuid,
                              const std::vector<std::uint8_t>& data)
{
    const auto bluetoothAddress = bluetoothAddressFromString(device.bluetoothAddress);
    if (!bluetoothAddress.has_value())
        throw std::runtime_error("蓝牙地址无效");

    const auto bleDevice = getBluetoothLeDevice(*bluetoothAddress);
    const auto service = getGattService(bleDevice, serviceGuid);
    const auto characteristic = getGattCharacteristic(service, characteristicGuid);
    writeGattCharacteristic(characteristic, data);
}

void controlLighthouseDevice(const LighthouseDevice& device, LighthouseControlOperation operation)
{
    if (!hasBluetoothLeAdapterForControl())
        throw std::runtime_error("未检测到可用的蓝牙 BLE 适配器");

    switch (device.version()) {
    case LighthouseVersion::V1:
        switch (operation) {
        case LighthouseControlOperation::PowerOn:
            writePowerCharacteristic(device,
                                     kV1ControlService,
                                     kV1PowerCharacteristic,
                                     buildV1ControlPayload(device.id, true));
            return;
        case LighthouseControlOperation::Sleep:
            writePowerCharacteristic(device,
                                     kV1ControlService,
                                     kV1PowerCharacteristic,
                                     buildV1ControlPayload(device.id, false));
            return;
        case LighthouseControlOperation::Standby:
            throw std::runtime_error("V1 基站不支持待机");
        case LighthouseControlOperation::Identify:
            throw std::runtime_error("V1 基站不支持识别");
        }
        break;
    case LighthouseVersion::V2:
        switch (operation) {
        case LighthouseControlOperation::PowerOn:
            writePowerCharacteristic(device,
                                     kV2ControlService,
                                     kV2PowerCharacteristic,
                                     std::vector<std::uint8_t>{0x01});
            return;
        case LighthouseControlOperation::Sleep:
            writePowerCharacteristic(device,
                                     kV2ControlService,
                                     kV2PowerCharacteristic,
                                     std::vector<std::uint8_t>{0x00});
            return;
        case LighthouseControlOperation::Standby:
            writePowerCharacteristic(device,
                                     kV2ControlService,
                                     kV2PowerCharacteristic,
                                     std::vector<std::uint8_t>{0x02});
            return;
        case LighthouseControlOperation::Identify:
            writePowerCharacteristic(device,
                                     kV2ControlService,
                                     kV2IdentifyCharacteristic,
                                     std::vector<std::uint8_t>{0x01});
            return;
        }
        break;
    case LighthouseVersion::Unknown:
        break;
    }

    throw std::runtime_error("当前设备类型不支持控制");
}

bool canControlLighthouseDevice(const LighthouseDevice& device, LighthouseControlOperation operation)
{
    switch (device.version()) {
    case LighthouseVersion::V1:
        switch (operation) {
        case LighthouseControlOperation::PowerOn:
        case LighthouseControlOperation::Sleep:
            return normalizeLighthouseId(device.id).size() == 8;
        case LighthouseControlOperation::Standby:
        case LighthouseControlOperation::Identify:
            return false;
        }
        break;
    case LighthouseVersion::V2:
        return true;
    case LighthouseVersion::Unknown:
        return false;
    }

    return false;
}

} // namespace

class LighthouseServicePrivate
{
public:
    winrt::Windows::Devices::Enumeration::DeviceWatcher watcher{nullptr};
    winrt::Windows::Devices::Enumeration::DeviceWatcher pairedWatcher{nullptr};
    winrt::Windows::Devices::Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher advertisementWatcher{nullptr};
    winrt::event_token watcherAddedToken{};
    winrt::event_token watcherCompletedToken{};
    winrt::event_token pairedWatcherAddedToken{};
    winrt::event_token advertisementReceivedToken{};
    std::mutex advertisementMutex;
    std::unordered_set<std::uint64_t> advertisementAddresses;
};

LighthouseService::LighthouseService(QObject* parent)
    : QObject(parent)
{
    connect(this, &LighthouseService::_bluetoothAvailabilityResult,
            this, &LighthouseService::_onBluetoothAvailabilityResult,
            Qt::QueuedConnection);
    _discoveryTimeoutTimer = new QTimer(this);
    _discoveryTimeoutTimer->setSingleShot(true);
    connect(_discoveryTimeoutTimer, &QTimer::timeout,
            this, &LighthouseService::_onDiscoveryTimeout);
    loadPersistedDevices();
    refreshBluetoothAvailability();
}

LighthouseService::~LighthouseService()
{
    stopDiscovery();
}

bool LighthouseService::hasBluetoothAdapter() const
{
    return _hasBluetoothAdapter;
}

bool LighthouseService::isDiscovering() const
{
    return _isDiscovering;
}

QString LighthouseService::statusText() const
{
    return _statusText;
}

const QVector<LighthouseDevice>& LighthouseService::devices() const
{
    return _devices;
}

QString LighthouseService::configFilePath() const
{
    auto resolveRootPath = [](const QString& startPath) -> QString {
        QDir dir(startPath);
        while (dir.exists()) {
            if (QFileInfo::exists(dir.filePath(QStringLiteral("CMakeLists.txt"))))
                return dir.absolutePath();

            if (!dir.cdUp())
                break;
        }
        return {};
    };

    QString rootPath = resolveRootPath(QCoreApplication::applicationDirPath());
    if (rootPath.isEmpty())
        rootPath = resolveRootPath(QDir::currentPath());
    if (rootPath.isEmpty())
        rootPath = QCoreApplication::applicationDirPath();

    return QDir(rootPath).filePath(QStringLiteral("config/lighthouse_devices.ini"));
}

void LighthouseService::loadPersistedDevices()
{
    const QString filePath = configFilePath();
    if (!QFileInfo::exists(filePath))
        return;

    QSettings settings(filePath, QSettings::IniFormat);
    const int count = settings.beginReadArray(QStringLiteral("devices"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);

        const QString bluetoothAddress =
            normalizeBluetoothAddress(settings.value(QStringLiteral("bluetooth_address")).toString());
        if (bluetoothAddress.isEmpty())
            continue;
        if (_knownBluetoothAddresses.contains(bluetoothAddress))
            continue;

        QString name = settings.value(QStringLiteral("name")).toString().trimmed();
        const LighthouseVersion persistedVersion =
            versionFromPersistentString(settings.value(QStringLiteral("version")).toString());
        if (name.isEmpty())
            name = fallbackNameFromVersion(persistedVersion);

        LighthouseDevice device;
        device.bluetoothAddress = bluetoothAddress;
        device.name = name;
        if (device.version() == LighthouseVersion::V1) {
            const QString persistedId =
                normalizeLighthouseId(settings.value(QStringLiteral("control_id")).toString());
            if (persistedId.size() == 8)
                device.id = persistedId;
        }

        _knownBluetoothAddresses.insert(device.bluetoothAddress);
        _devices.push_back(device);
    }
    settings.endArray();
}

void LighthouseService::savePersistedDevices() const
{
    const QString filePath = configFilePath();
    const QFileInfo fileInfo(filePath);
    QDir().mkpath(fileInfo.absolutePath());

    QSettings settings(filePath, QSettings::IniFormat);
    settings.clear();
    settings.beginWriteArray(QStringLiteral("devices"));

    int writeIndex = 0;
    for (const LighthouseDevice& device : _devices) {
        settings.setArrayIndex(writeIndex);
        ++writeIndex;

        settings.setValue(QStringLiteral("bluetooth_address"),
                          normalizeBluetoothAddress(device.bluetoothAddress));
        settings.setValue(QStringLiteral("name"), device.name);
        settings.setValue(QStringLiteral("version"),
                          versionToPersistentString(device.version()));

        if (device.version() == LighthouseVersion::V1) {
            const QString normalizedId = normalizeLighthouseId(device.id);
            settings.setValue(QStringLiteral("control_id"),
                              normalizedId.size() == 8 ? normalizedId : QString());
        } else {
            settings.setValue(QStringLiteral("control_id"), QString());
        }
    }

    settings.endArray();
    settings.sync();
}

void LighthouseService::refreshBluetoothAvailability()
{
    qInfo().noquote() << QStringLiteral("Lighthouse: refreshBluetoothAvailability");

    // 将阻塞的 WinRT 异步调用放到工作线程，避免在 Qt 主线程（STA）上
    // 调用 .get() 等待 IAsyncOperation 完成，防止 STA 死锁导致程序挂起。
    auto* worker = QThread::create([this]() {
        ensureWinRtApartmentForCurrentThread();

        bool newValue = false;
        QString status = QStringLiteral("未检测到蓝牙适配器");

        try {
            using namespace winrt;
            using namespace Windows::Devices::Bluetooth;

            // 在 MTA 工作线程上阻塞等待是安全的：
            // 完成回调可在任意线程执行，不会 marshal 回当前线程造成死锁。
            const auto adapter = BluetoothAdapter::GetDefaultAsync().get();
            newValue = adapter != nullptr && adapter.IsLowEnergySupported();
            status = newValue
                ? QStringLiteral("蓝牙 BLE 可用")
                : QStringLiteral("当前设备不支持蓝牙 BLE");
        } catch (const winrt::hresult_error& error) {
            status = QStringLiteral("蓝牙初始化失败：%1").arg(
                QString::fromWCharArray(error.message().c_str()));
        } catch (...) {
            status = QStringLiteral("蓝牙初始化时发生未知错误");
        }

        emit _bluetoothAvailabilityResult(newValue, status);
    });

    // worker 完成后自动销毁
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void LighthouseService::_onBluetoothAvailabilityResult(bool available, QString status)
{
    qInfo().noquote() << QStringLiteral("Lighthouse: bluetooth availability result available=%1 status=%2")
                             .arg(available ? 1 : 0)
                             .arg(status);

    if (_hasBluetoothAdapter != available) {
        _hasBluetoothAdapter = available;
        emit availabilityChanged();
    }
    setStatusText(status);
}

void LighthouseService::startDiscovery()
{
    qInfo().noquote() << QStringLiteral("Lighthouse: startDiscovery requested");
    if (_discoveryTimeoutTimer != nullptr)
        _discoveryTimeoutTimer->stop();

    // 蓝牙可用性检查是异步的，此处直接使用缓存状态
    if (!_hasBluetoothAdapter) {
        qWarning().noquote() << QStringLiteral("Lighthouse: startDiscovery aborted because BLE adapter is unavailable");
        return;
    }

    stopDiscovery();

    try {
        ensureWinRtApartmentForCurrentThread();

        auto requestedProperties = winrt::single_threaded_vector<winrt::hstring>();
        requestedProperties.Append(L"System.Devices.Aep.DeviceAddress");
        requestedProperties.Append(L"System.Devices.Aep.IsConnected");
        const quint64 discoverySessionId = ++_discoverySessionId;

        auto* state = new LighthouseServicePrivate();
        state->watcher = DeviceInformation::CreateWatcher(
            BluetoothLEDevice::GetDeviceSelectorFromPairingState(false),
            requestedProperties,
            DeviceInformationKind::AssociationEndpoint);
        state->pairedWatcher = DeviceInformation::CreateWatcher(
            BluetoothLEDevice::GetDeviceSelectorFromPairingState(true),
            requestedProperties,
            DeviceInformationKind::Device);
        state->advertisementWatcher = BluetoothLEAdvertisementWatcher();
        state->advertisementWatcher.ScanningMode(BluetoothLEScanningMode::Active);

        auto weakThis = QPointer<LighthouseService>(this);
        state->watcherAddedToken = state->watcher.Added([weakThis](auto&&, const DeviceInformation& args) {
            if (!weakThis)
                return;

            const QString id = toQString(args.Id());
            const QString name = toQString(args.Name());
            qInfo().noquote() << QStringLiteral("Lighthouse: watcher added name=%1 id=%2").arg(name, id);
            if (!isLighthouseName(name))
                return;

            const QString bluetoothAddress = resolveBluetoothAddress(args);
            qInfo().noquote() << QStringLiteral("Lighthouse: watcher added lighthouse name=%1 address=%2")
                                     .arg(name, bluetoothAddress.isEmpty() ? QStringLiteral("<empty>") : bluetoothAddress);
            if (bluetoothAddress.isEmpty())
                return;

            QMetaObject::invokeMethod(weakThis, [weakThis, id, name, bluetoothAddress]() {
                if (weakThis)
                    weakThis->handleDeviceAdded(id, name, bluetoothAddress);
            }, Qt::QueuedConnection);
        });
        state->pairedWatcherAddedToken = state->pairedWatcher.Added([weakThis](auto&&, const DeviceInformation& args) {
            if (!weakThis)
                return;

            const QString id = toQString(args.Id());
            const QString name = toQString(args.Name());
            qInfo().noquote() << QStringLiteral("Lighthouse: paired watcher added name=%1 id=%2").arg(name, id);
            if (!isLighthouseName(name))
                return;

            const QString bluetoothAddress = resolveBluetoothAddress(args);
            qInfo().noquote() << QStringLiteral("Lighthouse: paired watcher lighthouse name=%1 address=%2")
                                     .arg(name, bluetoothAddress.isEmpty() ? QStringLiteral("<empty>") : bluetoothAddress);
            if (bluetoothAddress.isEmpty())
                return;

            QMetaObject::invokeMethod(weakThis, [weakThis, id, name, bluetoothAddress]() {
                if (weakThis)
                    weakThis->handleDeviceAdded(id, name, bluetoothAddress);
            }, Qt::QueuedConnection);
        });
        state->advertisementReceivedToken = state->advertisementWatcher.Received(
            [weakThis, state](auto&&, const BluetoothLEAdvertisementReceivedEventArgs& args) {
                if (!weakThis)
                    return;

                const QString name = toQString(args.Advertisement().LocalName());
                if (!isLighthouseName(name))
                    return;

                const auto rawBluetoothAddress = args.BluetoothAddress();
                {
                    const std::lock_guard<std::mutex> lock(state->advertisementMutex);
                    if (!state->advertisementAddresses.insert(rawBluetoothAddress).second)
                        return;
                }

                const QString bluetoothAddress = bluetoothAddressToString(rawBluetoothAddress);
                const QString id = advertisementDeviceId(rawBluetoothAddress);
                qInfo().noquote() << QStringLiteral("Lighthouse: advertisement discovered name=%1 address=%2")
                                         .arg(name, bluetoothAddress);

                QMetaObject::invokeMethod(weakThis, [weakThis, id, name, bluetoothAddress]() {
                    if (weakThis)
                        weakThis->handleDeviceAdded(id, name, bluetoothAddress);
                }, Qt::QueuedConnection);
            });
        state->watcherCompletedToken = state->watcher.EnumerationCompleted([weakThis](auto&&, auto&&) {
            if (!weakThis)
                return;

            QMetaObject::invokeMethod(weakThis, [weakThis]() {
                if (weakThis)
                    weakThis->handleEnumerationCompleted();
            }, Qt::QueuedConnection);
        });

        _watcherState = state;

        state->watcher.Start();
        state->pairedWatcher.Start();
        state->advertisementWatcher.Start();

        _isDiscovering = true;
        emit discoveryStateChanged();
        setStatusText(QStringLiteral("正在扫描 Lighthouse 基站..."));
        _discoveryTimeoutTimer->start(kDiscoveryTimeoutMs);
        qInfo().noquote() << QStringLiteral("Lighthouse: watchers started");

        auto* worker = QThread::create([weakThis, discoverySessionId]() {
            ensureWinRtApartmentForCurrentThread();

            auto queueExistingDevices = [weakThis, discoverySessionId](const DeviceInformationCollection& infos) {
                for (const auto& info : infos) {
                    if (!weakThis)
                        return;

                    const QString id = toQString(info.Id());
                    const QString name = toQString(info.Name());
                    if (!isLighthouseName(name))
                        continue;

                    const QString bluetoothAddress = resolveBluetoothAddress(info);
                    qInfo().noquote() << QStringLiteral("Lighthouse: warmup candidate name=%1 address=%2")
                                             .arg(name,
                                                  bluetoothAddress.isEmpty()
                                                      ? QStringLiteral("<empty>")
                                                      : bluetoothAddress);
                    if (bluetoothAddress.isEmpty())
                        continue;

                    QMetaObject::invokeMethod(weakThis, [weakThis, discoverySessionId, id, name, bluetoothAddress]() {
                        if (!weakThis || !weakThis->isDiscovering() || weakThis->_discoverySessionId != discoverySessionId)
                            return;

                        weakThis->handleDeviceAdded(id, name, bluetoothAddress);
                    }, Qt::QueuedConnection);
                }
            };

            try {
                auto requestedProperties = winrt::single_threaded_vector<winrt::hstring>();
                requestedProperties.Append(L"System.Devices.Aep.DeviceAddress");
                requestedProperties.Append(L"System.Devices.Aep.IsConnected");

                queueExistingDevices(DeviceInformation::FindAllAsync(
                    BluetoothLEDevice::GetDeviceSelectorFromPairingState(false),
                    requestedProperties,
                    DeviceInformationKind::AssociationEndpoint).get());
                queueExistingDevices(DeviceInformation::FindAllAsync(
                    BluetoothLEDevice::GetDeviceSelectorFromPairingState(true),
                    requestedProperties,
                    DeviceInformationKind::Device).get());
            } catch (const winrt::hresult_error& error) {
                qWarning().noquote() << QStringLiteral("Lighthouse: warmup discovery failed %1")
                                            .arg(QString::fromWCharArray(error.message().c_str()));
            } catch (...) {
                qWarning().noquote() << QStringLiteral("Lighthouse: warmup discovery failed unknown");
            }
        });

        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        worker->start();
    } catch (const winrt::hresult_error& error) {
        qWarning().noquote() << QStringLiteral("Lighthouse: startDiscovery failed %1").arg(toQString(error.message()));
        setStatusText(QStringLiteral("启动扫描失败：%1").arg(toQString(error.message())));
    }
}

void LighthouseService::stopDiscovery()
{
    qInfo().noquote() << QStringLiteral("Lighthouse: stopDiscovery requested");
    ++_discoverySessionId;
    if (_discoveryTimeoutTimer != nullptr)
        _discoveryTimeoutTimer->stop();
    clearWatchers();

    if (_isDiscovering) {
        _isDiscovering = false;
        emit discoveryStateChanged();
    }
}

void LighthouseService::_onDiscoveryTimeout()
{
    if (!_isDiscovering)
        return;

    qInfo().noquote() << QStringLiteral("Lighthouse: discovery timeout reached ms=%1")
                             .arg(kDiscoveryTimeoutMs);
    stopDiscovery();

    if (_devices.isEmpty()) {
        setStatusText(QStringLiteral("扫描完成，未发现 Lighthouse 基站"));
        return;
    }

    setStatusText(QStringLiteral("扫描完成，已发现 %1 台 Lighthouse 基站").arg(_devices.size()));
}

void LighthouseService::controlDevice(int index, LighthouseControlOperation operation)
{
    if (index < 0 || index >= _devices.size()) {
        qWarning().noquote() << QStringLiteral("Lighthouse: controlDevice called without valid selection");
        setStatusText(QStringLiteral("请先选择要控制的 Lighthouse 基站"));
        emit operationFinished(false,
                               QStringLiteral("设备控制"),
                               QStringLiteral("请先选择要控制的 Lighthouse 基站"));
        return;
    }

    const LighthouseDevice device = _devices.at(index);
    qInfo().noquote() << QStringLiteral("Lighthouse: controlDevice device=%1 op=%2")
                             .arg(device.name, operationName(operation));
    setStatusText(QStringLiteral("正在向 %1 发送%2命令...").arg(device.name, operationName(operation)));

    auto weakThis = QPointer<LighthouseService>(this);
    auto* worker = QThread::create([weakThis, device, operation]() {
        ensureWinRtApartmentForCurrentThread();

        QString statusText;
        QString detailText;
        bool success = false;
        try {
            controlLighthouseDevice(device, operation);
            success = true;
            qInfo().noquote() << QStringLiteral("Lighthouse: control succeeded device=%1 op=%2")
                                     .arg(device.name, operationName(operation));
            statusText = QStringLiteral("%1：%2命令已发送").arg(device.name, operationName(operation));
        } catch (const winrt::hresult_error& error) {
            qWarning().noquote() << QStringLiteral("Lighthouse: control failed device=%1 op=%2 error=%3")
                                        .arg(device.name,
                                             operationName(operation),
                                             QString::fromWCharArray(error.message().c_str()));
            statusText = QStringLiteral("%1失败：%2")
                             .arg(operationName(operation),
                                  QString::fromWCharArray(error.message().c_str()));
        } catch (const std::exception& error) {
            qWarning().noquote() << QStringLiteral("Lighthouse: control failed device=%1 op=%2 error=%3")
                                        .arg(device.name,
                                             operationName(operation),
                                             QString::fromUtf8(error.what()));
            statusText = QStringLiteral("%1失败：%2")
                             .arg(operationName(operation),
                                  QString::fromUtf8(error.what()));
        } catch (...) {
            qWarning().noquote() << QStringLiteral("Lighthouse: control failed device=%1 op=%2 error=unknown")
                                        .arg(device.name, operationName(operation));
            statusText = QStringLiteral("%1失败：未知错误").arg(operationName(operation));
        }

        if (!weakThis)
            return;

        const QString message = success
            ? QStringLiteral("%1 执行“%2”成功").arg(device.name, operationName(operation))
            : QStringLiteral("%1 执行“%2”失败：%3").arg(device.name, operationName(operation), statusText);

        QMetaObject::invokeMethod(weakThis, [weakThis, success, statusText, message]() {
            if (weakThis) {
                weakThis->setStatusText(statusText);
                emit weakThis->operationFinished(success, QStringLiteral("设备控制"), message);
            }
        }, Qt::QueuedConnection);
    });

    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void LighthouseService::controlDevices(const QVector<int>& indices, LighthouseControlOperation operation)
{
    QVector<LighthouseDevice> targetDevices;
    targetDevices.reserve(indices.size());
    for (int index : indices) {
        if (index >= 0 && index < _devices.size())
            targetDevices.push_back(_devices.at(index));
    }

    if (targetDevices.isEmpty()) {
        const QString message = QStringLiteral("请先扫描并选择 Lighthouse 基站");
        qWarning().noquote() << QStringLiteral("Lighthouse: controlDevices called without valid targets");
        setStatusText(message);
        emit operationFinished(false, QStringLiteral("批量控制"), message);
        return;
    }

    qInfo().noquote() << QStringLiteral("Lighthouse: controlDevices count=%1 op=%2")
                             .arg(targetDevices.size())
                             .arg(operationName(operation));
    setStatusText(QStringLiteral("正在批量执行“%1”...").arg(operationName(operation)));

    auto weakThis = QPointer<LighthouseService>(this);
    auto* worker = QThread::create([weakThis, targetDevices, operation]() {
        ensureWinRtApartmentForCurrentThread();

        int successCount = 0;
        int skippedCount = 0;
        int failedCount = 0;
        QString firstError;

        for (const LighthouseDevice& device : targetDevices) {
            if (!canControlLighthouseDevice(device, operation)) {
                ++skippedCount;
                continue;
            }

            try {
                controlLighthouseDevice(device, operation);
                ++successCount;
            } catch (const winrt::hresult_error& error) {
                ++failedCount;
                if (firstError.isEmpty())
                    firstError = QString::fromWCharArray(error.message().c_str());
            } catch (const std::exception& error) {
                ++failedCount;
                if (firstError.isEmpty())
                    firstError = QString::fromUtf8(error.what());
            } catch (...) {
                ++failedCount;
                if (firstError.isEmpty())
                    firstError = QStringLiteral("未知错误");
            }
        }

        const bool success = failedCount == 0 && successCount > 0;
        QString statusText = QStringLiteral("批量“%1”完成：成功 %2 台")
                                 .arg(operationName(operation))
                                 .arg(successCount);
        if (skippedCount > 0)
            statusText += QStringLiteral("，跳过 %1 台").arg(skippedCount);
        if (failedCount > 0)
            statusText += QStringLiteral("，失败 %1 台").arg(failedCount);
        if (!firstError.isEmpty())
            statusText += QStringLiteral("：%1").arg(firstError);

        if (!weakThis)
            return;

        QMetaObject::invokeMethod(weakThis, [weakThis, success, statusText]() {
            if (!weakThis)
                return;

            weakThis->setStatusText(statusText);
            emit weakThis->operationFinished(success, QStringLiteral("批量控制"), statusText);
        }, Qt::QueuedConnection);
    });

    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void LighthouseService::controlAllDevices(LighthouseControlOperation operation)
{
    if (_devices.isEmpty()) {
        qWarning().noquote() << QStringLiteral("Lighthouse: controlAllDevices called without any devices");
        emit operationFinished(false,
                               QStringLiteral("批量控制"),
                               QStringLiteral("请先扫描并发现 Lighthouse 基站"));
        setStatusText(QStringLiteral("请先扫描并发现 Lighthouse 基站"));
        return;
    }

    const QVector<LighthouseDevice> devices = _devices;
    qInfo().noquote() << QStringLiteral("Lighthouse: controlAllDevices count=%1 op=%2")
                             .arg(devices.size())
                             .arg(operationName(operation));
    setStatusText(QStringLiteral("正在批量执行“%1”...").arg(operationName(operation)));

    auto weakThis = QPointer<LighthouseService>(this);
    auto* worker = QThread::create([weakThis, devices, operation]() {
        ensureWinRtApartmentForCurrentThread();

        int successCount = 0;
        int skippedCount = 0;
        int failedCount = 0;
        QString firstError;

        for (const LighthouseDevice& device : devices) {
            if (!canControlLighthouseDevice(device, operation)) {
                ++skippedCount;
                continue;
            }

            try {
                controlLighthouseDevice(device, operation);
                ++successCount;
            } catch (const winrt::hresult_error& error) {
                ++failedCount;
                if (firstError.isEmpty())
                    firstError = QString::fromWCharArray(error.message().c_str());
            } catch (const std::exception& error) {
                ++failedCount;
                if (firstError.isEmpty())
                    firstError = QString::fromUtf8(error.what());
            } catch (...) {
                ++failedCount;
                if (firstError.isEmpty())
                    firstError = QStringLiteral("未知错误");
            }
        }

        QString statusText = QStringLiteral("批量“%1”完成：成功 %2 台").arg(operationName(operation)).arg(successCount);
        if (skippedCount > 0)
            statusText += QStringLiteral("，跳过 %1 台").arg(skippedCount);
        if (failedCount > 0)
            statusText += QStringLiteral("，失败 %1 台").arg(failedCount);
        if (!firstError.isEmpty())
            statusText += QStringLiteral("：%1").arg(firstError);

        if (!weakThis)
            return;

        const bool success = failedCount == 0 && successCount > 0;
        QString message = QStringLiteral("批量“%1”：成功 %2 台").arg(operationName(operation)).arg(successCount);
        if (skippedCount > 0)
            message += QStringLiteral("，跳过 %1 台").arg(skippedCount);
        if (failedCount > 0)
            message += QStringLiteral("，失败 %1 台").arg(failedCount);
        if (!firstError.isEmpty())
            message += QStringLiteral("：%1").arg(firstError);

        QMetaObject::invokeMethod(weakThis, [weakThis, success, statusText, message]() {
            if (weakThis) {
                weakThis->setStatusText(statusText);
                emit weakThis->operationFinished(success, QStringLiteral("批量控制"), message);
            }
        }, Qt::QueuedConnection);
    });

    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void LighthouseService::setDeviceControlId(int index, const QString& lighthouseId)
{
    if (index < 0 || index >= _devices.size())
        return;

    const QString normalizedId = normalizeLighthouseId(lighthouseId);
    qInfo().noquote() << QStringLiteral("Lighthouse: setDeviceControlId device=%1 id=%2")
                             .arg(_devices[index].name, normalizedId);
    if (_devices[index].id == normalizedId)
        return;

    _devices[index].id = normalizedId;
    savePersistedDevices();
    emit devicesChanged();
}

void LighthouseService::removeDevice(const QString& bluetoothAddress)
{
    const QString normalizedAddress = normalizeBluetoothAddress(bluetoothAddress);
    if (normalizedAddress.isEmpty())
        return;

    for (int index = 0; index < _devices.size(); ++index) {
        if (normalizeBluetoothAddress(_devices.at(index).bluetoothAddress) != normalizedAddress)
            continue;

        const QString sourceId = _devices.at(index).sourceId;
        if (!sourceId.isEmpty())
            _knownDeviceIds.remove(sourceId);

        _knownBluetoothAddresses.remove(_devices.at(index).bluetoothAddress);
        _devices.remove(index);
        savePersistedDevices();
        emit devicesChanged();
        return;
    }
}

void LighthouseService::setStatusText(const QString& statusText)
{
    if (_statusText == statusText)
        return;

    _statusText = statusText;
    emit statusChanged();
}

void LighthouseService::handleDeviceAdded(const QString& id, const QString& name, const QString& bluetoothAddress)
{
    const QString normalizedAddress = normalizeBluetoothAddress(bluetoothAddress);
    if (normalizedAddress.isEmpty())
        return;

    if (_knownDeviceIds.contains(id) || _knownBluetoothAddresses.contains(normalizedAddress))
        return;

    LighthouseDevice device;
    device.name = name;
    device.bluetoothAddress = normalizedAddress;
    device.sourceId = id;

    _knownDeviceIds.insert(id);
    _knownBluetoothAddresses.insert(device.bluetoothAddress);
    _devices.push_back(device);
    savePersistedDevices();

    qInfo().noquote() << QStringLiteral("Lighthouse: device discovered name=%1 address=%2")
                             .arg(device.name, device.bluetoothAddress);
    emit devicesChanged();
    setStatusText(QStringLiteral("已发现 %1 台 Lighthouse 基站").arg(_devices.size()));
}

void LighthouseService::handleEnumerationCompleted()
{
    qInfo().noquote() << QStringLiteral("Lighthouse: enumeration completed count=%1").arg(_devices.size());
    stopDiscovery();

    if (_devices.isEmpty()) {
        setStatusText(QStringLiteral("扫描完成，未发现 Lighthouse 基站"));
    }
}

void LighthouseService::clearWatchers()
{
    if (_watcherState == nullptr)
        return;

    qInfo().noquote() << QStringLiteral("Lighthouse: clearWatchers");
    auto* state = reinterpret_cast<LighthouseServicePrivate*>(_watcherState);
    _watcherState = nullptr;

    try {
        if (state->watcher) {
            state->watcher.Added(state->watcherAddedToken);
            state->watcher.EnumerationCompleted(state->watcherCompletedToken);
            state->watcher.Stop();
        }
        if (state->pairedWatcher) {
            state->pairedWatcher.Added(state->pairedWatcherAddedToken);
            state->pairedWatcher.Stop();
        }
        if (state->advertisementWatcher) {
            state->advertisementWatcher.Received(state->advertisementReceivedToken);
            state->advertisementWatcher.Stop();
        }
    } catch (...) {
    }

    delete state;
}
