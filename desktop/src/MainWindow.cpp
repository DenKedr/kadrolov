#include "MainWindow.h"
#include "Autostart.h"
#include "DeviceInventory.h"
#include "Editor.h"
#include "GlobalHotkeys.h"
#include "RecordingPanel.h"
#include "Recovery.h"
#include "RegionSelector.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStorageInfo>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <memory>
namespace {
QString pictures() {
  return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) +
         QStringLiteral("/Кадролов");
}
QString movies() {
  return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) +
         QStringLiteral("/Кадролов");
}
QWidget *page() {
  auto *w = new QWidget;
  auto *l = new QVBoxLayout(w);
  l->setContentsMargins(28, 24, 28, 24);
  l->setSpacing(18);
  return w;
}
QWidget *scrollPage(QWidget *content) {
  auto *scroll = new QScrollArea;
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setWidget(content);
  return scroll;
}
QPushButton *primary(const QString &text) {
  auto *b = new QPushButton(text);
  b->setObjectName("primary");
  b->setMinimumHeight(46);
  b->setMinimumWidth(200);
  return b;
}
void screens(QComboBox *c) {
  auto list = QGuiApplication::screens();
  for (int i = 0; i < list.size(); ++i) {
    auto *s = list[i];
    c->addItem(QStringLiteral("Экран %1 · %2 × %3 · %4")
                   .arg(i + 1)
                   .arg(s->geometry().width())
                   .arg(s->geometry().height())
                   .arg(s->name()),
               i);
  }
}
} // namespace
MainWindow::MainWindow(QWidget *p)
    : QMainWindow(p), recorder_(new Recorder(this)),
      panel_(new RecordingPanel(recorder_)) {
  setWindowTitle(QStringLiteral("Кадролов — снимки и запись экрана"));
  resize(880, 720);
  setMinimumSize(680, 580);
  QSettings settings;
  files_ = settings.value("history").toStringList();
  lastRegion_ = settings.value("lastRegion").toRect();
  auto *root = new QWidget;
  auto *layout = new QVBoxLayout(root);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  auto *header = new QWidget;
  header->setObjectName("header");
  auto *h = new QHBoxLayout(header);
  h->setContentsMargins(28, 22, 28, 18);
  auto *brand = new QLabel(QStringLiteral("Кадролов"));
  brand->setObjectName("brand");
  h->addWidget(brand);
  h->addStretch();
  auto *tagline = new QLabel(QStringLiteral("Поймал. Отметил. Сохранил."));
  tagline->setObjectName("muted");
  h->addWidget(tagline);
  layout->addWidget(header);
  tabs_ = new QTabWidget;
  tabs_->setDocumentMode(true);
  layout->addWidget(tabs_);
  setCentralWidget(root);
  auto *shot = page();
  auto *sl = qobject_cast<QVBoxLayout *>(shot->layout());
  auto *st = new QLabel(QStringLiteral("Сохраните нужное с экрана"));
  st->setObjectName("sectionTitle");
  sl->addWidget(st);
  auto *sh = new QLabel(QStringLiteral(
      "Выберите область, добавьте пометки и сохраните изображение."));
  sh->setWordWrap(true);
  sl->addWidget(sh);
  auto *shotBox = new QGroupBox(QStringLiteral("Что снять"));
  auto *sf = new QFormLayout(shotBox);
  sf->setSpacing(14);
  shotScreen_ = new QComboBox;
  screens(shotScreen_);
  shotSource_ = new QComboBox;
  shotSource_->addItems({QStringLiteral("Выбрать область"),
                         QStringLiteral("Весь выбранный экран"),
                         QStringLiteral("Последняя область")});
#ifdef Q_OS_MACOS
  shotSource_->addItem(QStringLiteral("Отдельное окно"));
  connect(shotSource_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int index) { shotScreen_->setEnabled(index != 3); });
#endif
  sf->addRow(QStringLiteral("Источник"), shotSource_);
  sf->addRow(QStringLiteral("Монитор"), shotScreen_);
  delay_ = new QSpinBox;
  delay_->setRange(0, 30);
  delay_->setSuffix(QStringLiteral(" сек"));
  delay_->setValue(settings.value("shotDelay", 0).toInt());
  sf->addRow(QStringLiteral("Задержка"), delay_);
  screenshotCursor_ =
      new QCheckBox(QStringLiteral("Показывать курсор на снимке"));
#ifdef Q_OS_MACOS
  screenshotCursor_->setChecked(
      settings.value("screenshotCursor", false).toBool());
#else
  screenshotCursor_->setEnabled(false);
  screenshotCursor_->setToolTip(
      QStringLiteral("Этот механизм снимков не передаёт системный курсор. "
                     "Курсор видео настраивается отдельно."));
