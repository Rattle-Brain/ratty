/*
 * TerminalSession - one shell, one emulator, one I/O pump
 *
 * Everything between the pty file descriptor and the terminal grid, with no
 * rendering and no widget code. TerminalWidget used to own the pty, the socket
 * notifier, the byte decoding and the emulator directly, which made it
 * impossible to reason about (or test) terminal behaviour without an OpenGL
 * context.
 */

#ifndef CORE_TERMINAL_SESSION_H
#define CORE_TERMINAL_SESSION_H

#include "cursor.h"
#include "palette.h"
#include "pty.h"
#include "terminal_emulator.h"
#include <QByteArray>
#include <QObject>
#include <QString>
#include <memory>

class QSocketNotifier;

class TerminalSession : public QObject {
    Q_OBJECT

public:
    /* `palette` seeds this session's colours; an application may then retheme
     * them through OSC 4/10/11/12 without affecting other sessions. */
    TerminalSession(int rows, int cols, const Palette& palette, QObject* parent = nullptr);
    ~TerminalSession() override;

    bool isValid() const;

    const Screen& screen() const { return emulator_.screen(); }
    const Palette& palette() const { return emulator_.palette(); }

    /* The application's DECSCUSR request, if it made one. */
    bool hasRequestedCursorStyle() const { return emulator_.hasRequestedCursorStyle(); }
    CursorStyle requestedCursorStyle() const { return emulator_.requestedCursorStyle(); }
    bool cursorBlinkRequested() const { return emulator_.cursorBlinkRequested(); }
    int rows() const { return emulator_.rows(); }
    int cols() const { return emulator_.cols(); }

    /* Grid revision, so a view can skip repainting unchanged content. */
    uint64_t revision() const { return emulator_.screen().revision(); }

    void resize(int rows, int cols);

    /* Send key input / pasted text to the shell. */
    void sendInput(const QByteArray& bytes);
    /* Wraps `text` in bracketed-paste markers when the application asked for
     * them, so editors can tell a paste from typing. */
    void sendPaste(const QString& text);

    bool applicationCursorKeys() const { return emulator_.applicationCursorKeys(); }

signals:
    /* The grid changed and the view should repaint. */
    void screenChanged();
    /* OSC 0/2 window title. */
    void titleChanged(const QString& title);
    void bellRang();
    /* The shell exited. */
    void ended();

private slots:
    void onReadyRead();

private:
    void drainPty();
    void finish();

    std::unique_ptr<PTY> pty_;
    QSocketNotifier* notifier_ = nullptr;
    TerminalEmulator emulator_;
    bool finished_ = false;

    /* Sized to swallow a burst of shell output in one pass; the notifier fires
     * again if more is pending. */
    static constexpr int ReadChunkSize = 64 * 1024;
    static constexpr int MaxReadsPerEvent = 32;
};

#endif /* CORE_TERMINAL_SESSION_H */
