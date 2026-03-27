#include "steamvr_monitor_service.h"

SteamVrMonitorService::SteamVrMonitorService(QObject* parent)
    : QObject(parent)
    , _statusText(QStringLiteral("OpenVR SDK 头文件/库尚未接入，SteamVR 自动联动暂不可用"))
{
}

bool SteamVrMonitorService::isAvailable() const
{
    return _isAvailable;
}

QString SteamVrMonitorService::statusText() const
{
    return _statusText;
}
