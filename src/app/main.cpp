#include "BrushThumbnails.h"
#include "CanvasItem.h"
#include "RecoveryManager.h"

#include <QDir>
#include <QElapsedTimer>
#include <QTimer>
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(DrawAppPlugin)

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

namespace {

// Working set and private memory of this process in MiB (Windows), or -1.
void processMemory(double *workingSet, double *privateBytes)
{
    *workingSet = *privateBytes = -1;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX c{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&c), sizeof(c))) {
        *workingSet = c.WorkingSetSize / 1048576.0;
        *privateBytes = c.PrivateUsage / 1048576.0;
    }
#endif
}

} // namespace

int main(int argc, char *argv[])
{
    // DRAWAPP_STARTUP_PROBE=1: print the time from main() to the first rendered frame and the
    // process memory then (and again 2 s later, idle), then quit. For benchmarks.
    QElapsedTimer startup;
    startup.start();
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
    engine.addImageProvider(QStringLiteral("brushthumb"), new BrushThumbnails);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("DrawApp", "Main");

    if (qEnvironmentVariableIsSet("DRAWAPP_STARTUP_PROBE") && !engine.rootObjects().isEmpty()) {
        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
            QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&startup] {
                static bool done = false;
                if (done)
                    return;
                done = true;
                double ws, priv;
                processMemory(&ws, &priv);
                std::printf("first frame %.1f ms; working set %.1f MiB, private %.1f MiB\n", startup.nsecsElapsed() / 1e6, ws, priv);
                QTimer::singleShot(2000, [] {
                    double ws2, priv2;
                    processMemory(&ws2, &priv2);
                    std::printf("idle after 2 s: working set %.1f MiB, private %.1f MiB\n", ws2, priv2);
                    std::fflush(stdout);
                    QCoreApplication::quit();
                });
            }, Qt::DirectConnection);
        }
    }

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
        QString brushFile = qEnvironmentVariable("DRAWAPP_BRUSH_FILE");
        if (brushFile.isEmpty())
            brushFile = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("brushes.json"));
        canvas->brushes()->setStorePath(brushFile);
    }

    const int result = app.exec();
    if (canvas)
        canvas->shutdownRecovery();
    return result;
}
