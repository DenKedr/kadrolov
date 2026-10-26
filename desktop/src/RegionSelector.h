#pragma once
#include <QPixmap>
#include <QScreen>
#include <QWidget>
class RegionSelector : public QWidget {
  Q_OBJECT
public:
  explicit RegionSelector(QScreen *screen, const QPixmap &snapshot = QPixmap(),
                          QWidget *parent = nullptr);
  QPixmap capturedSelection() const;
signals:
  void selected(const QRect &globalRect, QScreen *screen);
  void cancelled();

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void keyPressEvent(QKeyEvent *) override;
  void closeEvent(QCloseEvent *) override;

private:
  void acceptSelection();
  QScreen *screen_;
  QPixmap snapshot_;
  QPoint start_, end_;
  bool dragging_ = false, accepted_ = false;
};
