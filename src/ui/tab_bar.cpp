/*
 * TabBar - a thin, self-drawn tab bar
 */

#include "tab_bar.h"
#include "../config/config.h"
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QHelpEvent>
#include <QToolTip>
#include <algorithm>

namespace {

/* Horizontal breathing room either side of a label. */
constexpr int kLabelPaddingX = 12;
/* Space reserved for the close affordance, when shown. */
constexpr int kCloseSize = 10;
constexpr int kCloseMargin = 8;
/* Thickness of an accent rule. */
constexpr int kAccentThickness = 2;
/* Angle of the Powerline chevron, as a fraction of the bar height. */
constexpr double kChevronSlant = 0.5;

constexpr int kMinTabWidth = 60;
constexpr int kMaxTabWidth = 240;

double relativeLuminance(const QColor& color) {
    return 0.2126 * color.redF() + 0.7152 * color.greenF() + 0.0722 * color.blueF();
}

/*
 * Whichever candidate stands out more against `background`.
 *
 * A fixed luminance threshold is not enough: a mid-tone accent such as Gruvbox
 * Light's blue sits close enough to the middle that either choice is defensible,
 * and picking by measured contrast keeps the label legible on all of the themes
 * rather than most of them.
 */
QColor mostReadableOn(const QColor& background, const QColor& first, const QColor& second) {
    const double base = relativeLuminance(background);
    return std::abs(relativeLuminance(first) - base)
               >= std::abs(relativeLuminance(second) - base)
           ? first : second;
}

} // namespace

TabBar::TabBar(QWidget* parent)
    : QTabBar(parent)
{
    setMouseTracking(true);
    setDrawBase(false);
    setExpanding(false);
    setMovable(true);
    setFocusPolicy(Qt::NoFocus);
    /* The close affordance is drawn in paintEvent, not added as a child widget;
     * see the class comment. */
    setTabsClosable(false);
    setElideMode(Qt::ElideNone);   // elision is done in paintLabel()

    applyConfiguration();
}

void TabBar::applyConfiguration() {
    const Config& config = Config::instance();
    style_ = config.tabBarStyle();

    /*
     * Size the bar from the terminal font so it scales with it, rather than
     * from a fixed pixel count that would look wrong at either extreme.
     */
    QFont labelFont = font();
    const QStringList families = config.fontFamilies();
    if (!families.isEmpty()) {
        labelFont.setFamily(families.first());
    }
    labelFont.setPointSizeF(std::max(8.0, config.fontSize() * 0.85));
    setFont(labelFont);

    const int textHeight = QFontMetrics(labelFont).height();
    barHeight_ = std::max(20, textHeight + 8);

    updateGeometry();
    update();
}

bool TabBar::barIsAtBottom() const {
    const QTabBar::Shape currentShape = shape();
    return currentShape == QTabBar::RoundedSouth || currentShape == QTabBar::TriangularSouth;
}

/* ------------------------------------------------------------- metrics */

QSize TabBar::tabSizeHint(int index) const {
    const QFontMetrics metrics(font());
    int width = metrics.horizontalAdvance(tabText(index)) + 2 * kLabelPaddingX;

    /* Always reserve the close area, so a label does not shift sideways when
     * the pointer enters the tab. */
    width += kCloseSize + kCloseMargin;

    if (style_ == TabBarStyle::Powerline) {
        width += static_cast<int>(barHeight_ * kChevronSlant);
    }

    return QSize(std::clamp(width, kMinTabWidth, kMaxTabWidth), barHeight_);
}

QSize TabBar::minimumTabSizeHint(int index) const {
    return QSize(kMinTabWidth, tabSizeHint(index).height());
}

/* ------------------------------------------------------------- painting */