#endif
  sf->addRow(screenshotCursor_);
  sl->addWidget(shotBox);
  auto *shotAction = primary(QStringLiteral("Сделать снимок"));
  connect(shotAction, &QPushButton::clicked, this, &MainWindow::takeScreenshot);
  sl->addWidget(shotAction, 0, Qt::AlignLeft);
  auto *open =
      new QPushButton(QStringLiteral("Открыть изображение для пометок…"));
  connect(open, &QPushButton::clicked, this, [this] {
    auto path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Открыть изображение"), imageFolder_->text(),
        QStringLiteral("Изображения (*.png *.jpg *.jpeg *.webp *.bmp)"));
    if (path.isEmpty())
      return;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    auto img = reader.read();
    if (img.isNull()) {
      QMessageBox::warning(this, QStringLiteral("Не удалось открыть"),
                           reader.errorString());
      return;
    }
    openEditor(img);
  });
  sl->addWidget(open, 0, Qt::AlignLeft);
  sl->addStretch();
  auto *shotFooter = new QLabel(
      QStringLiteral("Работает локально. Снимки никуда не отправляются."));
  shotFooter->setObjectName("muted");
  sl->addWidget(shotFooter);
  tabs_->addTab(shot, QStringLiteral("Снимок"));
  auto *video = page();
  auto *vl = qobject_cast<QVBoxLayout *>(video->layout());
  auto *vt =
      new QLabel(QStringLiteral("Запишите и покажите, как это работает"));
  vt->setObjectName("sectionTitle");
  vl->addWidget(vt);
  auto *videoBox = new QGroupBox(QStringLiteral("Перед началом записи"));
  auto *vf = new QFormLayout(videoBox);
  vf->setSpacing(12);
  videoSource_ = new QComboBox;
  videoSource_->addItems({QStringLiteral("Выбрать область"),
                          QStringLiteral("Весь выбранный экран"),
                          QStringLiteral("Последняя область")});
  videoScreen_ = new QComboBox;
  screens(videoScreen_);
  audio_ = new QComboBox;
  audio_->addItem(QStringLiteral("Без звука"), "none");
  audio_->addItem(QStringLiteral("Звук компьютера"), "system");
  audio_->addItem(QStringLiteral("Микрофон"), "mic");
  audio_->addItem(QStringLiteral("Компьютер и микрофон"), "both");
  vf->addRow(QStringLiteral("Источник"), videoSource_);
  vf->addRow(QStringLiteral("Монитор"), videoScreen_);
  vf->addRow(QStringLiteral("Звук"), audio_);
  savedMicrophoneId_ = settings.value("microphone").toString();
  microphone_ = new QComboBox;
  microphone_->setAccessibleName(QStringLiteral("Устройство микрофона"));
  microphone_->addItem(QStringLiteral("Системный микрофон"), QString());
  microphone_->setEnabled(false);
  auto *microphoneRow = new QWidget;
  auto *microphoneLayout = new QHBoxLayout(microphoneRow);
  microphoneLayout->setContentsMargins(0, 0, 0, 0);
  microphoneLayout->addWidget(microphone_, 1);
  auto *refreshMicrophone = new QPushButton(QStringLiteral("Обновить"));
  refreshMicrophone->setToolTip(QStringLiteral("Найти подключённые микрофоны"));
  microphoneLayout->addWidget(refreshMicrophone);
  vf->addRow(QStringLiteral("Микрофон"), microphoneRow);
  connect(refreshMicrophone, &QPushButton::clicked, this,
          &MainWindow::refreshMicrophones);
  connect(audio_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this] {
            auto mode = audio_->currentData().toString();
            microphone_->setEnabled(mode == "mic" || mode == "both");
          });

  auto *toggles = new QWidget;
  auto *tl = new QHBoxLayout(toggles);
  tl->setContentsMargins(0, 0, 0, 0);
  cursor_ = new QCheckBox(QStringLiteral("Курсор"));
  cursor_->setChecked(settings.value("cursor", true).toBool());
  clicks_ = new QCheckBox(QStringLiteral("Круги кликов"));
  clicks_->setChecked(settings.value("clicks", false).toBool());
  clicks_->setEnabled(Recorder::supportsClicks());
  if (!clicks_->isEnabled())
    clicks_->setToolTip(QStringLiteral(
        "Глобальные клики недоступны для этого механизма захвата."));
  tl->addWidget(cursor_);
  tl->addWidget(clicks_);
  camera_ = new QCheckBox(QStringLiteral("Камера"));
  camera_->setChecked(false);
  tl->addWidget(camera_);
  tl->addStretch();
  vf->addRow(QStringLiteral("В кадре"), toggles);
  quality_ = new QComboBox;
  quality_->addItem(QStringLiteral("Высокое — больше размер"), 18);
  quality_->addItem(QStringLiteral("Обычное — для работы"), 23);
  quality_->addItem(QStringLiteral("Компактное — меньше размер"), 28);
  quality_->setCurrentIndex(settings.value("quality", 1).toInt());
  vf->addRow(QStringLiteral("Качество"), quality_);
  resolution_ = new QComboBox;
  resolution_->addItem(QStringLiteral("Исходный размер"), 0);
  resolution_->addItem(QStringLiteral("Не больше 1920 × 1080"), 1080);
  resolution_->addItem(QStringLiteral("Не больше 1280 × 720"), 720);
  resolution_->setCurrentIndex(settings.value("resolution", 1).toInt());
  vf->addRow(QStringLiteral("Разрешение"), resolution_);
  vl->addWidget(videoBox);
  auto *advancedButton = new QToolButton;
  advancedButton->setText(QStringLiteral("Дополнительные параметры"));
  advancedButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  advancedButton->setArrowType(Qt::RightArrow);
  advancedButton->setCheckable(true);
  vl->addWidget(advancedButton);
  auto *advanced = new QWidget;
  auto *af = new QFormLayout(advanced);
  af->setContentsMargins(0, 0, 0, 0);
  format_ = new QComboBox;
  format_->addItems({"MP4", "MKV", "WebM"});
  format_->setCurrentIndex(settings.value("format", 0).toInt());
  fps_ = new QSpinBox;
  fps_->setRange(5, 60);
  fps_->setValue(settings.value("fps", 30).toInt());
  fps_->setSuffix(QStringLiteral(" кадр/с"));
  systemAudio_ = new QLineEdit(settings.value("systemAudio").toString());
  systemAudio_->setPlaceholderText(
      QStringLiteral("По умолчанию; monitor/loopback устройства"));
  af->addRow(QStringLiteral("Формат"), format_);
  af->addRow(QStringLiteral("Частота кадров"), fps_);
  cameraDevice_ = new QLineEdit(settings.value("cameraDevice").toString());
  cameraDevice_->setPlaceholderText(
      QStringLiteral("По умолчанию; имя устройства"));
  af->addRow(QStringLiteral("Камера"), cameraDevice_);
  af->addRow(QStringLiteral("Системный звук"), systemAudio_);
  advanced->hide();
  connect(advancedButton, &QToolButton::toggled, advanced,
          &QWidget::setVisible);
  connect(advancedButton, &QToolButton::toggled, this,
          [advancedButton](bool on) {
            advancedButton->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
          });
  vl->addWidget(advanced);
  summary_ = new QLabel;
  summary_->setWordWrap(true);
  summary_->setObjectName("summary");
  vl->addWidget(summary_);
  recordButton_ = primary(QStringLiteral("Начать запись"));
  connect(recordButton_, &QPushButton::clicked, this,
          &MainWindow::startRecording);
  vl->addWidget(recordButton_, 0, Qt::AlignLeft);
  stateLabel_ = new QLabel(Recorder::capabilityNote());
  stateLabel_->setWordWrap(true);
  stateLabel_->setObjectName("muted");
  vl->addWidget(stateLabel_);
  vl->addStretch();
  tabs_->addTab(scrollPage(video), QStringLiteral("Запись"));
  auto *historyPage = page();
  auto *hl = qobject_cast<QVBoxLayout *>(historyPage->layout());
  auto *ht = new QLabel(QStringLiteral("Ваши снимки и видео"));
  ht->setObjectName("sectionTitle");
  hl->addWidget(ht);
  search_ = new QLineEdit;
  search_->setPlaceholderText(QStringLiteral("Найти файл по имени…"));
  hl->addWidget(search_);
  history_ = new QListWidget;
  history_->setIconSize(QSize(96, 64));
  history_->setSpacing(6);
  history_->setWordWrap(true);
  hl->addWidget(history_, 1);
  auto *historyActions = new QHBoxLayout;
  auto *openFile = new QPushButton(QStringLiteral("Открыть"));
  auto *showFolder = new QPushButton(QStringLiteral("Показать в папке"));
  auto *removeHistory = new QPushButton(QStringLiteral("Убрать из списка"));
  historyActions->addWidget(openFile);
  historyActions->addWidget(showFolder);
  auto *trim = new QPushButton(QStringLiteral("Обрезать видео…"));
  historyActions->addWidget(trim);
  connect(trim, &QPushButton::clicked, this, &MainWindow::trimSelected);
  historyActions->addStretch();
  historyActions->addWidget(removeHistory);
  hl->addLayout(historyActions);
  connect(openFile, &QPushButton::clicked, this, &MainWindow::openSelected);
  connect(showFolder, &QPushButton::clicked, this, &MainWindow::revealSelected);
  connect(history_, &QListWidget::itemDoubleClicked, this,
          [this](QListWidgetItem *) { openSelected(); });
  connect(removeHistory, &QPushButton::clicked, this, [this] {
    auto *i = history_->currentItem();
    if (!i)
      return;
    files_.removeAll(i->data(Qt::UserRole).toString());
    QSettings().setValue("history", files_);
    refreshHistory();
  });
  connect(search_, &QLineEdit::textChanged, this, [this] { refreshHistory(); });
  tabs_->addTab(historyPage, QStringLiteral("Мои файлы"));
  auto *settingsPage = page();
  auto *setl = qobject_cast<QVBoxLayout *>(settingsPage->layout());
  auto *settitle =
      new QLabel(QStringLiteral("Всё сохраняется на вашем устройстве"));
  settitle->setObjectName("sectionTitle");
  setl->addWidget(settitle);
  auto *paths = new QGroupBox(QStringLiteral("Куда сохранять"));
  auto *pf = new QFormLayout(paths);
  imageFolder_ =
      new QLineEdit(settings.value("imageFolder", pictures()).toString());
  videoFolder_ =
      new QLineEdit(settings.value("videoFolder", movies()).toString());
  auto folderRow = [this](QLineEdit *edit, const QString &key) {
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(edit, 1);
    auto *b = new QPushButton(QStringLiteral("Выбрать…"));
    l->addWidget(b);
    connect(b, &QPushButton::clicked, this,
            [this, edit, key] { chooseFolder(edit, key); });
    return w;
  };
  pf->addRow(QStringLiteral("Снимки"), folderRow(imageFolder_, "imageFolder"));
  pf->addRow(QStringLiteral("Видео"), folderRow(videoFolder_, "videoFolder"));
  askVideoPath_ =
      new QCheckBox(QStringLiteral("Спрашивать место перед каждой записью"));
  askVideoPath_->setChecked(settings.value("askVideoPath", false).toBool());
  pf->addRow(askVideoPath_);
  connect(askVideoPath_, &QCheckBox::toggled, this,
          [this] { savePreferences(); });
  setl->addWidget(paths);
  auto *shortcutBox = new QGroupBox(QStringLiteral("Горячие клавиши"));
  auto *shortcutForm = new QFormLayout(shortcutBox);
  screenshotKeys_ =
      new QKeySequenceEdit(GlobalHotkeys::defaultScreenshot(), shortcutBox);
  videoKeys_ = new QKeySequenceEdit(GlobalHotkeys::defaultVideo(), shortcutBox);
  screenshotKeys_->setAccessibleName(QStringLiteral("Горячая клавиша снимка"));
  videoKeys_->setAccessibleName(QStringLiteral("Горячая клавиша записи"));
  shortcutForm->addRow(QStringLiteral("Сделать снимок"), screenshotKeys_);
  shortcutForm->addRow(QStringLiteral("Запись экрана"), videoKeys_);
  auto *shortcutActions = new QWidget;
  auto *shortcutRow = new QHBoxLayout(shortcutActions);
  shortcutRow->setContentsMargins(0, 0, 0, 0);
  auto *applyKeys = new QPushButton(QStringLiteral("Применить"));
  auto *resetKeys = new QPushButton(QStringLiteral("По умолчанию"));
  shortcutRow->addWidget(applyKeys);
  shortcutRow->addWidget(resetKeys);
  shortcutRow->addStretch();
  shortcutForm->addRow(shortcutActions);
  connect(applyKeys, &QPushButton::clicked, this, [this] {
    if (!hotkeys_)
      return;
    QString error;
    if (!hotkeys_->setShortcuts(screenshotKeys_->keySequence(),
                                videoKeys_->keySequence(), &error)) {
      QMessageBox::warning(this, QStringLiteral("Не удалось назначить клавиши"),
                           error);
      screenshotKeys_->setKeySequence(hotkeys_->screenshotShortcut());
      videoKeys_->setKeySequence(hotkeys_->videoShortcut());
    }
    setGlobalShortcutStatus(hotkeys_->statusText());
  });
  connect(resetKeys, &QPushButton::clicked, this, [this] {
    screenshotKeys_->setKeySequence(GlobalHotkeys::defaultScreenshot());
    videoKeys_->setKeySequence(GlobalHotkeys::defaultVideo());
  });
  setl->addWidget(shortcutBox);
  autostart_ =
      new QCheckBox(QStringLiteral("Запускать Кадролов при входе в систему"));
  autostart_->setChecked(Autostart::isEnabled());
  autostart_->setToolTip(Autostart::description());
  connect(autostart_, &QCheckBox::toggled, this, [this](bool enabled) {
    QString error;
    if (!Autostart::setEnabled(enabled, &error)) {
      QSignalBlocker blocker(autostart_);
      autostart_->setChecked(Autostart::isEnabled());
      QMessageBox::warning(this, QStringLiteral("Автозапуск не изменён"),
                           error);
    }
  });
  setl->addWidget(autostart_);
