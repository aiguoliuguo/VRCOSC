#include "oscsettingspage.h"

#include "ElaCheckBox.h"
#include "ElaFlowLayout.h"
#include "ElaMultiSelectComboBox.h"
#include "ElaPushButton.h"
#include "ElaScrollPageArea.h"
#include "ElaText.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDir>
#include <QDropEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QSettings>
#include <QSet>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

namespace {

constexpr int kItemHeight = 30;
constexpr int kRowSpacing = 12;
constexpr int kItemSpacing = 10;
constexpr int kPanelPadding = 12;
constexpr int kPlacementRowMinHeight = 44;
constexpr int kMinRowCount = 1;
constexpr int kMaxRowCount = 7;
constexpr int kControlRowHeight = 38;
constexpr char kDragMimeType[] = "application/x-vrcosc-hardware-item-index";

struct OscHardwareInfoItemSpec
{
    QString label;
    bool checked;
};

QVector<OscHardwareInfoItemSpec> defaultHardwareInfoSpecs()
{
    return {
        {QStringLiteral("CPU\u540d\u5b57"), false},
        {QStringLiteral("CPU%"), false},
        {QStringLiteral("CPU\u7535\u538b"), false},
        {QStringLiteral("CPU\u6e29\u5ea6"), false},
        {QStringLiteral("CPU\u9891\u7387"), false},
        {QStringLiteral("CPU\u529f\u8017"), false},
        {QStringLiteral("GPU\u540d\u5b57"), false},
        {QStringLiteral("GPU%"), false},
        {QStringLiteral("GPU\u7535\u538b"), false},
        {QStringLiteral("GPU\u6e29\u5ea6"), false},
        {QStringLiteral("GPU\u9891\u7387"), false},
        {QStringLiteral("\u663e\u5b58\u5360\u7528"), false},
        {QStringLiteral("GPU\u529f\u8017"), false},
        {QStringLiteral("RAM%"), false},
        {QStringLiteral("RAM\u9891\u7387"), false},
        {QStringLiteral("\u5185\u5b58\u5360\u7528"), false},
        {QStringLiteral("\u529f\u8017\u5b8c\u6574"), false},
        {QStringLiteral("FPS"), false},
        {QStringLiteral("Time"), false},
        {QStringLiteral("Battery"), false},
        {QStringLiteral("Heart"), false},
    };
}

bool supportsItemShortOption(const QString& label)
{
    static const QSet<QString> shortHiddenLabels{
        QStringLiteral("RAM%"),
        QStringLiteral("CPU%"),
        QStringLiteral("CPU\u6e29\u5ea6"),
        QStringLiteral("CPU\u529f\u8017"),
        QStringLiteral("GPU%"),
        QStringLiteral("GPU\u6e29\u5ea6"),
        QStringLiteral("GPU\u529f\u8017"),
    };

    return !shortHiddenLabels.contains(label);
}

void clearLayout(QLayout* layout)
{
    if (!layout)
        return;

    while (QLayoutItem* item = layout->takeAt(0))
        delete item;
}

} // namespace

OscSettingsPage::OscSettingsPage(QWidget* parent)
    : ElaScrollPage(parent)
{
    setTitleVisible(false);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 15, 0);
    layout->setSpacing(8);

    layout->addWidget(createHardwareInfoCard(central));
    layout->addStretch();

    addCentralWidget(central, true, false, 0);
}

