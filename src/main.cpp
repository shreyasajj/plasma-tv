// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "appsettings.h"
#include "bridgecontrol.h"
#include "inputdebug.h"
#include "logoimageprovider.h"
#include "remotekeys.h"
#include "tvhbackend.h"
#ifdef FLIMMER_DEV_TOOLS
#include "devscript.h"
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDir>
#include <QIcon>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStyleFactory>
#include <QUuid>

int main(int argc, char *argv[])
{
    // Bigscreen UI is sized for TVs at 1.5 times its native size, so scale the
    // whole window (popups included) unless the user chose a factor.
    if (qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) {
        qputenv("QT_SCALE_FACTOR", "1.5");
    }

    // QApplication (not QGuiApplication): the Plasma Qt Quick Controls style
    // (org.kde.desktop) paints through the widget style.
    QApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("flimmer"));
    QGuiApplication::setApplicationName(QStringLiteral("flimmer"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Flimmer"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    // Must match the launcher's file name: the Plasma Bigscreen home screen only shows the running
    // window instead of starting another copy when the window's app ID matches it.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.jclsn.Flimmer"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Fullscreen TVHeadend and M3U/XMLTV client for the living room"));

    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption const windowed({QStringLiteral("w"), QStringLiteral("windowed")},
                                      QStringLiteral("Start in a window instead of fullscreen"));

    parser.addOption(windowed);
    parser.process(app);

    auto bus = QDBusConnection::sessionBus();

    if (!bus.isConnected()) {
        qWarning("A session bus is required for single-instance control");

        return 1;
    }

    if (!bus.registerService(BridgeControl::serviceName())) {
        QDBusInterface existing(BridgeControl::serviceName(),
                                BridgeControl::objectPath(),
                                BridgeControl::serviceName(),
                                bus);
        const QJsonObject request{
            {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("command"), QStringLiteral("focus")}};
        const auto reply =
            existing.call(QStringLiteral("Execute"),
                          QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));

        return reply.type() == QDBusMessage::ErrorMessage ? 1 : 0;
    }

    BridgeControl bridgeControl(TvhBackend::instance()->channels());

    if (!bus.registerObject(BridgeControl::objectPath(),
                            &bridgeControl,
                            QDBusConnection::ExportAllSlots)) {
        qWarning("Cannot register the local control endpoint");

        return 1;
    }

    // Follow Plasma's look; outside a Plasma session fall back to Breeze explicitly.
    const bool plasmaSession =
        qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("KDE"));

    if (!plasmaSession
        && QStyleFactory::keys().contains(QStringLiteral("Breeze"), Qt::CaseInsensitive)) {
        QApplication::setStyle(QStringLiteral("Breeze"));
    }

    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor")) {
        QIcon::setThemeName(QStringLiteral("breeze"));
    }

    // The bundled icon also works when the application isn't installed.
    QApplication::setWindowIcon(
        QIcon(QStringLiteral(":/qt/qml/Flimmer/data/icons/hicolor/256x256/apps/flimmer.png")));

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
        QQuickStyle::setFallbackStyle(QStringLiteral("Fusion"));
    }

    AppSettings::instance()->applyColorScheme();

    RemoteKeys::install(&app);

    QQmlApplicationEngine engine;
#ifdef FLIMMER_QML_DIR
    // Bigscreen UI built along with the app: next to the executable in the build tree, in a
    // private directory once installed.
    const QDir binDir(QCoreApplication::applicationDirPath());
    engine.addImportPath(binDir.filePath(QStringLiteral("bin")));
    engine.addImportPath(binDir.filePath(QStringLiteral(FLIMMER_QML_DIR)));
#endif

    engine.addImageProvider(QStringLiteral("logo"), new LogoImageProvider);
    engine.setInitialProperties(
        {{QStringLiteral("startWindowed"), parser.isSet(windowed)},
         {QStringLiteral("bridgeControl"), QVariant::fromValue(&bridgeControl)}});
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] {
            QCoreApplication::exit(1);
        },
        Qt::QueuedConnection);
    engine.loadFromModule("Flimmer", "Main");
    InputDebug::install(&engine);
#ifdef FLIMMER_DEV_TOOLS
    DevScript::runFromEnvironment(&engine);
#endif

    return QApplication::exec();
}
