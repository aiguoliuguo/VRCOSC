#ifndef STEAMVR_MONITOR_SERVICE_H
#define STEAMVR_MONITOR_SERVICE_H

#include <QString>
#include <QObject>

class SteamVrMonitorService : public QObject
{
    Q_OBJECT

public:
    explicit SteamVrMonitorService(QObject* parent = nullptr);
    ~SteamVrMonitorService() override = default;

    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] QString statusText() const;

private:
    bool _isAvailable{false};
    QString _statusText;
};

#endif // STEAMVR_MONITOR_SERVICE_H
