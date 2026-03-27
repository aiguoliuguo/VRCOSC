#include "mainwindow.h"

#include "ElaApplication.h"
#include <QApplication>
#include <QByteArray>
#include <QDateTime>

#include <Windows.h>

#include <string>
#include <cstdio>
#include <io.h>
#include <fcntl.h>

namespace {

void ensureConsoleAttached()
{
    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0) {
        if (GetLastError() != ERROR_ACCESS_DENIED)
            AllocConsole();
    }

    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    freopen_s(&stream, "CONIN$", "r", stdin);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
}

void consoleMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    Q_UNUSED(context);

    const QString level = [type]() -> QString {
        switch (type) {
        case QtDebugMsg:
            return QStringLiteral("DEBUG");
        case QtInfoMsg:
            return QStringLiteral("INFO");
        case QtWarningMsg:
            return QStringLiteral("WARN");
        case QtCriticalMsg:
            return QStringLiteral("ERROR");
        case QtFatalMsg:
            return QStringLiteral("FATAL");
        }
        return QStringLiteral("LOG");
    }();

    const QString line = QStringLiteral("%1 [%2] %3\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  level,
                                  message);

    const std::wstring wideLine = line.toStdWString();
    DWORD written = 0;
    if (!WriteConsoleW(GetStdHandle(STD_ERROR_HANDLE),
                       wideLine.c_str(),
                       static_cast<DWORD>(wideLine.size()),
                       &written,
                       nullptr)) {
        const QByteArray utf8 = line.toUtf8();
        fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stderr);
        fflush(stderr);
    }

    if (type == QtFatalMsg)
        abort();
}

} // namespace

int main(int argc, char *argv[])
{
    ensureConsoleAttached();
    qInstallMessageHandler(consoleMessageHandler);

    QApplication a(argc, argv);
    eApp->init();
    MainWindow w;
    w.show();

    qInfo().noquote() << QStringLiteral("VRCOSC started");

    return a.exec();
}
