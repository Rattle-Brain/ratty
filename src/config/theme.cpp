/*
 * Themes - implementation
 */

#include "theme.h"
#include <QDir>
#include <QFileInfo>

bool PaletteOverrides::isEmpty() const {
    if (background || foreground || cursor || selectionBackground) return false;
    for (const auto& entry : ansi) {
        if (entry) return false;
    }
    return true;
}

void PaletteOverrides::mergeInto(Palette& palette) const {
    if (background) palette.setDefaultBackground(*background);
    if (foreground) {
        palette.setDefaultForeground(*foreground);
        /*
         * The cursor tracks the foreground unless stated. Without this a theme
         * that inverts the terminal would leave the cursor in the previous
         * theme's colour, which on a light scheme is close to invisible.
         */
        palette.setCursorColor(*foreground);
    }
    if (cursor) palette.setCursorColor(*cursor);
    if (selectionBackground) palette.setSelectionBackground(*selectionBackground);

    for (int i = 0; i < AnsiCount; ++i) {
        if (ansi[static_cast<size_t>(i)]) {
            palette.setEntry(i, *ansi[static_cast<size_t>(i)]);
        }
    }
}

void PaletteOverrides::absorb(const PaletteOverrides& later) {
    if (later.background) background = later.background;
    if (later.foreground) foreground = later.foreground;
    if (later.cursor) cursor = later.cursor;
    if (later.selectionBackground) selectionBackground = later.selectionBackground;

    for (size_t i = 0; i < ansi.size(); ++i) {
        if (later.ansi[i]) ansi[i] = later.ansi[i];
    }
}

namespace themes {

QStringList available() {
    /*
     * Enumerated from the resource system rather than a second hard-coded list,
     * so adding a theme is adding a file and a line to themes.qrc.
     */
    QStringList ids;
    const QDir directory(QStringLiteral(":/themes"));
    for (const QString& file : directory.entryList({QStringLiteral("*.yaml")}, QDir::Files)) {
        ids << QFileInfo(file).completeBaseName();
    }
    ids.sort();
    return ids;
}

bool exists(const QString& id) {
    return !id.isEmpty() && QFileInfo::exists(resourcePath(id));
}

QString resourcePath(const QString& id) {
    return QStringLiteral(":/themes/%1.yaml").arg(id);
}

} // namespace themes