#ifdef Q_OS_MACOS
  auto *permissionBox = new QGroupBox(QStringLiteral("Доступ к экрану"));
  auto *permissionLayout = new QVBoxLayout(permissionBox);
  auto *permissionText = new QLabel(
      QStringLiteral("Для снимков и видео разрешите Кадролову запись экрана в "
                     "macOS: «Конфиденциальность и безопасность → Запись "
                     "экрана». Разрешение включаете вы. После изменения "
                     "полностью закройте и снова откройте Кадролов."));
  permissionText->setWordWrap(true);
  permissionLayout->addWidget(permissionText);
  auto *permissionButton =
      new QPushButton(QStringLiteral("Разрешить запись экрана…"));
  permissionButton->setAccessibleName(QStringLiteral(
      "Открыть запрос разрешения и системные настройки записи экрана"));
  connect(permissionButton, &QPushButton::clicked, this,
          &MainWindow::requestScreenPermission);
  permissionLayout->addWidget(permissionButton, 0, Qt::AlignLeft);
  setl->addWidget(permissionBox);
#endif

  theme_ = new QComboBox;
  theme_->addItem(QStringLiteral("Как в системе"), "system");
  theme_->addItem(QStringLiteral("Светлая"), "light");
  theme_->addItem(QStringLiteral("Тёмная"), "dark");
  theme_->setCurrentIndex(settings.value("themeIndex", 0).toInt());
  auto *look = new QFormLayout;
  look->addRow(QStringLiteral("Оформление"), theme_);
  setl->addLayout(look);
  auto *about = new QLabel(QStringLiteral(
      "Кадролов · локальная программа\n\nСнимок: Ctrl+Shift+S в окне "
      "приложения\nЗапись: Ctrl+Shift+R в окне приложения\n\nРазрешение на "
      "захват экрана и звук "
      "запрашивает операционная система.\nНа Wayland требуется поддерживаемый "
      "портал; доступность показана во вкладке «Запись»."));
  about->setWordWrap(true);
  about->setObjectName("muted");
  setl->addWidget(about);
  hotkeyStatus_ = new QLabel;
  hotkeyStatus_->setWordWrap(true);
  hotkeyStatus_->setObjectName("muted");
  setl->addWidget(hotkeyStatus_);
  setl->addStretch();
  tabs_->addTab(scrollPage(settingsPage), QStringLiteral("Настройки"));
  const QPalette systemPalette = QApplication::palette();
  auto applyTheme = [this, systemPalette] {
    bool dark = theme_->currentData().toString() == "dark" ||
                (theme_->currentIndex() == 0 &&
                 systemPalette.color(QPalette::Window).lightness() < 128);
    QString bg = dark ? "#191B20" : "#ECEEF1",
            surface = dark ? "#24272E" : "#F7F8FA",
            text = dark ? "#F4F4F5" : "#252B35",
            muted = dark ? "#B3B7C2" : "#555F6E",
            border = dark ? "#454A55" : "#ABB3BF";
    QPalette palette = systemPalette;
    if (!dark) {
      palette.setColor(QPalette::Window, QColor(bg));
      palette.setColor(QPalette::WindowText, QColor(text));
      palette.setColor(QPalette::Base, QColor(surface));
      palette.setColor(QPalette::AlternateBase, QColor(bg));
      palette.setColor(QPalette::Text, QColor(text));
      palette.setColor(QPalette::Button, QColor(surface));
      palette.setColor(QPalette::ButtonText, QColor(text));
      palette.setColor(QPalette::Light, QColor("#FFFFFF"));
      palette.setColor(QPalette::Midlight, QColor("#E4E8EE"));
      palette.setColor(QPalette::Mid, QColor("#ABB3BF"));
      palette.setColor(QPalette::Dark, QColor("#7A8798"));
      palette.setColor(QPalette::Shadow, QColor("#46566C"));
      palette.setColor(QPalette::ToolTipBase, QColor(surface));
      palette.setColor(QPalette::ToolTipText, QColor(text));
      palette.setColor(QPalette::Highlight, QColor("#DDE7F3"));
      palette.setColor(QPalette::HighlightedText, QColor("#202C3D"));
      palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#697483"));
      palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                       QColor("#697483"));
    }
    qApp->setPalette(palette);
    qApp->setStyleSheet(
        QString(
            "QWidget{font-size:14px;color:%1;} "
            "QMainWindow,QDialog{background:%2;} "
            "QTabWidget::pane{border:0;background:%2;} "
            "QTabBar::tab{padding:14px 24px;background:%2;border-bottom:3px "
            "solid transparent;} QTabBar::tab:selected{border-bottom:3px solid "
            "#C2410C;font-weight:600;} #header{background:%3;} "
            "#brand{font-size:27px;font-weight:700;letter-spacing:-.5px;} "
            "#sectionTitle{font-size:21px;font-weight:600;} #muted{color:%4;} "
            "QGroupBox{background:%3;border:1px solid "
            "%5;border-radius:10px;margin-top:10px;padding:18px 14px 12px;} "
            "QGroupBox::title{subcontrol-origin:margin;left:14px;padding:0 "
            "5px;} QComboBox,QLineEdit,QSpinBox{background:%3;border:1px solid "
            "%5;border-radius:6px;padding:7px;min-height:20px;} "
            "QPushButton{background:%3;border:1px solid "
            "%5;border-radius:7px;padding:9px 14px;} "
            "QPushButton:hover{border-color:#C2410C;} "
            "QPushButton#primary{background:#C2410C;color:white;border:0;font-"
            "weight:600;} QPushButton:disabled{color:%4;} "
            "QCheckBox{spacing:7px;} "
            "#summary{padding:12px;background:%3;border-radius:7px;} "
            "QListWidget{background:%3;border:1px solid %5;border-radius:8px;} "
            "QListWidget::item{padding:8px;border-bottom:1px solid %5;} "
            "QToolBar{spacing:4px;padding:5px;background:%3;border-bottom:1px "
            "solid %5;} QToolButton{padding:6px;border-radius:4px;} "
            "QToolButton:checked{background:#C2410C;color:white;} "
            "QLabel{background:transparent;}")
            .arg(text, bg, surface, muted, border));
    if (!dark)
      qApp->setStyleSheet(
          qApp->styleSheet() +
          QStringLiteral(
              "QScrollArea,QScrollArea>QWidget>QWidget,QStackedWidget{"
              "background:#ECEEF1;} "
              "QComboBox:focus,QLineEdit:focus,QSpinBox:focus,QKeySequenceEdit:"
              "focus{border:2px solid #647F9F;} "
              "QComboBox::drop-down{width:24px;border-left:1px solid #ABB3BF;} "
              "QComboBox "
              "QAbstractItemView,QMenu{background:#F7F8FA;color:#252B35;"
              "border:1px solid "
              "#ABB3BF;selection-background-color:#DDE7F3;selection-color:#"
              "202C3D;} "
              "QMenu::item:selected,QListWidget::item:selected{background:#"
              "DDE7F3;color:#202C3D;} "
              "QLineEdit,QSpinBox{selection-background-color:#DDE7F3;selection-"
              "color:#202C3D;} "
              "QComboBox:disabled,QLineEdit:disabled,QSpinBox:disabled,"
              "QPushButton:disabled,"
              "QPushButton#primary:disabled{background:#E2E5EA;color:#697483;"
              "border:1px solid #BCC3CD;} "
              "QCheckBox:disabled,#muted:disabled{color:#697483;} "
              "QCheckBox::indicator{width:18px;height:18px;border:1px solid "
              "#7A8798;border-radius:4px;background:#F7F8FA;} "
              "QCheckBox::indicator:hover{border:1px solid "
              "#46566C;background:#E8EDF3;} "
              "QCheckBox::indicator:checked{background:#A9380A;border:1px "
              "solid #8D3009;image:url(:/icons/light-check.xpm);} "
              "QCheckBox::indicator:disabled{background:#E2E5EA;border:1px "
              "solid #A2ACBA;} "
              "QCheckBox::indicator:checked:disabled{background:#798696;image:"
              "url(:/icons/light-check.xpm);} "
              "QComboBox{padding-right:30px;} "
              "QComboBox::down-arrow{image:url(:/icons/"
              "light-chevron-down.xpm);width:12px;height:8px;} "
              "QSpinBox::up-button,QSpinBox::down-button{width:22px;border-"
              "left:1px solid #ABB3BF;background:#E8EDF3;} "
              "QSpinBox::up-button:hover,QSpinBox::down-button:hover{"
              "background:#DDE3EB;} "
              "QSpinBox::up-arrow{image:url(:/icons/"
              "light-chevron-up.xpm);width:10px;height:6px;} "
              "QSpinBox::down-arrow{image:url(:/icons/"
              "light-chevron-down.xpm);width:10px;height:6px;} "
              "QPushButton:hover{background:#E8EDF3;} "
              "QPushButton:pressed{background:#DDE3EB;} "
              "QPushButton#primary:hover{background:#A9380A;color:white;} "
              "QPushButton#primary:pressed{background:#8D3009;color:white;} "
              "QTabBar::tab:!selected:hover{background:#E1E5EB;} "
              "QToolTip{background:#F7F8FA;color:#252B35;border:1px solid "
              "#ABB3BF;}"));
  };
  connect(theme_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this, applyTheme] {
            applyTheme();
            savePreferences();
          });
  applyTheme();
  for (auto *c : {audio_, quality_, format_, resolution_, videoSource_})
    connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
      updateSummary();
      savePreferences();
    });
  connect(fps_, qOverload<int>(&QSpinBox::valueChanged), this, [this] {
    updateSummary();
    savePreferences();
  });
  connect(cursor_, &QCheckBox::toggled, this, [this] {
    updateSummary();
    savePreferences();
  });
  connect(clicks_, &QCheckBox::toggled, this, [this] {
    updateSummary();
    savePreferences();
  });
  connect(imageFolder_, &QLineEdit::editingFinished, this,
          &MainWindow::savePreferences);
  connect(videoFolder_, &QLineEdit::editingFinished, this,
          &MainWindow::savePreferences);
  connect(microphone_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this] {
            savedMicrophoneId_ = microphone_->currentData().toString();
            savePreferences();
          });
  connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
    if (index == 1 || index == 3)
      refreshMicrophones();
  });
  connect(systemAudio_, &QLineEdit::editingFinished, this,
          &MainWindow::savePreferences);
  connect(recorder_, &Recorder::stateChanged, this,
          [this](Recorder::State state) {
            bool active = state != Recorder::Idle && state != Recorder::Failed;
            recordButton_->setEnabled(!active);
            if (state == Recorder::Recording || state == Recorder::Paused ||
                state == Recorder::Starting || state == Recorder::Finalizing) {
              panel_->show();
              panel_->raise();
            }
            if (state == Recorder::Idle || state == Recorder::Failed) {
              show();
              raise();
            }
          });
  connect(recorder_, &Recorder::completed, this, [this](const QString &path) {
    rememberFile(path);
    if (exitAfterSave_) {
      exitAllowed_ = true;
      savePreferences();
      emit exitReady();
      return;
    }
    show();
    raise();
    tabs_->setCurrentIndex(2);
    statusBar()->showMessage(QStringLiteral("Видео сохранено: %1").arg(path),
                             12000);
  });
  connect(recorder_, &Recorder::error, this, [this](const QString &message) {
    exitAfterSave_ = false;
    show();
    raise();
    showCaptureError(message);
  });
  auto *skey = new QShortcut(QKeySequence("Ctrl+Shift+S"), this);
  connect(skey, &QShortcut::activated, this, &MainWindow::takeScreenshot);
  auto *rkey = new QShortcut(QKeySequence("Ctrl+Shift+R"), this);
  connect(rkey, &QShortcut::activated, this, &MainWindow::startRecording);
  if (QSystemTrayIcon::isSystemTrayAvailable()) {
    QPixmap icon(64, 64);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#C2410C"), 7, Qt::SolidLine, Qt::RoundCap));
    painter.drawRoundedRect(QRectF(11, 13, 42, 38), 6, 6);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#C2410C"));
    painter.drawEllipse(QPointF(44, 24), 5, 5);
    painter.end();
    auto *tray = new QSystemTrayIcon(QIcon(icon), this);
    tray->setToolTip(QStringLiteral("Кадролов — снимок и запись"));
    auto *menu = new QMenu(this);
    menu->addAction(QStringLiteral("Сделать снимок"), this,
                    &MainWindow::takeScreenshot);
    menu->addAction(QStringLiteral("Записать экран"), this,
                    &MainWindow::startRecording);
    menu->addSeparator();
    menu->addAction(QStringLiteral("Открыть Кадролов"), this, [this] {
      show();
      raise();
      activateWindow();
    });
    menu->addAction(QStringLiteral("Выход"), this, &MainWindow::requestExit);
    tray->setContextMenu(menu);
    connect(tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
              if (reason == QSystemTrayIcon::DoubleClick) {
                show();
                raise();
                activateWindow();
              }
            });
    tray->show();
  }
  auto *exit = menuBar()
                   ->addMenu(QStringLiteral("Кадролов"))
                   ->addAction(QStringLiteral("Выход"));
  exit->setMenuRole(QAction::QuitRole);
  connect(exit, &QAction::triggered, this, &MainWindow::requestExit);
  dockActivationArmed_ =
      !QCoreApplication::arguments().contains("--background");
  qApp->installEventFilter(this);
  refreshHistory();
  updateSummary();
  QTimer::singleShot(0, this, &MainWindow::refreshMicrophones);
  if (!QCoreApplication::arguments().contains("--background"))
    QTimer::singleShot(600, this, [this] {
      if (isVisible())
        Recovery::offer(this, videoFolder_->text());
    });
  statusBar()->showMessage(
      QStringLiteral("Локальный режим · ваши файлы остаются у вас"));
}
QScreen *MainWindow::selectedScreen(bool video) const {
  auto list = QGuiApplication::screens();
  int index = (video ? videoScreen_ : shotScreen_)->currentData().toInt();
  return index >= 0 && index < list.size() ? list[index]
                                           : QGuiApplication::primaryScreen();
}
void MainWindow::setHotkeys(GlobalHotkeys *hotkeys) {
  hotkeys_ = hotkeys;
  if (hotkeys_) {
    screenshotKeys_->setKeySequence(hotkeys_->screenshotShortcut());
    videoKeys_->setKeySequence(hotkeys_->videoShortcut());
    setGlobalShortcutStatus(hotkeys_->statusText());
  }
}
void MainWindow::setGlobalShortcutStatus(const QString &text) {
  hotkeyStatus_->setText(text);
}
void MainWindow::savePreferences() {
  QSettings s;
  s.setValue("imageFolder", imageFolder_->text());
  s.setValue("videoFolder", videoFolder_->text());
  s.setValue("cursor", cursor_->isChecked());
  s.setValue("clicks", clicks_->isChecked());
  s.setValue("quality", quality_->currentIndex());
  s.setValue("resolution", resolution_->currentIndex());
  s.setValue("format", format_->currentIndex());
  s.setValue("fps", fps_->value());
  s.setValue("shotDelay", delay_->value());
  s.setValue("askVideoPath", askVideoPath_->isChecked());
  s.setValue("screenshotCursor", screenshotCursor_->isChecked());
  s.setValue("microphone", microphone_->currentData().toString());
  s.setValue("systemAudio", systemAudio_->text());
  s.setValue("themeIndex", theme_->currentIndex());
  s.setValue("cameraDevice", cameraDevice_->text());
}
void MainWindow::chooseFolder(QLineEdit *edit, const QString &key) {
  auto dir = QFileDialog::getExistingDirectory(
      this, QStringLiteral("Выберите папку сохранения"), edit->text());
  if (!dir.isEmpty()) {
    edit->setText(dir);
    QSettings().setValue(key, dir);
    updateSummary();
  }
}
void MainWindow::acquireSnapshot(QScreen *screen,
                                 std::function<void(QPixmap)> callback) {
  QString program;
  QStringList args;
  auto temporary = std::make_shared<QTemporaryDir>();
  if (!temporary->isValid()) {
    show();
    QMessageBox::warning(
        this, QStringLiteral("Нет временной папки"),
        QStringLiteral("Не удалось подготовить локальный снимок."));
    return;
  }
  QString path = temporary->filePath("screen.png");
  auto geometry = screen->geometry();
#ifdef Q_OS_MACOS
  program = QCoreApplication::applicationDirPath() + "/kadrolov-capture";
  if (!QFileInfo::exists(program))
    program = QStandardPaths::findExecutable("kadrolov-capture");
  if (program.isEmpty()) {
    show();
    QMessageBox::warning(
        this, QStringLiteral("Нужен системный захват"),
        QStringLiteral("В этой сборке не найден kadrolov-capture. Используйте "
                       "полный пакет приложения."));
    return;
  }
  args = QStringList{"--screenshot",
                     path,
                     "--rect",
                     QString("%1,%2,%3,%4")
                         .arg(geometry.x())
                         .arg(geometry.y())
                         .arg(geometry.width())
                         .arg(geometry.height()),
                     "--cursor",
                     screenshotCursor_->isChecked() ? "1" : "0",
                     "--exclude-pid",
                     QString::number(QCoreApplication::applicationPid())};
#elif defined(Q_OS_LINUX)
  if (qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland") {
    QString script = qEnvironmentVariable("KADROLOV_PORTAL_HELPER");
    if (script.isEmpty())
      script = QCoreApplication::applicationDirPath() + "/portal_capture.py";
    if (!QFileInfo::exists(script)) {
      show();
      QMessageBox::warning(
          this, QStringLiteral("Нужен портал Wayland"),
          QStringLiteral("Не найден portal_capture.py. Установите полный пакет "
                         "или задайте KADROLOV_PORTAL_HELPER."));
      return;
    }
    program = QStandardPaths::findExecutable("python3");
    args = QStringList{script, "--screenshot", path};
  } else {
    callback(screen->grabWindow(0));
    return;
  }
#else
  callback(screen->grabWindow(0));
  return;
#endif
  if (program.isEmpty()) {
    show();
    QMessageBox::warning(
        this, QStringLiteral("Захват недоступен"),
        QStringLiteral("Не найден системный механизм захвата."));
    return;
  }
  auto *process = new QProcess(this);
  QTimer::singleShot(60000, process, [process] {
    if (process->state() != QProcess::NotRunning) {
      process->setProperty("timedOut", true);
      process->kill();
    }
  });
  connect(process, &QProcess::errorOccurred, this,
          [this, process](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
              return;
            show();
            QMessageBox::warning(this, QStringLiteral("Захват не запустился"),
                                 process->errorString());
            process->deleteLater();
          });
  connect(
      process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
      [this, process, temporary, path, screen,
       callback](int code, QProcess::ExitStatus status) {
        QString message;
        if (process->property("timedOut").toBool())
          message = QStringLiteral("Захват не ответил за минуту. Проверьте "
                                   "системное разрешение и повторите попытку.");
        auto lines = process->readAllStandardOutput().split('\n');
        for (const auto &line : lines) {
          auto object = QJsonDocument::fromJson(line).object();
          if (object.value("event").toString() == "error")
            message = object.value("message").toString();
        }
        if (message.isEmpty())
          message =
              QString::fromUtf8(process->readAllStandardError()).left(800);
        process->deleteLater();
        QPixmap snapshot(path);
        if (code != 0 || status != QProcess::NormalExit || snapshot.isNull()) {
          show();
          raise();
          showCaptureError(
              message.isEmpty()
                  ? QStringLiteral("Разрешите Кадролову запись экрана в "
                                   "системных настройках и повторите захват.")
                  : message);
          return;
        }
        qreal ratio =
            qreal(snapshot.width()) / qMax(1, screen->geometry().width());
        snapshot.setDevicePixelRatio(ratio);
        callback(snapshot);
      });
  process->start(program, args);
}
void MainWindow::takeScreenshot() {
  savePreferences();
  int source = shotSource_->currentIndex();
#ifdef Q_OS_MACOS
  if (source == 3) {
    selectWindowScreenshot();
    return;
  }
#endif

  if (source == 0) {
    selectArea(false);
    return;
  }
  auto *screen = selectedScreen(false);
  QRect region = source == 2 ? lastRegion_ : screen->geometry();
  if (region.isEmpty() || !screen->geometry().contains(region)) {
    QMessageBox::information(
        this, QStringLiteral("Выберите область"),
        QStringLiteral("Последняя область недоступна на этом мониторе. "
                       "Выделите её снова."));
    selectArea(false);
    return;
  }
  hide();
  QTimer::singleShot(
      delay_->value() * 1000 + 250, this, [this, screen, region] {
        acquireSnapshot(screen, [this, screen, region](QPixmap pix) {
          openCaptureOverlay(screen, pix, region, false);
        });
      });
}
void MainWindow::selectArea(bool video) {
  auto *screen = selectedScreen(video);
  hide();
  QTimer::singleShot(
      video ? 250 : delay_->value() * 1000 + 250, this, [this, video, screen] {
        acquireSnapshot(screen, [this, video, screen](QPixmap snapshot) {
          openCaptureOverlay(screen, snapshot, QRect(), video);
        });
      });
}
void MainWindow::openCaptureOverlay(QScreen *screen, const QPixmap &snapshot,
                                    const QRect &initial, bool video) {
  QImage image = snapshot.toImage();
  image.setDevicePixelRatio(1);
  if (image.isNull()) {
    show();
    QMessageBox::warning(
        this, QStringLiteral("Нет изображения"),
        QStringLiteral("Операционная система не передала изображение экрана."));
    return;
  }
  auto *editor = new Editor(image);
  editor->setDefaultSaveDirectory(imageFolder_->text());
  editor->enableOverlay(screen, initial, video);
  connect(editor, &Editor::saved, this, &MainWindow::rememberFile);
  connect(editor, &Editor::videoRegionRequested, this,
          [this](const QRect &region, QScreen *selected) {
            lastRegion_ = region;
            QSettings().setValue("lastRegion", region);
            QTimer::singleShot(80, this, [this, region, selected] {
              beginRecording(selected, region);
            });
          });
  connect(editor, &Editor::overlayFinished, this, [this, editor] {
    auto region = editor->overlaySelectionGlobal();
    if (!region.isEmpty()) {
      lastRegion_ = region;
      QSettings().setValue("lastRegion", region);
    }
  });
  editor->show();
  editor->raise();
  editor->activateWindow();
}
void MainWindow::openEditor(const QImage &image) {
  auto *editor = new Editor(image);
  editor->setDefaultSaveDirectory(imageFolder_->text());
  connect(editor, &Editor::saved, this, &MainWindow::rememberFile);
  editor->show();
  editor->raise();
  editor->activateWindow();
}
void MainWindow::startRecording() {
  if (recorder_->state() != Recorder::Idle &&
      recorder_->state() != Recorder::Failed)
    return;
  savePreferences();
  auto mode = audio_->currentData().toString();
  if ((mode == "system" || mode == "both") &&
      !Recorder::supportsSystemAudio()) {
    QMessageBox::information(this, QStringLiteral("Системный звук недоступен"),
                             Recorder::capabilityNote());
    return;
  }
  if ((mode == "mic" || mode == "both") &&
      microphone_->currentData(Qt::UserRole + 1).isValid() &&
      !microphone_->currentData(Qt::UserRole + 1).toBool()) {
    QMessageBox::warning(
        this, QStringLiteral("Микрофон недоступен"),
        QStringLiteral("Выбранный микрофон отключён. Подключите его и нажмите "
                       "«Обновить» или выберите другое устройство."));
    return;
  }
  int source = videoSource_->currentIndex();
  if (source == 0) {
    selectArea(true);
    return;
  }
  auto *screen = selectedScreen(true);
  auto r = source == 2 ? lastRegion_ : screen->geometry();
  if (r.isEmpty() || !screen->geometry().contains(r)) {
    selectArea(true);
    return;
  }
  hide();
  QTimer::singleShot(250, this, [this, screen, r] {
    acquireSnapshot(screen, [this, screen, r](QPixmap snapshot) {
      openCaptureOverlay(screen, snapshot, r, true);
    });
  });
}
void MainWindow::beginRecording(QScreen *screen, const QRect &region) {
  auto folder = videoFolder_->text();
  if (!QDir().mkpath(folder)) {
    show();
    QMessageBox::warning(
        this, QStringLiteral("Папка недоступна"),
        QStringLiteral("Выберите доступную папку для видео в настройках."));
    return;
  }
  auto ext = format_->currentText().toLower();
  auto stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss-zzz");
  auto name = QStringLiteral("Кадролов_%1.%2").arg(stamp, ext);
  QString path = QDir(folder).filePath(name);
  for (int suffix = 1; QFileInfo::exists(path); ++suffix)
    path = QDir(folder).filePath(
        QStringLiteral("Кадролов_%1_%2.%3").arg(stamp).arg(suffix).arg(ext));
  if (askVideoPath_->isChecked()) {
    path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Куда сохранить запись"), path,
        QStringLiteral("Видео (*.%1)").arg(ext));
    if (path.isEmpty()) {
      show();
      return;
    }
    if (QFileInfo(path).suffix().isEmpty())
      path += "." + ext;
  }
  CaptureOptions o;
  o.screen = screen;
  o.region = region;
  o.outputPath = path;
  o.format = ext;
  o.audioMode = audio_->currentData().toString();
  o.microphoneDevice = microphone_->currentData().toString();
  o.systemAudioDevice = systemAudio_->text().trimmed();
  o.fps = fps_->value();
  o.quality = quality_->currentData().toInt();
  o.showCursor = cursor_->isChecked();
  o.highlightClicks = clicks_->isEnabled() && clicks_->isChecked();
  o.camera = camera_->isChecked();
  o.cameraDevice = cameraDevice_->text().trimmed();
  int max = resolution_->currentData().toInt();
  qreal d = screen->devicePixelRatio();
  o.width = qRound(region.width() * d);
  o.height = qRound(region.height() * d);
  if (max > 0 && o.height > max) {
    o.width = qRound(qreal(o.width) * max / o.height);
    o.height = max;
  }
  o.width = qMax(2, o.width / 2 * 2);
  o.height = qMax(2, o.height / 2 * 2);
  panel_->setCaptureRegion(region, screen);
  auto *countdown =
      new QProgressDialog(QStringLiteral("Запись начнётся через 3 секунды"),
                          QStringLiteral("Отмена"), 0, 3, this);
  countdown->setWindowTitle(QStringLiteral("Готовимся к записи"));
  countdown->setWindowModality(Qt::ApplicationModal);
  countdown->setMinimumDuration(0);
  countdown->setValue(0);
  auto *timer = new QTimer(countdown);
  timer->setInterval(1000);
  auto remaining = std::make_shared<int>(3);
  connect(timer, &QTimer::timeout, this,
          [this, countdown, timer, remaining, o] {
            --*remaining;
            if (countdown->wasCanceled()) {
              timer->stop();
              countdown->deleteLater();
              show();
              return;
            }
            if (*remaining <= 0) {
              timer->stop();
              countdown->close();
              countdown->deleteLater();
              hide();
              QTimer::singleShot(200, this, [this, o] {
                if (!recorder_->start(o))
                  show();
              });
              return;
            }
            countdown->setLabelText(
                QStringLiteral("Запись начнётся через %1 сек").arg(*remaining));
            countdown->setValue(3 - *remaining);
          });
  timer->start();
  countdown->show();
}
void MainWindow::rememberFile(const QString &path) {
  files_.removeAll(path);
  files_.prepend(path);
  while (files_.size() > 300)
    files_.removeLast();
  QSettings().setValue("history", files_);
  refreshHistory();
}
void MainWindow::refreshHistory() {
  history_->clear();
  auto query = search_->text();
  QFileIconProvider icons;
  for (const auto &path : files_) {
    QFileInfo file(path);
    if (!file.fileName().contains(query, Qt::CaseInsensitive))
      continue;
    auto *i = new QListWidgetItem;
    QPixmap preview(path);
    if (!preview.isNull())
      i->setIcon(QIcon(preview.scaled(96, 64, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation)));
    else
      i->setIcon(icons.icon(file));
    QString detail =
        file.exists()
            ? QStringLiteral("%1 · %2 КБ")
                  .arg(file.lastModified().toString("dd.MM.yyyy HH:mm"))
                  .arg(file.size() / 1024)
            : QStringLiteral("Файл перемещён или удалён");
    i->setText(file.fileName() + "\n" + detail);
    i->setToolTip(path);
    i->setData(Qt::UserRole, path);
    history_->addItem(i);
  }
  if (files_.isEmpty()) {
    auto *i = new QListWidgetItem(
        QStringLiteral("Здесь появятся сохранённые снимки и видео"));
    i->setFlags(Qt::NoItemFlags);
    history_->addItem(i);
  }
}
void MainWindow::openSelected() {
  auto *i = history_->currentItem();
  if (!i)
    return;
  auto path = i->data(Qt::UserRole).toString();
  if (path.isEmpty())
    return;
  if (!QFileInfo::exists(path)) {
    QMessageBox::information(
        this, QStringLiteral("Файл не найден"),
        QStringLiteral("Возможно, файл был перемещён. История хранит путь, а "
                       "не копию файла."));
    return;
  }
  QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
void MainWindow::revealSelected() {
  auto *i = history_->currentItem();
  if (i && !i->data(Qt::UserRole).toString().isEmpty())
    QDesktopServices::openUrl(QUrl::fromLocalFile(
        QFileInfo(i->data(Qt::UserRole).toString()).absolutePath()));
}
void MainWindow::updateSummary() {
  summary_->setText(
      QStringLiteral("%1 · %2 · %3 · %4 · %5 кадр/с\nСохранение: %6")
          .arg(videoSource_->currentText(), audio_->currentText(),
               cursor_->isChecked() ? QStringLiteral("курсор включён")
                                    : QStringLiteral("без курсора"),
               format_->currentText())
          .arg(fps_->value())
          .arg(videoFolder_->text()));
}
void MainWindow::showExistingWindow() {
  dockActivationArmed_ = true;
  showNormal();
  raise();
  activateWindow();
}
void MainWindow::closeEvent(QCloseEvent *e) {
  savePreferences();
  if (exitAllowed_ || qApp->isSavingSession()) {
    if (qApp->isSavingSession() && recorder_->state() != Recorder::Idle &&
        recorder_->state() != Recorder::Failed)
      recorder_->stop();
    QMainWindow::closeEvent(e);
    return;
  }
  hide();
  e->ignore();
}
bool MainWindow::eventFilter(QObject *object, QEvent *event) {
  if (object == qApp && event->type() == QEvent::Quit && !exitAllowed_ &&
      !qApp->isSavingSession()) {
    requestExit();
    return true;
  }
#ifdef Q_OS_MACOS
  // Login launches may receive an initial activation despite --background.
  // A later deactivate/activate cycle represents a deliberate Dock reopen.
  if (object == qApp && event->type() == QEvent::ApplicationDeactivate)
    dockActivationArmed_ = true;
  if (object == qApp && event->type() == QEvent::ApplicationActivate &&
      dockActivationArmed_ && !isVisible() && !QApplication::activeWindow())
    showExistingWindow();
#endif
  return QMainWindow::eventFilter(object, event);
}
void MainWindow::requestExit() {
  if (exitAfterSave_)
    return;
  const auto state = recorder_->state();
  if (state != Recorder::Idle && state != Recorder::Failed) {
    QMessageBox question(
        QMessageBox::Question, QStringLiteral("Завершить Кадролов?"),
        QStringLiteral("Остановить запись, сохранить видео и выйти?"),
        QMessageBox::Yes | QMessageBox::Cancel, this);
    question.button(QMessageBox::Yes)
        ->setText(QStringLiteral("Сохранить и выйти"));
    question.button(QMessageBox::Cancel)->setText(QStringLiteral("Отмена"));
    question.setDefaultButton(QMessageBox::Cancel);
    if (question.exec() != QMessageBox::Yes)
      return;
    exitAfterSave_ = true;
    if (state != Recorder::Finalizing)
      recorder_->stop();
    return;
  }
  savePreferences();
  exitAllowed_ = true;
  emit exitReady();
}

void MainWindow::trimSelected() {
  auto *item = history_->currentItem();
  if (!item)
    return;
  QString input = item->data(Qt::UserRole).toString();
  QFileInfo source(input);
  if (!source.exists()) {
    QMessageBox::information(
        this, QStringLiteral("Выберите видео"),
        QStringLiteral("Сначала выберите существующий видеофайл в истории."));
    return;
  }
  QString ext = source.suffix().toLower();
  if (ext != "mp4" && ext != "mkv" && ext != "webm" && ext != "mov") {
    QMessageBox::information(this, QStringLiteral("Это изображение"),
                             QStringLiteral("Для обрезки выберите видеофайл."));
    return;
  }
  QString ffmpeg = Recorder::ffmpegPath();
  if (ffmpeg.isEmpty()) {
    QMessageBox::information(
        this, QStringLiteral("Нужен FFmpeg"),
        QStringLiteral("Для локальной обрезки установите FFmpeg или "
                       "используйте пакет Кадролова с FFmpeg."));
    return;
  }
  QDialog range(this);
  range.setWindowTitle(QStringLiteral("Обрезать видео"));
  auto *form = new QFormLayout(&range);
  QDoubleSpinBox start, end;
  for (auto *spin : {&start, &end}) {
    spin->setRange(0, 360000);
    spin->setDecimals(2);
    spin->setSuffix(QStringLiteral(" сек"));
  }
  end.setValue(10);
  form->addRow(QStringLiteral("Начало"), &start);
  form->addRow(QStringLiteral("Конец"), &end);
  auto *hint = new QLabel(QStringLiteral(
      "Исходное видео сохранится. Будет создана новая MP4-копия."));
  hint->setWordWrap(true);
  form->addRow(hint);
  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  buttons.button(QDialogButtonBox::Ok)->setText(QStringLiteral("Обрезать"));
  buttons.button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
  form->addRow(&buttons);
  connect(&buttons, &QDialogButtonBox::accepted, &range, &QDialog::accept);
  connect(&buttons, &QDialogButtonBox::rejected, &range, &QDialog::reject);
  if (range.exec() != QDialog::Accepted)
    return;
  if (end.value() <= start.value()) {
    QMessageBox::warning(this, QStringLiteral("Проверьте время"),
                         QStringLiteral("Конец должен быть позже начала."));
    return;
  }
  QString output = QFileDialog::getSaveFileName(
      this, QStringLiteral("Сохранить обрезанную копию"),
      QDir(source.absolutePath())
          .filePath(source.completeBaseName() +
                    QStringLiteral("_фрагмент.mp4")),
      QStringLiteral("MP4 (*.mp4)"));
  if (output.isEmpty())
    return;
  if (QFileInfo(output).suffix().isEmpty())
    output += ".mp4";
  if (QFileInfo(output).absoluteFilePath() == source.absoluteFilePath()) {
    QMessageBox::warning(this, QStringLiteral("Выберите другое имя"),
                         QStringLiteral("Исходное видео не перезаписывается."));
    return;
  }
  QString temporary = output + QStringLiteral(".part.mp4");
  if (QFileInfo::exists(temporary)) {
    QMessageBox::warning(
        this, QStringLiteral("Временный файл уже существует"),
        QStringLiteral(
            "Выберите другое имя, чтобы сохранить предыдущий результат."));
    return;
  }
  auto *process = new QProcess(this);
  auto *progress = new QProgressDialog(
      QStringLiteral("Создаём обрезанную копию на устройстве…"),
      QStringLiteral("Отмена"), 0, 0, this);
  progress->setMinimumDuration(0);
  progress->setWindowTitle(QStringLiteral("Обрезка видео"));
  progress->setAutoClose(false);
  connect(progress, &QProgressDialog::canceled, process, &QProcess::kill);
  connect(
      process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
      [this, process, progress, output,
       temporary](int code, QProcess::ExitStatus status) {
        bool cancelled = progress->wasCanceled();
        progress->close();
        progress->deleteLater();
        QString error =
            QString::fromUtf8(process->readAllStandardError()).right(1000);
        process->deleteLater();
        if (code != 0 || status != QProcess::NormalExit) {
          QFile::remove(temporary);
          if (!cancelled)
            QMessageBox::warning(
                this, QStringLiteral("Не удалось обрезать видео"), error);
          return;
        }
        QFile part(temporary);
        QSaveFile destination(output);
        if (QFileInfo(temporary).size() < 1024 ||
            !part.open(QIODevice::ReadOnly) ||
            !destination.open(QIODevice::WriteOnly)) {
          QMessageBox::warning(
              this, QStringLiteral("Не удалось сохранить копию"),
              QStringLiteral(
                  "Проверьте диапазон времени и папку. Временный результат: %1")
                  .arg(temporary));
          return;
        }
        while (!part.atEnd()) {
          QByteArray chunk = part.read(1024 * 1024);
          if (chunk.isEmpty() && part.error() != QFile::NoError) {
            destination.cancelWriting();
            return;
          }
          if (destination.write(chunk) != chunk.size()) {
            destination.cancelWriting();
            QMessageBox::warning(this,
                                 QStringLiteral("Не удалось сохранить копию"),
                                 destination.errorString());
            return;
          }
        }
        if (!destination.commit()) {
          QMessageBox::warning(this,
                               QStringLiteral("Не удалось сохранить копию"),
                               destination.errorString());
          return;
        }
        part.close();
        QFile::remove(temporary);
        rememberFile(output);
        statusBar()->showMessage(
            QStringLiteral("Фрагмент сохранён: %1").arg(output), 8000);
      });
  connect(process, &QProcess::errorOccurred, this,
          [this, process, progress](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
              return;
            progress->close();
            QMessageBox::warning(this, QStringLiteral("FFmpeg не запустился"),
                                 process->errorString());
          });
  QStringList args = QStringList{
      "-hide_banner", "-loglevel",
      "error",        "-n",
      "-ss",          QString::number(start.value(), 'f', 2),
      "-i",           input,
      "-t",           QString::number(end.value() - start.value(), 'f', 2),
      "-c:v",         "libx264",
      "-preset",      "veryfast",
      "-crf",         "23",
      "-c:a",         "aac",
      "-movflags",    "+faststart",
      temporary};
  process->start(ffmpeg, args);
  progress->show();
}

void MainWindow::refreshMicrophones() {
  QString selected = microphone_->currentData().toString();
  if (selected.isEmpty() && !savedMicrophoneId_.isEmpty())
    selected = savedMicrophoneId_;
  QSignalBlocker blocker(microphone_);
  microphone_->clear();
  microphone_->addItem(QStringLiteral("Системный микрофон"), QString());
  microphone_->setItemData(0, true, Qt::UserRole + 1);
  const auto devices = DeviceInventory::microphones();
  for (const auto &device : devices) {
    if (device.id.isEmpty())
      continue;
    microphone_->addItem(device.name, device.id);
    microphone_->setItemData(microphone_->count() - 1, true, Qt::UserRole + 1);
  }
  int index = microphone_->findData(selected);
  if (index < 0 && !selected.isEmpty()) {
    microphone_->addItem(
        QStringLiteral("Недоступный микрофон — подключите устройство"),
        selected);
    index = microphone_->count() - 1;
    microphone_->setItemData(index, false, Qt::UserRole + 1);
    if (auto *model = qobject_cast<QStandardItemModel *>(microphone_->model()))
      if (auto *item = model->item(index))
        item->setEnabled(false);
  }
  microphone_->setCurrentIndex(qMax(0, index));
}

void MainWindow::requestScreenPermission() {
#ifdef Q_OS_MACOS
  const QUrl settings(
      QStringLiteral("x-apple.systempreferences:com.apple.preference.security?"
                     "Privacy_ScreenCapture"));
  if (permissionRequestRunning_)
    return;
  QString helper = QCoreApplication::applicationDirPath() +
                   QStringLiteral("/kadrolov-capture");
  if (!QFileInfo::exists(helper))
    helper = QStandardPaths::findExecutable(QStringLiteral("kadrolov-capture"));
  if (helper.isEmpty()) {
    QDesktopServices::openUrl(settings);
    return;
  }
  permissionRequestRunning_ = true;
  auto *process = new QProcess(this);
  auto openSettings = [this, process, settings] {
    permissionRequestRunning_ = false;
    QDesktopServices::openUrl(settings);
    statusBar()->showMessage(
        QStringLiteral(
            "После включения доступа полностью перезапустите Кадролов."),
        15000);
    process->deleteLater();
  };
  connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, [openSettings](int, QProcess::ExitStatus) { openSettings(); });
  connect(process, &QProcess::errorOccurred, this,
          [openSettings](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
              openSettings();
          });
  process->start(helper, QStringList{QStringLiteral("--request-permission")});
#else
  QMessageBox::information(
      this, QStringLiteral("Разрешение записи экрана"),
      QStringLiteral(
          "Доступом к захвату управляет ваша операционная система."));
#endif
}
void MainWindow::showCaptureError(const QString &message) {
#ifdef Q_OS_MACOS
  bool permission =
      message.contains(QStringLiteral("разреш"), Qt::CaseInsensitive) ||
      message.contains(QStringLiteral("permission"), Qt::CaseInsensitive) ||
      message.contains(QStringLiteral("not authorized"), Qt::CaseInsensitive);
  if (permission) {
    QMessageBox box(QMessageBox::Warning,
                    QStringLiteral("Нужен доступ к экрану"), message,
                    QMessageBox::Close, this);
    box.setInformativeText(QStringLiteral(
        "Включите Кадролов в «Конфиденциальность и безопасность → Запись "
        "экрана». После изменения полностью перезапустите программу."));
    auto *settings = box.addButton(QStringLiteral("Открыть настройки"),
                                   QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == settings)
      requestScreenPermission();
    return;
  }
#endif
  QMessageBox::warning(this, QStringLiteral("Не удалось захватить экран"),
                       message);
}

