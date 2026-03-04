/*
 * TerminalEmulator - ANSI/VT100 terminal emulation implementation
 */

#include "terminal_emulator.h"
#include <QDebug>

// Initialize static members
QColor TerminalEmulator::defaultForeground_(220, 220, 220);
QColor TerminalEmulator::defaultBackground_(30, 30, 30);
QVector<QColor> TerminalEmulator::ansiColors_;

void TerminalEmulator::initializeColors() {
    if (!ansiColors_.isEmpty()) return;

    // Standard 16 ANSI colors (0-15)
    ansiColors_ = {
        // Normal colors (0-7)
        QColor(0, 0, 0),         // Black
        QColor(205, 49, 49),     // Red
        QColor(13, 188, 121),    // Green
        QColor(229, 229, 16),    // Yellow
        QColor(36, 114, 200),    // Blue
        QColor(188, 63, 188),    // Magenta
        QColor(17, 168, 205),    // Cyan
        QColor(229, 229, 229),   // White

        // Bright colors (8-15)
        QColor(102, 102, 102),   // Bright Black (Gray)
        QColor(241, 76, 76),     // Bright Red
        QColor(35, 209, 139),    // Bright Green
        QColor(245, 245, 67),    // Bright Yellow
        QColor(59, 142, 234),    // Bright Blue
        QColor(214, 112, 214),   // Bright Magenta
        QColor(41, 184, 219),    // Bright Cyan
        QColor(255, 255, 255)    // Bright White
    };
}

QColor TerminalEmulator::getAnsiColor(int index) {
    initializeColors();

    if (index >= 0 && index < ansiColors_.size()) {
        return ansiColors_[index];
    }

    return defaultForeground_;
}

TerminalEmulator::TerminalEmulator(int rows, int cols)
    : rows_(rows)
    , cols_(cols)
    , cursorRow_(0)
    , cursorCol_(0)
    , state_(StateGround)
{
    initializeColors();

    // Initialize grid
    grid_.resize(rows_);
    for (int i = 0; i < rows_; ++i) {
        grid_[i].resize(cols_);
    }

    currentAttrs_.foreground = defaultForeground_;
    currentAttrs_.background = defaultBackground_;
}

const Cell& TerminalEmulator::cellAt(int row, int col) const {
    static Cell emptyCell;

    if (row >= 0 && row < rows_ && col >= 0 && col < cols_) {
        return grid_[row][col];
    }

    return emptyCell;
}

void TerminalEmulator::resize(int rows, int cols) {
    QVector<QVector<Cell>> newGrid;
    newGrid.resize(rows);

    for (int i = 0; i < rows; ++i) {
        newGrid[i].resize(cols);

        // Copy old content
        if (i < grid_.size()) {
            for (int j = 0; j < cols && j < grid_[i].size(); ++j) {
                newGrid[i][j] = grid_[i][j];
            }
        }
    }

    grid_ = newGrid;
    rows_ = rows;
    cols_ = cols;

    // Clamp cursor
    if (cursorRow_ >= rows_) cursorRow_ = rows_ - 1;
    if (cursorCol_ >= cols_) cursorCol_ = cols_ - 1;
    if (cursorRow_ < 0) cursorRow_ = 0;
    if (cursorCol_ < 0) cursorCol_ = 0;
}

void TerminalEmulator::clear() {
    for (int i = 0; i < rows_; ++i) {
        for (int j = 0; j < cols_; ++j) {
            grid_[i][j].ch = ' ';
            grid_[i][j].attrs.foreground = defaultForeground_;
            grid_[i][j].attrs.background = defaultBackground_;
            grid_[i][j].attrs.bold = false;
            grid_[i][j].attrs.italic = false;
            grid_[i][j].attrs.underline = false;
            grid_[i][j].attrs.inverse = false;
        }
    }
    cursorRow_ = 0;
    cursorCol_ = 0;
}

void TerminalEmulator::processData(const QString& data) {
    for (QChar ch : data) {
        processChar(ch);
    }
}

