/*
 * TerminalWidget - OpenGL-accelerated terminal display
 */

#include "terminal_widget.h"
#include "../config/config.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSurfaceFormat>
#include <QClipboard>
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QDebug>

TerminalWidget::TerminalWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , pty_(nullptr)
    , ptyNotifier_(nullptr)
    , rows_(DEFAULT_ROWS)
    , cols_(DEFAULT_COLS)
    , focusedBorder_(false)
    , cursorVisible_(true)
    , blinkTimer_(nullptr)
{
    // Request OpenGL 3.3 Core Profile (minimum required for texture swizzling)
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4);  // MSAA
    setFormat(format);

    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    // Set minimum size based on font metrics (will be updated after GL init)
    setMinimumSize(400, 150);

    // Cursor blink timer
    blinkTimer_ = new QTimer(this);
    connect(blinkTimer_, &QTimer::timeout, this, &TerminalWidget::onBlinkTimer);
    blinkTimer_->start(CURSOR_BLINK_MS);

    qDebug() << "TerminalWidget: Created";
}

TerminalWidget::~TerminalWidget() {
}

void TerminalWidget::initializeGL() {
    initializeOpenGLFunctions();

    // Validate OpenGL version
    QOpenGLContext* ctx = context();
    if (!ctx) {
        qCritical() << "TerminalWidget: No OpenGL context available";
        return;
    }

    QSurfaceFormat fmt = ctx->format();
    int major = fmt.majorVersion();
    int minor = fmt.minorVersion();
    qDebug() << "TerminalWidget: OpenGL version" << major << "." << minor;
    qDebug() << "TerminalWidget: Profile:" << (fmt.profile() == QSurfaceFormat::CoreProfile ? "Core" : "Compatibility");

    if (major < 3 || (major == 3 && minor < 3)) {
        qCritical() << "TerminalWidget: OpenGL 3.3 or higher required, got" << major << "." << minor;
        qCritical() << "TerminalWidget: Your system may not support the required OpenGL version";
        return;
    }

    // Create renderer (it will get OpenGL functions from the current context)
    renderer_ = std::make_unique<GLRenderer>();
    if (!renderer_->initialize()) {
        qCritical() << "TerminalWidget: Failed to initialize renderer";
        return;
    }

    // Load default font with config and screen DPI
    Config& config = Config::instance();
    int fontSize = config.fontSize();

    // Get actual screen DPI for proper font rendering
    // Qt's logicalDotsPerInch accounts for system DPI scaling
    QScreen* screen = QGuiApplication::primaryScreen();
    int dpi = screen ? static_cast<int>(screen->logicalDotsPerInch()) : 96;

    qDebug() << "TerminalWidget: Loading font size" << fontSize << "at" << dpi << "DPI";

    if (!renderer_->loadDefaultFont(fontSize, dpi)) {
        qCritical() << "TerminalWidget: Failed to load default font";
        return;
    }

    // Calculate terminal size
    calculateTerminalSize();

    // Create terminal emulator
    emulator_ = std::make_unique<TerminalEmulator>(rows_, cols_);
    qDebug() << "TerminalWidget: Created emulator with" << rows_ << "x" << cols_;

    // Create PTY
    createPTY();

    qDebug() << "TerminalWidget: OpenGL initialized";
}

void TerminalWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);

    // Recalculate terminal size
    calculateTerminalSize();

    // Resize emulator
    if (emulator_) {
        emulator_->resize(rows_, cols_);
        qDebug() << "TerminalWidget: Resized emulator to" << rows_ << "x" << cols_;
    }

    // Resize PTY
    if (pty_ && pty_->isValid()) {
        pty_->resize(rows_, cols_);
        qDebug() << "TerminalWidget: Resized PTY to" << rows_ << "x" << cols_;
    }
}

void TerminalWidget::paintGL() {
    if (!renderer_ || !renderer_->isInitialized()) {
        // Clear to a visible color to show rendering is being called
        glClearColor(0.5f, 0.0f, 0.5f, 1.0f);  // Purple to show paintGL is being called
        glClear(GL_COLOR_BUFFER_BIT);
        qWarning() << "TerminalWidget::paintGL called but renderer not initialized";
        return;
    }

    renderContent();
}

void TerminalWidget::createPTY() {
    // Create PTY
    pty_ = std::make_unique<PTY>(rows_, cols_);

    if (!pty_->isValid()) {
        qCritical() << "TerminalWidget: Failed to create PTY";
        return;
    }

    // Set up notifier for PTY data
    ptyNotifier_ = new QSocketNotifier(pty_->masterFd(), QSocketNotifier::Read, this);
    connect(ptyNotifier_, &QSocketNotifier::activated, this, &TerminalWidget::onPTYDataReady);

    qDebug() << "TerminalWidget: PTY created with shell:" << QString::fromStdString(PTY::getUserShell());
}

void TerminalWidget::calculateTerminalSize() {
    if (!renderer_ || !renderer_->isInitialized()) return;

    FontMetrics metrics = renderer_->getFontMetrics();

    if (metrics.cellWidth > 0 && metrics.cellHeight > 0) {
        rows_ = height() / metrics.cellHeight;
        cols_ = width() / metrics.cellWidth;

        // Minimum dimensions
        if (rows_ < 1) rows_ = 1;
        if (cols_ < 1) cols_ = 1;

        qDebug() << "TerminalWidget: Calculated size:" << rows_ << "x" << cols_;
    }
}

