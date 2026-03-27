#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include "ElaScrollPage.h"

#include <QVector>

class ElaLineEdit;
class ElaPushButton;
class ElaScrollPageArea;
class ElaText;

class SettingsPage : public ElaScrollPage
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);
    ~SettingsPage() override = default;

private slots:
    void detectPortOccupancy();
    void sendOscChatboxTest();
    void terminatePortOccupancy();

private:
    ElaScrollPageArea* createOscChatboxTestCard(QWidget* parent);
    ElaScrollPageArea* createOscPortCard(QWidget* parent);
    quint16 currentPort(bool* isValid = nullptr) const;
    void setOscTestStatusMessage(const QString& message, bool isError = false);
    void setStatusMessage(const QString& message, bool isError = false);

    ElaLineEdit* _portEdit{nullptr};
    ElaPushButton* _detectButton{nullptr};
    ElaPushButton* _sendTestButton{nullptr};
    ElaPushButton* _terminateButton{nullptr};
    ElaLineEdit* _oscTestInputEdit{nullptr};
    ElaText* _oscTestStatusText{nullptr};
    ElaText* _statusText{nullptr};
    QVector<quint32> _occupiedPids;
};

#endif // SETTINGSPAGE_H
