#include "ApplicationInstance.h"
#include "MainWindow.h"
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QScrollArea>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QTabWidget>
#include <QUuid>
#include <QtTest>
class LifecycleTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    qApp->setStyle(QStyleFactory::create("Fusion"));
    qApp->setQuitOnLastWindowClosed(false);
    qApp->setApplicationName("KadrolovLifecycleTest");
  }
  void themesRenderConsistentlyOverLightSystemPalette() {
    const auto originalPalette = qApp->palette();
    const auto originalStyleSheet = qApp->styleSheet();
    qApp->setStyleSheet({});
    QPalette light = qApp->style()->standardPalette();
    light.setColor(QPalette::Window, Qt::white);
    light.setColor(QPalette::Base, Qt::white);
    light.setColor(QPalette::WindowText, Qt::black);
    light.setColor(QPalette::Text, Qt::black);
    qApp->setPalette(light);
    QSettings settings;
    settings.clear();
    settings.setValue("imageFolder",
                      QDir::tempPath() + "/KadrolovThemeTest/Pictures");
    settings.setValue("videoFolder",
                      QDir::tempPath() + "/KadrolovThemeTest/Movies");
    settings.setValue("themeIndex", 1);
    bool darkSurfaces = true, lightSurfaces = true;
    const QString output = QDir::currentPath() + "/theme-screenshots";
    QVERIFY(QDir().mkpath(output));
    {
      MainWindow window;
      window.resize(1040, 820);
      window.show();
      auto *tabs = window.findChild<QTabWidget *>();
      QVERIFY(tabs);
      QComboBox *theme = nullptr;
      for (auto *combo : window.findChildren<QComboBox *>())
        if (combo->findData("dark") >= 0 && combo->findData("light") >= 0)
          theme = combo;
      QVERIFY(theme);
      auto snapshot = [&](const QString &name, bool dark) {
        QApplication::processEvents();
        if (!window.grab().save(output + "/" + name + ".png"))
          return false;
        auto *scroll = qobject_cast<QScrollArea *>(tabs->currentWidget());
        QWidget *surface = scroll ? scroll->viewport() : tabs->currentWidget();
        auto image = surface->grab().toImage();
        const QColor pixel =
            image.pixelColor(image.width() - 3, image.height() - 3);
        return dark ? pixel.lightness() < 90 : pixel.lightness() > 190;
      };
      theme->setCurrentIndex(theme->findData("dark"));
      for (int tab = 0; tab < tabs->count(); ++tab) {
        tabs->setCurrentIndex(tab);
        darkSurfaces &= snapshot(QStringLiteral("dark-tab-%1").arg(tab), true);
      }
      darkSurfaces &= qApp->palette().color(QPalette::Window).lightness() < 90;
      darkSurfaces &= qApp->palette().color(QPalette::Base).lightness() < 90;
      darkSurfaces &=
          qApp->palette().color(QPalette::WindowText).lightness() > 170;
      darkSurfaces &= qApp->palette()
                          .color(QPalette::Disabled, QPalette::Text)
                          .lightness() > 90;
      // Exercise native control glyphs against the app palette, independently
      // of the user's connected devices or checkbox preferences.
      QComboBox combo;
      combo.addItem(QStringLiteral("Источник"));
      combo.resize(180, 36);
      combo.ensurePolished();
      auto comboImage = combo.grab().toImage();
      const qreal ratio = comboImage.devicePixelRatio();
      int arrowPixels = 0;
      for (int y = int(8 * ratio); y < int(28 * ratio); ++y)
        for (int x = int(158 * ratio); x < int(176 * ratio); ++x)
          if (comboImage.pixelColor(x, y).lightness() > 140)
            ++arrowPixels;
      darkSurfaces &= arrowPixels > 4;
      QCheckBox checkbox(QStringLiteral("Курсор"));
      checkbox.resize(180, 32);
      checkbox.ensurePolished();
      QStyleOptionButton option;
      option.initFrom(&checkbox);
      auto indicator = checkbox.style()->subElementRect(
          QStyle::SE_CheckBoxIndicator, &option, &checkbox);
      auto checkImage = checkbox.grab().toImage();
      auto center = indicator.center() * checkImage.devicePixelRatio();
      darkSurfaces &= checkImage.pixelColor(center).lightness() < 90;
      checkbox.setChecked(true);
      auto checkedImage = checkbox.grab().toImage();
      darkSurfaces &= checkedImage != checkImage;
      auto barImage = window.menuBar()->grab().toImage();
      darkSurfaces &=
          barImage.pixelColor(barImage.width() - 3, barImage.height() - 3)
              .lightness() < 90;
      QMenu menu;
      menu.addAction(QStringLiteral("Проверка меню"));
      menu.ensurePolished();
      menu.adjustSize();
      auto menuImage = menu.grab().toImage();
      darkSurfaces &=
          menuImage.pixelColor(menuImage.width() - 3, menuImage.height() - 3)
              .lightness() < 90;
      theme->setCurrentIndex(theme->findData("light"));
      tabs->setCurrentIndex(3);
      lightSurfaces &= snapshot("light-settings-after-dark", false);
      lightSurfaces &= qApp->palette().color(QPalette::Text).lightness() < 90;
      theme->setCurrentIndex(theme->findData("dark"));
      darkSurfaces &= snapshot("dark-settings-after-light", true);
      theme->setCurrentIndex(theme->findData("light"));
      lightSurfaces &= snapshot("light-settings-final", false);
      window.hide();
    }
    qApp->setPalette(originalPalette);
    qApp->setStyleSheet(originalStyleSheet);
    QVERIFY2(darkSurfaces, "Dark surfaces, menus and text must stay dark/light "
                           "over a light OS palette");
    QVERIFY2(lightSurfaces,
             "Light surfaces and text must survive repeated theme switching");
  }
  void secondaryRaisesPrimaryButBackgroundDoesNot() {
    const QString id =
        "test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    ApplicationInstance primary(id);
    QCOMPARE(primary.start(false), ApplicationInstance::Primary);
    QSignalSpy show(&primary, &ApplicationInstance::showRequested);
    auto runSecondary = [&](bool background) {
      QProcess child;
      QStringList args{"--lifecycle-secondary-child", id};
      if (background)
        args << "--background";
      child.start(QCoreApplication::applicationFilePath(), args);
      if (!child.waitForStarted())
        return false;
      QElapsedTimer deadline;
      deadline.start();
      while (child.state() != QProcess::NotRunning && deadline.elapsed() < 7000)
        QTest::qWait(10); // Primary must accept/read while secondary waits.
      if (child.state() != QProcess::NotRunning) {
        child.kill();
        child.waitForFinished();
        return false;
      }
      if (child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0) {
        qWarning().noquote() << child.readAllStandardError();
        return false;
      }
      return true;
    };
    QVERIFY2(runSecondary(false),
             "Secondary child must receive primary acknowledgement");
    QCOMPARE(show.count(), 1);
    QVERIFY(runSecondary(true));
    QTest::qWait(30);
    QCOMPARE(show.count(), 1);
  }
  void explicitExitActuallyEndsApplicationProcess() {
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(),
                QStringList{"--lifecycle-exit-child", "--background"});
    QVERIFY(child.waitForStarted());
    QVERIFY2(child.waitForFinished(15000),
             qPrintable(child.readAllStandardError()));
    QCOMPARE(child.exitStatus(), QProcess::NormalExit);
    QCOMPARE(child.exitCode(), 0);
  }
  void exitRechecksRecorderAfterModalQuestion_data() {
    QTest::addColumn<int>("nextState");
    QTest::addColumn<bool>("accept");
    QTest::newRow("finished while deciding") << int(Recorder::Idle) << true;
    QTest::newRow("failed while deciding") << int(Recorder::Failed) << true;
    QTest::newRow("cancel keeps finalizing")
        << int(Recorder::Finalizing) << false;
  }
  void exitRechecksRecorderAfterModalQuestion() {
    QFETCH(int, nextState);
    QFETCH(bool, accept);
    MainWindow window;
    auto *recorder = window.findChild<Recorder *>();
    QVERIFY(recorder);
    recorder->setState(Recorder::Finalizing);
    QSignalSpy exit(&window, &MainWindow::exitReady);
    bool answered = false;
    QTimer::singleShot(0, [&] {
      auto *question =
          qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      if (!question)
        return;
      recorder->setState(Recorder::State(nextState));
      answered = true;
      question->button(accept ? QMessageBox::Yes : QMessageBox::Cancel)
          ->click();
    });
    window.requestExit();
    QVERIFY(answered);
    QCOMPARE(exit.count(), accept && nextState == Recorder::Idle ? 1 : 0);
    if (exit.isEmpty()) {
      // Failure and cancellation must not leave a pending-exit latch that
      // silently discards the user's next explicit request.
      recorder->setState(Recorder::Idle);
      window.requestExit();
      QCOMPARE(exit.count(), 1);
    }
  }
  void closingKeepsWindowAndEventLoopUntilExplicitExit() {
    MainWindow window;
    QSignalSpy exit(&window, &MainWindow::exitReady);
    window.show();
    QApplication::processEvents();
    window.close();
    QVERIFY(!window.isVisible());
    QCOMPARE(exit.count(), 0);
    bool eventLoopAlive = false;
    QTimer::singleShot(0, [&] { eventLoopAlive = true; });
    QTRY_VERIFY(eventLoopAlive);
    window.showExistingWindow();
    QVERIFY(window.isVisible());
    window.requestExit();
    QCOMPARE(exit.count(), 1);
  }
};
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  if (app.arguments().contains("--lifecycle-secondary-child")) {
    QStandardPaths::setTestModeEnabled(true);
    app.setApplicationName("KadrolovLifecycleTest");
    const int index = app.arguments().indexOf("--lifecycle-secondary-child");
    ApplicationInstance instance(app.arguments().value(index + 1));
    QString error;
    const auto result =
        instance.start(app.arguments().contains("--background"), &error);
    if (result != ApplicationInstance::Secondary) {
      fprintf(stderr, "Secondary result=%d: %s\n", int(result),
              qPrintable(error));
      return 31;
    }
    return 0;
  }
  if (app.arguments().contains("--lifecycle-exit-child")) {
    QStandardPaths::setTestModeEnabled(true);
    app.setApplicationName("KadrolovLifecycleChild");
    app.setQuitOnLastWindowClosed(false);
    MainWindow window;
    QObject::connect(&window, &MainWindow::exitReady, &app,
                     &QApplication::quit);
    QTimer::singleShot(0, [&] {
      QEvent initialActivation(QEvent::ApplicationActivate);
      QCoreApplication::sendEvent(&app, &initialActivation);
      if (window.isVisible())
        app.exit(21);
      else {
#ifdef Q_OS_MACOS
        QEvent deactivation(QEvent::ApplicationDeactivate);
        QCoreApplication::sendEvent(&app, &deactivation);
        QEvent dockActivation(QEvent::ApplicationActivate);
        QCoreApplication::sendEvent(&app, &dockActivation);
        if (!window.isVisible()) {
          app.exit(23);
          return;
        }
#else
        window.showExistingWindow();
#endif
        QTimer::singleShot(0, &window, &MainWindow::requestExit);
      }
    });
    QTimer::singleShot(10000, [&] { app.exit(22); });
    return app.exec();
  }
  LifecycleTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "lifecycle_test.moc"
