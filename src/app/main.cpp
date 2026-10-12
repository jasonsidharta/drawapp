#include "CanvasItem.h"
#include "RecoveryManager.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>

namespace {

CanvasItem *findCanvas(QQuickItem *item)
{
    if (auto *canvas = qobject_cast<CanvasItem *>(item))
        return canvas;
    for (QQuickItem *child : item->childItems()) {
        if (CanvasItem *canvas = findCanvas(child))
            return canvas;
    }
    return nullptr;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("DrawApp"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("DrawApp (prototype)"));

    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    // Declared before the engine so it outlives the canvas.
    QString recoveryDir = qEnvironmentVariable("DRAWAPP_RECOVERY_DIR");
    if (recoveryDir.isEmpty())
        recoveryDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("recovery"));
    RecoveryManager recovery(recoveryDir);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("DrawApp", "Main");

    CanvasItem *canvas = nullptr;
    if (!engine.rootObjects().isEmpty()) {
        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()))
            canvas = findCanvas(window->contentItem());
    }
    if (canvas) {
        bool ok = false;
        const int interval = qEnvironmentVariableIntValue("DRAWAPP_AUTOSAVE_MS", &ok);
        if (ok && interval > 0)
            canvas->setAutosaveInterval(interval);
        canvas->setRecoveryManager(&recovery);
    }

    const int result = app.exec();
    if (canvas)
        canvas->shutdownRecovery();
    return result;
}