bool OscSettingsPage::eventFilter(QObject* watched, QEvent* event)
{
    QWidget* itemWidget = resolveItemWidget(watched);
    QWidget* widget = qobject_cast<QWidget*>(watched);

    if (widget == _sourcePanel && event->type() == QEvent::Resize) {
        refreshSourceArea();
        refreshCardHeight();
        return ElaScrollPage::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        if (!itemWidget)
            break;
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            _dragSourceItem = itemWidget;
            _dragStartPos = mouseEvent->pos();
        }
        break;
    }
    case QEvent::MouseMove: {
        if (!itemWidget)
            break;
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (!(mouseEvent->buttons() & Qt::LeftButton) || _dragSourceItem != itemWidget)
            break;

        if ((mouseEvent->pos() - _dragStartPos).manhattanLength() < QApplication::startDragDistance())
            break;

        const int itemId = itemIdFromWidget(itemWidget);
        if (itemId < 0)
            break;

        auto* drag = new QDrag(itemWidget);
        auto* mimeData = new QMimeData;
        mimeData->setData(kDragMimeType, QByteArray::number(itemId));
        drag->setMimeData(mimeData);
        drag->setPixmap(itemWidget->grab());
        drag->setHotSpot(mouseEvent->pos());
        drag->exec(Qt::MoveAction);
        return true;
    }
    case QEvent::DragEnter: {
        auto* dragEvent = static_cast<QDragEnterEvent*>(event);
        if (dragEvent->mimeData()->hasFormat(kDragMimeType)) {
            dragEvent->acceptProposedAction();
            return true;
        }
        break;
    }
    case QEvent::DragMove: {
        auto* dragEvent = static_cast<QDragMoveEvent*>(event);
        if (dragEvent->mimeData()->hasFormat(kDragMimeType)) {
            dragEvent->acceptProposedAction();
            return true;
        }
        break;
    }
    case QEvent::Drop: {
        auto* dropEvent = static_cast<QDropEvent*>(event);
        if (!dropEvent->mimeData()->hasFormat(kDragMimeType))
            break;

        bool ok = false;
        const int itemId = dropEvent->mimeData()->data(kDragMimeType).toInt(&ok);
        if (!ok || itemId < 0 || itemId >= _itemWidgets.size())
            break;

        if (widget == _sourcePanel) {
            moveItemToSource(itemId);
            refreshAllAreas();
            dropEvent->acceptProposedAction();
            return true;
        }

        if (itemWidget) {
            const int targetItemId = itemIdFromWidget(itemWidget);
            if (targetItemId < 0)
                break;

            const int targetRow = placementRowForItem(targetItemId);
            if (targetRow >= 0) {
                const int insertIndex = _placementRowsItems[targetRow].indexOf(targetItemId);
                moveItemToPlacementRow(itemId, targetRow, insertIndex);
            } else {
                // Parameter pool does not allow drag-reorder.
                // If dragging from placement to source area, put item back in default order.
                if (placementRowForItem(itemId) >= 0)
                    moveItemToSource(itemId);
            }

            refreshAllAreas();
            dropEvent->acceptProposedAction();
            return true;
        }

        if (widget) {
            const int rowIndex = widget->property("placement_row_index").toInt();
            if (rowIndex >= 0 && rowIndex < _placementRowsItems.size()) {
                moveItemToPlacementRow(itemId, rowIndex, -1);
                refreshAllAreas();
                dropEvent->acceptProposedAction();
                return true;
            }
        }
        break;
    }
    default:
        break;
    }

    return ElaScrollPage::eventFilter(watched, event);
}

