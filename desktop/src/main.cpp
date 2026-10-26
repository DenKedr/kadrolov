#include "ApplicationInstance.h"
#include "GlobalHotkeys.h"
#include "MainWindow.h"
#include <QApplication>
#include <QFontDatabase>
#include <QIcon>
#include <QMessageBox>
#include <QStyleFactory>
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  app.setApplicationName(QStringLiteral("Кадролов"));
  app.setOrganizationName(QStringLiteral("Kadrolov"));
  app.setOrganizationDomain(QStringLiteral("kadrolov.local"));
  app.setApplicationVersion("0.1.1");
  app.setWindowIcon(QIcon(QStringLiteral(":/icons/kadrolov.png")));
  app.setStyle(QStyleFactory::create("Fusion"));
  app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
  ApplicationInstance instance;
  QString instanceError;
  const auto instanceResult =
      instance.start(app.arguments().contains("--background"), &instanceError);
  if (instanceResult == ApplicationInstance::Secondary)
    return 0;
  if (instanceResult == ApplicationInstance::Error) {
    QMessageBox::warning(nullptr, QStringLiteral("Кадролов"), instanceError);
    return 1;
  }
  MainWindow window;
  QObject::connect(&instance, &ApplicationInstance::showRequested, &window,
                   &MainWindow::showExistingWindow);
  QObject::connect(&window, &MainWindow::exitReady, &app, &QApplication::quit);
  GlobalHotkeys hotkeys(&app);
  QObject::connect(&hotkeys, &GlobalHotkeys::screenshotRequested, &window,
                   &MainWindow::takeScreenshot);
  QObject::connect(&hotkeys, &GlobalHotkeys::recordingRequested, &window,
                   &MainWindow::startRecording);
  window.setHotkeys(&hotkeys);
  if (!app.arguments().contains("--background"))
    window.show();
  return app.exec();
}
