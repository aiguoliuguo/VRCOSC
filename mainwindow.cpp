#include "mainwindow.h"

#include "ElaDef.h"
#include "ElaTheme.h"
#include "homepage.h"
#include "lighthousepage.h"
#include "oscsettingspage.h"
#include "settingspage.h"

#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : ElaWindow(parent)
{
    initWindow();
    initContent();
    moveToCenter();
}

MainWindow::~MainWindow() = default;

void MainWindow::initWindow()
{
    resize(1100, 700);
    setWindowTitle("VRCOSC");
    setUserInfoCardTitle("VRCOSC");
    setUserInfoCardSubTitle(QStringLiteral("VRChat OSC \u63a7\u5236\u53f0"));
    setUserInfoCardVisible(false);

    connect(this, &MainWindow::userInfoCardClicked, this, [=]() {
        const auto mode = eTheme->getThemeMode();
        eTheme->setThemeMode(mode == ElaThemeType::Light ? ElaThemeType::Dark : ElaThemeType::Light);
    });
}

void MainWindow::initContent()
{
    _homePage = new HomePage(this);
    _lighthousePage = new LighthousePage(this);
    _oscSettingsPage = new OscSettingsPage(this);
    _settingsPage = new SettingsPage(this);

    connect(_oscSettingsPage, &OscSettingsPage::selectedParametersChanged,
            _homePage, &HomePage::setOscSelectedParameters);
    _homePage->setOscSelectedParameters(_oscSettingsPage->selectedParametersOrdered());

    addPageNode(QStringLiteral("\u9996\u9875"), _homePage, ElaIconType::House);
    addPageNode(QStringLiteral("\u706f\u5854\u7ba1\u7406"), _lighthousePage, ElaIconType::SatelliteDish);
    addPageNode(QStringLiteral("OSC \u8bbe\u7f6e"), _oscSettingsPage, ElaIconType::Gear);
    addPageNode(QStringLiteral("\u53c2\u6570\u76d1\u63a7"), new QWidget(this), ElaIconType::ChartLine);

    QString aboutKey;
    addFooterNode(QStringLiteral("\u5173\u4e8e"), aboutKey, 0, ElaIconType::CircleInfo);

    QString settingsKey;
    addFooterNode(QStringLiteral("\u8bbe\u7f6e"), _settingsPage, settingsKey, 0, ElaIconType::Gear);
}