void TerminalEmulator::processChar(QChar ch) {
    switch (state_) {
    case StateGround:
        if (ch == '\x1b') {  // ESC
            state_ = StateEscape;
        } else if (ch == '\n') {
            newLine();
        } else if (ch == '\r') {
            carriageReturn();
        } else if (ch == '\t') {
            // Tab: move to next tab stop (every 8 columns)
            cursorCol_ = ((cursorCol_ / 8) + 1) * 8;
            if (cursorCol_ >= cols_) {
                cursorCol_ = cols_ - 1;
            }
        } else if (ch == '\b') {
            // Backspace
            if (cursorCol_ > 0) {
                cursorCol_--;
            }
        } else if (ch.isPrint()) {
            putChar(ch);
        }
        // Ignore other control characters
        break;

    case StateEscape:
        handleEscapeSequence(ch);
        break;

    case StateCSI:
        handleCSI(ch);
        break;

    case StateOSC:
    case StateOSCString:
        handleOSC(ch);
        break;
    }
}

void TerminalEmulator::handleEscapeSequence(QChar ch) {
    if (ch == '[') {
        state_ = StateCSI;
        csiParams_.clear();
    } else if (ch == ']') {
        state_ = StateOSC;
        oscString_.clear();
    } else if (ch == 'c') {
        // Reset terminal
        clear();
        currentAttrs_ = CellAttributes();
        state_ = StateGround;
    } else {
        // Unknown escape sequence, return to ground
        state_ = StateGround;
    }
}

void TerminalEmulator::handleCSI(QChar ch) {
    if (ch.isDigit() || ch == ';' || ch == '?') {
        csiParams_.append(ch);
    } else {
        // Command character - append it and execute
        csiParams_.append(ch);
        executeCSI();
        state_ = StateGround;
    }
}

void TerminalEmulator::executeCSI() {
    if (csiParams_.isEmpty()) {
        return;
    }

    // Get the command character (last char in the sequence)
    QChar cmd = csiParams_.back();
    QString params = csiParams_.left(csiParams_.length() - 1);

    // Parse parameters
    QStringList paramList = params.split(';');
    QVector<int> paramInts;
    for (const QString& p : paramList) {
        paramInts.append(p.isEmpty() ? 0 : p.toInt());
    }

    // Execute command
    switch (cmd.toLatin1()) {
    case 'm':  // SGR - Select Graphic Rendition
        handleSGR();
        break;

    case 'H':  // CUP - Cursor Position
    case 'f':  // HVP - Horizontal and Vertical Position
    {
        int row = (paramInts.size() > 0 && paramInts[0] > 0) ? paramInts[0] - 1 : 0;
        int col = (paramInts.size() > 1 && paramInts[1] > 0) ? paramInts[1] - 1 : 0;
        moveCursor(row, col);
        break;
    }

    case 'A':  // CUU - Cursor Up
    {
        int n = (paramInts.size() > 0 && paramInts[0] > 0) ? paramInts[0] : 1;
        cursorRow_ = qMax(0, cursorRow_ - n);
        break;
    }

    case 'B':  // CUD - Cursor Down
    {
        int n = (paramInts.size() > 0 && paramInts[0] > 0) ? paramInts[0] : 1;
        cursorRow_ = qMin(rows_ - 1, cursorRow_ + n);
        break;
    }

    case 'C':  // CUF - Cursor Forward
    {
        int n = (paramInts.size() > 0 && paramInts[0] > 0) ? paramInts[0] : 1;
        cursorCol_ = qMin(cols_ - 1, cursorCol_ + n);
        break;
    }

    case 'D':  // CUB - Cursor Back
    {
        int n = (paramInts.size() > 0 && paramInts[0] > 0) ? paramInts[0] : 1;
        cursorCol_ = qMax(0, cursorCol_ - n);
        break;
    }

    case 'J':  // ED - Erase in Display
    {
        int mode = (paramInts.size() > 0) ? paramInts[0] : 0;
        eraseInDisplay(mode);
        break;
    }

    case 'K':  // EL - Erase in Line
    {
        int mode = (paramInts.size() > 0) ? paramInts[0] : 0;
        eraseInLine(mode);
        break;
    }

    default:
        // Unknown CSI command
        break;
    }
}

