#pragma once
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QString>
// Small original vector glyphs. No external icon fonts or borrowed artwork.
inline QIcon captureIcon(const QString &name,
                         QColor color = QColor("#4B5059")) {
  QPixmap bitmap(64, 64);
  bitmap.fill(Qt::transparent);
  bitmap.setDevicePixelRatio(2);
  QPainter p(&bitmap);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(Qt::NoBrush);
  if (name == "arrow") {
    p.drawLine(7, 25, 25, 7);
    p.drawLine(15, 7, 25, 7);
    p.drawLine(25, 7, 25, 17);
  } else if (name == "line")
    p.drawLine(7, 25, 25, 7);
  else if (name == "rectangle")
    p.drawRect(QRectF(7, 8, 18, 16));
  else if (name == "ellipse")
    p.drawEllipse(QRectF(6, 9, 20, 14));
  else if (name == "circle")
    p.drawEllipse(QRectF(7, 7, 18, 18));
  else if (name == "text") {
    QFont font = p.font();
    font.setPixelSize(23);
    font.setBold(true);
    p.setFont(font);
    p.drawText(QRect(2, 1, 28, 30), Qt::AlignCenter, "A");
  } else if (name == "number") {
    p.setBrush(color);
    p.drawEllipse(QRectF(6, 6, 20, 20));
    p.setPen(Qt::white);
    QFont font = p.font();
    font.setPixelSize(15);
    font.setBold(true);
    p.setFont(font);
    p.drawText(QRect(5, 5, 22, 22), Qt::AlignCenter, "1");
  } else if (name == "pencil" || name == "marker") {
    QPainterPath path;
    path.moveTo(7, 25);
    path.lineTo(10, 17);
    path.lineTo(23, 4);
    path.lineTo(28, 9);
    path.lineTo(15, 22);
    path.closeSubpath();
    if (name == "marker")
      p.setBrush(color);
    p.drawPath(path);
    p.drawLine(10, 17, 15, 22);
  } else if (name == "redact") {
    p.setBrush(color);
    p.drawRoundedRect(QRectF(6, 8, 20, 16), 2, 2);
  } else if (name == "pixelate") {
    p.setPen(Qt::NoPen);
    for (int y = 0; y < 3; ++y)
      for (int x = 0; x < 3; ++x) {
        auto c = color;
        c.setAlpha((x + y) % 2 ? 100 : 230);
        p.setBrush(c);
        p.drawRect(6 + x * 7, 6 + y * 7, 6, 6);
      }
  } else if (name == "blur") {
    for (int i = 0; i < 4; ++i) {
      auto c = color;
      c.setAlpha(55 + i * 40);
      p.setPen(QPen(c, 3));
      p.drawLine(7, 8 + i * 5, 25, 8 + i * 5);
    }
  } else if (name == "crop") {
    p.drawLine(10, 4, 10, 23);
    p.drawLine(10, 23, 28, 23);
    p.drawLine(4, 10, 23, 10);
    p.drawLine(23, 10, 23, 28);
  } else if (name == "select") {
    QPainterPath path;
    path.moveTo(8, 5);
    path.lineTo(9, 26);
    path.lineTo(15, 20);
    path.lineTo(20, 28);
    path.lineTo(24, 25);
    path.lineTo(19, 18);
    path.lineTo(27, 16);
    path.closeSubpath();
    p.drawPath(path);
  } else if (name == "undo" || name == "redo") {
    if (name == "redo") {
      p.translate(32, 0);
      p.scale(-1, 1);
    }
    QPainterPath path;
    path.moveTo(9, 11);
    path.cubicTo(26, 5, 30, 25, 13, 25);
    p.drawPath(path);
    p.drawLine(9, 5, 9, 12);
    p.drawLine(9, 12, 16, 13);
  } else if (name == "save") {
    p.drawRoundedRect(QRectF(6, 5, 20, 23), 2, 2);
    p.drawRect(QRectF(11, 5, 11, 8));
    p.drawRect(QRectF(10, 19, 12, 9));
  } else if (name == "copy") {
    p.drawRoundedRect(QRectF(6, 10, 15, 18), 2, 2);
    QPolygonF corner;
    corner << QPointF(12, 7) << QPointF(12, 4) << QPointF(27, 4)
           << QPointF(27, 21) << QPointF(24, 21);
    p.drawPolyline(corner);
  } else if (name == "check") {
    p.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(6, 17, 13, 24);
    p.drawLine(13, 24, 27, 8);
  } else if (name == "close") {
    p.drawLine(8, 8, 24, 24);
    p.drawLine(24, 8, 8, 24);
  } else if (name == "pause") {
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(9, 7, 5, 18), 1, 1);
    p.drawRoundedRect(QRectF(19, 7, 5, 18), 1, 1);
  } else if (name == "play") {
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    QPolygonF triangle;
    triangle << QPointF(10, 6) << QPointF(27, 16) << QPointF(10, 26);
    p.drawPolygon(triangle);
  } else if (name == "stop") {
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(7, 7, 18, 18), 3, 3);
  } else if (name == "record") {
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QRectF(7, 7, 18, 18));
  } else if (name == "color") {
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QRectF(6, 6, 20, 20));
  } else if (name == "settings") {
    p.drawLine(6, 9, 26, 9);
    p.drawLine(6, 16, 26, 16);
    p.drawLine(6, 23, 26, 23);
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(12, 9), 3, 3);
    p.drawEllipse(QPointF(22, 16), 3, 3);
    p.drawEllipse(QPointF(14, 23), 3, 3);
  } else if (name == "ocr") {
    p.drawLine(5, 12, 5, 5);
    p.drawLine(5, 5, 12, 5);
    p.drawLine(20, 5, 27, 5);
    p.drawLine(27, 5, 27, 12);
    p.drawLine(5, 20, 5, 27);
    p.drawLine(5, 27, 12, 27);
    p.drawLine(20, 27, 27, 27);
    p.drawLine(27, 27, 27, 20);
    QFont f = p.font();
    f.setPixelSize(15);
    p.setFont(f);
    p.drawText(QRect(5, 5, 22, 22), Qt::AlignCenter, "T");
  } else if (name == "grip") {
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    for (int y = 0; y < 3; ++y)
      for (int x = 0; x < 2; ++x)
        p.drawEllipse(QPointF(12 + x * 7, 9 + y * 7), 1.5, 1.5);
  } else if (name == "delete") {
    p.drawLine(7, 9, 25, 9);
    p.drawLine(12, 5, 20, 5);
    p.drawLine(10, 10, 11, 27);
    p.drawLine(22, 10, 21, 27);
    p.drawLine(11, 27, 21, 27);
    p.drawLine(15, 13, 15, 23);
    p.drawLine(18, 13, 18, 23);
  }
  return QIcon(bitmap);
}
