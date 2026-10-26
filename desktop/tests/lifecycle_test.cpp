#include "ApplicationInstance.h"
#include "MainWindow.h"
#include <QApplication>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QUuid>
#include <QtTest>
class LifecycleTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    qApp->setQuitOnLastWindowClosed(false);
    qApp->setApplicationName("KadrolovLifecycleTest");
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