void TerminalEmulator::handleSGR() {
    QString params = csiParams_.left(csiParams_.length() - 1);

    if (params.isEmpty() || params == "0") {
        // Reset to default
        currentAttrs_ = CellAttributes();
        currentAttrs_.foreground = defaultForeground_;
        currentAttrs_.background = defaultBackground_;
        return;
    }

    QStringList paramList = params.split(';');

    for (int i = 0; i < paramList.size(); ++i) {
        int code = paramList[i].toInt();

        switch (code) {
        case 0:  // Reset
            currentAttrs_ = CellAttributes();
            currentAttrs_.foreground = defaultForeground_;
            currentAttrs_.background = defaultBackground_;
            break;

        case 1:  // Bold
            currentAttrs_.bold = true;
            break;

        case 3:  // Italic
            currentAttrs_.italic = true;
            break;

        case 4:  // Underline
            currentAttrs_.underline = true;
            break;

        case 7:  // Inverse
            currentAttrs_.inverse = true;
            break;

        case 22: // Normal intensity (not bold)
            currentAttrs_.bold = false;
            break;

        case 23: // Not italic
            currentAttrs_.italic = false;
            break;

        case 24: // Not underlined
            currentAttrs_.underline = false;
            break;

        case 27: // Not inverse
            currentAttrs_.inverse = false;
            break;

        // Foreground colors (30-37)
        case 30: currentAttrs_.foreground = getAnsiColor(0); break;
        case 31: currentAttrs_.foreground = getAnsiColor(1); break;
        case 32: currentAttrs_.foreground = getAnsiColor(2); break;
        case 33: currentAttrs_.foreground = getAnsiColor(3); break;
        case 34: currentAttrs_.foreground = getAnsiColor(4); break;
        case 35: currentAttrs_.foreground = getAnsiColor(5); break;
        case 36: currentAttrs_.foreground = getAnsiColor(6); break;
        case 37: currentAttrs_.foreground = getAnsiColor(7); break;

        // Bright foreground colors (90-97)
        case 90: currentAttrs_.foreground = getAnsiColor(8); break;
        case 91: currentAttrs_.foreground = getAnsiColor(9); break;
        case 92: currentAttrs_.foreground = getAnsiColor(10); break;
        case 93: currentAttrs_.foreground = getAnsiColor(11); break;
        case 94: currentAttrs_.foreground = getAnsiColor(12); break;
        case 95: currentAttrs_.foreground = getAnsiColor(13); break;
        case 96: currentAttrs_.foreground = getAnsiColor(14); break;
        case 97: currentAttrs_.foreground = getAnsiColor(15); break;

        // Background colors (40-47)
        case 40: currentAttrs_.background = getAnsiColor(0); break;
        case 41: currentAttrs_.background = getAnsiColor(1); break;
        case 42: currentAttrs_.background = getAnsiColor(2); break;
        case 43: currentAttrs_.background = getAnsiColor(3); break;
        case 44: currentAttrs_.background = getAnsiColor(4); break;
        case 45: currentAttrs_.background = getAnsiColor(5); break;
        case 46: currentAttrs_.background = getAnsiColor(6); break;
        case 47: currentAttrs_.background = getAnsiColor(7); break;

        // Bright background colors (100-107)
        case 100: currentAttrs_.background = getAnsiColor(8); break;
        case 101: currentAttrs_.background = getAnsiColor(9); break;
        case 102: currentAttrs_.background = getAnsiColor(10); break;
        case 103: currentAttrs_.background = getAnsiColor(11); break;
        case 104: currentAttrs_.background = getAnsiColor(12); break;
        case 105: currentAttrs_.background = getAnsiColor(13); break;
        case 106: currentAttrs_.background = getAnsiColor(14); break;
        case 107: currentAttrs_.background = getAnsiColor(15); break;

        case 39: // Default foreground
            currentAttrs_.foreground = defaultForeground_;
            break;

        case 49: // Default background
            currentAttrs_.background = defaultBackground_;
            break;

        default:
            // Unsupported SGR code
            break;
        }
    }
}

void TerminalEmulator::handleOSC(QChar ch) {
    if (state_ == StateOSC) {
        // First character after ESC ]
        state_ = StateOSCString;
        oscString_.append(ch);
    } else {
        // In OSC string
        if (ch == '\x07' || ch == '\x1b') {  // BEL or ESC terminates OSC
            // Process OSC command (mostly used for window title, etc.)
            // We can ignore these for now
            state_ = StateGround;
            oscString_.clear();
        } else {
            oscString_.append(ch);
        }
    }
}