ElaScrollPageArea* OscSettingsPage::createHardwareInfoCard(QWidget* parent)
{
    _hardwareCard = new ElaScrollPageArea(parent);
    _hardwareCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* cardLayout = new QVBoxLayout(_hardwareCard);
    cardLayout->setContentsMargins(16, 12, 16, 12);
    cardLayout->setSpacing(8);
    cardLayout->setAlignment(Qt::AlignTop);

    auto* title = new ElaText(QStringLiteral("Hardware Info"), _hardwareCard);
    title->setTextPixelSize(16);
    title->setStyleSheet(QStringLiteral("color: #7cbf63; font-weight: 600;"));

    auto* sourceHint = new ElaText(QStringLiteral("Top: parameter pool (drag items to placement area below)"), _hardwareCard);
    sourceHint->setTextPixelSize(13);
    sourceHint->setStyleSheet(QStringLiteral("color: rgb(120, 120, 120);"));

    _sourcePanel = new QFrame(_hardwareCard);
    _sourcePanel->setObjectName(QStringLiteral("sourcePanel"));
    _sourcePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _sourcePanel->setAcceptDrops(true);
    _sourcePanel->installEventFilter(this);
    _sourcePanel->setStyleSheet(QStringLiteral(
        "#sourcePanel {"
        "  border: 1px solid rgba(120, 120, 120, 0.35);"
        "  border-radius: 0px;"
        "  background: transparent;"
        "}"));

    _sourceLayout = new ElaFlowLayout(_sourcePanel, 0, kItemSpacing, kRowSpacing);
    _sourceLayout->setIsAnimation(false);
    _sourceLayout->setContentsMargins(kPanelPadding, kPanelPadding, kPanelPadding, kPanelPadding);

    auto* placementHint = new ElaText(QStringLiteral("Bottom: placement rows (drop items here)"), _hardwareCard);
    placementHint->setTextPixelSize(13);
    placementHint->setStyleSheet(QStringLiteral("color: rgb(120, 120, 120);"));

    _placementContainer = new QWidget(_hardwareCard);
    _placementContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _placementContainerLayout = new QVBoxLayout(_placementContainer);
    _placementContainerLayout->setContentsMargins(0, 0, 0, 0);
    _placementContainerLayout->setSpacing(8);
    _placementContainerLayout->setAlignment(Qt::AlignTop);

    auto* controlRow = new QWidget(_hardwareCard);
    controlRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    controlRow->setFixedHeight(kControlRowHeight);

    auto* controlLayout = new QHBoxLayout(controlRow);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(8);

    _decreaseRowButton = new ElaPushButton(QStringLiteral("-"), controlRow);
    _decreaseRowButton->setFixedSize(28, 28);
    _increaseRowButton = new ElaPushButton(QStringLiteral("+"), controlRow);
    _increaseRowButton->setFixedSize(28, 28);
    _resetParamsButton = new ElaPushButton(QStringLiteral("Reset"), controlRow);
    _rowCountText = new ElaText(QStringLiteral("Rows %1").arg(_rowCount), controlRow);
    _rowCountText->setTextPixelSize(14);
    _rowCountText->setStyleSheet(QStringLiteral("color: rgb(120, 120, 120);"));

    controlLayout->addWidget(_resetParamsButton);
    controlLayout->addStretch();
    controlLayout->addWidget(_decreaseRowButton);
    controlLayout->addWidget(_rowCountText);
    controlLayout->addWidget(_increaseRowButton);

    cardLayout->addWidget(title);
    cardLayout->addWidget(sourceHint);
    cardLayout->addWidget(_sourcePanel);
    cardLayout->addWidget(placementHint);
    cardLayout->addWidget(_placementContainer);
    cardLayout->addWidget(controlRow);

    const QVector<OscHardwareInfoItemSpec> specs = defaultHardwareInfoSpecs();
    _itemWidgets.reserve(specs.size());
    _defaultCheckedStates.reserve(specs.size());
    _sourceOrder.reserve(specs.size());
    for (int i = 0; i < specs.size(); ++i) {
        _itemWidgets.push_back(createHardwareInfoItem(specs[i].label, specs[i].checked, _sourcePanel));
        _defaultCheckedStates.push_back(specs[i].checked);
        _sourceOrder.push_back(i);
    }

    _placementRowsItems.resize(_rowCount);
    rebuildPlacementRows();
    refreshAllAreas();

    connect(_decreaseRowButton, &ElaPushButton::clicked, this, &OscSettingsPage::decreaseRowCount);
    connect(_increaseRowButton, &ElaPushButton::clicked, this, &OscSettingsPage::increaseRowCount);
    connect(_resetParamsButton, &ElaPushButton::clicked, this, &OscSettingsPage::resetParameters);
    loadPersistentState();

    return _hardwareCard;
}

