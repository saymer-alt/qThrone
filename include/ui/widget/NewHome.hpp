#pragma once

// The new interface: the mobile app's new style in its wide (tablet and laptop) layout, over the same actions,
// menus and models as the classic window, so it adds a look and loses no function.

#include <QAbstractButton>
#include <QElapsedTimer>
#include <QListView>
#include <QPalette>
#include <QStyledItemDelegate>
#include <QWidget>
#include <functional>
#include <memory>

namespace Configs { class Group; }

class MainWindow;
class QBoxLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QTimer;
class QVariantAnimation;
class QVBoxLayout;

namespace NewStyle {
    // Theme keys kept in settings; anything else is the classic interface.
    inline constexpr auto OledTheme = "NewOLED";
    inline constexpr auto WhiteTheme = "NewWhite";
    bool isTheme(const QString &theme);
    void setTheme(const QString &theme);
    bool active();
    bool light();

    QColor background();
    QColor text();
    QColor muted();
    QColor solid();
    QColor outline();
    QColor overlay(int alpha);

    QPalette palette();
    QString styleSheet();
    // Geologica, the mobile app's typeface; registered once.
    QString fontFamily();
    QFont font(qreal px, int weight = QFont::Normal);

    void paintBackdrop(QPainter &p, const QRect &r);
    // A translucent body, lighter at the top, with one thin rim; tinted for the chosen row or action.
    void paintGlass(QPainter &p, const QRectF &r, qreal radius, const QColor &tint = QColor());
    void paintIcon(QPainter &p, const QString &name, const QRectF &box, const QColor &color);
    QIcon icon(const QString &name, const QColor &color);
    // The flat square flag of a profile name's leading flag, or a null pixmap.
    QPixmap flag(const QString &leading, int size, qreal dpr);
    QString leadingFlag(const QString &name);

    // Fusion with the new style's own check boxes, radio buttons and menu ticks.
    QStyle *createStyle();
    // Windows 11's round corners for a window without a frame of its own.
    void roundCorners(QWidget *window, bool compact);
    // A widget arriving: fades in while it rises into place, as the mobile screens do.
    void rise(QWidget *widget, int distance, int duration);
}

// A row, tile or tab of the new style: glass that lights under the pointer and presses like the mobile ones.
class NewTile : public QAbstractButton {
public:
    enum Kind { Icon, Chip, Tab, Text };
    explicit NewTile(Kind kind, QWidget *parent = nullptr);
    void setIconName(const QString &name) { m_icon = name; update(); }
    // A second, muted line under the text, as the mobile subscription rows have.
    void setSubtitle(const QString &subtitle) { m_subtitle = subtitle; updateGeometry(); update(); }
    void setSelected(bool selected) { m_selected = selected; update(); }
    void setSpinning(bool spinning);
    void setTextSize(qreal px) { m_textSize = px; updateGeometry(); update(); }
    void setFlat(bool flat) { m_flat = flat; update(); }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    void animatePress(qreal to, int duration);
    Kind m_kind;
    QString m_icon;
    QString m_subtitle;
    bool m_selected = false;
    bool m_hover = false;
    bool m_flat = false;
    qreal m_textSize = 14;
    qreal m_press = 0;
    qreal m_angle = 0;
    QVariantAnimation *m_pressAnim = nullptr;
    QTimer *m_spin = nullptr;
};

// The TUN and system proxy switches of the mobile home screen.
class NewSwitch : public QAbstractButton {
public:
    explicit NewSwitch(const QString &label, QWidget *parent = nullptr);
    void setOn(bool on);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    bool m_on = false;
    bool m_hover = false;
    qreal m_knob = 0;
    QVariantAnimation *m_anim = nullptr;
};

