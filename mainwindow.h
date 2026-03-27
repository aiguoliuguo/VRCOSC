#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ElaWindow.h"

class HomePage;
class LighthousePage;
class OscSettingsPage;
class SettingsPage;

class MainWindow : public ElaWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void initWindow();
    void initContent();

    HomePage*         _homePage{nullptr};
    LighthousePage*   _lighthousePage{nullptr};
    OscSettingsPage*  _oscSettingsPage{nullptr};
    SettingsPage*     _settingsPage{nullptr};
};
#endif // MAINWINDOW_H
