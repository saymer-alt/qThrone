#pragma once
#include <QTextBrowser>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QEvent>
#include <QtMath>

// A separate notice keeps actionable text readable without squeezing the toolbar.
class RestartNotice final : public QTextBrowser {
public:
    explicit RestartNotice(QWidget *parent) : QTextBrowser(parent) {
        setObjectName("restartNotice");
        setWordWrapMode(QTextOption::WordWrap);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setOpenLinks(false);setOpenExternalLinks(false);
        setStyleSheet("QTextBrowser { background: palette(window); border: 1px solid palette(mid); border-radius: 6px; padding: 8px; }");
        parent->installEventFilter(this); hide();
    }
    void present(const QString &html) { setHtml(html); reposition(); show(); raise(); }
protected:
    bool eventFilter(QObject *object,QEvent *event) override {
        if (event->type()==QEvent::Resize && object==parentWidget()) reposition();
        return false;
    }
private:
    void reposition() {
        const int limit=qMax(120,qMin(360,parentWidget()->width()-24));
        document()->setTextWidth(limit-28);
        // No wider than its text: a short notice does not leave an empty field beside it.
        const int width=qMax(120,qMin(limit,qCeil(document()->idealWidth())+28));
        document()->setTextWidth(width-28);
        const int height=qCeil(document()->size().height())+28;
        int bottom = 12;
        if (objectName() != "restartNotice") {
            const auto *restart = parentWidget()->findChild<QTextBrowser *>("restartNotice");
            if (restart && restart->isVisible()) bottom += restart->height() + 8;
        }
        setGeometry(parentWidget()->width()-width-12,qMax(12,parentWidget()->height()-height-bottom),width,height);
    }
};
