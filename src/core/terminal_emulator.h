/*
 * TerminalEmulator - ANSI/VT100 terminal emulation
 *
 * Handles:
 * - ANSI escape sequence parsing (CSI, OSC, etc.)
 * - Terminal grid with character cells and attributes
 * - Cursor positioning and scrolling
 * - SGR (color and text attributes)
 */

#ifndef CORE_TERMINAL_EMULATOR_H
#define CORE_TERMINAL_EMULATOR_H

#include <QVector>
#include <QString>
#include <QColor>
#include <cstdint>

/* Terminal cell attributes */
struct CellAttributes {
    QColor foreground;
    QColor background;
    bool bold;
    bool italic;
    bool underline;
    bool inverse;

    CellAttributes()
        : foreground(220, 220, 220)
        , background(30, 30, 30)
        , bold(false)
        , italic(false)
        , underline(false)
        , inverse(false)
    {}
};

/* Terminal cell (character + attributes) */
struct Cell {
    QChar ch;
    CellAttributes attrs;

    Cell() : ch(' ') {}
};

/* Terminal emulator state machine states */
enum ParserState {
    StateGround,        // Normal text
    StateEscape,        // After ESC
    StateCSI,           // Control Sequence Introducer (ESC [)
    StateOSC,           // Operating System Command (ESC ])
    StateOSCString      // OSC string content
};

class TerminalEmulator {
public:
    TerminalEmulator(int rows, int cols);

    // Process incoming data
    void processData(const QString& data);

    // Grid access
    const Cell& cellAt(int row, int col) const;
    int rows() const { return rows_; }
    int cols() const { return cols_; }

    // Cursor position
    int cursorRow() const { return cursorRow_; }
    int cursorCol() const { return cursorCol_; }

    // Resize terminal
    void resize(int rows, int cols);

    // Clear screen
    void clear();

private:
    // Parser methods
    void processChar(QChar ch);
    void handleEscapeSequence(QChar ch);
    void handleCSI(QChar ch);
    void executeCSI();
    void handleOSC(QChar ch);
    void handleSGR();

    // Terminal operations
    void putChar(QChar ch);
    void newLine();
    void carriageReturn();
    void moveCursor(int row, int col);
    void scrollUp(int lines = 1);
    void eraseInDisplay(int mode);
    void eraseInLine(int mode);

    // Grid
    QVector<QVector<Cell>> grid_;
    int rows_;
    int cols_;

    // Cursor
    int cursorRow_;
    int cursorCol_;

    // Current attributes
    CellAttributes currentAttrs_;

    // Parser state
    ParserState state_;
    QString csiParams_;
    QString oscString_;

    // Default colors
    static QColor defaultForeground_;
    static QColor defaultBackground_;

    // ANSI color palette
    static QVector<QColor> ansiColors_;
    static void initializeColors();
    static QColor getAnsiColor(int index);
};

#endif /* CORE_TERMINAL_EMULATOR_H */
