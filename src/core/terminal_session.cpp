/*
 * TerminalSession - shell I/O pump implementation
 */

#include "terminal_session.h"
#include <QSocketNotifier>
#include <QDebug>
#include <vector>

TerminalSession::TerminalSession(int rows, int cols, const Palette& palette,
                                 const QString& workingDirectory, QObject* parent)
    : QObject(parent)
    , emulator_(rows, cols)
{
    emulator_.setBasePalette(palette);

    pty_ = std::make_unique<PTY>(rows, cols, workingDirectory.toStdString());

    if (!pty_->isValid()) {
        qCritical() << "TerminalSession: failed to create PTY";
        return;
    }

    /* Replies (cursor position reports, device attributes) go straight back to
     * the shell. */
    emulator_.setReplySink([this](const std::string& utf8) {
        if (pty_ && pty_->isValid()) {
            pty_->write(utf8.data(), utf8.size());
        }
    });

    emulator_.setTitleSink([this](const std::string& utf8) {
        emit titleChanged(QString::fromUtf8(utf8.data(), static_cast<int>(utf8.size())));
    });

    emulator_.setBellSink([this]() {
        emit bellRang();
    });

    notifier_ = new QSocketNotifier(pty_->masterFd(), QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &TerminalSession::onReadyRead);
}

TerminalSession::~TerminalSession() = default;

QString TerminalSession::workingDirectory() const {
    if (!pty_) return QString();
    return QString::fromStdString(pty_->workingDirectory());
}

bool TerminalSession::isValid() const {
    return pty_ && pty_->isValid();
}

void TerminalSession::resize(int rows, int cols) {
    emulator_.resize(rows, cols);
    if (pty_ && pty_->isValid()) {
        pty_->resize(emulator_.rows(), emulator_.cols());
    }
}

void TerminalSession::setScrollbackLines(int lines) {
    emulator_.setScrollbackLines(lines);
}

bool TerminalSession::scrollViewBy(int lines) {
    return emulator_.scrollViewBy(lines);
}

bool TerminalSession::scrollViewToBottom() {
    return emulator_.scrollViewToBottom();
}

bool TerminalSession::scrollViewToTop() {
    return emulator_.scrollViewToTop();
}

bool TerminalSession::scrollViewToLine(int64_t line, int preferredRow) {
    return emulator_.scrollViewToLine(line, preferredRow);
}

void TerminalSession::clearScrollback() {
    emulator_.clearScrollback();
}

void TerminalSession::sendMouseReport(const MouseReport& report) {
    const std::string encoded = encodeMouseReport(report, emulator_.mouseTracking(),
                                                  emulator_.mouseEncoding());
    if (encoded.empty()) return;
    sendInput(QByteArray(encoded.data(), static_cast<qsizetype>(encoded.size())));
}

void TerminalSession::sendFocusEvent(bool focused) {
    if (!emulator_.focusEvents()) return;
    sendInput(focused ? QByteArray("\x1b[I") : QByteArray("\x1b[O"));
}

void TerminalSession::setClipboardHandlers(ClipboardSetter setter, ClipboardGetter getter) {
    /*
     * The emulator names the selection the way OSC 52 spells it -- 'c' for the
     * clipboard, 'p' or 's' for the primary selection -- and that is the only
     * part of the protocol worth passing on; everything else about the request
     * has already been decoded by the time it gets here.
     */
    if (setter) {
        emulator_.setClipboardWriter(
            [setter = std::move(setter)](char which, const std::string& utf8) {
                setter(QString::fromUtf8(utf8.data(), static_cast<qsizetype>(utf8.size())),
                       which == 'p' || which == 's');
            });
    } else {
        emulator_.setClipboardWriter(nullptr);
    }

    if (getter) {
        emulator_.setClipboardReader(
            [getter = std::move(getter)](char which, std::string& utf8Out) {
                const QString text = getter(which == 'p' || which == 's');
                if (text.isNull()) return false;
                const QByteArray encoded = text.toUtf8();
                utf8Out.assign(encoded.constData(), static_cast<size_t>(encoded.size()));
                return true;
            });
    } else {
        emulator_.setClipboardReader(nullptr);
    }
}

void TerminalSession::sendInput(const QByteArray& bytes) {
    if (bytes.isEmpty() || !pty_ || !pty_->isValid()) return;
    pty_->write(bytes.constData(), static_cast<size_t>(bytes.size()));
}

void TerminalSession::sendPaste(const QString& text) {
    if (text.isEmpty()) return;

    QByteArray payload = text.toUtf8();
    /* CR is what a terminal delivers for Enter; a pasted LF must be translated
     * or the shell sees a literal newline mid-line. */
    payload.replace('\n', '\r');

    if (emulator_.bracketedPaste()) {
        QByteArray wrapped = "\x1b[200~";
        wrapped += payload;
        wrapped += "\x1b[201~";
        sendInput(wrapped);
    } else {
        sendInput(payload);
    }
}

void TerminalSession::onReadyRead() {
    drainPty();
}

void TerminalSession::drainPty() {
    if (!pty_ || finished_) return;

    /*
     * The buffer is a member, not a local. At 64 KiB, allocating and
     * zero-filling it per notifier activation was the single largest cost of
     * receiving output -- and a command producing megabytes of it activates the
     * notifier constantly. Allocated on first use so an idle session does not
     * hold it.
     */
    if (readBuffer_.size() != static_cast<size_t>(ReadChunkSize)) {
        readBuffer_.resize(static_cast<size_t>(ReadChunkSize));
    }
    bool changed = false;

    /*
     * Drain in a bounded loop rather than one read per notifier activation: a
     * command producing megabytes of output otherwise costs one event-loop trip
     * (and one repaint) per 4 KiB. The bound keeps a runaway producer from
     * starving the UI.
     */
    for (int i = 0; i < MaxReadsPerEvent; ++i) {
        const PTY::ReadResult result = pty_->read(readBuffer_.data(), readBuffer_.size());

        if (result.bytes > 0) {
            emulator_.write(readBuffer_.data(), static_cast<size_t>(result.bytes));
            changed = true;
            continue;
        }

        if (result.wouldBlock) break;

        if (result.eof || result.error) {
            if (changed) emit screenChanged();
            finish();
            return;
        }
        break;
    }

    if (changed) emit screenChanged();
}

void TerminalSession::finish() {
    if (finished_) return;
    finished_ = true;

    if (notifier_) {
        notifier_->setEnabled(false);
    }

    emit ended();
}