void TabBar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const Config& config = Config::instance();
    const ChromeColors::Resolved colors = config.chromeColors().resolve(config.palette());

    painter.fillRect(rect(), colors.tabBarBackground);

    /* A hairline along the edge that meets the terminal, which is what separates
     * the bar from the text without needing a heavier background. */
    painter.setPen(colors.tabBarBorder);
    const int edgeY = barIsAtBottom() ? 0 : height() - 1;
    painter.drawLine(0, edgeY, width(), edgeY);

    /* Current tab last, so its accent and fill sit above its neighbours. */
    const int current = currentIndex();
    for (int index = 0; index < count(); ++index) {
        if (index != current) paintTab(painter, index, colors);
    }
    if (current >= 0 && current < count()) {
        paintTab(painter, current, colors);
    }
}

void TabBar::paintTab(QPainter& painter, int index,
                      const ChromeColors::Resolved& colors) const {
    const QRect rect = tabRect(index);
    if (rect.isEmpty()) return;

    const bool current = (index == currentIndex());

    switch (style_) {
    case TabBarStyle::Minimal:   paintMinimal(painter, index, rect, current, colors); break;
    case TabBarStyle::Underline: paintUnderline(painter, index, rect, current, colors); break;
    case TabBarStyle::Blocks:    paintBlocks(painter, index, rect, current, colors); break;
    case TabBarStyle::Pills:     paintPills(painter, index, rect, current, colors); break;
    case TabBarStyle::Powerline: paintPowerline(painter, index, rect, current, colors); break;
    }
}

void TabBar::paintMinimal(QPainter& painter, int index, const QRect& rect, bool current,
                          const ChromeColors::Resolved& colors) const {
    /* No fill at all; the active tab is marked by a short accent along the edge
     * that faces the terminal. */
    if (!current && index == hoveredTab_) {
        QColor hover = colors.activeTabBackground;
        hover.setAlpha(90);
        painter.fillRect(rect, hover);
    }
    if (current) {
        const int y = barIsAtBottom() ? rect.top() : rect.bottom() - kAccentThickness + 1;
        painter.fillRect(QRect(rect.left() + kLabelPaddingX / 2, y,
                               rect.width() - kLabelPaddingX, kAccentThickness),
                         colors.accent);
    }

    const QColor color = current ? colors.activeTabForeground : colors.inactiveTabForeground;
    paintLabel(painter, index, rect, color);
    paintCloseButton(painter, index, rect, color);
}

void TabBar::paintUnderline(QPainter& painter, int index, const QRect& rect, bool current,
                            const ChromeColors::Resolved& colors) const {
    if (current) {
        /* Full-width rule, plus a barely-there wash so the tab reads as a
         * surface rather than only a line. */
        QColor wash = colors.accent;
        wash.setAlpha(28);
        painter.fillRect(rect, wash);

        const int y = barIsAtBottom() ? rect.top() : rect.bottom() - kAccentThickness + 1;
        painter.fillRect(QRect(rect.left(), y, rect.width(), kAccentThickness),
                         colors.accent);
    }

    const QColor color = current ? colors.activeTabForeground : colors.inactiveTabForeground;
    paintLabel(painter, index, rect, color);
    paintCloseButton(painter, index, rect, color);
}

void TabBar::paintBlocks(QPainter& painter, int index, const QRect& rect, bool current,
                         const ChromeColors::Resolved& colors) const {
    if (current) {
        /* The fill is the whole signal here; adding an accent rule as well would
         * make this style a tinted copy of `underline`. */
        painter.fillRect(rect, colors.activeTabBackground);
    } else {
        if (index == hoveredTab_) {
            QColor hover = colors.activeTabBackground;
            hover.setAlpha(110);
            painter.fillRect(rect, hover);
        }
        /* Hairline divider between inactive tabs only: next to the filled active
         * tab a divider would double up with its edge. */
        if (index + 1 < count() && index + 1 != currentIndex()) {
            painter.setPen(colors.tabBarBorder);
            const int x = rect.right();
            painter.drawLine(x, rect.top() + 5, x, rect.bottom() - 5);
        }
    }

    const QColor color = current ? colors.activeTabForeground : colors.inactiveTabForeground;
    paintLabel(painter, index, rect, color);
    paintCloseButton(painter, index, rect, color);
}