QWidget* OscSettingsPage::createHardwareInfoItem(const QString& label, bool checked, QWidget* parent)
{
    auto* container = new QWidget(parent);
    const bool supportsShortOption = supportsItemShortOption(label);
    container->setProperty("supports_short_option", supportsShortOption);
    container->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    container->setFixedHeight(kItemHeight);
    container->setAcceptDrops(true);
    container->installEventFilter(this);

    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->setAlignment(Qt::AlignVCenter);

    auto* dragHandle = new QLabel(QStringLiteral("::"), container);
    dragHandle->setFixedWidth(12);
    dragHandle->setAlignment(Qt::AlignCenter);
    dragHandle->setStyleSheet(QStringLiteral("color: rgb(150, 150, 150);"));
    dragHandle->setCursor(Qt::OpenHandCursor);
    dragHandle->installEventFilter(this);

    auto* checkBox = new ElaCheckBox(label, container);
    checkBox->setChecked(checked);
    checkBox->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(checkBox, &ElaCheckBox::toggled, this, [this](bool) {
        emitSelectionChanged();
    });

    auto* optionHost = new QWidget(container);
    optionHost->setObjectName(QStringLiteral("itemOptionHost"));
    optionHost->setFixedSize(44, 24);
    optionHost->setVisible(false);

    auto* optionHostLayout = new QGridLayout(optionHost);
    optionHostLayout->setContentsMargins(0, 0, 0, 0);
    optionHostLayout->setSpacing(0);

    auto* optionCombo = new ElaMultiSelectComboBox(optionHost);
    optionCombo->setObjectName(QStringLiteral("itemOptionCombo"));
    QStringList optionItems{QStringLiteral("\u65e0\u524d\u7f00"),
                            QStringLiteral("\u7a7a\u683c")};
    if (supportsShortOption)
        optionItems.insert(1, QStringLiteral("\u7b80\u7565"));
    optionCombo->addItems(optionItems);
    optionCombo->setCurrentSelection(QList<int>{});
    optionCombo->setFixedSize(44, 24);
    optionCombo->setStyleSheet(QStringLiteral("color: transparent;"));
    if (optionCombo->view())
        optionCombo->view()->setMinimumWidth(126);
    connect(optionCombo, &ElaMultiSelectComboBox::currentTextListChanged, this, [this](const QStringList&) {
        emitSelectionChanged();
    });

    auto* optionOverlay = new ElaText(QStringLiteral("..."), optionHost);
    optionOverlay->setObjectName(QStringLiteral("itemOptionOverlay"));
    optionOverlay->setAlignment(Qt::AlignCenter);
    optionOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    optionOverlay->setTextPixelSize(13);
    optionOverlay->setStyleSheet(QStringLiteral("padding-right: 12px;"));

    optionHostLayout->addWidget(optionCombo, 0, 0);
    optionHostLayout->addWidget(optionOverlay, 0, 0);

    layout->addWidget(dragHandle);
    layout->addWidget(checkBox);
    layout->addWidget(optionHost);
    return container;
}

