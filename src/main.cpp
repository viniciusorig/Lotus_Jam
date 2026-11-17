#include <QApplication>
#include <QTimer>
#include <memory>
#include <string>

#include "audio/pipewire_backend.h"
#include "ui/main_window.h"
#include "ui/theme.h"

// Lotus Jam. Development aid: "--quit-after-ms N" closes the window after N ms (used for a
// short offscreen start/shutdown check; never starts a session).
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("lotus-jam");
    QApplication::setApplicationDisplayName("Lotus Jam");
    theme::apply(app);

    MainWindow w([](const std::string& in, const std::string& out) -> std::unique_ptr<IAudioBackend> {
        return std::make_unique<PipeWireBackend>(in, out);
    });
    w.show();

    const QStringList args = app.arguments();
    const int i = int(args.indexOf("--quit-after-ms"));
    if (i >= 0 && i + 1 < args.size()) QTimer::singleShot(args[i + 1].toInt(), &w, &QWidget::close);
    return app.exec();
}
