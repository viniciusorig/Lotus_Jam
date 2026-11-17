#pragma once
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>

class QApplication;

// Lotus Jam design tokens (front-end briefing §2). Colours are QColor constants; hex() gives
// the "#RRGGBB" string for QSS snippets. Fonts fall back cleanly when Inter / JetBrains Mono
// are not installed (Noto Sans / Fira Code / Noto Sans Mono / any fixed-pitch font).
namespace theme {

inline const QColor bg_canvas{0x09, 0x09, 0x0B};      // window, timeline, dial
inline const QColor bg_surface{0x11, 0x11, 0x15};     // cards, header
inline const QColor bg_elevated{0x19, 0x19, 0x20};    // inputs, secondary buttons
inline const QColor border_subtle{0x26, 0x26, 0x30};  // thin dividers, grids
inline const QColor border_punk{0x3F, 0x1D, 0x32};    // magenta-tinted highlight borders
inline const QColor text_primary{0xF4, 0xF4, 0xF6};
inline const QColor text_secondary{0x9E, 0x9E, 0xA9};
inline const QColor text_muted{0x5C, 0x5C, 0x68};
inline const QColor nana_pink{0xFF, 0x2A, 0x85};      // signature colour
inline const QColor nana_glow{0xFF, 0x6E, 0xB0};      // halo
inline const QColor magenta_deep{0xB8, 0x0D, 0x57};
inline const QColor magenta_neon{0xE1, 0x1D, 0x74};
inline const QColor zone_exact{0x00, 0xF5, 0xA0};     // <= 15 ms
inline const QColor zone_warn{0xFF, 0xB8, 0x00};      // <= 50 ms
inline const QColor zone_miss{0xFF, 0x17, 0x44};      // > 50 ms, misses, backend failures
inline const QColor timeline_bg{0x0B, 0x0B, 0x0E};    // timeline / dial background (§5.3)

inline QString hex(const QColor& c) { return c.name(QColor::HexRgb).toUpper(); }

// Font stacks (§2.2).
QFont uiFont(int px, int weight = QFont::Medium);
QFont monoFont(int px, bool bold = false);

// Application icon (SVG from the resources plus the pre-rendered PNG sizes).
QIcon appIcon();

// Registers the embedded resources (static library). Idempotent.
void initResources();

// Global look: stylesheet (:/lotus_jam.qss), palette, default font and window icon.
void apply(QApplication& app);

}  // namespace theme