void TabBar::paintPills(QPainter& painter, int index, const QRect& rect, bool current,
                        const ChromeColors::Resolved& colors) const {
    const QRect pill = rect.adjusted(3, 4, -3, -4);

    if (current) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.accent);
        painter.drawRoundedRect(pill, pill.height() / 2.0, pill.height() / 2.0);
    } else if (index == hoveredTab_) {
        QColor hover = colors.accent;
        hover.setAlpha(38);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hover);
        painter.drawRoundedRect(pill, pill.height() / 2.0, pill.height() / 2.0);
    }

    /*
     * The label sits on the accent when current, so it takes whichever of the
     * theme's two candidate colours reads better against it.
     */
    const QColor color = current
        ? mostReadableOn(colors.accent, colors.activeTabForeground, colors.tabBarBackground)
        : colors.inactiveTabForeground;

    paintLabel(painter, index, rect, color);
    paintCloseButton(painter, index, rect, color);
}

void TabBar::paintPowerline(QPainter& painter, int index, const QRect& rect, bool current,
                            const ChromeColors::Resolved& colors) const {
    const int slant = static_cast<int>(rect.height() * kChevronSlant);

    if (current) {
        /*
         * A chevron pointing along the bar: square on the leading edge, angled
         * on the trailing one, echoing a Powerline prompt separator. Drawn
         * geometrically rather than with the font's separator glyph so it fits
         * the bar exactly whatever font is in use.
         */
        QPainterPath path;
        path.moveTo(rect.left(), rect.top());
        path.lineTo(rect.right() - slant, rect.top());
        path.lineTo(rect.right(), rect.center().y() + 0.5);
        path.lineTo(rect.right() - slant, rect.bottom() + 1);
        path.lineTo(rect.left(), rect.bottom() + 1);
        path.closeSubpath();

        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.accent);
        painter.drawPath(path);
    } else if (index + 1 < count() && index + 1 != currentIndex()) {
        /* Thin chevron outline as the divider between inactive tabs. */
        QPen pen(colors.tabBarBorder);
        pen.setWidth(1);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const int x = rect.right() - slant / 2;
        painter.drawLine(x, rect.top() + 4, x + slant / 2, rect.center().y());
        painter.drawLine(x + slant / 2, rect.center().y(), x, rect.bottom() - 4);
    }

    const QColor color = current
        ? mostReadableOn(colors.accent, colors.activeTabForeground, colors.tabBarBackground)
        : colors.inactiveTabForeground;

    /* Keep the label clear of the chevron. */
    paintLabel(painter, index, rect.adjusted(0, 0, -slant, 0), color);
    paintCloseButton(painter, index, rect.adjusted(0, 0, -slant, 0), color);
}

void TabBar::paintLabel(QPainter& painter, int index, const QRect& contentRect,
                        const QColor& color) const {
    QRect textRect = contentRect.adjusted(kLabelPaddingX, 0,
                                          -(kLabelPaddingX + kCloseSize + kCloseMargin), 0);
    if (textRect.width() <= 0) return;

    const QFontMetrics metrics(font());
    const QString label = metrics.elidedText(tabText(index), Qt::ElideRight,
                                             textRect.width());

    painter.setPen(color);
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, label);
}

bool TabBar::showsCloseButton(int index) const {
    /* Only the hovered and current tabs, so a row of tabs is not a row of
     * crosses. A single tab has nothing to close down to. */
    if (count() <= 1) return false;
    return index == hoveredTab_ || index == currentIndex();
}

QRect TabBar::closeRect(int index) const {
    if (!showsCloseButton(index)) return QRect();

    QRect rect = tabRect(index);
    if (style_ == TabBarStyle::Powerline) {
        rect.adjust(0, 0, -static_cast<int>(rect.height() * kChevronSlant), 0);
    }
    const int y = rect.center().y() - kCloseSize / 2;
    return QRect(rect.right() - kCloseMargin - kCloseSize, y, kCloseSize, kCloseSize);
}

