#include "ui/theme.h"

#include <QApplication>
#include <QFile>
#include <QPalette>

static void initLotusResources() { Q_INIT_RESOURCE(resources); }

namespace theme {

QFont uiFont(int px, int weight) {
    QFont f;
    f.setFamilies({"Inter", "Noto Sans", "DejaVu Sans"});
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(px);
    f.setWeight(QFont::Weight(weight));
    return f;
}

QFont monoFont(int px, bool bold) {
    QFont f;
    f.setFamilies({"JetBrains Mono", "Fira Code", "Noto Sans Mono", "DejaVu Sans Mono"});
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    f.setPixelSize(px);
    f.setWeight(bold ? QFont::Bold : QFont::Medium);
    return f;
}

void initResources() {
    static bool done = false;
    if (!done) {
        initLotusResources();
        done = true;
    }
}

QIcon appIcon() {
    initResources();
    QIcon icon(":/icons/lotus_jam.svg");
    for (int s : {16, 32, 64, 128, 256}) icon.addFile(QString(":/icons/lotus_jam_%1.png").arg(s), QSize(s, s));
    return icon;
}

void apply(QApplication& app) {
    initResources();
    QPalette p = app.palette();
    p.setColor(QPalette::Window, bg_canvas);
    p.setColor(QPalette::WindowText, text_primary);
    p.setColor(QPalette::Base, bg_elevated);
    p.setColor(QPalette::AlternateBase, bg_surface);
    p.setColor(QPalette::Text, text_primary);
    p.setColor(QPalette::Button, bg_elevated);
    p.setColor(QPalette::ButtonText, text_primary);
    p.setColor(QPalette::Highlight, nana_pink);
    p.setColor(QPalette::HighlightedText, QColor(Qt::white));
    p.setColor(QPalette::ToolTipBase, bg_surface);
    p.setColor(QPalette::ToolTipText, text_primary);
    p.setColor(QPalette::PlaceholderText, text_muted);
    app.setPalette(p);
    app.setFont(uiFont(13, QFont::Medium));
    QFile qss(":/lotus_jam.qss");
    if (qss.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(qss.readAll()));
    QApplication::setWindowIcon(appIcon());
}

}  // namespace theme
