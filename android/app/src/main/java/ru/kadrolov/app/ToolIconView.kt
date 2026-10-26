package ru.kadrolov.app

import android.content.Context
import android.graphics.*
import android.view.View

/** Own vector-style canvas icons; accessible labels remain available without icon fonts. */
class ToolIconView(context: Context, val tool: String, label: String) : View(context) {
  private val paint = Paint(Paint.ANTI_ALIAS_FLAG)

  init {
    contentDescription = label
    tooltipText = label
    isClickable = true
    isFocusable = true
    setBackgroundResource(android.R.drawable.list_selector_background)
  }

  override fun onMeasure(w: Int, h: Int) {
    val size = (48 * resources.displayMetrics.density).toInt()
    setMeasuredDimension(resolveSize(size, w), resolveSize(size, h))
  }

  override fun onDraw(canvas: Canvas) {
    super.onDraw(canvas)
    canvas.save()
    canvas.scale(width / 48f, height / 48f)
    paint.reset()
    paint.isAntiAlias = true
    paint.color = if (isSelected) 0xFFC2410C.toInt() else 0xFF44403C.toInt()
    paint.strokeWidth = 2.5f
    paint.strokeCap = Paint.Cap.ROUND
    paint.style = Paint.Style.STROKE
    when (tool) {
      "select" -> {
        val p = Path()
        p.moveTo(14f, 10f)
        p.lineTo(35f, 27f)
        p.lineTo(25f, 29f)
        p.lineTo(22f, 39f)
        p.close()
        canvas.drawPath(p, paint)
      }
      "crop" -> {
        canvas.drawLine(12f, 8f, 12f, 34f, paint)
        canvas.drawLine(12f, 34f, 39f, 34f, paint)
        canvas.drawLine(8f, 14f, 34f, 14f, paint)
        canvas.drawLine(34f, 14f, 34f, 40f, paint)
      }
      "arrow" -> {
        canvas.drawLine(11f, 36f, 37f, 12f, paint)
        canvas.drawLine(37f, 12f, 22f, 12f, paint)
        canvas.drawLine(37f, 12f, 37f, 27f, paint)
      }
      "line" -> canvas.drawLine(12f, 36f, 36f, 12f, paint)
      "rect" -> canvas.drawRect(10f, 14f, 38f, 35f, paint)
      "circle" -> canvas.drawCircle(24f, 24f, 14f, paint)
      "oval" -> canvas.drawOval(8f, 15f, 40f, 33f, paint)
      "text" -> {
        canvas.drawLine(11f, 12f, 37f, 12f, paint)
        canvas.drawLine(24f, 12f, 24f, 37f, paint)
        canvas.drawLine(18f, 37f, 30f, 37f, paint)
      }
      "pen",
      "marker" -> {
        canvas.drawLine(13f, 34f, 34f, 13f, paint)
        canvas.drawLine(17f, 38f, 38f, 17f, paint)
        canvas.drawLine(13f, 34f, 12f, 39f, paint)
        canvas.drawLine(12f, 39f, 17f, 38f, paint)
        if (tool == "marker") {
          paint.strokeWidth = 6f
          canvas.drawLine(9f, 42f, 39f, 42f, paint)
        }
      }
      "cover" -> {
        paint.style = Paint.Style.FILL
        canvas.drawRect(10f, 15f, 38f, 34f, paint)
      }
      "pixel" -> {
        paint.style = Paint.Style.FILL
        for (y in 0..2) for (x in 0..2) {
          paint.alpha = if ((x + y) % 2 == 0) 255 else 90
          canvas.drawRect(11f + x * 9, 11f + y * 9, 18f + x * 9, 18f + y * 9, paint)
        }
      }
      "number" -> {
        canvas.drawCircle(24f, 24f, 15f, paint)
        paint.style = Paint.Style.FILL
        paint.textSize = 21f
        paint.textAlign = Paint.Align.CENTER
        canvas.drawText("1", 24f, 31f, paint)
      }
      "save" -> {
        canvas.drawRect(11f, 10f, 37f, 38f, paint)
        canvas.drawRect(17f, 10f, 30f, 21f, paint)
        canvas.drawRect(17f, 28f, 31f, 38f, paint)
      }
      "undo" -> {
        canvas.drawLine(19f, 13f, 10f, 22f, paint)
        canvas.drawLine(10f, 22f, 19f, 30f, paint)
        canvas.drawArc(10f, 20f, 39f, 40f, 180f, 190f, false, paint)
      }
      "redo" -> {
        canvas.drawLine(29f, 13f, 38f, 22f, paint)
        canvas.drawLine(38f, 22f, 29f, 30f, paint)
        canvas.drawArc(9f, 20f, 38f, 40f, -10f, 190f, false, paint)
      }
    }
    canvas.restore()
  }
}
