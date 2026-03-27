#ifndef OSCSETTINGSPAGE_H
#define OSCSETTINGSPAGE_H

#include "ElaScrollPage.h"

#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>

class ElaPushButton;
class ElaScrollPageArea;
class ElaText;
class ElaFlowLayout;
class QEvent;
class QFrame;
class QHBoxLayout;
class QObject;
class QVBoxLayout;
class QWidget;

class OscSettingsPage : public ElaScrollPage
{
    Q_OBJECT

public:
    explicit OscSettingsPage(QWidget* parent = nullptr);
    ~OscSettingsPage() override = default;
    QStringList selectedParametersOrdered() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void selectedParametersChanged(const QStringList& parameters);

private:
    ElaScrollPageArea* createHardwareInfoCard(QWidget* parent);
    QWidget* createHardwareInfoItem(const QString& label, bool checked, QWidget* parent);
    QWidget* resolveItemWidget(QObject* watched) const;
    int itemIdFromWidget(QWidget* itemWidget) const;
    int itemIdByLabel(const QString& label) const;
    QString itemLabelById(int itemId) const;

    void loadPersistentState();
    void savePersistentState() const;
    QString configFilePath() const;

    void decreaseRowCount();
    void increaseRowCount();
    void resetParameters();
    void adjustRowCount(int delta);
    void rebuildPlacementRows();
    void refreshSourceArea();
    void refreshPlacementArea();
    void refreshAllAreas();
    void refreshRowCountControls();
    void refreshCardHeight();

    void moveItemToSource(int itemId);
    void moveItemToPlacementRow(int itemId, int rowIndex, int insertIndex = -1);
    void removeItemFromCurrentLocation(int itemId);
    int placementRowForItem(int itemId) const;
    void emitSelectionChanged();

    int _rowCount{1};
    ElaScrollPageArea* _hardwareCard{nullptr};

    QFrame* _sourcePanel{nullptr};
    ElaFlowLayout* _sourceLayout{nullptr};

    QWidget* _placementContainer{nullptr};
    QVBoxLayout* _placementContainerLayout{nullptr};
    QVector<QFrame*> _placementRowFrames;
    QVector<QHBoxLayout*> _placementRowLayouts;

    ElaPushButton* _decreaseRowButton{nullptr};
    ElaPushButton* _increaseRowButton{nullptr};
    ElaPushButton* _resetParamsButton{nullptr};
    ElaText* _rowCountText{nullptr};

    QVector<QWidget*> _itemWidgets;
    QVector<bool> _defaultCheckedStates;
    QVector<int> _sourceOrder;
    QVector<QVector<int>> _placementRowsItems;
    bool _isRestoringState{false};

    QPoint _dragStartPos;
    QWidget* _dragSourceItem{nullptr};
};

#endif // OSCSETTINGSPAGE_H
