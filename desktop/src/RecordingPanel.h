#pragma once
#include "Recorder.h"
#include <QWidget>
class QLabel;
class QPushButton;
class QScreen;
class RecordingPanel : public QWidget {
  Q_OBJECT
public:
  explicit RecordingPanel(Recorder *recorder, QWidget *parent = nullptr);
  void setCaptureRegion(const QRect &globalRect, QScreen *screen);
signals:
  void stopRequested();

protected:
  void closeEvent(QCloseEvent *) override;
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;

private:
  Recorder *recorder_;
  QLabel *status_, *time_;
  QPushButton *pause_, *stop_;
  QWidget *frame_;
  QPoint dragOffset_;
  bool dragging_ = false;
};
