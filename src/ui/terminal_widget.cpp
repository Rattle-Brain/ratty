/*
 * TerminalWidget - OpenGL-accelerated terminal display
 */

#include "terminal_widget.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSurfaceFormat>
#include <QClipboard>
#include <QApplication>
#include <QDebug>

TerminalWidget::TerminalWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , pty_(nullptr)
    , ptyNotifier_(nullptr)
    , rows_(DEFAULT_ROWS)
    , cols_(DEFAULT_COLS)
    , currentLine_(0)
    , focusedBorder_(false)
    , cursorVisible_(true)
    , blinkTimer_(nullptr)
{
    // Request OpenGL 3.2 Core Profile
    QSurfaceFormat format;
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4);  // MSAA
    setFormat(format);

    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    // Set minimum size based on font metrics (will be updated after GL init)
    setMinimumSize(400, 300);

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

    // Create renderer (it will get OpenGL functions from the current context)
    renderer_ = std::make_unique<GLRenderer>();
    if (!renderer_->initialize()) {
        qCritical() << "TerminalWidget: Failed to initialize renderer";
        return;
    }

    // Load default font
    if (!renderer_->loadDefaultFont(14, 96)) {
        qCritical() << "TerminalWidget: Failed to load default font";
        return;
    }

    // Calculate terminal size
    calculateTerminalSize();

    // Create PTY
    createPTY();

    qDebug() << "TerminalWidget: OpenGL initialized";
}

void TerminalWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);

    // Recalculate terminal size
    calculateTerminalSize();

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

    FontMetrics metrics = renderer_->getFontMetrics();

    if (metrics.cellWidth <= 0 || metrics.cellHeight <= 0) {
        qWarning() << "TerminalWidget: Invalid font metrics";
        return;
    }

    // Begin frame
    renderer_->beginFrame(width(), height());

    // Clear background
    renderer_->clear(QColor(30, 30, 30));

    // Draw focus border if focused
    if (focusedBorder_) {
        renderer_->drawRectOutline(0, 0, width(), height(), QColor(100, 149, 237), 2.0f);
    }

    // Draw raw buffer content (line-wrapped)
    // This is a simplified display until terminal emulation is implemented
    if (!rawBuffer_.isEmpty()) {
        QColor textColor(220, 220, 220);
        float y = metrics.ascender + 5;  // Add padding from top
        int lineCount = 0;

        // Split into lines for display
        QStringList displayLines;
        QString currentLine;

        for (QChar ch : rawBuffer_) {
            if (ch == '\n') {
                displayLines.append(currentLine);
                currentLine.clear();
            } else if (ch == '\r') {
                // Ignore carriage return for now
                continue;
            } else if (ch.isPrint() || ch == '\t') {
                currentLine.append(ch);

                // Wrap if too long
                if (currentLine.length() >= cols_) {
                    displayLines.append(currentLine);
                    currentLine.clear();
                }
            }
        }

        if (!currentLine.isEmpty()) {
            displayLines.append(currentLine);
        }

        // Display only the last N lines that fit on screen
        int startLine = qMax(0, displayLines.size() - rows_);
        for (int i = startLine; i < displayLines.size() && lineCount < rows_; ++i, ++lineCount) {
            renderer_->drawText(displayLines[i], 5, y, textColor);  // Add padding from left
            y += metrics.cellHeight;
        }

        // Draw cursor at the end
        if (cursorVisible_) {
            int cursorLine = lineCount;
            int cursorCol = currentLine.length();

            float cursorX = 5 + cursorCol * metrics.cellWidth;
            float cursorY = metrics.ascender + 5 + cursorLine * metrics.cellHeight - metrics.ascender;

            renderer_->drawRect(cursorX, cursorY, metrics.cellWidth, metrics.cellHeight,
                              QColor(220, 220, 220, 128));
        }
    } else {
        // Display welcome message
        QColor textColor(100, 149, 237);
        float y = height() / 2.0f;

        // Draw background test rect to verify rendering works
        renderer_->drawRect(10, y - 30, 300, 40, QColor(50, 50, 50));
        renderer_->drawText("Ratty Terminal - Ready", 15, y, textColor);

        qDebug() << "TerminalWidget: Drawing welcome message at" << y << "size:" << width() << "x" << height();
    }

    // End frame
    renderer_->endFrame();
}

void TerminalWidget::setFocusedBorder(bool focused) {
    focusedBorder_ = focused;
    update();
}

void TerminalWidget::onPTYDataReady() {
    if (!pty_ || !pty_->isValid()) return;

    char buffer[4096];
    ssize_t n = pty_->read(buffer, sizeof(buffer));

    if (n > 0) {
        QString text = QString::fromUtf8(buffer, n);
        rawBuffer_.append(text);

        // Limit buffer size to prevent memory issues
        if (rawBuffer_.length() > 100000) {
            rawBuffer_ = rawBuffer_.right(50000);
        }

        update();
    } else if (n < 0) {
        qWarning() << "TerminalWidget: PTY read error";
        ptyNotifier_->setEnabled(false);
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
    // TODO: Implement text selection and copy when terminal emulation is added
    // For now, copy the last line of raw buffer as a placeholder
    if (!rawBuffer_.isEmpty()) {
        QStringList lines = rawBuffer_.split('\n');
        if (!lines.isEmpty()) {
            QString lastLine = lines.last();
            QClipboard* clipboard = QApplication::clipboard();
            clipboard->setText(lastLine);
            qDebug() << "TerminalWidget: Copied to clipboard:" << lastLine.left(50);
        }
    }
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
