#pragma once
#include "Recorder.h"
#include <QMainWindow>
#include <QPixmap>
#include <QRect>
#include <functional>
class QComboBox;
class QCheckBox;
class QSpinBox;
class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;
class QTabWidget;
class RecordingPanel;
class GlobalHotkeys;
class QKeySequenceEdit;
class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget *parent = nullptr);
  void setGlobalShortcutStatus(const QString &text);
  void setHotkeys(GlobalHotkeys *hotkeys);
public slots:
  void takeScreenshot();
  void startRecording();

protected:
  void closeEvent(QCloseEvent *) override;

private:
  void acquireSnapshot(QScreen *, std::function<void(QPixmap)>);
  void selectArea(bool video);
  void openEditor(const QImage &image);
  void openCaptureOverlay(QScreen *, const QPixmap &,
                          const QRect &initial = QRect(), bool video = false);
  void beginRecording(QScreen *, const QRect &);
  void trimSelected();
  void refreshMicrophones();
  void requestScreenPermission();
  void showCaptureError(const QString &message);
  void chooseFolder(QLineEdit *, const QString &key);
  void rememberFile(const QString &);
  void refreshHistory();
  void openSelected();
  void revealSelected();
  void updateSummary();
  void savePreferences();
  QScreen *selectedScreen(bool video) const;
  bool permissionRequestRunning_ = false;
  GlobalHotkeys *hotkeys_ = nullptr;
  QKeySequenceEdit *screenshotKeys_, *videoKeys_;
  QCheckBox *autostart_, *askVideoPath_;
  Recorder *recorder_;
  RecordingPanel *panel_;
  QTabWidget *tabs_;
  QComboBox *shotScreen_, *videoScreen_, *shotSource_, *videoSource_, *audio_,
      *quality_, *format_, *resolution_, *theme_, *microphone_;
  QCheckBox *cursor_, *clicks_, *camera_, *screenshotCursor_;
  QSpinBox *fps_, *delay_;
  QLineEdit *imageFolder_, *videoFolder_, *systemAudio_, *cameraDevice_,
      *search_;
  QLabel *summary_, *stateLabel_, *hotkeyStatus_;
  QPushButton *recordButton_;
  QListWidget *history_;
  QRect lastRegion_;
  QStringList files_;
  QString savedMicrophoneId_;
};
