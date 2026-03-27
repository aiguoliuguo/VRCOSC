#include "lighthousepage.h"

#include "ElaCheckBox.h"
#include "ElaMenu.h"
#include "ElaMessageBar.h"
#include "ElaPushButton.h"
#include "ElaScrollPageArea.h"
#include "ElaText.h"
#include "ElaToolButton.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QWidget>

LighthousePage::LighthousePage(QWidget* parent)
    : ElaScrollPage(parent)
{
    setTitleVisible(false);

    _lighthouseService = new LighthouseService(this);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 15, 0);
    layout->setSpacing(6);

    auto* devicesHeaderContainer = new QWidget(central);
    auto* devicesHeader = new QHBoxLayout(devicesHeaderContainer);
    devicesHeader->setContentsMargins(0, 0, 0, 0);
    devicesHeader->setSpacing(8);

    auto* devicesTitle = new ElaText(QStringLiteral("发现的基站"), devicesHeaderContainer);
    devicesTitle->setTextPixelSize(18);

    _powerOnAllButton = new ElaPushButton(QStringLiteral("开启所有"), devicesHeaderContainer);
    _sleepAllButton = new ElaPushButton(QStringLiteral("关闭所有"), devicesHeaderContainer);
    _scanButton = new ElaPushButton(QStringLiteral("扫描基站"), devicesHeaderContainer);

    _powerOnAllButton->setMinimumHeight(34);
    _sleepAllButton->setMinimumHeight(34);
    _scanButton->setMinimumHeight(34);
    _powerOnAllButton->setMinimumWidth(112);
    _sleepAllButton->setMinimumWidth(112);
    _scanButton->setMinimumWidth(132);

    devicesHeader->addWidget(devicesTitle);
    devicesHeader->addStretch();
    devicesHeader->addWidget(_powerOnAllButton);
    devicesHeader->addWidget(_sleepAllButton);
    devicesHeader->addWidget(_scanButton);

    _deviceListContainer = new QWidget(central);
    _deviceListContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    _deviceListLayout = new QVBoxLayout(_deviceListContainer);
    _deviceListLayout->setContentsMargins(0, 0, 0, 12);
    _deviceListLayout->setSpacing(6);

    layout->addWidget(devicesHeaderContainer);
    layout->addWidget(_deviceListContainer);
    layout->addStretch();

    addCentralWidget(central, true, false, 0);

    connect(_scanButton, &ElaPushButton::clicked, this, [this]() {
        if (_lighthouseService->isDiscovering())
            _lighthouseService->stopDiscovery();
        else
            _lighthouseService->startDiscovery();
        refreshUi();
    });

    connect(_powerOnAllButton, &ElaPushButton::clicked, this, [this]() {
        performBulkOperation(LighthouseControlOperation::PowerOn);
    });

    connect(_sleepAllButton, &ElaPushButton::clicked, this, [this]() {
        performBulkOperation(LighthouseControlOperation::Sleep);
    });

    connect(_lighthouseService, &LighthouseService::discoveryStateChanged, this, &LighthousePage::refreshUi);
    connect(_lighthouseService, &LighthouseService::devicesChanged, this, &LighthousePage::refreshUi);
    connect(_lighthouseService, &LighthouseService::statusChanged, this, &LighthousePage::refreshUi);
    connect(_lighthouseService, &LighthouseService::operationFinished, this, &LighthousePage::showOperationMessage);

    refreshUi();
}

