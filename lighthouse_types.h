#ifndef LIGHTHOUSE_TYPES_H
#define LIGHTHOUSE_TYPES_H

#include <QString>

#include <optional>

enum class LighthouseVersion
{
    Unknown,
    V1,
    V2,
};

struct LighthouseDevice
{
    QString name;
    QString bluetoothAddress;
    QString id;
    QString sourceId;
    bool isManaged{false};

    [[nodiscard]] LighthouseVersion version() const
    {
        if (name.startsWith(QStringLiteral("HTC BS")))
            return LighthouseVersion::V1;
        if (name.startsWith(QStringLiteral("LHB-")))
            return LighthouseVersion::V2;
        return LighthouseVersion::Unknown;
    }
};

inline QString lighthouseVersionToString(LighthouseVersion version)
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

inline QString bluetoothAddressToString(quint64 bluetoothAddress)
{
    const QString hex = QStringLiteral("%1").arg(bluetoothAddress, 12, 16, QLatin1Char('0')).toUpper();
    QString result;
    result.reserve(17);
    for (int i = 0; i < hex.size(); i += 2) {
        if (!result.isEmpty())
            result += QLatin1Char(':');
        result += hex.mid(i, 2);
    }
    return result;
}

inline std::optional<quint64> bluetoothAddressFromString(const QString& bluetoothAddress)
{
    bool ok = false;
    const quint64 address = bluetoothAddress.trimmed().remove(QLatin1Char(':')).toULongLong(&ok, 16);
    if (!ok)
        return std::nullopt;
    return address;
}

#endif // LIGHTHOUSE_TYPES_H