QStringList OscSettingsPage::selectedParametersOrdered() const
{
    QStringList parameters;
    for (const QVector<int>& rowItems : _placementRowsItems) {
        QStringList rowParameters;

        for (int itemId : rowItems) {
            if (itemId < 0 || itemId >= _itemWidgets.size())
                continue;

            if (auto* checkBox = _itemWidgets[itemId]->findChild<ElaCheckBox*>()) {
                if (!checkBox->isChecked())
                    continue;

                QString parameterText = checkBox->text();
                if (auto* optionCombo = _itemWidgets[itemId]->findChild<ElaMultiSelectComboBox*>(QStringLiteral("itemOptionCombo"))) {
                    const QStringList selections = optionCombo->getCurrentSelection();
                    if (selections.contains(QStringLiteral("无前缀")))
                        parameterText += QStringLiteral("|||noprefix");
                    if (selections.contains(QStringLiteral("简略")))
                        parameterText += QStringLiteral("|||short");
                    if (selections.contains(QStringLiteral("空格")))
                        parameterText += QStringLiteral("|||space");
                }

                rowParameters.push_back(parameterText);
            }
        }

        if (!rowParameters.isEmpty()) {
            parameters += rowParameters;
            parameters.push_back(QStringLiteral("|||rowbreak"));
        }
    }

    if (!parameters.isEmpty() && parameters.back() == QStringLiteral("|||rowbreak"))
        parameters.pop_back();

    return parameters;
}

QWidget* OscSettingsPage::resolveItemWidget(QObject* watched) const
{
    auto* widget = qobject_cast<QWidget*>(watched);
    while (widget) {
        if (_itemWidgets.contains(widget))
            return widget;
        widget = widget->parentWidget();
    }
    return nullptr;
}

int OscSettingsPage::itemIdFromWidget(QWidget* itemWidget) const
{
    return _itemWidgets.indexOf(itemWidget);
}

int OscSettingsPage::itemIdByLabel(const QString& label) const
{
    for (int itemId = 0; itemId < _itemWidgets.size(); ++itemId) {
        if (itemLabelById(itemId) == label)
            return itemId;
    }
    return -1;
}

QString OscSettingsPage::itemLabelById(int itemId) const
{
    if (itemId < 0 || itemId >= _itemWidgets.size())
        return {};

    if (auto* checkBox = _itemWidgets[itemId]->findChild<ElaCheckBox*>())
        return checkBox->text();

    return {};
}

QString OscSettingsPage::configFilePath() const
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

    return QDir(rootPath).filePath(QStringLiteral("config/osc_settings.ini"));
}

void OscSettingsPage::savePersistentState() const
{
    const QString filePath = configFilePath();
    const QFileInfo fileInfo(filePath);
    QDir().mkpath(fileInfo.absolutePath());

    QSettings settings(filePath, QSettings::IniFormat);
    settings.clear();
    settings.beginGroup(QStringLiteral("osc_settings"));

    settings.setValue(QStringLiteral("row_count"), _rowCount);

    settings.beginWriteArray(QStringLiteral("rows"));
    for (int rowIndex = 0; rowIndex < _placementRowsItems.size(); ++rowIndex) {
        settings.setArrayIndex(rowIndex);
        QStringList labels;
        for (const int itemId : _placementRowsItems[rowIndex])
            labels.push_back(itemLabelById(itemId));
        settings.setValue(QStringLiteral("items"), labels);
    }
    settings.endArray();

    QStringList sourceOrderLabels;
    sourceOrderLabels.reserve(_sourceOrder.size());
    for (const int itemId : _sourceOrder)
        sourceOrderLabels.push_back(itemLabelById(itemId));
    settings.setValue(QStringLiteral("source_order"), sourceOrderLabels);

    settings.beginWriteArray(QStringLiteral("items"));
    for (int itemId = 0; itemId < _itemWidgets.size(); ++itemId) {
        settings.setArrayIndex(itemId);
        settings.setValue(QStringLiteral("label"), itemLabelById(itemId));

        if (auto* checkBox = _itemWidgets[itemId]->findChild<ElaCheckBox*>())
            settings.setValue(QStringLiteral("checked"), checkBox->isChecked());

        if (auto* optionCombo = _itemWidgets[itemId]->findChild<ElaMultiSelectComboBox*>(QStringLiteral("itemOptionCombo")))
            settings.setValue(QStringLiteral("options"), optionCombo->getCurrentSelection());
    }
    settings.endArray();

    settings.endGroup();
    settings.sync();
}

