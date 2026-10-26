#include "Editor.h"
#include <QApplication>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QScreen>
#include <QSpinBox>
#include <QtTest>
class EditorTest : public QObject {
  Q_OBJECT
private slots:
  void redactionFlattensAndUndoRestoresPixels() {
    QImage source(100, 80, QImage::Format_ARGB32);
    source.fill(Qt::red);
    Editor editor(source);
    editor.addRedaction(QRect(10, 10, 30, 20), Qt::black);
    QCOMPARE(editor.flattenedImage().pixelColor(15, 15), QColor(Qt::black));
    QCOMPARE(editor.flattenedImage().pixelColor(5, 5), QColor(Qt::red));
    editor.undo();
    QCOMPARE(editor.flattenedImage().pixelColor(15, 15), QColor(Qt::red));
    editor.redo();
    QCOMPARE(editor.flattenedImage().pixelColor(15, 15), QColor(Qt::black));
  }
  void propertyChangePreservesRedactionColorAndCanUndo() {
    QImage source(100, 80, QImage::Format_ARGB32);
    source.fill(Qt::red);
    Editor editor(source);
    editor.addRedaction(QRect(10, 10, 30, 20), Qt::black);
    auto *canvas = editor.findChild<EditorCanvas *>();
    QVERIFY(canvas);
    QGraphicsRectItem *rect = nullptr;
    for (auto *i : canvas->scene()->items())
      if ((rect = qgraphicsitem_cast<QGraphicsRectItem *>(i)))
        break;
    QVERIFY(rect);
    rect->setSelected(true);
    int previous = rect->pen().width();
    auto *width = editor.findChild<QSpinBox *>("strokeWidth");
    QVERIFY(width);
    width->setValue(12);
    QCOMPARE(rect->pen().width(), 12);
    QCOMPARE(rect->brush().color(), QColor(Qt::black));
    editor.undo();
    QCOMPARE(rect->pen().width(), previous);
    QCOMPARE(rect->brush().color(), QColor(Qt::black));
  }
  void overlayExportsOnlySelectionWithoutDimOrResizeHandles() {
    auto *screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    QImage source(screen->geometry().size(), QImage::Format_ARGB32);
    source.fill(Qt::red);
    Editor editor(source);
    QRect local(40, 50, 120, 90);
    QRect global = local.translated(screen->geometry().topLeft());
    editor.enableOverlay(screen, global);
    QCOMPARE(editor.overlaySelectionGlobal(), global);
    QCOMPARE(editor.flattenedImage().size(), local.size());
    QCOMPARE(editor.flattenedImage().pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(editor.flattenedImage().pixelColor(119, 89), QColor(Qt::red));
    editor.addRedaction(QRect(60, 70, 20, 20), Qt::black);
    QCOMPARE(editor.flattenedImage().pixelColor(25, 25), QColor(Qt::black));
    QCOMPARE(editor.flattenedImage().pixelColor(10, 10), QColor(Qt::red));
  }
  void overlayToolbarStaysOnScreenAndMovesAboveBottomSelection() {
    auto *screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    auto bounds = screen->geometry();
    QImage source(bounds.size(), QImage::Format_ARGB32);
    source.fill(Qt::white);
    Editor editor(source);
    QRect local(80, bounds.height() - 110, 200, 90);
    editor.enableOverlay(screen, local.translated(bounds.topLeft()));
    editor.show();
    QApplication::processEvents();
    auto *toolbar = editor.findChild<QWidget *>("captureToolbar");
    QVERIFY(toolbar);
    QVERIFY(toolbar->isVisible());
    QVERIFY2(toolbar->x() >= 0, "toolbar left edge must remain visible");
    QVERIFY2(toolbar->geometry().right() < editor.width(),
             "toolbar save/cancel must not run off screen");
    QVERIFY2(toolbar->geometry().bottom() < local.top(),
             "toolbar must move above a selection at the bottom edge");
    editor.setAttribute(Qt::WA_DeleteOnClose, false);
    editor.close();
    QApplication::processEvents();
  }
  void overlayMouseSelectionAndResizeRemainUndoable() {
    auto *screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    QImage source(screen->geometry().size(), QImage::Format_ARGB32);
    source.fill(Qt::white);
    Editor editor(source);
    editor.setAttribute(Qt::WA_DeleteOnClose, false);
    editor.enableOverlay(screen);
    editor.show();
    QApplication::processEvents();
    auto *canvas = editor.findChild<EditorCanvas *>();
    QVERIFY(canvas);
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(40, 50));
    QTest::mouseMove(canvas->viewport(), QPoint(200, 170));
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                        QPoint(200, 170));
    QCOMPARE(editor.flattenedImage().size(), QSize(160, 120));
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(200, 170));
    QTest::mouseMove(canvas->viewport(), QPoint(250, 220));
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                        QPoint(250, 220));
    QCOMPARE(editor.flattenedImage().size(), QSize(210, 170));
    editor.undo();
    QCOMPARE(editor.flattenedImage().size(), QSize(160, 120));
    editor.redo();
    QCOMPARE(editor.flattenedImage().size(), QSize(210, 170));
    editor.close();
    QApplication::processEvents();
  }
  void cropExportsOnlySelectedRectangle() {
    QImage source(100, 80, QImage::Format_ARGB32);
    source.fill(Qt::green);
    Editor editor(source);
    editor.cropTo(QRect(20, 10, 40, 30));
    QCOMPARE(editor.flattenedImage().size(), QSize(40, 30));
    editor.undo();
    QCOMPARE(editor.flattenedImage().size(), source.size());
  }
};
QTEST_MAIN(EditorTest)
#include "editor_test.moc"
