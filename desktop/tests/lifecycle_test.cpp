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
    ApplicationInstance primary(id), secondary(id), background(id);
    QCOMPARE(primary.start(false), ApplicationInstance::Primary);
    QSignalSpy show(&primary, &ApplicationInstance::showRequested);
    QCOMPARE(secondary.start(false), ApplicationInstance::Secondary);
    QTRY_COMPARE(show.count(), 1);
    QCOMPARE(background.start(true), ApplicationInstance::Secondary);
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