void LighthousePage::refreshUi()
{
    constexpr int kEmptyCardHeight = 126;

    const bool isDiscovering = _lighthouseService->isDiscovering();
    _scanButton->setText(isDiscovering ? QStringLiteral("停止扫描") : QStringLiteral("扫描基站"));

    const QVector<LighthouseDevice>& devices = _lighthouseService->devices();

    while (QLayoutItem* item = _deviceListLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    const int visibleCount = devices.size();
    _powerOnAllButton->setEnabled(visibleCount > 0);
    _sleepAllButton->setEnabled(visibleCount > 0);

    if (visibleCount == 0) {
        auto* emptyCard = new ElaScrollPageArea(_deviceListContainer);
        emptyCard->setFixedHeight(kEmptyCardHeight);

        auto* emptyLayout = new QVBoxLayout(emptyCard);
        emptyLayout->setContentsMargins(12, 10, 12, 10);
        emptyLayout->setSpacing(4);

        auto* emptyTitle = new ElaText(QStringLiteral("尚未发现 Lighthouse 基站"), emptyCard);
        emptyTitle->setTextPixelSize(15);

        auto* emptyDesc = new ElaText(QStringLiteral("点击右上角“扫描基站”开始搜索附近设备。"), emptyCard);
        emptyDesc->setTextPixelSize(13);
        emptyDesc->setWordWrap(true);

        emptyLayout->addWidget(emptyTitle);
        emptyLayout->addWidget(emptyDesc);
        _deviceListLayout->addWidget(emptyCard);
        return;
    }

    for (int index = 0; index < devices.size(); ++index) {
        const LighthouseDevice& device = devices.at(index);

        auto* itemCard = new ElaScrollPageArea(_deviceListContainer);
        itemCard->setMinimumHeight(device.version() == LighthouseVersion::V1 ? 126 : 96);

        auto* itemLayout = new QHBoxLayout(itemCard);
        itemLayout->setContentsMargins(12, 10, 12, 10);
        itemLayout->setSpacing(10);

        auto* checkBox = new ElaCheckBox(itemCard);
        checkBox->setText(QString());
        checkBox->setChecked(_checkedDeviceAddresses.contains(device.bluetoothAddress));
        connect(checkBox, &QCheckBox::toggled, this, [this, address = device.bluetoothAddress](bool checked) {
            if (checked)
                _checkedDeviceAddresses.insert(address);
            else
                _checkedDeviceAddresses.remove(address);
        });

        auto* textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 2, 0, 2);
        textLayout->setSpacing(4);

        auto* nameLabel = new ElaText(
            QStringLiteral("%1 [%2]").arg(device.name, lighthouseVersionToString(device.version())),
            itemCard);
        nameLabel->setTextPixelSize(15);

        auto* addrLabel = new ElaText(device.bluetoothAddress, itemCard);
        addrLabel->setTextPixelSize(13);

        textLayout->addWidget(nameLabel);
        textLayout->addWidget(addrLabel);

        if (device.version() == LighthouseVersion::V1) {
            auto* idEdit = new QLineEdit(itemCard);
            idEdit->setPlaceholderText(QStringLiteral("输入 8 位十六进制 V1 基站 ID"));
            idEdit->setText(device.id);
            idEdit->setMaxLength(8);
            idEdit->setMinimumHeight(30);
            connect(idEdit, &QLineEdit::editingFinished, this, [this, index, idEdit]() {
                _lighthouseService->setDeviceControlId(index, idEdit->text());
            });
            textLayout->addWidget(idEdit);
        }

        auto* menuButton = new ElaToolButton(itemCard);
        menuButton->setText(QStringLiteral("..."));
        menuButton->setPopupMode(QToolButton::InstantPopup);
        menuButton->setFixedSize(40, 40);

        auto* menu = new ElaMenu(menuButton);
        menu->setMenuItemHeight(40);

        QAction* powerOnAction = menu->addAction(QStringLiteral("开机"));
        QAction* sleepAction = menu->addAction(QStringLiteral("休眠"));
        QAction* standbyAction = menu->addAction(QStringLiteral("待机"));
        QAction* identifyAction = menu->addAction(QStringLiteral("识别"));
        menu->addSeparator();
        QAction* removeAction = menu->addAction(QStringLiteral("移除"));

        const bool v1Ready = device.version() == LighthouseVersion::V1 && device.id.size() == 8;
        const bool isV2 = device.version() == LighthouseVersion::V2;
        powerOnAction->setEnabled(isV2 || v1Ready);
        sleepAction->setEnabled(isV2 || v1Ready);
        standbyAction->setEnabled(isV2);
        identifyAction->setEnabled(isV2);

        connect(powerOnAction, &QAction::triggered, this, [this, index]() {
            _lighthouseService->controlDevice(index, LighthouseControlOperation::PowerOn);
        });
        connect(sleepAction, &QAction::triggered, this, [this, index]() {
            _lighthouseService->controlDevice(index, LighthouseControlOperation::Sleep);
        });
        connect(standbyAction, &QAction::triggered, this, [this, index]() {
            _lighthouseService->controlDevice(index, LighthouseControlOperation::Standby);
        });
        connect(identifyAction, &QAction::triggered, this, [this, index]() {
            _lighthouseService->controlDevice(index, LighthouseControlOperation::Identify);
        });
        connect(removeAction, &QAction::triggered, this, [this, address = device.bluetoothAddress]() {
            removeDevice(address);
        });

        menuButton->setMenu(menu);

        itemLayout->addWidget(checkBox, 0, Qt::AlignVCenter);
        itemLayout->addLayout(textLayout, 1);
        itemLayout->addWidget(menuButton, 0, Qt::AlignVCenter);
        itemLayout->setAlignment(textLayout, Qt::AlignVCenter);

        _deviceListLayout->addWidget(itemCard);
    }
}

void LighthousePage::performBulkOperation(LighthouseControlOperation operation)
{
    const QVector<LighthouseDevice>& devices = _lighthouseService->devices();
    QVector<int> targetIndexes;
    targetIndexes.reserve(devices.size());

    for (int index = 0; index < devices.size(); ++index) {
        const LighthouseDevice& device = devices.at(index);
        if (_checkedDeviceAddresses.isEmpty() || _checkedDeviceAddresses.contains(device.bluetoothAddress))
            targetIndexes.push_back(index);
    }

    if (targetIndexes.isEmpty()) {
        showOperationMessage(false,
                             QStringLiteral("批量控制"),
                             QStringLiteral("请先勾选要操作的 Lighthouse 基站。"));
        return;
    }

    _lighthouseService->controlDevices(targetIndexes, operation);
}

void LighthousePage::removeDevice(const QString& bluetoothAddress)
{
    _checkedDeviceAddresses.remove(bluetoothAddress);
    _lighthouseService->removeDevice(bluetoothAddress);
    refreshUi();
}

void LighthousePage::showOperationMessage(bool success, const QString& title, const QString& text)
{
    if (success) {
        ElaMessageBar::success(ElaMessageBarType::TopRight, title, text, 3000, this);
        return;
    }

    ElaMessageBar::error(ElaMessageBarType::TopRight, title, text, 4000, this);
}
