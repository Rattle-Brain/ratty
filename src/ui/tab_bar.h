/*
 * TabBar - a thin, self-drawn tab bar
 *
 * Qt's stock tab bar is a document-style control: tall, boxy, and styled by the
 * platform rather than by the terminal's own theme. This subclass keeps
 * QTabBar's model -- page association, ordering, drag-to-reorder, keyboard
 * navigation, accessibility -- and replaces only the drawing and the metrics.
 *
 * The close affordance is drawn here rather than being a child widget, because
 * QTabBar's built-in one is a platform-styled button whose size fights a bar
 * this thin. It is shown for the hovered and current tab only; on every tab at
 * once it reads as clutter.
 *
 * Labels are drawn in the configured terminal font family, a little smaller,
 * which is most of what makes the bar look like it belongs to the terminal
 * rather than to the window manager.
 */

#ifndef UI_TAB_BAR_H
#define UI_TAB_BAR_H

#include "../config/chrome.h"
#include <QTabBar>
#include <QTabWidget>

class TabBar : public QTabBar {
    Q_OBJECT

public:
    explicit TabBar(QWidget* parent = nullptr);

    /* Re-read style, colours and metrics from the configuration. */
    void applyConfiguration();

signals:
    /* The user clicked a tab's close affordance. */
    void tabCloseClicked(int index);

protected:
    QSize tabSizeHint(int index) const override;
    QSize minimumTabSizeHint(int index) const override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;

private:
    void paintTab(QPainter& painter, int index, const ChromeColors::Resolved& colors) const;
    void paintMinimal(QPainter& painter, int index, const QRect& rect, bool current,
                      const ChromeColors::Resolved& colors) const;
    void paintUnderline(QPainter& painter, int index, const QRect& rect, bool current,
                        const ChromeColors::Resolved& colors) const;
    void paintBlocks(QPainter& painter, int index, const QRect& rect, bool current,
                     const ChromeColors::Resolved& colors) const;
    void paintPills(QPainter& painter, int index, const QRect& rect, bool current,
                    const ChromeColors::Resolved& colors) const;
    void paintPowerline(QPainter& painter, int index, const QRect& rect, bool current,
                        const ChromeColors::Resolved& colors) const;
    void paintLabel(QPainter& painter, int index, const QRect& contentRect,
                    const QColor& color) const;
    void paintCloseButton(QPainter& painter, int index, const QRect& rect,
                          const QColor& color) const;

    /* Hit area for the close affordance, or a null rect when it is not shown. */
    QRect closeRect(int index) const;
    bool showsCloseButton(int index) const;
    /* Which edge of the bar faces the terminal. */
    bool barIsAtBottom() const;

    TabBarStyle style_ = TabBarStyle::Minimal;
    int barHeight_ = 24;
    int hoveredTab_ = -1;
    int pressedCloseTab_ = -1;
    bool hoveringClose_ = false;
};

/*
 * TabWidget - a QTabWidget that hosts the custom bar.
 *
 * QTabWidget::setTabBar() is protected, so installing a replacement has to
 * happen from a subclass. This exists for that one reason and adds nothing else.
 */
class TabWidget : public QTabWidget {
    Q_OBJECT

public:
    explicit TabWidget(QWidget* parent = nullptr);

    TabBar* rattyTabBar() const { return bar_; }

private:
    TabBar* bar_ = nullptr;
};

#endif /* UI_TAB_BAR_H */