void MainWindow::selectWindowScreenshot() {
#ifdef Q_OS_MACOS
  if (windowListing_)
    return;
  QString helper = QCoreApplication::applicationDirPath() +
                   QStringLiteral("/kadrolov-capture");
  if (!QFileInfo::exists(helper))
    helper = QStandardPaths::findExecutable(QStringLiteral("kadrolov-capture"));
  if (helper.isEmpty()) {
    showCaptureError(QStringLiteral("Не найден системный модуль снимков. "
                                    "Используйте полный пакет Кадролова."));
    return;
  }
  windowListing_ = true;
  auto *process = new QProcess(this);
  QTimer::singleShot(15000, process, [process] {
    if (process->state() != QProcess::NotRunning)
      process->kill();
  });
  connect(process, &QProcess::errorOccurred, this,
          [this, process](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
              return;
            windowListing_ = false;
            showCaptureError(process->errorString());
            process->deleteLater();
          });
  connect(
      process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
      [this, process](int code, QProcess::ExitStatus status) {
        QJsonArray windows;
        QString error;
        for (const auto &line : process->readAllStandardOutput().split('\n')) {
          auto object = QJsonDocument::fromJson(line).object();
          if (object.value("event").toString() == "windows")
            windows = object.value("windows").toArray();
          if (object.value("event").toString() == "error")
            error = object.value("message").toString();
        }
        process->deleteLater();
        if (code != 0 || status != QProcess::NormalExit) {
          windowListing_ = false;
          showCaptureError(
              error.isEmpty()
                  ? QStringLiteral("Не удалось получить список окон. Проверьте "
                                   "разрешение записи экрана.")
                  : error);
          return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Выберите окно для снимка"));
        dialog.resize(580, 420);
        auto *layout = new QVBoxLayout(&dialog);
        auto *description =
            new QLabel(QStringLiteral("Снимок захватит только выбранное окно, "
                                      "даже если оно перекрыто другим."));
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *list = new QListWidget(&dialog);
        list->setSpacing(3);
        for (const auto &value : windows) {
          auto object = value.toObject();
          if (object.value("pid").toInt() == QCoreApplication::applicationPid())
            continue;
          auto application = object.value("application").toString();
          auto title = object.value("title").toString();
          if (application.isEmpty() && title.isEmpty())
            continue;
          auto *item = new QListWidgetItem(
              application + (title.isEmpty() ? QStringLiteral(" — без названия")
                                             : QStringLiteral(" — ") + title),
              list);
          item->setData(Qt::UserRole, object.toVariantMap());
        }
        layout->addWidget(list, 1);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                             QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Ok)
            ->setText(QStringLiteral("Снять окно"));
        buttons->button(QDialogButtonBox::Cancel)
            ->setText(QStringLiteral("Отмена"));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(list->count() > 0);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog,
                &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog,
                &QDialog::reject);
        connect(list, &QListWidget::itemDoubleClicked, &dialog,
                [&dialog](QListWidgetItem *) { dialog.accept(); });
        if (list->count() > 0)
          list->setCurrentRow(0);
        else
          description->setText(
              QStringLiteral("Доступных окон нет. Откройте нужную программу и "
                             "повторите выбор."));
        if (dialog.exec() != QDialog::Accepted || !list->currentItem()) {
          windowListing_ = false;
          return;
        }
        auto object = QJsonObject::fromVariantMap(
            list->currentItem()->data(Qt::UserRole).toMap());
        QRect frame =
            QRectF(object.value("x").toDouble(), object.value("y").toDouble(),
                   object.value("width").toDouble(),
                   object.value("height").toDouble())
                .toAlignedRect();
        QScreen *screen = nullptr;
        for (auto *candidate : QGuiApplication::screens())
          if (candidate->geometry().contains(frame)) {
            screen = candidate;
            break;
          }
        windowListing_ = false;
        if (!screen) {
          QMessageBox::information(
              this, QStringLiteral("Окно на нескольких мониторах"),
              QStringLiteral(
                  "Для снимка отдельного окна переместите его целиком на один "
                  "монитор. Либо используйте снимок области."));
          return;
        }
        qint64 id = object.value("id").toVariant().toLongLong();
        hide();
        QTimer::singleShot(delay_->value() * 1000 + 250, this,
                           [this, id, frame, screen] {
                             captureWindowScreenshot(id, frame, screen);
                           });
      });
  process->start(helper, QStringList{QStringLiteral("--list-windows")});
