#ifndef LIGHTHOUSEPAGE_H
#define LIGHTHOUSEPAGE_H

#include "ElaScrollPage.h"
#include "lighthouse_service.h"

#include <QSet>

class LighthouseService;
class ElaPushButton;
class ElaScrollPageArea;
class QVBoxLayout;
class QWidget;

class LighthousePage : public ElaScrollPage
{
    Q_OBJECT

public:
    explicit LighthousePage(QWidget* parent = nullptr);
    ~LighthousePage() override = default;

private:
    void refreshUi();
    void performBulkOperation(LighthouseControlOperation operation);
    void removeDevice(const QString& bluetoothAddress);
    void showOperationMessage(bool success, const QString& title, const QString& text);

    LighthouseService* _lighthouseService{nullptr};
    QWidget* _deviceListContainer{nullptr};
    QVBoxLayout* _deviceListLayout{nullptr};
    ElaPushButton* _powerOnAllButton{nullptr};
    ElaPushButton* _sleepAllButton{nullptr};
    ElaPushButton* _scanButton{nullptr};
    QSet<QString> _checkedDeviceAddresses;
};

#endif // LIGHTHOUSEPAGE_H
