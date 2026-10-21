#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QStyleHints>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("GtEdit"));
    QCoreApplication::setApplicationName(QStringLiteral("QtEdit"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QApplication::setApplicationDisplayName(QStringLiteral("QtEdit"));
    QGuiApplication::setDesktopFileName(QStringLiteral("qtedit"));
    app.setWindowIcon(QIcon(QStringLiteral(":/resources/appicon.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("A small, fast text editor built with Qt 6 and C++."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("file"), QStringLiteral("File to open."), QStringLiteral("[file]"));
    parser.process(app);

    MainWindow window;
    window.show();

    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty())
        window.openFile(files.first());

    return app.exec();
}