void TerminalEmulator::putChar(QChar ch) {
    if (cursorRow_ < 0 || cursorRow_ >= rows_ || cursorCol_ < 0 || cursorCol_ >= cols_) {
        return;
    }

    grid_[cursorRow_][cursorCol_].ch = ch;
    grid_[cursorRow_][cursorCol_].attrs = currentAttrs_;

    cursorCol_++;

    // Wrap to next line
    if (cursorCol_ >= cols_) {
        cursorCol_ = 0;
        cursorRow_++;

        // Scroll if at bottom
        if (cursorRow_ >= rows_) {
            scrollUp(1);
            cursorRow_ = rows_ - 1;
        }
    }
}

void TerminalEmulator::newLine() {
    // Modern terminal behavior: LF acts like CR+LF (newline)
    // Move to next line AND reset column to 0
    cursorCol_ = 0;
    cursorRow_++;

    // Scroll if at bottom
    if (cursorRow_ >= rows_) {
        scrollUp(1);
        cursorRow_ = rows_ - 1;
    }
}

void TerminalEmulator::carriageReturn() {
    cursorCol_ = 0;
}

void TerminalEmulator::moveCursor(int row, int col) {
    cursorRow_ = qBound(0, row, rows_ - 1);
    cursorCol_ = qBound(0, col, cols_ - 1);
}

void TerminalEmulator::scrollUp(int lines) {
    if (lines <= 0 || lines >= rows_) {
        return;
    }

    // Move lines up
    for (int i = 0; i < rows_ - lines; ++i) {
        grid_[i] = grid_[i + lines];
    }

    // Clear bottom lines
    for (int i = rows_ - lines; i < rows_; ++i) {
        for (int j = 0; j < cols_; ++j) {
            grid_[i][j].ch = ' ';
            grid_[i][j].attrs.foreground = defaultForeground_;
            grid_[i][j].attrs.background = defaultBackground_;
            grid_[i][j].attrs.bold = false;
            grid_[i][j].attrs.italic = false;
            grid_[i][j].attrs.underline = false;
            grid_[i][j].attrs.inverse = false;
        }
    }
}

void TerminalEmulator::eraseInDisplay(int mode) {
    switch (mode) {
    case 0:  // Clear from cursor to end of screen
        eraseInLine(0);
        for (int i = cursorRow_ + 1; i < rows_; ++i) {
            for (int j = 0; j < cols_; ++j) {
                grid_[i][j].ch = ' ';
                grid_[i][j].attrs = currentAttrs_;
            }
        }
        break;

    case 1:  // Clear from cursor to beginning of screen
        eraseInLine(1);
        for (int i = 0; i < cursorRow_; ++i) {
            for (int j = 0; j < cols_; ++j) {
                grid_[i][j].ch = ' ';
                grid_[i][j].attrs = currentAttrs_;
            }
        }
        break;

    case 2:  // Clear entire screen
    case 3:  // Clear entire screen + scrollback (we don't have scrollback yet)
        clear();
        break;
    }
}

void TerminalEmulator::eraseInLine(int mode) {
    if (cursorRow_ < 0 || cursorRow_ >= rows_) {
        return;
    }

    switch (mode) {
    case 0:  // Clear from cursor to end of line
        for (int j = cursorCol_; j < cols_; ++j) {
            grid_[cursorRow_][j].ch = ' ';
            grid_[cursorRow_][j].attrs = currentAttrs_;
        }
        break;

    case 1:  // Clear from cursor to beginning of line
        for (int j = 0; j <= cursorCol_ && j < cols_; ++j) {
            grid_[cursorRow_][j].ch = ' ';
            grid_[cursorRow_][j].attrs = currentAttrs_;
        }
        break;

    case 2:  // Clear entire line
        for (int j = 0; j < cols_; ++j) {
            grid_[cursorRow_][j].ch = ' ';
            grid_[cursorRow_][j].attrs = currentAttrs_;
        }
        break;
    }
}
