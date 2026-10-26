#include "Editor.h"
#include <QAction>
#include <QApplication>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QScreen>
#include <QSpinBox>
#include <QTextCursor>
#include <QToolButton>
#include <QWindow>
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
    auto *window = editor.windowHandle();
    QVERIFY(window);
    auto point = [canvas, window](const QPoint &position) {
      return window->mapFromGlobal(canvas->viewport()->mapToGlobal(position));
    };
    // Qt5's QWidget mouseMove overload moves the OS cursor instead of sending
    // a button-state-aware drag. That move can target our floating toolbar.
    // QWindow injection follows the real QWidgetWindow grab/dispatch path on
    // both Qt5 and Qt6, even when the pointer crosses the toolbar while sizing.
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier,
                      point(QPoint(40, 50)));
    QTest::mouseMove(window, point(QPoint(200, 170)));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier,
                        point(QPoint(200, 170)));
    QCOMPARE(editor.flattenedImage().size(), QSize(160, 120));
    auto *toolbar = editor.findChild<QWidget *>("captureToolbar");
    QVERIFY(toolbar);
    QVERIFY2(toolbar->geometry().contains(QPoint(250, 220)),
             "Regression drag must cross the visible toolbar, not evade it");
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier,
                      point(QPoint(200, 170)));
    QTest::mouseMove(window, point(QPoint(250, 220)));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier,
                        point(QPoint(250, 220)));
    QCOMPARE(editor.flattenedImage().size(), QSize(210, 170));
    editor.undo();
    QCOMPARE(editor.flattenedImage().size(), QSize(160, 120));
    editor.redo();
    QCOMPARE(editor.flattenedImage().size(), QSize(210, 170));
    editor.close();
    QApplication::processEvents();
  }
  void overlayToolbarRoutesDrawingTools_data() {
    QTest::addColumn<bool>("checkedState");
    QTest::newRow("mouse click") << false;
    QTest::newRow("accessible checked state") << true;
  }
  void overlayToolbarRoutesDrawingTools() {
    QFETCH(bool, checkedState);
    auto *screen = QGuiApplication::primaryScreen();
    QImage source(screen->geometry().size(), QImage::Format_ARGB32);
    source.fill(Qt::white);
    Editor editor(source);
    editor.setAttribute(Qt::WA_DeleteOnClose, false);
    const QRect region(40, 50, 260, 180);
    editor.enableOverlay(screen,
                         region.translated(screen->geometry().topLeft()));
    editor.show();
    QApplication::processEvents();
    auto *canvas = editor.findChild<EditorCanvas *>();
    auto *toolbar = editor.findChild<QWidget *>("captureToolbar");
    auto choose = [&](const QString &name) {
      for (auto *button : toolbar->findChildren<QToolButton *>()) {
        auto *action = button->defaultAction();
        if (action && action->text() == name) {
          if (checkedState)
            action->setChecked(true);
          else
            QTest::mouseClick(button, Qt::LeftButton);
          return true;
        }
      }
      return false;
    };
    QVERIFY(choose(QStringLiteral("Стрелка")));
    QCOMPARE(canvas->tool, EditorCanvas::Arrow);
    auto *window = editor.windowHandle();
    auto point = [canvas, window](QPoint p) {
      return window->mapFromGlobal(canvas->viewport()->mapToGlobal(p));
    };
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier,
                      point(QPoint(80, 90)));
    QTest::mouseMove(window, point(QPoint(220, 170)));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier,
                        point(QPoint(220, 170)));
    QCOMPARE(editor.overlaySelectionGlobal(),
             region.translated(screen->geometry().topLeft()));
    bool painted = false;
    auto image = editor.flattenedImage();
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x)
        painted |= image.pixelColor(x, y) != QColor(Qt::white);
    QVERIFY(painted);
    QVERIFY(choose(QStringLiteral("Текст")));
    QCOMPARE(canvas->tool, EditorCanvas::Text);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      point(QPoint(100, 110)));
    int texts = 0;
    for (auto *item : canvas->scene()->items())
      if (qgraphicsitem_cast<QGraphicsTextItem *>(item))
        ++texts;
    QCOMPARE(texts, 1);
    QCOMPARE(editor.overlaySelectionGlobal(),
             region.translated(screen->geometry().topLeft()));
    editor.close();
  }
  void exportOmitsTextSelectionAndRestoresEditingState() {
    QImage source(300, 180, QImage::Format_ARGB32);
    source.fill(Qt::white);
    Editor editor(source);
    auto *canvas = editor.findChild<EditorCanvas *>();
    auto *text = new QGraphicsTextItem(QStringLiteral("Проверка текста"));
    text->setPos(30, 30);
    text->setTextInteractionFlags(Qt::TextEditorInteraction);
    text->setFlag(QGraphicsItem::ItemIsSelectable);
    canvas->scene()->addItem(text);
    auto cursor = text->textCursor();
    cursor.select(QTextCursor::Document);
    text->setTextCursor(cursor);
    text->setSelected(true);
    text->setFocus();
    auto *rect =
        canvas->scene()->addRect(QRectF(10, 10, 210, 90), QPen(Qt::red));
    rect->setFlag(QGraphicsItem::ItemIsSelectable);
    rect->setSelected(true);
    const auto selectedCursor = text->textCursor();
    auto *focus = canvas->scene()->focusItem();
    auto selectedExport = editor.flattenedImage();
    QVERIFY(text->isSelected());
    QVERIFY(rect->isSelected());
    QCOMPARE(canvas->scene()->focusItem(), focus);
    QCOMPARE(text->textCursor().anchor(), selectedCursor.anchor());
    QCOMPARE(text->textCursor().position(), selectedCursor.position());
    QCOMPARE(text->textInteractionFlags(), Qt::TextEditorInteraction);
    cursor.clearSelection();
    text->setTextCursor(cursor);
    text->setSelected(false);
    rect->setSelected(false);
    text->clearFocus();
    text->setTextInteractionFlags(Qt::NoTextInteraction);
    QCOMPARE(selectedExport, editor.flattenedImage());
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