void OscSettingsPage::loadPersistentState()
{
    const QString filePath = configFilePath();
    if (!QFileInfo::exists(filePath))
        return;

    _isRestoringState = true;

    QSettings settings(filePath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("osc_settings"));

    const int storedRowCount = settings.value(QStringLiteral("row_count"), _rowCount).toInt();
    _rowCount = qBound(kMinRowCount, storedRowCount, kMaxRowCount);
    _placementRowsItems = QVector<QVector<int>>(_rowCount);

    QSet<int> usedItemIds;
    const int storedRows = settings.beginReadArray(QStringLiteral("rows"));
    for (int rowIndex = 0; rowIndex < qMin(storedRows, _rowCount); ++rowIndex) {
        settings.setArrayIndex(rowIndex);
        const QStringList labels = settings.value(QStringLiteral("items")).toStringList();
        for (const QString& label : labels) {
            const int itemId = itemIdByLabel(label);
            if (itemId >= 0 && !usedItemIds.contains(itemId)) {
                _placementRowsItems[rowIndex].push_back(itemId);
                usedItemIds.insert(itemId);
            }
        }
    }
    settings.endArray();

    _sourceOrder.clear();
    const QStringList sourceOrderLabels = settings.value(QStringLiteral("source_order")).toStringList();
    for (const QString& label : sourceOrderLabels) {
        const int itemId = itemIdByLabel(label);
        if (itemId >= 0 && !usedItemIds.contains(itemId)) {
            _sourceOrder.push_back(itemId);
            usedItemIds.insert(itemId);
        }
    }
    for (int itemId = 0; itemId < _itemWidgets.size(); ++itemId) {
        if (!usedItemIds.contains(itemId))
            _sourceOrder.push_back(itemId);
    }

    QHash<QString, bool> checkedByLabel;
    QHash<QString, QStringList> optionsByLabel;
    const int itemCount = settings.beginReadArray(QStringLiteral("items"));
    for (int i = 0; i < itemCount; ++i) {
        settings.setArrayIndex(i);
        const QString label = settings.value(QStringLiteral("label")).toString();
        if (label.isEmpty())
            continue;
        checkedByLabel.insert(label, settings.value(QStringLiteral("checked"), false).toBool());
        optionsByLabel.insert(label, settings.value(QStringLiteral("options")).toStringList());
    }
    settings.endArray();
    settings.endGroup();

    rebuildPlacementRows();
    refreshAllAreas();

    for (int itemId = 0; itemId < _itemWidgets.size(); ++itemId) {
        const QString label = itemLabelById(itemId);
        if (auto* checkBox = _itemWidgets[itemId]->findChild<ElaCheckBox*>()) {
            const bool checked = checkedByLabel.contains(label)
                                     ? checkedByLabel.value(label)
                                     : (placementRowForItem(itemId) >= 0);
            checkBox->setChecked(checked);
        }

        if (auto* optionCombo = _itemWidgets[itemId]->findChild<ElaMultiSelectComboBox*>(QStringLiteral("itemOptionCombo"))) {
            QStringList options = optionsByLabel.value(label);
            if (!supportsItemShortOption(label))
                options.removeAll(QStringLiteral("\u7b80\u7565"));

            QList<int> selectedIndexes;
            for (const QString& option : options) {
                for (int index = 0; index < optionCombo->count(); ++index) {
                    if (optionCombo->itemText(index) == option) {
                        selectedIndexes.push_back(index);
                        break;
                    }
                }
            }
            optionCombo->setCurrentSelection(selectedIndexes);
        }
    }

    _isRestoringState = false;
    emitSelectionChanged();
}

void OscSettingsPage::decreaseRowCount()
{
    adjustRowCount(-1);
}

void OscSettingsPage::increaseRowCount()
{
    adjustRowCount(1);
}

