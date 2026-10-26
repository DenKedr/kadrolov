#pragma once
#include <QColor>
#include <QGraphicsView>
#include <QImage>
#include <QMainWindow>
#include <QPainterPath>
#include <QUndoStack>
class QGraphicsScene;
class QGraphicsItem;
class QComboBox;
class QSpinBox;
class QFontComboBox;
class QScreen;
class QToolButton;
class QActionGroup;
class EditorCanvas : public QGraphicsView {
  Q_OBJECT
public:
  enum Tool {
    Select,
    Arrow,
    Line,
    Rectangle,
    Ellipse,
    Circle,
    Text,
    Pencil,
    Marker,
    Number,
    Redact,
    Pixelate,
    Blur,
    Crop
  };
  explicit EditorCanvas(QWidget *parent = nullptr);
  Tool tool = Select;
  QColor color = QColor("#E64A19");
  int stroke = 4, textSize = 24;
  QFont textFont;
  int nextNumber = 1;
  void setOverlayMode(bool enabled, qreal scale = 1);
  void setOverlaySelection(const QRectF &rect);
  QRectF overlaySelection() const { return selection_; }
  bool overlayMode() const { return overlayMode_; }

signals:
  void itemCreated(QGraphicsItem *item);
  void areaCreated(const QRectF &rect, int tool);
  void itemMoved(QGraphicsItem *item, const QPointF &before,
                 const QPointF &after);
  void deleteRequested();
  void selectionChanged(const QRectF &rect);
  void selectionEdited(const QRectF &before, const QRectF &after);

protected:
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void keyPressEvent(QKeyEvent *) override;
  void drawForeground(QPainter *, const QRectF &) override;

private:
  int handleAt(const QPointF &point) const;
  bool overlayMode_ = false, selectionDragging_ = false;
  qreal overlayScale_ = 1;
  QRectF selection_, originalSelection_;
  QPointF selectionDragStart_;
  int selectionHandle_ = -1;
  QPointF origin_, last_, previousPosition_;
  QGraphicsItem *preview_ = nullptr, *moving_ = nullptr;
  QPainterPath path_;
  bool drawing_ = false;
  QGraphicsItem *shape(const QPointF &end);
};
class Editor : public QMainWindow {
  Q_OBJECT
public:
  explicit Editor(const QImage &image, QWidget *parent = nullptr);
  QImage flattenedImage() const;
  void addRedaction(const QRect &rect, const QColor &color = Qt::black);
  void cropTo(const QRect &rect);
  void setDefaultSaveDirectory(const QString &directory);
  void enableOverlay(QScreen *screen, const QRect &initialGlobalRect = QRect(),
                     bool videoSelection = false);
  bool isOverlay() const { return overlay_; }
  QRect overlaySelectionGlobal() const;

public slots:
  void undo();
  void redo();
  void saveAs();
signals:
  void saved(const QString &path);
  void overlayFinished();
  void videoRegionRequested(const QRect &rect, QScreen *screen);

protected:
  void closeEvent(QCloseEvent *) override;
  void keyPressEvent(QKeyEvent *) override;

private:
  void buildOverlayToolbar();
  void positionOverlayToolbar();
  void finishOverlay(bool copyOnly);
  void addItem(QGraphicsItem *);
  void deleteSelection();
  void processArea(const QRectF &, int tool);
  void setTool(EditorCanvas::Tool tool);
  void updateSelectionProperties(int property = 0);
  void chooseColor();
  bool overlay_ = false, closingOverlay_ = false, videoSelection_ = false;
  qreal overlayScale_ = 1;
  QScreen *overlayScreen_ = nullptr;
  QWidget *overlayToolbar_ = nullptr;
  QToolButton *overlayColor_ = nullptr;
  QActionGroup *toolGroup_ = nullptr;
  QGraphicsScene *scene_;
  EditorCanvas *canvas_;
  QUndoStack undo_;
  QString saveDirectory_;
  bool exported_ = false;
  QAction *colorAction_;
  QFontComboBox *font_;
  QSpinBox *fontSize_, *width_;
};
