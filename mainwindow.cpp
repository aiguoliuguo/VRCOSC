#include "mainwindow.h"

#include "homepage.h"
#include "ElaContentDialog.h"
#include "ElaText.h"
#include "ElaTheme.h"
#include "ElaDef.h"

#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : ElaWindow(parent)
{
    initWindow();
    initContent();

    // 拦截默认关闭事件，弹出确认对话框
    _closeDialog = new ElaContentDialog(this);
    _closeDialog->setLeftButtonText("取消");
    _closeDialog->setMiddleButtonText("最小化");
    _closeDialog->setRightButtonText("退出");
    connect(_closeDialog, &ElaContentDialog::rightButtonClicked, this, &MainWindow::closeWindow);
    connect(_closeDialog, &ElaContentDialog::middleButtonClicked, this, [=]() {
        _closeDialog->close();
        showMinimized();
    });
    setIsDefaultClosed(false);
    connect(this, &MainWindow::closeButtonClicked, this, [=]() {
        _closeDialog->exec();
    });

    moveToCenter();
}

MainWindow::~MainWindow() {}

void MainWindow::initWindow()
{
    resize(1100, 700);
    setWindowTitle("VRCOSC");
    setUserInfoCardTitle("VRCOSC");
    setUserInfoCardSubTitle("VRChat OSC 控制台");

    // 主题切换
    connect(this, &MainWindow::userInfoCardClicked, this, [=]() {
        auto mode = eTheme->getThemeMode();
        eTheme->setThemeMode(mode == ElaThemeType::Light ? ElaThemeType::Dark : ElaThemeType::Light);
    });
}

void MainWindow::initContent()
{
    _homePage = new HomePage(this);
    addPageNode("主页", _homePage, ElaIconType::House);
    addPageNode("OSC 设置", new QWidget(this), ElaIconType::Gear);
    addPageNode("参数监控", new QWidget(this), ElaIconType::ChartLine);

    QString aboutKey;
    addFooterNode("关于", aboutKey, 0, ElaIconType::CircleInfo);
}