void OscSettingsPage::resetParameters()
{
    _sourceOrder.clear();
    _sourceOrder.reserve(_itemWidgets.size());
    for (int itemId = 0; itemId < _itemWidgets.size(); ++itemId)
        _sourceOrder.push_back(itemId);

    for (QVector<int>& rowItems : _placementRowsItems)
        rowItems.clear();

    for (int i = 0; i < _itemWidgets.size(); ++i) {
        if (i >= _defaultCheckedStates.size())
            break;
        if (auto* checkBox = _itemWidgets[i]->findChild<ElaCheckBox*>())
            checkBox->setChecked(_defaultCheckedStates[i]);
        if (auto* optionCombo = _itemWidgets[i]->findChild<ElaMultiSelectComboBox*>(QStringLiteral("itemOptionCombo")))
            optionCombo->setCurrentSelection(QList<int>{});
    }

    refreshAllAreas();
    emitSelectionChanged();
}

void OscSettingsPage::adjustRowCount(int delta)
{
    const int targetCount = qBound(kMinRowCount, _rowCount + delta, kMaxRowCount);
    if (targetCount == _rowCount)
        return;

    if (targetCount > _rowCount) {
        while (_placementRowsItems.size() < targetCount)
            _placementRowsItems.push_back({});
    } else {
        while (_placementRowsItems.size() > targetCount) {
            QVector<int> removed = _placementRowsItems.takeLast();
            if (!_placementRowsItems.isEmpty())
                _placementRowsItems.last() += removed;
            else
                _sourceOrder += removed;
        }
    }

    _rowCount = targetCount;
    rebuildPlacementRows();
    refreshAllAreas();
    emitSelectionChanged();
}

void OscSettingsPage::rebuildPlacementRows()
{
    clearLayout(_placementContainerLayout);
    _placementRowFrames.clear();
    _placementRowLayouts.clear();

    for (int row = 0; row < _rowCount; ++row) {
        auto* rowFrame = new QFrame(_placementContainer);
        rowFrame->setObjectName(QStringLiteral("placementRow"));
        rowFrame->setProperty("placement_row_index", row);
        rowFrame->setAcceptDrops(true);
        rowFrame->installEventFilter(this);
        rowFrame->setMinimumHeight(kPlacementRowMinHeight);
        rowFrame->setStyleSheet(QStringLiteral(
            "#placementRow {"
            "  border: 1px dashed rgba(120, 120, 120, 0.55);"
            "  border-radius: 0px;"
            "  background: rgba(255, 255, 255, 0.03);"
            "}"));

        auto* rowLayout = new QHBoxLayout(rowFrame);
        rowLayout->setContentsMargins(kPanelPadding, 6, kPanelPadding, 6);
        rowLayout->setSpacing(kItemSpacing);
        rowLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        _placementContainerLayout->addWidget(rowFrame);
        _placementRowFrames.push_back(rowFrame);
        _placementRowLayouts.push_back(rowLayout);
    }
}

void OscSettingsPage::refreshSourceArea()
{
    clearLayout(_sourceLayout);

    for (int itemId : _sourceOrder) {
        QWidget* widget = _itemWidgets[itemId];
        widget->setProperty("placement_row_index", -1);
        if (auto* optionHost = widget->findChild<QWidget*>(QStringLiteral("itemOptionHost")))
            optionHost->setVisible(false);
        _sourceLayout->addWidget(widget);
    }

    int panelWidth = _sourcePanel->width();
    if (panelWidth <= 0 && _hardwareCard)
        panelWidth = _hardwareCard->width() - 32;
    panelWidth = qMax(panelWidth, 1);

    int sourceHeight = _sourceLayout->heightForWidth(panelWidth);
    sourceHeight = qMax(sourceHeight, kPanelPadding * 2 + kItemHeight);
    _sourcePanel->setFixedHeight(sourceHeight);
}