#endif
}
void MainWindow::captureWindowScreenshot(qint64 windowId, const QRect &frame,
                                         QScreen *screen) {
#ifdef Q_OS_MACOS
  auto temporary = std::make_shared<QTemporaryDir>();
  if (!temporary->isValid()) {
    show();
    showCaptureError(
        QStringLiteral("Не удалось создать временный файл снимка."));
    return;
  }
  QString output = temporary->filePath(QStringLiteral("window.png"));
  QString helper = QCoreApplication::applicationDirPath() +
                   QStringLiteral("/kadrolov-capture");
  if (!QFileInfo::exists(helper))
    helper = QStandardPaths::findExecutable(QStringLiteral("kadrolov-capture"));
  if (helper.isEmpty()) {
    show();
    showCaptureError(QStringLiteral("Не найден системный модуль снимков."));
    return;
  }
  auto geometry = screen->geometry();
  QStringList args{QStringLiteral("--screenshot"),
                   output,
                   QStringLiteral("--window"),
                   QString::number(windowId),
                   QStringLiteral("--rect"),
                   QString("%1,%2,%3,%4")
                       .arg(geometry.x())
                       .arg(geometry.y())
                       .arg(geometry.width())
                       .arg(geometry.height()),
                   QStringLiteral("--cursor"),
                   screenshotCursor_->isChecked() ? QStringLiteral("1")
                                                  : QStringLiteral("0")};
  auto *process = new QProcess(this);
  QTimer::singleShot(20000, process, [process] {
    if (process->state() != QProcess::NotRunning)
      process->kill();
  });
  connect(process, &QProcess::errorOccurred, this,
          [this, process](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
              return;
            show();
            showCaptureError(process->errorString());
            process->deleteLater();
          });
  connect(
      process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
      [this, process, temporary, output, frame,
       screen](int code, QProcess::ExitStatus status) {
        QString error;
        for (const auto &line : process->readAllStandardOutput().split('\n')) {
          auto object = QJsonDocument::fromJson(line).object();
          if (object.value("event").toString() == "error")
            error = object.value("message").toString();
        }
        process->deleteLater();
        QImage window(output);
        if (code != 0 || status != QProcess::NormalExit || window.isNull()) {
          show();
          showCaptureError(
              error.isEmpty()
                  ? QStringLiteral("Выбранное окно не передало изображение. "
                                   "Повторите выбор окна.")
                  : error);
          return;
        }
        acquireSnapshot(screen, [this, screen, frame,
                                 window](QPixmap background) {
          qreal scale =
              qreal(background.width()) / qMax(1, screen->geometry().width());
          QSize expected(qRound(frame.width() * scale),
                         qRound(frame.height() * scale));
          if (qAbs(window.width() - expected.width()) > 2 ||
              qAbs(window.height() - expected.height()) > 2) {
            show();
            showCaptureError(QStringLiteral("Размер окна изменился во время "
                                            "снимка. Повторите выбор окна."));
            return;
          }
          QImage full = background.toImage();
          full.setDevicePixelRatio(1);
          QPoint local = frame.topLeft() - screen->geometry().topLeft();
          QRectF target(local.x() * scale, local.y() * scale,
                        frame.width() * scale, frame.height() * scale);
          QPainter painter(&full);
          painter.setCompositionMode(QPainter::CompositionMode_Source);
          painter.drawImage(target, window);
          painter.end();
          QPixmap composite = QPixmap::fromImage(full);
          composite.setDevicePixelRatio(scale);
          openCaptureOverlay(screen, composite, frame, false);
        });
      });
  process->start(helper, args);
#else
  Q_UNUSED(windowId)
  Q_UNUSED(frame)
  Q_UNUSED(screen)
#endif
}