void TabBar::paintCloseButton(QPainter& painter, int index, const QRect& rect,
                              const QColor& color) const {
    Q_UNUSED(rect);
    const QRect area = closeRect(index);
    if (area.isNull()) return;

    const bool hot = (index == hoveredTab_ && hoveringClose_);

    if (hot) {
        QColor halo = color;
        halo.setAlpha(45);
        painter.setPen(Qt::NoPen);
        painter.setBrush(halo);
        painter.drawEllipse(area.adjusted(-3, -3, 3, 3));
    }

    QColor strokeColor = color;
    strokeColor.setAlpha(hot ? 255 : 150);
    QPen pen(strokeColor);
    pen.setWidthF(1.4);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const QRectF glyph = QRectF(area).adjusted(1.5, 1.5, -1.5, -1.5);
    painter.drawLine(glyph.topLeft(), glyph.bottomRight());
    painter.drawLine(glyph.topRight(), glyph.bottomLeft());
}

/* --------------------------------------------------------------- input */

void TabBar::mouseMoveEvent(QMouseEvent* event) {
    const int tab = tabAt(event->position().toPoint());
    const bool overClose = tab >= 0
                        && closeRect(tab).contains(event->position().toPoint());

    if (tab != hoveredTab_ || overClose != hoveringClose_) {
        hoveredTab_ = tab;
        hoveringClose_ = overClose;
        update();
    }

    /*
     * Swallow the move while the pointer is over a close affordance, so a small
     * drag there does not start reordering the tab.
     */
    if (overClose) {
        event->accept();
        return;
    }
    QTabBar::mouseMoveEvent(event);
}

void TabBar::mousePressEvent(QMouseEvent* event) {
    const int tab = tabAt(event->position().toPoint());

    if (event->button() == Qt::LeftButton && tab >= 0
        && closeRect(tab).contains(event->position().toPoint())) {
        pressedCloseTab_ = tab;
        event->accept();
        return;
    }

    /* Middle click closes, as it does on a browser tab. */
    if (event->button() == Qt::MiddleButton && tab >= 0) {
        event->accept();
        emit tabCloseClicked(tab);
        return;
    }

    pressedCloseTab_ = -1;
    QTabBar::mousePressEvent(event);
}

void TabBar::mouseReleaseEvent(QMouseEvent* event) {
    if (pressedCloseTab_ >= 0) {
        const int tab = pressedCloseTab_;
        pressedCloseTab_ = -1;

        /* Only if the release is still on the same affordance, which is what
         * lets a user change their mind mid-click. */
        if (tab == tabAt(event->position().toPoint())
            && closeRect(tab).contains(event->position().toPoint())) {
            event->accept();
            emit tabCloseClicked(tab);
            return;
        }
        event->accept();
        return;
    }
    QTabBar::mouseReleaseEvent(event);
}

void TabBar::leaveEvent(QEvent* event) {
    if (hoveredTab_ != -1 || hoveringClose_) {
        hoveredTab_ = -1;
        hoveringClose_ = false;
        update();
    }
    QTabBar::leaveEvent(event);
}

bool TabBar::event(QEvent* event) {
    /* Labels are elided, so the full title belongs in a tooltip. */
    if (event->type() == QEvent::ToolTip) {
        auto* helpEvent = static_cast<QHelpEvent*>(event);
        const int tab = tabAt(helpEvent->pos());
        if (tab >= 0) {
            const QFontMetrics metrics(font());
            const QRect area = tabRect(tab);
            const int available = area.width() - 2 * kLabelPaddingX - kCloseSize - kCloseMargin;
            if (metrics.horizontalAdvance(tabText(tab)) > available) {
                QToolTip::showText(helpEvent->globalPos(), tabText(tab), this);
            } else {
                QToolTip::hideText();
            }
            return true;
        }
    }
    return QTabBar::event(event);
}

/* ------------------------------------------------------------ TabWidget */

TabWidget::TabWidget(QWidget* parent)
    : QTabWidget(parent)
    , bar_(new TabBar(this))
{
    /* setTabBar() is protected on QTabWidget, which is the only reason this
     * subclass exists. */
    setTabBar(bar_);
}