void OscSettingsPage::refreshPlacementArea()
{
    for (int row = 0; row < _placementRowLayouts.size(); ++row) {
        QHBoxLayout* rowLayout = _placementRowLayouts[row];
        clearLayout(rowLayout);

        for (int itemId : _placementRowsItems[row]) {
            QWidget* widget = _itemWidgets[itemId];
            widget->setProperty("placement_row_index", row);
            if (auto* optionHost = widget->findChild<QWidget*>(QStringLiteral("itemOptionHost")))
                optionHost->setVisible(true);
            rowLayout->addWidget(widget);
        }

        rowLayout->addStretch();
    }
}

void OscSettingsPage::refreshAllAreas()
{
    refreshSourceArea();
    refreshPlacementArea();
    refreshRowCountControls();
    refreshCardHeight();
}

void OscSettingsPage::refreshRowCountControls()
{
    _rowCountText->setText(QStringLiteral("Rows %1").arg(_rowCount));
    _decreaseRowButton->setEnabled(_rowCount > kMinRowCount);
    _increaseRowButton->setEnabled(_rowCount < kMaxRowCount);
}

void OscSettingsPage::refreshCardHeight()
{
    const int sourceHeight = _sourcePanel ? _sourcePanel->height() : (kPanelPadding * 2 + kItemHeight);
    const int placementHeight = _rowCount * kPlacementRowMinHeight + (_rowCount - 1) * 8;

    const int titleAndHints = 16 + 13 + 13;
    const int verticalSpacing = 8 * 8;
    const int margins = 12 + 12;
    const int controls = kControlRowHeight;

    _hardwareCard->setFixedHeight(margins + titleAndHints + sourceHeight + placementHeight + controls + verticalSpacing);
}

void OscSettingsPage::moveItemToSource(int itemId)
{
    removeItemFromCurrentLocation(itemId);

    int insertIndex = 0;
    while (insertIndex < _sourceOrder.size() && _sourceOrder[insertIndex] < itemId)
        ++insertIndex;

    _sourceOrder.insert(insertIndex, itemId);

    if (itemId >= 0 && itemId < _itemWidgets.size()) {
        if (auto* checkBox = _itemWidgets[itemId]->findChild<ElaCheckBox*>())
            checkBox->setChecked(false);
    }
    emitSelectionChanged();
}

void OscSettingsPage::moveItemToPlacementRow(int itemId, int rowIndex, int insertIndex)
{
    if (rowIndex < 0 || rowIndex >= _placementRowsItems.size())
        return;

    removeItemFromCurrentLocation(itemId);

    QVector<int>& rowItems = _placementRowsItems[rowIndex];
    if (insertIndex < 0 || insertIndex > rowItems.size())
        rowItems.push_back(itemId);
    else
        rowItems.insert(insertIndex, itemId);

    if (itemId >= 0 && itemId < _itemWidgets.size()) {
        if (auto* checkBox = _itemWidgets[itemId]->findChild<ElaCheckBox*>())
            checkBox->setChecked(true);
    }
    emitSelectionChanged();
}

void OscSettingsPage::removeItemFromCurrentLocation(int itemId)
{
    int sourceIndex = _sourceOrder.indexOf(itemId);
    if (sourceIndex >= 0) {
        _sourceOrder.removeAt(sourceIndex);
        return;
    }

    for (QVector<int>& rowItems : _placementRowsItems) {
        const int rowIndex = rowItems.indexOf(itemId);
        if (rowIndex >= 0) {
            rowItems.removeAt(rowIndex);
            return;
        }
    }
}

int OscSettingsPage::placementRowForItem(int itemId) const
{
    for (int row = 0; row < _placementRowsItems.size(); ++row) {
        if (_placementRowsItems[row].contains(itemId))
            return row;
    }
    return -1;
}

void OscSettingsPage::emitSelectionChanged()
{
    if (_isRestoringState)
        return;

    savePersistentState();
    emit selectedParametersChanged(selectedParametersOrdered());
}
