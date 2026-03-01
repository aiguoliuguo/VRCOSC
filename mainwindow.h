#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ElaWindow.h"

class HomePage;
class ElaContentDialog;

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
    ElaContentDialog* _closeDialog{nullptr};
};
#endif // MAINWINDOW_H