// The new style's connect button: a disc whose edge is a slow wave, rings leaving it while connected,
// a stroke running round it while connecting.
class NewOrb : public QAbstractButton {
public:
    explicit NewOrb(QWidget *parent = nullptr);
    void setState(bool connected, bool connecting, const QString &status, const QString &elapsed);
    QSize sizeHint() const override { return {240, 190}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    bool hitButton(const QPoint &pos) const override;

private:
    void animatePress(qreal to, int duration);
    bool m_connected = false;
    bool m_connecting = false;
    QString m_status;
    QString m_elapsed;
    qreal m_wave = .3, m_spin = 0, m_lit = 0, m_angle = 0, m_phase = 0, m_pressScale = 1;
    qint64 m_lastFrame = 0;
    QElapsedTimer m_clock;
    QTimer *m_frame = nullptr;
    QVariantAnimation *m_pressAnim = nullptr;
};

// Close, maximise and minimise in the window's own style.
class NewTitleBar : public QWidget {
public:
    explicit NewTitleBar(QWidget *parent = nullptr);
    // Shows the window's icon and title, as its own frame would.
    void follow(QWidget *window);

protected:
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QLabel *m_icon = nullptr;
    QLabel *m_title = nullptr;
    QWidget *m_window = nullptr;
};

class NewProfileDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    int runningId = -1;
    int hoverRow = -1;
    qreal spinAngle = 0;
    static QRect chevronRect(const QRect &row);
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

class NewProfileList : public QListView {
public:
    explicit NewProfileList(QWidget *parent = nullptr);
    NewProfileDelegate *rowDelegate = nullptr;
    std::function<void(const QModelIndex &)> editRequested;
    std::function<void(int from, int to)> rowsSwapped;
    std::function<int(int)> toSourceRow;
    QString emptyText;

protected:
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void dropEvent(QDropEvent *) override;

private:
    QTimer *m_spinner = nullptr;
};

class NewHome : public QWidget {
public:
    // Which routing the group's servers run with, "" for a plain group on the global profile; shared with the routing menu.
    static QString routeText(const std::shared_ptr<Configs::Group> &group, bool *fromProvider);
    explicit NewHome(MainWindow *window);
    // Moves the notice and the log, connections, traffic and runtime pages in, or back to the classic window.
    void attach();
    void detach();
    bool attached() const { return m_attached; }
    void groupChanged();
    void refresh();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Qt::Edges edgesAt(const QPoint &pos) const;
    void buildSide(QVBoxLayout *side);
    void buildBody(QVBoxLayout *body);
    void selectTab(int index, bool animated = true);
    void showGroupPicker();
    void showMoreMenu();
    void showAddMenu();
    void showGroupMenu();
    void checkCurrentConnection();
    void refreshGroupHeader();
    void openRouting();
    void updateNotice();
    // Puts the panel, the subscription card and the connect block where the settings say.
    void applyArrangement();
    void setArranging(bool on);

    MainWindow *mw;
    QHBoxLayout *m_columns = nullptr;
    QWidget *m_side = nullptr;
    QVBoxLayout *m_sideLayout = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    QWidget *m_header = nullptr;
    QWidget *m_chipBox = nullptr;
    QWidget *m_controls = nullptr;
    QWidget *m_arrange = nullptr;
    bool m_attached = false;
    struct Slot { QBoxLayout *layout = nullptr; int index = -1; int stretch = 0; };
    Slot m_dataSlot, m_statsSlot;

    NewTitleBar *m_titleBar = nullptr;
    NewOrb *m_orb = nullptr;
    NewTile *m_chip = nullptr;
    NewTile *m_more = nullptr;
    NewTile *m_sendLogs = nullptr;
    NewTile *m_add = nullptr;
    NewTile *m_refresh = nullptr;
    NewTile *m_groupMore = nullptr;
    NewTile *m_support = nullptr;
    NewSwitch *m_tun = nullptr;
    NewSwitch *m_proxy = nullptr;
    QLabel *m_check = nullptr;
    QLabel *m_down = nullptr;
    QLabel *m_up = nullptr;
    QLabel *m_running = nullptr;
    QLabel *m_inbound = nullptr;
    QLabel *m_groupName = nullptr;
    QLabel *m_groupDate = nullptr;
    QLabel *m_quota = nullptr;
    QWidget *m_traffic = nullptr;
    QLabel *m_expiry = nullptr;
    QLabel *m_announce = nullptr;
    // Which routing the subscription runs with; a click opens where it is changed.
    QLabel *m_routeLine = nullptr;
    bool m_routeFromProvider = false;
    QLineEdit *m_search = nullptr;
    QWidget *m_notice = nullptr;
    QVBoxLayout *m_noticeLayout = nullptr;
    QVBoxLayout *m_statsLayout = nullptr;
    QWidget *m_empty = nullptr;
    QStackedWidget *m_pages = nullptr;
    QList<NewTile *> m_tabs;
    NewProfileList *m_list = nullptr;
    QTimer *m_tick = nullptr;
    QVariantAnimation *m_noticeAnim = nullptr;

    QElapsedTimer m_connectedFor;
    bool m_wasRunning = false;
    qint64 m_checkSince = 0;
    qint64 m_checkShownAt = 0;
    QString m_checkResult;
    bool m_announceOpen = false;
    int m_renderedGroup = -1;
    QString m_headerKey;
    bool m_syncingTabs = false;
    double m_rateDown = 0, m_rateUp = 0;
};
