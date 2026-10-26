#include "RegionSelector.h"
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
namespace {
QPoint point(QMouseEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return e->position().toPoint();
#else
  return e->pos();
#endif
}
} // namespace
RegionSelector::RegionSelector(QScreen *screen, const QPixmap &snapshot,
                               QWidget *parent)
    : QWidget(parent), screen_(screen),
      snapshot_(snapshot.isNull() ? screen->grabWindow(0) : snapshot) {
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
  setAttribute(Qt::WA_DeleteOnClose);
  setGeometry(screen->geometry());
  setCursor(Qt::CrossCursor);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setWindowTitle(QStringLiteral("Кадролов — выбор области"));
}
QPixmap RegionSelector::capturedSelection() const {
  QRect r = QRect(start_, end_).normalized().intersected(rect());
  qreal ratio = snapshot_.devicePixelRatio();
  QRect pixels(qRound(r.x() * ratio), qRound(r.y() * ratio),
               qRound(r.width() * ratio), qRound(r.height() * ratio));
  auto result = snapshot_.copy(pixels);
  result.setDevicePixelRatio(1);
  return result;
}
void RegionSelector::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.drawPixmap(rect(), snapshot_);
  p.fillRect(rect(), QColor(0, 0, 0, 115));
  auto selection = QRect(start_, end_).normalized();
  if (!selection.isEmpty()) {
    p.save();
    p.setClipRect(selection);
    p.drawPixmap(rect(), snapshot_);
    p.restore();
    p.setPen(QPen(Qt::white, 2));
    p.drawRect(selection);
    QString size = QStringLiteral("%1 × %2")
                       .arg(selection.width())
                       .arg(selection.height());
    QRect label(selection.left(), qMax(0, selection.top() - 34), 180, 28);
    p.fillRect(label, QColor("#1E2025"));
    p.setPen(Qt::white);
    p.drawText(label.adjusted(8, 0, -5, 0), Qt::AlignVCenter, size);
  }
  QRect help(24, 24, qMin(width() - 48, 660), 52);
  p.fillRect(help, QColor(20, 22, 26, 230));
  p.setPen(Qt::white);
  p.drawText(
      help.adjusted(16, 4, -12, -4), Qt::AlignVCenter | Qt::TextWordWrap,
      QStringLiteral(
          "Выделите область мышью · Enter — подтвердить · Esc — отменить"));
}
void RegionSelector::mousePressEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton) {
    start_ = end_ = point(e);
    dragging_ = true;
    update();
  }
}
void RegionSelector::mouseMoveEvent(QMouseEvent *e) {
  if (dragging_) {
    end_ = point(e);
    end_.setX(qBound(0, end_.x(), width() - 1));
    end_.setY(qBound(0, end_.y(), height() - 1));
    update();
  }
}
void RegionSelector::mouseReleaseEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton && dragging_) {
    dragging_ = false;
    end_ = point(e);
    update();
    if (QRect(start_, end_).normalized().width() > 4 &&
        QRect(start_, end_).normalized().height() > 4)
      acceptSelection();
  }
}
void RegionSelector::acceptSelection() {
  auto r = QRect(start_, end_).normalized().intersected(rect());
  if (r.width() < 4 || r.height() < 4)
    return;
  accepted_ = true;
  hide();
  emit selected(r.translated(screen_->geometry().topLeft()), screen_);
  close();
}
void RegionSelector::keyPressEvent(QKeyEvent *e) {
  if (e->key() == Qt::Key_Escape) {
    close();
    return;
  }
  if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
    acceptSelection();
    return;
  }
  QWidget::keyPressEvent(e);
}
void RegionSelector::closeEvent(QCloseEvent *e) {
  if (!accepted_)
    emit cancelled();
  QWidget::closeEvent(e);
}
