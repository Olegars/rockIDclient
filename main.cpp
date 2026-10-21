#include "rockclient.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("rockID"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("rockid.local"));
    QGuiApplication::setApplicationName(QStringLiteral("rockIDclient"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    RockClient client;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("client"), &client);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("rockid", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    client.start();
    return app.exec();
}