void TerminalWidget::renderContent() {
    if (!renderer_ || !renderer_->isInitialized()) {
        qWarning() << "TerminalWidget: Renderer not initialized, clearing to red for debugging";
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);  // Red to show problem
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    if (!emulator_) {
        qWarning() << "TerminalWidget: Emulator not initialized";
        return;
    }

    FontMetrics metrics = renderer_->getFontMetrics();

    if (metrics.cellWidth <= 0 || metrics.cellHeight <= 0) {
        qWarning() << "TerminalWidget: Invalid font metrics";
        return;
    }

    // Get colors from config
    Config& config = Config::instance();
    QColor backgroundColor = config.backgroundColor();
    QColor cursorColor = config.cursorColor();

    // Begin frame
    renderer_->beginFrame(width(), height());

    // Clear background using config color
    renderer_->clear(backgroundColor);

    // Note: Focus border removed - only needed when multiple splits exist
    // TODO: Re-enable subtle border only when there are multiple terminal panes

    // Render terminal grid
    for (int row = 0; row < emulator_->rows(); ++row) {
        for (int col = 0; col < emulator_->cols(); ++col) {
            const Cell& cell = emulator_->cellAt(row, col);

            float x = col * metrics.cellWidth;
            float y = row * metrics.cellHeight;

            // Get colors (apply inverse if needed)
            QColor fgColor = cell.attrs.foreground;
            QColor bgColor = cell.attrs.background;

            if (cell.attrs.inverse) {
                std::swap(fgColor, bgColor);
            }

            // Draw background if not default
            if (bgColor != backgroundColor) {
                renderer_->drawRect(x, y, metrics.cellWidth, metrics.cellHeight, bgColor);
            }

            // Draw character if not space
            if (cell.ch != ' ') {
                // Make bold text brighter
                if (cell.attrs.bold) {
                    fgColor = fgColor.lighter(130);
                }

                float textY = y + metrics.ascender;
                renderer_->drawText(QString(cell.ch), x, textY, fgColor);

                // Draw underline if needed
                if (cell.attrs.underline) {
                    float underlineY = y + metrics.cellHeight - 2;
                    renderer_->drawRect(x, underlineY, metrics.cellWidth, 1, fgColor);
                }
            }
        }
    }

    // Draw cursor using config color with transparency
    if (cursorVisible_) {
        int cursorRow = emulator_->cursorRow();
        int cursorCol = emulator_->cursorCol();

        if (cursorRow >= 0 && cursorRow < emulator_->rows() &&
            cursorCol >= 0 && cursorCol < emulator_->cols()) {

            float cursorX = cursorCol * metrics.cellWidth;
            float cursorY = cursorRow * metrics.cellHeight;

            // Draw cursor as semi-transparent block using config color
            QColor cursorDrawColor = cursorColor;
            cursorDrawColor.setAlpha(128);  // Semi-transparent
            renderer_->drawRect(cursorX, cursorY, metrics.cellWidth, metrics.cellHeight, cursorDrawColor);
        }
    }

    // End frame
    renderer_->endFrame();
}

void TerminalWidget::setFocusedBorder(bool focused) {
    focusedBorder_ = focused;
    update();
}

void TerminalWidget::onPTYDataReady() {
    if (!pty_ || !pty_->isValid() || !emulator_) return;

    char buffer[4096];
    ssize_t n = pty_->read(buffer, sizeof(buffer));

    if (n > 0) {
        QString text = QString::fromUtf8(buffer, n);

        // Process data through terminal emulator
        emulator_->processData(text);

        update();
    } else if (n < 0) {
        qWarning() << "TerminalWidget: PTY read error";
        ptyNotifier_->setEnabled(false);
    } else if (n == 0) {
        // Check if the child process has exited
        if (pty_->hasChildExited()) {
            qDebug() << "TerminalWidget: PTY session ended (child exited)";
            ptyNotifier_->setEnabled(false);
            emit sessionEnded();
        }
    }
}

void TerminalWidget::onBlinkTimer() {
    cursorVisible_ = !cursorVisible_;
    update();
}

void TerminalWidget::keyPressEvent(QKeyEvent* event) {
    if (!pty_ || !pty_->isValid()) {
        QOpenGLWidget::keyPressEvent(event);
        return;
    }

    // Convert key event to VT bytes
    QByteArray data = inputHandler_.keyEventToBytes(event);

    // Send to PTY
    if (!data.isEmpty()) {
        pty_->write(data.constData(), data.size());
    }

    event->accept();
}

void TerminalWidget::mousePressEvent(QMouseEvent* event) {
    // Set focus when clicked
    setFocus();
    event->accept();
}

void TerminalWidget::wheelEvent(QWheelEvent* event) {
    // Scrollback will be implemented with terminal emulation
    event->accept();
}

void TerminalWidget::focusInEvent(QFocusEvent* event) {
    QOpenGLWidget::focusInEvent(event);
    setFocusedBorder(true);
}

void TerminalWidget::focusOutEvent(QFocusEvent* event) {
    QOpenGLWidget::focusOutEvent(event);
    setFocusedBorder(false);
}

void TerminalWidget::copySelection() {
    // TODO: Implement text selection
    // For now, just log that copy was requested
    qDebug() << "TerminalWidget: Copy requested (selection not yet implemented)";
}

void TerminalWidget::paste() {
    if (!pty_ || !pty_->isValid()) return;

    QClipboard* clipboard = QApplication::clipboard();
    QString text = clipboard->text();

    if (!text.isEmpty()) {
        // Convert to UTF-8 and send to PTY
        QByteArray data = text.toUtf8();
        pty_->write(data.constData(), data.size());
        qDebug() << "TerminalWidget: Pasted" << data.size() << "bytes";
    }
}
