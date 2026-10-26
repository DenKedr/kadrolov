#include "Editor.h"
#include "CaptureIcons.h"
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <cmath>
#include <memory>
namespace {
QPoint mousePoint(QMouseEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return e->position().toPoint();
#else
  return e->pos();
#endif
}
class AddCommand : public QUndoCommand {
  QGraphicsScene *scene_;
  QGraphicsItem *item_;
  bool inScene_ = false;

public:
  AddCommand(QGraphicsScene *s, QGraphicsItem *i) : scene_(s), item_(i) {
    setText(QStringLiteral("Добавить объект"));
  }
  ~AddCommand() override {
    if (!inScene_)
      delete item_;
  }
  void redo() override {
    scene_->addItem(item_);
    inScene_ = true;
  }
  void undo() override {
    scene_->removeItem(item_);
    inScene_ = false;
  }
};
class RemoveCommand : public QUndoCommand {
  QGraphicsScene *scene_;
  QList<QGraphicsItem *> items_;
  bool removed_ = false;

public:
  RemoveCommand(QGraphicsScene *s, const QList<QGraphicsItem *> &i)
      : scene_(s), items_(i) {
    setText(QStringLiteral("Удалить объект"));
  }
  ~RemoveCommand() override {
    if (removed_)
      qDeleteAll(items_);
  }
  void redo() override {
    for (auto *i : items_)
      scene_->removeItem(i);
    removed_ = true;
  }
  void undo() override {
    for (auto *i : items_)
      scene_->addItem(i);
    removed_ = false;
  }
};
class MoveCommand : public QUndoCommand {
  QGraphicsItem *item_;
  QPointF before_, after_;

public:
  MoveCommand(QGraphicsItem *i, QPointF b, QPointF a)
      : item_(i), before_(b), after_(a) {
    setText(QStringLiteral("Переместить объект"));
  }
  void redo() override { item_->setPos(after_); }
  void undo() override { item_->setPos(before_); }
};
class CropCommand : public QUndoCommand {
  QGraphicsScene *scene_;
  QRectF before_, after_;

public:
  CropCommand(QGraphicsScene *s, QRectF after)
      : scene_(s), before_(s->sceneRect()), after_(after) {
    setText(QStringLiteral("Обрезать"));
  }
  void redo() override { scene_->setSceneRect(after_); }
  void undo() override { scene_->setSceneRect(before_); }
};
struct Style {
  QPen pen;
  QBrush brush;
  QFont font;
  QColor textColor;
};
Style readStyle(QGraphicsItem *item) {
  Style s;
  if (auto *t = qgraphicsitem_cast<QGraphicsTextItem *>(item)) {
    s.font = t->font();
    s.textColor = t->defaultTextColor();
  }
  if (auto *shape = dynamic_cast<QAbstractGraphicsShapeItem *>(item)) {
    s.pen = shape->pen();
    s.brush = shape->brush();
  }
  return s;
}
void applyStyle(QGraphicsItem *item, const Style &s) {
  if (auto *t = qgraphicsitem_cast<QGraphicsTextItem *>(item)) {
    t->setFont(s.font);
    t->setDefaultTextColor(s.textColor);
  }
  if (auto *shape = dynamic_cast<QAbstractGraphicsShapeItem *>(item)) {
    shape->setPen(s.pen);
    shape->setBrush(s.brush);
  }
}
class SelectionCommand : public QUndoCommand {
  EditorCanvas *canvas_;
  QRectF before_, after_;

public:
  SelectionCommand(EditorCanvas *c, const QRectF &before, const QRectF &after)
      : canvas_(c), before_(before), after_(after) {
    setText(QStringLiteral("Изменить область"));
  }
  void undo() override { canvas_->setOverlaySelection(before_); }
  void redo() override { canvas_->setOverlaySelection(after_); }
};
class StyleCommand : public QUndoCommand {
  QList<QGraphicsItem *> items_;
  QList<Style> before_, after_;

public:
  StyleCommand(const QList<QGraphicsItem *> &items, const QList<Style> &before,
               const QList<Style> &after)
      : items_(items), before_(before), after_(after) {
    setText(QStringLiteral("Изменить свойства"));
  }
  void undo() override {
    for (int n = 0; n < items_.size(); ++n)
      applyStyle(items_[n], before_[n]);
  }
  void redo() override {
    for (int n = 0; n < items_.size(); ++n)
      applyStyle(items_[n], after_[n]);
  }
};
void selectable(QGraphicsItem *item) {
  item->setFlags(QGraphicsItem::ItemIsSelectable |
                 QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsFocusable);
  item->setZValue(1);
}
} // namespace
EditorCanvas::EditorCanvas(QWidget *p) : QGraphicsView(p) {
  setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing |
                 QPainter::SmoothPixmapTransform);
  setDragMode(RubberBandDrag);
  setMouseTracking(true);
  setBackgroundBrush(QColor("#24262B"));
  setAccessibleName(QStringLiteral("Изображение и пометки"));
}
QGraphicsItem *EditorCanvas::shape(const QPointF &end) {
  QPen pen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  QRectF r = QRectF(origin_, end).normalized();
  if (tool == Circle) {
    qreal side = qMin(r.width(), r.height());
    r.setSize(QSizeF(side, side));
  }
  if (tool == Rectangle || tool == Redact || tool == Crop || tool == Pixelate ||
      tool == Blur) {
    auto *i = new QGraphicsRectItem(r);
    i->setPen(pen);
    if (tool == Redact)
      i->setBrush(color);
    else if (tool == Crop || tool == Pixelate || tool == Blur) {
      i->setPen(QPen(Qt::white, 1, Qt::DashLine));
      i->setBrush(QColor(0, 0, 0, 35));
    }
    return i;
  }
  if (tool == Ellipse || tool == Circle) {
    auto *i = new QGraphicsEllipseItem(r);
    i->setPen(pen);
    return i;
  }
  QPainterPath p;
  if (tool == Pencil || tool == Marker)
    p = path_;
  else {
    p.moveTo(origin_);
    p.lineTo(end);
    if (tool == Arrow) {
      auto angle = std::atan2(end.y() - origin_.y(), end.x() - origin_.x());
      auto length = qMax(12.0, stroke * 4.0);
      QPointF a = end - QPointF(std::cos(angle - .5) * length,
                                std::sin(angle - .5) * length);
      QPointF b = end - QPointF(std::cos(angle + .5) * length,
                                std::sin(angle + .5) * length);
      p.moveTo(a);
      p.lineTo(end);
      p.lineTo(b);
    }
  }
  auto *i = new QGraphicsPathItem(p);
  if (tool == Marker) {
    QColor c = color;
    c.setAlpha(100);
    pen.setColor(c);
    pen.setWidth(stroke * 4);
  }
  i->setPen(pen);
  return i;
}
void EditorCanvas::setOverlayMode(bool enabled, qreal scale) {
  overlayMode_ = enabled;
  overlayScale_ = qMax(.1, scale);
  viewport()->update();
}
void EditorCanvas::setOverlaySelection(const QRectF &rect) {
  selection_ = rect.normalized().intersected(scene()->sceneRect());
  emit selectionChanged(selection_);
  viewport()->update();
}
int EditorCanvas::handleAt(const QPointF &point) const {
  if (selection_.isEmpty())
    return -1;
  QList<QPointF> handles = {
      selection_.topLeft(),
      QPointF(selection_.center().x(), selection_.top()),
      selection_.topRight(),
      QPointF(selection_.right(), selection_.center().y()),
      selection_.bottomRight(),
      QPointF(selection_.center().x(), selection_.bottom()),
      selection_.bottomLeft(),
      QPointF(selection_.left(), selection_.center().y())};
  qreal hit = 8 * overlayScale_;
  for (int i = 0; i < handles.size(); ++i)
    if (QRectF(handles[i] - QPointF(hit, hit), QSizeF(hit * 2, hit * 2))
            .contains(point))
      return i;
  return -1;
}
void EditorCanvas::drawForeground(QPainter *p, const QRectF &) {
  if (!overlayMode_)
    return;
  QPainterPath dim;
  dim.setFillRule(Qt::OddEvenFill);
  dim.addRect(scene()->sceneRect());
  if (!selection_.isEmpty())
    dim.addRect(selection_);
  p->fillPath(dim, QColor(0, 0, 0, 110));
  if (selection_.isEmpty()) {
    p->save();
    QFont font = p->font();
    font.setPixelSize(qRound(14 * overlayScale_));
    p->setFont(font);
    p->setPen(Qt::white);
    p->drawText(QRectF(24 * overlayScale_, 24 * overlayScale_,
                       700 * overlayScale_, 50 * overlayScale_),
                Qt::AlignLeft | Qt::AlignVCenter,
                QStringLiteral("Выделите область мышью · Esc — отменить"));
    p->restore();
    return;
  }
  p->setPen(QPen(QColor("#F97316"), 1.3 * overlayScale_, Qt::DashLine));
  p->setBrush(Qt::NoBrush);
  p->drawRect(selection_);
  const QList<QPointF> handles = {
      selection_.topLeft(),
      QPointF(selection_.center().x(), selection_.top()),
      selection_.topRight(),
      QPointF(selection_.right(), selection_.center().y()),
      selection_.bottomRight(),
      QPointF(selection_.center().x(), selection_.bottom()),
      selection_.bottomLeft(),
      QPointF(selection_.left(), selection_.center().y())};
  p->setPen(QPen(Qt::white, overlayScale_));
  p->setBrush(QColor("#F97316"));
  qreal size = 6 * overlayScale_;
  for (auto point : handles)
    p->drawRect(
        QRectF(point - QPointF(size / 2, size / 2), QSizeF(size, size)));
  QFont font = p->font();
  font.setPixelSize(qRound(11 * overlayScale_));
  p->setFont(font);
  QString label = QStringLiteral("%1 × %2")
                      .arg(qRound(selection_.width()))
                      .arg(qRound(selection_.height()));
  QRectF text(selection_.right() + 8 * overlayScale_,
              selection_.bottom() - 24 * overlayScale_, 130 * overlayScale_,
              24 * overlayScale_);
  if (text.right() > scene()->sceneRect().right())
    text.moveRight(selection_.right() - 8 * overlayScale_);
  p->fillRect(text, QColor(25, 28, 33, 200));
  p->setPen(Qt::white);
  p->drawText(text, Qt::AlignCenter, label);
}
void EditorCanvas::mousePressEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton) {
    QGraphicsView::mousePressEvent(e);
    return;
  }
  origin_ = mapToScene(mousePoint(e));
  last_ = origin_;
  if (overlayMode_ && tool == Select) {
    int handle = handleAt(origin_);
    auto *hit = itemAt(mousePoint(e));
    bool movable = hit && hit->flags().testFlag(QGraphicsItem::ItemIsMovable);
    if (handle >= 0 || !selection_.contains(origin_) || !movable) {
      selectionHandle_ =
          handle >= 0 ? handle : (selection_.contains(origin_) ? 8 : 9);
      selectionDragging_ = true;
      selectionDragStart_ = origin_;
      originalSelection_ = selection_;
      if (selectionHandle_ == 9)
        setOverlaySelection(QRectF(origin_, origin_));
      e->accept();
      return;
    }
  }
  if (overlayMode_ && tool != Select && !selection_.contains(origin_))
    return;
  if (tool == Select) {
    moving_ = itemAt(mousePoint(e));
    if (moving_ && moving_->flags().testFlag(QGraphicsItem::ItemIsMovable))
      previousPosition_ = moving_->pos();
    else
      moving_ = nullptr;
    QGraphicsView::mousePressEvent(e);
    return;
  }
  if (!scene()->sceneRect().contains(origin_))
    return;
  scene()->clearSelection();
  if (tool == Text) {
    auto *i = new QGraphicsTextItem(QStringLiteral("Текст"));
    QFont f = textFont;
    f.setPointSize(textSize);
    i->setFont(f);
    i->setDefaultTextColor(color);
    i->setTextInteractionFlags(Qt::TextEditorInteraction);
    i->setPos(origin_);
    selectable(i);
    emit itemCreated(i);
    i->setSelected(true);
    i->setFocus();
    QTextCursor cursor = i->textCursor();
    cursor.select(QTextCursor::Document);
    i->setTextCursor(cursor);
    return;
  }
  if (tool == Number) {
    auto *i = new QGraphicsTextItem(QString::number(nextNumber++));
    QFont f = textFont;
    f.setBold(true);
    f.setPointSize(textSize);
    i->setFont(f);
    i->setDefaultTextColor(color);
    i->setPos(origin_);
    selectable(i);
    emit itemCreated(i);
    return;
  }
  drawing_ = true;
  path_ = QPainterPath(origin_);
  preview_ = shape(origin_);
  preview_->setZValue(100);
  scene()->addItem(preview_);
  e->accept();
}
void EditorCanvas::mouseMoveEvent(QMouseEvent *e) {
  if (selectionDragging_) {
    QPointF point = mapToScene(mousePoint(e));
    auto bounds = scene()->sceneRect();
    point.setX(qBound(bounds.left(), point.x(), bounds.right()));
    point.setY(qBound(bounds.top(), point.y(), bounds.bottom()));
    QRectF next = originalSelection_;
    if (selectionHandle_ == 9)
      next = QRectF(selectionDragStart_, point).normalized();
    else if (selectionHandle_ == 8) {
      QPointF delta = point - selectionDragStart_;
      delta.setX(qBound(bounds.left() - next.left(), delta.x(),
                        bounds.right() - next.right()));
      delta.setY(qBound(bounds.top() - next.top(), delta.y(),
                        bounds.bottom() - next.bottom()));
      next.translate(delta);
    } else {
      if (selectionHandle_ == 0 || selectionHandle_ == 6 ||
          selectionHandle_ == 7)
        next.setLeft(point.x());
      if (selectionHandle_ == 0 || selectionHandle_ == 1 ||
          selectionHandle_ == 2)
        next.setTop(point.y());
      if (selectionHandle_ == 2 || selectionHandle_ == 3 ||
          selectionHandle_ == 4)
        next.setRight(point.x());
      if (selectionHandle_ == 4 || selectionHandle_ == 5 ||
          selectionHandle_ == 6)
        next.setBottom(point.y());
      next = next.normalized();
    }
    setOverlaySelection(next);
    e->accept();
    return;
  }
  if (!drawing_) {
    QGraphicsView::mouseMoveEvent(e);
    return;
  }
  QPointF end = mapToScene(mousePoint(e));
  auto bounds = overlayMode_ ? selection_ : scene()->sceneRect();
  end.setX(qBound(bounds.left(), end.x(), bounds.right()));
  end.setY(qBound(bounds.top(), end.y(), bounds.bottom()));
  if (tool == Pencil || tool == Marker)
    path_.lineTo(end);
  if (preview_) {
    scene()->removeItem(preview_);
    delete preview_;
  }
  preview_ = shape(end);
  preview_->setZValue(100);
  scene()->addItem(preview_);
  last_ = end;
}
void EditorCanvas::mouseReleaseEvent(QMouseEvent *e) {
  if (selectionDragging_) {
    selectionDragging_ = false;
    if (selection_.width() < 3 * overlayScale_ ||
        selection_.height() < 3 * overlayScale_)
      setOverlaySelection(originalSelection_);
    else if (selection_ != originalSelection_)
      emit selectionEdited(originalSelection_, selection_);
    e->accept();
    return;
  }
  if (tool == Select) {
    QGraphicsView::mouseReleaseEvent(e);
    if (moving_ && moving_->pos() != previousPosition_)
      emit itemMoved(moving_, previousPosition_, moving_->pos());
    moving_ = nullptr;
    return;
  }
  if (!drawing_)
    return;
  drawing_ = false;
  if (preview_) {
    scene()->removeItem(preview_);
    delete preview_;
    preview_ = nullptr;
  }
  if (QLineF(origin_, last_).length() < 2)
    return;
  auto r = QRectF(origin_, last_).normalized();
  if (tool == Crop || tool == Pixelate || tool == Blur) {
    emit areaCreated(r, tool);
    return;
  }
  auto *i = shape(last_);
  selectable(i);
  emit itemCreated(i);
  i->setSelected(true);
}
void EditorCanvas::keyPressEvent(QKeyEvent *e) {
  if (e->key() == Qt::Key_Escape && drawing_) {
    drawing_ = false;
    if (preview_) {
      scene()->removeItem(preview_);
      delete preview_;
      preview_ = nullptr;
    }
    return;
  }
  if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
    auto *text = qgraphicsitem_cast<QGraphicsTextItem *>(scene()->focusItem());
    if (text && text->textInteractionFlags().testFlag(Qt::TextEditable)) {
      QGraphicsView::keyPressEvent(e);
      return;
    }
    emit deleteRequested();
    return;
  }
  QGraphicsView::keyPressEvent(e);
}
Editor::Editor(const QImage &image, QWidget *parent)
    : QMainWindow(parent), scene_(new QGraphicsScene(this)),
      canvas_(new EditorCanvas(this)), undo_(this) {
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(QStringLiteral("Кадролов — редактор снимка"));
  resize(1120, 760);
  auto *base = scene_->addPixmap(QPixmap::fromImage(image));
  base->setZValue(-1000);
  scene_->setSceneRect(QRectF(QPointF(0, 0), image.size()));
  canvas_->setScene(scene_);
  setCentralWidget(canvas_);
  saveDirectory_ =
      QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) +
      QStringLiteral("/Кадролов");
  auto *bar = addToolBar(QStringLiteral("Инструменты"));
  bar->setMovable(false);
  bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
  auto *group = new QActionGroup(this);
  toolGroup_ = group;
  group->setExclusive(true);
  const QList<QPair<QString, EditorCanvas::Tool>> tools = {
      {QStringLiteral("Выбрать"), EditorCanvas::Select},
      {QStringLiteral("Стрелка"), EditorCanvas::Arrow},
      {QStringLiteral("Линия"), EditorCanvas::Line},
      {QStringLiteral("Рамка"), EditorCanvas::Rectangle},
      {QStringLiteral("Овал"), EditorCanvas::Ellipse},
      {QStringLiteral("Круг"), EditorCanvas::Circle},
      {QStringLiteral("Текст"), EditorCanvas::Text},
      {QStringLiteral("Карандаш"), EditorCanvas::Pencil},
      {QStringLiteral("Маркер"), EditorCanvas::Marker},
      {QStringLiteral("Номер"), EditorCanvas::Number},
      {QStringLiteral("Закрыть"), EditorCanvas::Redact},
      {QStringLiteral("Пиксели"), EditorCanvas::Pixelate},
      {QStringLiteral("Размыть"), EditorCanvas::Blur},
      {QStringLiteral("Обрезать"), EditorCanvas::Crop}};
  for (auto pair : tools) {
    auto *a = bar->addAction(pair.first);
    a->setCheckable(true);
    a->setData(int(pair.second));
    group->addAction(a);
    if (pair.second == EditorCanvas::Select)
      a->setChecked(true);
    connect(a, &QAction::triggered, this,
            [this, pair] { setTool(pair.second); });
  }
  addToolBarBreak();
  auto *props = addToolBar(QStringLiteral("Свойства"));
  props->setMovable(false);
  props->setToolButtonStyle(Qt::ToolButtonTextOnly);
  colorAction_ = props->addAction(QStringLiteral("Цвет"));
  connect(colorAction_, &QAction::triggered, this, &Editor::chooseColor);
  props->addWidget(new QLabel(QStringLiteral(" Толщина ")));
  width_ = new QSpinBox;
  width_->setRange(1, 40);
  width_->setValue(4);
  width_->setObjectName("strokeWidth");
  width_->setAccessibleName(QStringLiteral("Толщина линии"));
  props->addWidget(width_);
  props->addWidget(new QLabel(QStringLiteral(" Шрифт ")));
  font_ = new QFontComboBox;
  font_->setMaximumWidth(175);
  font_->setAccessibleName(QStringLiteral("Шрифт текста"));
  props->addWidget(font_);
  fontSize_ = new QSpinBox;
  fontSize_->setRange(8, 180);
  fontSize_->setValue(24);
  fontSize_->setAccessibleName(QStringLiteral("Размер текста"));
  props->addWidget(fontSize_);
  connect(width_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
    canvas_->stroke = v;
    updateSelectionProperties(1);
  });
  connect(fontSize_, qOverload<int>(&QSpinBox::valueChanged), this,
          [this](int v) {
            canvas_->textSize = v;
            updateSelectionProperties(3);
          });
  connect(font_, &QFontComboBox::currentFontChanged, this,
          [this](const QFont &f) {
            canvas_->textFont = f;
            updateSelectionProperties(2);
          });
  props->addSeparator();
  auto *undoAction = undo_.createUndoAction(this, QStringLiteral("Отменить"));
  undoAction->setShortcut(QKeySequence::Undo);
  props->addAction(undoAction);
  auto *redoAction = undo_.createRedoAction(this, QStringLiteral("Повторить"));
  redoAction->setShortcut(QKeySequence::Redo);
  props->addAction(redoAction);
  auto *remove = props->addAction(QStringLiteral("Удалить"));
  connect(remove, &QAction::triggered, this, &Editor::deleteSelection);
  auto *zoomOut = props->addAction(QStringLiteral("−"));
  connect(zoomOut, &QAction::triggered, this,
          [this] { canvas_->scale(.8, .8); });
  auto *fit = props->addAction(QStringLiteral("Вписать"));
  connect(fit, &QAction::triggered, this, [this] {
    canvas_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
  });
  auto *zoomIn = props->addAction(QStringLiteral("+"));
  connect(zoomIn, &QAction::triggered, this,
          [this] { canvas_->scale(1.25, 1.25); });
  auto *saveBar = addToolBar(QStringLiteral("Сохранение"));
  saveBar->setMovable(false);
  saveBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
  auto *save = saveBar->addAction(QStringLiteral("Сохранить…"));
  save->setShortcut(QKeySequence::Save);
  connect(save, &QAction::triggered, this, &Editor::saveAs);
  auto *copy = saveBar->addAction(QStringLiteral("Копировать"));
  copy->setShortcut(QKeySequence::Copy);
  connect(copy, &QAction::triggered, this, [this] {
    QApplication::clipboard()->setImage(flattenedImage());
    statusBar()->showMessage(QStringLiteral("Изображение скопировано"), 3000);
  });
  auto *ocr = saveBar->addAction(QStringLiteral("Распознать текст"));
  connect(ocr, &QAction::triggered, this, [this, ocr] {
    QString program;
    QStringList args;
    bool mac = false;
    auto temporary = std::make_shared<QTemporaryDir>();
    if (!temporary->isValid()) {
      QMessageBox::warning(
          this, QStringLiteral("OCR недоступен"),
          QStringLiteral("Не удалось создать временный файл."));
      return;
    }
    QString input = temporary->filePath("ocr.png");
    if (!flattenedImage().save(input)) {
      QMessageBox::warning(
          this, QStringLiteral("OCR недоступен"),
          QStringLiteral("Не удалось подготовить изображение."));
      return;
    }
#ifdef Q_OS_MACOS
    program = QCoreApplication::applicationDirPath() + "/kadrolov-capture";
    if (!QFileInfo::exists(program))
      program = QStandardPaths::findExecutable("kadrolov-capture");
    args = {"--ocr", input};
    mac = true;
#else
 program=QStandardPaths::findExecutable("tesseract");args={input,"stdout","-l","rus+eng"};
#endif
    if (program.isEmpty()) {
      QMessageBox::information(
          this, QStringLiteral("Нужен локальный OCR"),
          QStringLiteral(
              "Установите Tesseract и языки rus/eng либо соберите штатный "
              "helper macOS. Изображение остаётся на устройстве."));
      return;
    }
    auto *process = new QProcess(this);
    ocr->setEnabled(false);
    statusBar()->showMessage(
        QStringLiteral("Распознавание текста на устройстве…"));
    connect(process, &QProcess::errorOccurred, this,
            [this, process, ocr](QProcess::ProcessError error) {
              if (error != QProcess::FailedToStart)
                return;
              ocr->setEnabled(true);
              statusBar()->clearMessage();
              QMessageBox::warning(this, QStringLiteral("OCR не запустился"),
                                   process->errorString());
              process->deleteLater();
            });
    connect(
        process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this, process, ocr, temporary, mac](int code,
                                             QProcess::ExitStatus status) {
          ocr->setEnabled(true);
          statusBar()->clearMessage();
          QString output = QString::fromUtf8(process->readAllStandardOutput());
          QString diagnostic =
              QString::fromUtf8(process->readAllStandardError());
          process->deleteLater();
          if (code != 0 || status != QProcess::NormalExit) {
            QMessageBox::warning(
                this, QStringLiteral("Не удалось распознать текст"),
                QStringLiteral("Проверьте локальный OCR и языки rus/eng.\n%1")
                    .arg(diagnostic.left(800)));
            return;
          }
          if (mac) {
            auto json = QJsonDocument::fromJson(output.toUtf8());
            if (json.isObject())
              output = json.object().value("text").toString();
            else
              output.clear();
          }
          auto *dialog = new QDialog(this);
          dialog->setAttribute(Qt::WA_DeleteOnClose);
          dialog->setWindowTitle(QStringLiteral("Распознанный текст"));
          dialog->resize(620, 430);
          auto *l = new QVBoxLayout(dialog);
          auto *text = new QPlainTextEdit;
          text->setPlainText(output);
          text->setPlaceholderText(QStringLiteral("Текст не найден"));
          l->addWidget(text);
          auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
          auto *copy = buttons->addButton(QStringLiteral("Копировать текст"),
                                          QDialogButtonBox::ActionRole);
          connect(copy, &QPushButton::clicked, dialog, [text] {
            QApplication::clipboard()->setText(text->toPlainText());
          });
          connect(buttons, &QDialogButtonBox::rejected, dialog,
                  &QDialog::close);
          l->addWidget(buttons);
          dialog->show();
        });
    process->start(program, args);
  });
  auto *print = saveBar->addAction(QStringLiteral("Печать…"));
  print->setShortcut(QKeySequence::Print);
  connect(print, &QAction::triggered, this, [this] {
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted)
      return;
    QPainter painter(&printer);
    auto image = flattenedImage();
    QRectF page = printer.pageRect(QPrinter::DevicePixel);
    QSizeF size = image.size();
    size.scale(page.size(), Qt::KeepAspectRatio);
    QRectF target(page.topLeft() + (QPointF(page.width(), page.height()) -
                                    QPointF(size.width(), size.height())) /
                                       2,
                  size);
    painter.drawImage(target, image);
  });
  connect(&undo_, &QUndoStack::indexChanged, this,
          [this] { exported_ = false; });
  connect(canvas_, &EditorCanvas::selectionChanged, this,
          [this](const QRectF &) { positionOverlayToolbar(); });
  connect(canvas_, &EditorCanvas::selectionEdited, this,
          [this](const QRectF &before, const QRectF &after) {
            undo_.push(new SelectionCommand(canvas_, before, after));
          });
  connect(canvas_, &EditorCanvas::itemCreated, this, &Editor::addItem);
  connect(canvas_, &EditorCanvas::areaCreated, this, &Editor::processArea);
  connect(canvas_, &EditorCanvas::deleteRequested, this,
          &Editor::deleteSelection);
  connect(canvas_, &EditorCanvas::itemMoved, this,
          [this](QGraphicsItem *i, QPointF before, QPointF after) {
            undo_.push(new MoveCommand(i, before, after));
          });
  statusBar()->showMessage(
      QStringLiteral("Выберите инструмент. «Закрыть» непрозрачно скрывает "
                     "данные в сохранённом изображении."));
}
void Editor::setTool(EditorCanvas::Tool tool) {
  canvas_->tool = tool;
  canvas_->setDragMode(tool == EditorCanvas::Select
                           ? QGraphicsView::RubberBandDrag
                           : QGraphicsView::NoDrag);
  canvas_->setCursor(tool == EditorCanvas::Select ? Qt::ArrowCursor
                                                  : Qt::CrossCursor);
}
void Editor::setDefaultSaveDirectory(const QString &d) { saveDirectory_ = d; }
void Editor::addItem(QGraphicsItem *i) {
  undo_.push(new AddCommand(scene_, i));
  if (auto *t = qgraphicsitem_cast<QGraphicsTextItem *>(i))
    connect(t->document(), &QTextDocument::contentsChanged, this,
            [this] { exported_ = false; });
  exported_ = false;
}
void Editor::addRedaction(const QRect &r, const QColor &c) {
  auto *i = new QGraphicsRectItem(r);
  i->setPen(Qt::NoPen);
  i->setBrush(c);
  selectable(i);
  addItem(i);
}
void Editor::cropTo(const QRect &r) {
  if (overlay_) {
    auto valid = scene_->sceneRect().intersected(r);
    if (valid.width() >= 1 && valid.height() >= 1)
      undo_.push(
          new SelectionCommand(canvas_, canvas_->overlaySelection(), valid));
    return;
  }
  auto valid = scene_->sceneRect().intersected(r);
  if (valid.width() >= 1 && valid.height() >= 1)
    undo_.push(new CropCommand(scene_, valid));
}
void Editor::undo() {
  undo_.undo();
  exported_ = false;
}
void Editor::redo() {
  undo_.redo();
  exported_ = false;
}
QImage Editor::flattenedImage() const {
  QRectF r = overlay_ ? canvas_->overlaySelection() : scene_->sceneRect();
  QSize size(qMax(1, qRound(r.width())), qMax(1, qRound(r.height())));
  QImage out(size, QImage::Format_ARGB32_Premultiplied);
  out.fill(Qt::transparent);
  auto selected = scene_->selectedItems();
  for (auto *i : selected)
    i->setSelected(false);
  auto *focus = scene_->focusItem();
  if (focus)
    focus->clearFocus();
  QPainter p(&out);
  p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing |
                   QPainter::SmoothPixmapTransform);
  scene_->render(&p, QRectF(QPointF(0, 0), size), r);
  p.end();
  for (auto *i : selected)
    i->setSelected(true);
  if (focus)
    focus->setFocus();
  return out;
}
void Editor::processArea(const QRectF &raw, int tool) {
  QRect area = scene_->sceneRect().intersected(raw).toAlignedRect();
  if (area.isEmpty())
    return;
  if (tool == EditorCanvas::Crop) {
    cropTo(area);
    if (!overlay_)
      canvas_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
    return;
  }
  auto exported = flattenedImage();
  QRect local = area.translated(
      -(overlay_ ? canvas_->overlaySelection() : scene_->sceneRect())
           .topLeft()
           .toPoint());
  auto part = exported.copy(local);
  int factor = tool == EditorCanvas::Blur ? 10 : 14;
  auto small = part.scaled(qMax(1, part.width() / factor),
                           qMax(1, part.height() / factor),
                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  auto replaced =
      small.scaled(part.size(), Qt::IgnoreAspectRatio,
                   tool == EditorCanvas::Blur ? Qt::SmoothTransformation
                                              : Qt::FastTransformation);
  auto *i = new QGraphicsPixmapItem(QPixmap::fromImage(replaced));
  i->setPos(area.topLeft());
  selectable(i);
  addItem(i);
}
void Editor::deleteSelection() {
  auto items = scene_->selectedItems();
  if (!items.isEmpty())
    undo_.push(new RemoveCommand(scene_, items));
}
void Editor::chooseColor() {
  auto c = QColorDialog::getColor(canvas_->color, this,
                                  QStringLiteral("Цвет инструмента"));
  if (c.isValid()) {
    canvas_->color = c;
    updateSelectionProperties();
  }
}
void Editor::updateSelectionProperties(int property) {
  QList<QGraphicsItem *> items;
  QList<Style> before, after;
  for (auto *i : scene_->selectedItems()) {
    Style old = readStyle(i), next = old;
    bool supported = false;
    if (qgraphicsitem_cast<QGraphicsTextItem *>(i)) {
      if (property == 0)
        next.textColor = canvas_->color;
      if (property == 2)
        next.font.setFamily(font_->currentFont().family());
      if (property == 3)
        next.font.setPointSize(fontSize_->value());
      supported = property != 1;
    }
    if (dynamic_cast<QAbstractGraphicsShapeItem *>(i)) {
      if (property == 0) {
        next.pen.setColor(canvas_->color);
        if (next.brush.style() != Qt::NoBrush)
          next.brush = canvas_->color;
      }
      if (property == 1)
        next.pen.setWidth(width_->value());
      supported = property == 0 || property == 1;
    }
    if (supported &&
        (old.pen != next.pen || old.brush != next.brush ||
         old.font != next.font || old.textColor != next.textColor)) {
      items.append(i);
      before.append(old);
      after.append(next);
    }
  }
  if (!items.isEmpty())
    undo_.push(new StyleCommand(items, before, after));
}
void Editor::saveAs() {
  if (overlay_ && canvas_->overlaySelection().isEmpty())
    return;
  QDir().mkpath(saveDirectory_);
  QString name = QStringLiteral("Кадролов_%1.png")
                     .arg(QDateTime::currentDateTime().toString(
                         QStringLiteral("yyyy-MM-dd_HH-mm-ss")));
  QString filter;
  auto path = QFileDialog::getSaveFileName(
      this, QStringLiteral("Сохранить снимок"),
      QDir(saveDirectory_).filePath(name),
      QStringLiteral("PNG (*.png);;JPEG (*.jpg *.jpeg);;WebP (*.webp)"),
      &filter);
  if (path.isEmpty())
    return;
  QString suffix = QFileInfo(path).suffix().toLower();
  if (suffix.isEmpty()) {
    suffix = filter.startsWith("JPEG")   ? "jpg"
             : filter.startsWith("WebP") ? "webp"
                                         : "png";
    path += "." + suffix;
  }
  int quality = 95;
  if (suffix != "png") {
    bool ok = false;
    quality = QInputDialog::getInt(this, QStringLiteral("Качество изображения"),
                                   QStringLiteral("Качество от 1 до 100"), 95,
                                   1, 100, 1, &ok);
    if (!ok)
      return;
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    QMessageBox::warning(this, QStringLiteral("Не удалось сохранить"),
                         file.errorString());
    return;
  }
  QImageWriter writer(&file, suffix.toLatin1());
  writer.setQuality(quality);
  if (!writer.write(flattenedImage()) || !file.commit()) {
    QMessageBox::warning(this, QStringLiteral("Не удалось сохранить"),
                         writer.errorString());
    return;
  }
  saveDirectory_ = QFileInfo(path).absolutePath();
  undo_.setClean();
  exported_ = true;
  emit saved(path);
  statusBar()->showMessage(QStringLiteral("Сохранено: %1").arg(path), 6000);
}
void Editor::closeEvent(QCloseEvent *e) {
  if (overlay_) {
    emit overlayFinished();
    QMainWindow::closeEvent(e);
    return;
  }
  if (!exported_) {
    auto answer = QMessageBox::question(
        this, QStringLiteral("Сохранить изменения?"),
        QStringLiteral("Пометки ещё не сохранены."),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Cancel) {
      e->ignore();
      return;
    }
    if (answer == QMessageBox::Save) {
      saveAs();
      if (!exported_) {
        e->ignore();
        return;
      }
    }
  }
  QMainWindow::closeEvent(e);
}

void Editor::enableOverlay(QScreen *screen, const QRect &initialGlobalRect,
                           bool videoSelection) {
  overlay_ = true;
  videoSelection_ = videoSelection;
  overlayScreen_ = screen;
  overlayScale_ =
      qreal(scene_->sceneRect().width()) / qMax(1, screen->geometry().width());
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
  setAttribute(Qt::WA_TranslucentBackground, false);
  for (auto *bar : findChildren<QToolBar *>())
    bar->hide();
  statusBar()->hide();
  setContentsMargins(0, 0, 0, 0);
  canvas_->setFrameShape(QFrame::NoFrame);
  canvas_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  canvas_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  canvas_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  canvas_->resetTransform();
  canvas_->scale(1.0 / overlayScale_, 1.0 / overlayScale_);
  canvas_->setOverlayMode(true, overlayScale_);
  setGeometry(screen->geometry());
  buildOverlayToolbar();
  if (!initialGlobalRect.isEmpty()) {
    auto local = initialGlobalRect.translated(-screen->geometry().topLeft());
    canvas_->setOverlaySelection(
        QRectF(local.x() * overlayScale_, local.y() * overlayScale_,
               local.width() * overlayScale_, local.height() * overlayScale_));
  } else
    canvas_->setOverlaySelection(QRectF());
  canvas_->setFocus();
  QTimer::singleShot(0, this, [this] { positionOverlayToolbar(); });
}
void Editor::buildOverlayToolbar() {
  overlayToolbar_ = new QFrame(this);
  overlayToolbar_->setObjectName("captureToolbar");
  overlayToolbar_->setStyleSheet(
      QStringLiteral("#captureToolbar{background:#FFFFFF;border:1px solid "
                     "#D8DADD;border-radius:9px;} #captureToolbar "
                     "QToolButton{background:transparent;color:#30353D;border:"
                     "0;border-radius:5px;padding:3px;} #captureToolbar "
                     "QToolButton:hover{background:#F0F1F3;} #captureToolbar "
                     "QToolButton:checked{background:#FFE5D4;} #captureToolbar "
                     "QToolButton:pressed{background:#FAD2B7;}"));
  auto *row = new QHBoxLayout(overlayToolbar_);
  row->setContentsMargins(7, 5, 7, 5);
  row->setSpacing(1);
  auto add = [this, row](const QString &icon, const QString &tip,
                         const std::function<void()> &fn) {
    auto *b = new QToolButton(overlayToolbar_);
    b->setIcon(captureIcon(icon));
    b->setIconSize(QSize(23, 23));
    b->setFixedSize(31, 33);
    b->setToolTip(tip);
    b->setAccessibleName(tip);
    connect(b, &QToolButton::clicked, this, fn);
    row->addWidget(b);
    return b;
  };
  auto separator = [row, this] {
    auto *line = new QFrame(overlayToolbar_);
    line->setFrameShape(QFrame::VLine);
    line->setFixedHeight(22);
    line->setStyleSheet(QStringLiteral("color:#D8DADD;"));
    row->addWidget(line);
  };
  if (videoSelection_) {
    add("undo", QStringLiteral("Выбрать область заново"),
        [this] { canvas_->setOverlaySelection(QRectF()); });
    auto *record = add(
        "record", QStringLiteral("Начать запись выбранной области"), [this] {
          auto region = overlaySelectionGlobal();
          if (region.isEmpty())
            return;
          emit videoRegionRequested(region, overlayScreen_);
          closingOverlay_ = true;
          close();
        });
    record->setIcon(captureIcon("record", QColor("#DC2626")));
    auto *time = new QLabel(QStringLiteral("00:00"), overlayToolbar_);
    time->setStyleSheet(
        QStringLiteral("font-size:16px;color:#20242B;padding:0 14px;"));
    row->addWidget(time);
    add("close", QStringLiteral("Отменить (Esc)"), [this] {
      closingOverlay_ = true;
      close();
    });
    overlayToolbar_->adjustSize();
    positionOverlayToolbar();
    return;
  }
  auto *undoAction = undo_.createUndoAction(this, QStringLiteral("Отменить"));
  undoAction->setIcon(captureIcon("undo"));
  auto *undoButton = new QToolButton(overlayToolbar_);
  undoButton->setDefaultAction(undoAction);
  undoButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  undoButton->setFixedSize(31, 33);
  undoButton->setIconSize(QSize(23, 23));
  row->addWidget(undoButton);
  auto *redoAction = undo_.createRedoAction(this, QStringLiteral("Повторить"));
  redoAction->setIcon(captureIcon("redo"));
  auto *redoButton = new QToolButton(overlayToolbar_);
  redoButton->setDefaultAction(redoAction);
  redoButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  redoButton->setFixedSize(31, 33);
  redoButton->setIconSize(QSize(23, 23));
  row->addWidget(redoButton);
  separator();
  overlayColor_ = add("color", QStringLiteral("Цвет инструмента"), [] {});
  overlayColor_->disconnect(this);
  auto *colors = new QMenu(overlayColor_);
  colors->setStyleSheet(QStringLiteral(
      "QMenu{background:white;color:#20242B;padding:5px;}QMenu::item{padding:"
      "6px 12px;}QMenu::item:selected{background:#FFE5D4;}"));
  const QList<QPair<QString, QColor>> palette = {
      {QStringLiteral("Красный"), QColor("#E53935")},
      {QStringLiteral("Оранжевый"), QColor("#F97316")},
      {QStringLiteral("Синий"), QColor("#2563EB")},
      {QStringLiteral("Зелёный"), QColor("#16A34A")},
      {QStringLiteral("Чёрный"), Qt::black},
      {QStringLiteral("Белый"), Qt::white}};
  for (auto pair : palette) {
    auto *a = colors->addAction(captureIcon("color", pair.second), pair.first);
    connect(a, &QAction::triggered, this, [this, pair] {
      canvas_->color = pair.second;
      overlayColor_->setIcon(captureIcon("color", pair.second));
      updateSelectionProperties();
    });
  }
  colors->addSeparator();
  colors->addAction(QStringLiteral("Другой цвет…"), this, [this] {
    chooseColor();
    overlayColor_->setIcon(captureIcon("color", canvas_->color));
  });
  overlayColor_->setMenu(colors);
  overlayColor_->setPopupMode(QToolButton::InstantPopup);
  overlayColor_->setIcon(captureIcon("color", canvas_->color));
  auto *properties =
      add("settings", QStringLiteral("Толщина линии, шрифт и размер текста"),
          [] {});
  auto *menu = new QMenu(properties);
  auto *widget = new QWidget(menu);
  widget->setStyleSheet(
      QStringLiteral("QWidget{background:white;color:#20242B;}QComboBox,"
                     "QSpinBox{background:white;color:#20242B;border:1px solid "
                     "#D1D5DB;border-radius:4px;padding:5px;}"));
  auto *form = new QFormLayout(widget);
  auto *lineWidth = new QSpinBox(widget);
  lineWidth->setRange(1, 40);
  lineWidth->setValue(width_->value());
  auto *family = new QFontComboBox(widget);
  family->setCurrentFont(font_->currentFont());
  auto *size = new QSpinBox(widget);
  size->setRange(8, 180);
  size->setValue(fontSize_->value());
  form->addRow(QStringLiteral("Толщина"), lineWidth);
  form->addRow(QStringLiteral("Шрифт"), family);
  form->addRow(QStringLiteral("Размер текста"), size);
  connect(lineWidth, qOverload<int>(&QSpinBox::valueChanged), width_,
          &QSpinBox::setValue);
  connect(family, &QFontComboBox::currentFontChanged, font_,
          &QFontComboBox::setCurrentFont);
  connect(size, qOverload<int>(&QSpinBox::valueChanged), fontSize_,
          &QSpinBox::setValue);
  auto *action = new QWidgetAction(menu);
  action->setDefaultWidget(widget);
  menu->addAction(action);
  properties->setMenu(menu);
  properties->setPopupMode(QToolButton::InstantPopup);
  separator();
  const QMap<int, QString> icons = {{EditorCanvas::Select, "select"},
                                    {EditorCanvas::Arrow, "arrow"},
                                    {EditorCanvas::Line, "line"},
                                    {EditorCanvas::Rectangle, "rectangle"},
                                    {EditorCanvas::Ellipse, "ellipse"},
                                    {EditorCanvas::Circle, "circle"},
                                    {EditorCanvas::Text, "text"},
                                    {EditorCanvas::Pencil, "pencil"},
                                    {EditorCanvas::Marker, "marker"},
                                    {EditorCanvas::Number, "number"},
                                    {EditorCanvas::Redact, "redact"},
                                    {EditorCanvas::Pixelate, "pixelate"},
                                    {EditorCanvas::Blur, "blur"},
                                    {EditorCanvas::Crop, "crop"}};
  auto *moreMenu = new QMenu(overlayToolbar_);
  for (auto *a : toolGroup_->actions()) {
    auto tool = a->data().toInt();
    a->setIcon(captureIcon(icons.value(tool)));
    bool primary =
        tool == EditorCanvas::Select || tool == EditorCanvas::Pencil ||
        tool == EditorCanvas::Arrow || tool == EditorCanvas::Ellipse ||
        tool == EditorCanvas::Rectangle || tool == EditorCanvas::Text ||
        tool == EditorCanvas::Redact || tool == EditorCanvas::Pixelate;
    if (primary) {
      auto *b = new QToolButton(overlayToolbar_);
      b->setDefaultAction(a);
      b->setToolButtonStyle(Qt::ToolButtonIconOnly);
      b->setFixedSize(31, 33);
      b->setIconSize(QSize(23, 23));
      b->setToolTip(a->text());
      b->setAccessibleName(a->text());
      row->addWidget(b);
    } else
      moreMenu->addAction(a);
  }
  auto *more = add(
      "settings",
      QStringLiteral(
          "Ещё инструменты: линия, круг, маркер, номера, размытие, обрезка"),
      [] {});
  more->setText(QStringLiteral("⋯"));
  more->setIcon(QIcon());
  more->setMenu(moreMenu);
  more->setPopupMode(QToolButton::InstantPopup);
  add("delete", QStringLiteral("Удалить выбранную пометку"),
      [this] { deleteSelection(); });
  separator();
  for (auto *a : findChildren<QAction *>())
    if (a->text() == QStringLiteral("Распознать текст")) {
      auto *button =
          add("ocr", QStringLiteral("Распознать текст в выделенной области"),
              [a] { a->trigger(); });
      Q_UNUSED(button);
      break;
    }
  add("save", QStringLiteral("Сохранить как…"), [this] {
    bool wrote = false;
    auto connection = connect(this, &Editor::saved, this,
                              [&wrote](const QString &) { wrote = true; });
    saveAs();
    disconnect(connection);
    if (wrote) {
      closingOverlay_ = true;
      close();
    }
  });
  add("copy", QStringLiteral("Скопировать изображение и завершить"),
      [this] { finishOverlay(true); });
  auto *done = add(
      "check",
      QStringLiteral("Готово — сохранить PNG в выбранную папку и скопировать"),
      [this] { finishOverlay(false); });
  done->setIcon(captureIcon("check", QColor("#C2410C")));
  done->setFixedWidth(37);
  add("close", QStringLiteral("Отменить захват (Esc)"), [this] {
    closingOverlay_ = true;
    close();
  });
  overlayToolbar_->adjustSize();
  if (overlayScreen_ &&
      overlayToolbar_->width() > overlayScreen_->geometry().width() - 16) {
    for (auto *button : overlayToolbar_->findChildren<QToolButton *>(
             QString(), Qt::FindDirectChildrenOnly)) {
      button->setFixedSize(26, 33);
      button->setIconSize(QSize(20, 20));
    }
    overlayToolbar_->adjustSize();
  }
  overlayToolbar_->raise();
  positionOverlayToolbar();
}
void Editor::positionOverlayToolbar() {
  if (!overlay_ || !overlayToolbar_)
    return;
  auto selection = canvas_->overlaySelection();
  if (selection.width() < 3 || selection.height() < 3) {
    overlayToolbar_->hide();
    return;
  }
  overlayToolbar_->adjustSize();
  auto bottom = canvas_->mapFromScene(selection.bottomLeft());
  auto top = canvas_->mapFromScene(selection.topLeft());
  int x =
      qBound(8, bottom.x(), qMax(8, width() - overlayToolbar_->width() - 8));
  int y = bottom.y() + 9;
  if (y + overlayToolbar_->height() > height() - 8)
    y = top.y() - overlayToolbar_->height() - 9;
  if (y < 8)
    y = qBound(8, bottom.y() - overlayToolbar_->height() - 9,
               qMax(8, height() - overlayToolbar_->height() - 8));
  overlayToolbar_->move(x, y);
  overlayToolbar_->show();
  overlayToolbar_->raise();
}
void Editor::finishOverlay(bool copyOnly) {
  if (canvas_->overlaySelection().isEmpty())
    return;
  QImage image = flattenedImage();
  if (!copyOnly) {
    if (!QDir().mkpath(saveDirectory_)) {
      QMessageBox::warning(
          this, QStringLiteral("Не удалось сохранить"),
          QStringLiteral("Выберите доступную папку в настройках."));
      return;
    }
    QString path = QDir(saveDirectory_)
                       .filePath(QStringLiteral("Кадролов_%1.png")
                                     .arg(QDateTime::currentDateTime().toString(
                                         "yyyy-MM-dd_HH-mm-ss-zzz")));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") ||
        !file.commit()) {
      QMessageBox::warning(this, QStringLiteral("Не удалось сохранить"),
                           file.errorString());
      return;
    }
    emit saved(path);
  }
  QApplication::clipboard()->setImage(image);
  exported_ = true;
  closingOverlay_ = true;
  close();
}
void Editor::keyPressEvent(QKeyEvent *event) {
  if (overlay_ && event->key() == Qt::Key_Escape) {
    closingOverlay_ = true;
    close();
    return;
  }
  QMainWindow::keyPressEvent(event);
}

QRect Editor::overlaySelectionGlobal() const {
  if (!overlay_ || !overlayScreen_ || canvas_->overlaySelection().isEmpty())
    return QRect();
  auto r = canvas_->overlaySelection();
  QRect rect(qRound(r.x() / overlayScale_), qRound(r.y() / overlayScale_),
             qRound(r.width() / overlayScale_),
             qRound(r.height() / overlayScale_));
  return rect.translated(overlayScreen_->geometry().topLeft());
}
