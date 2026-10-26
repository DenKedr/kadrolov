package ru.kadrolov.app

import android.app.*
import android.content.Intent
import android.graphics.*
import android.os.Bundle
import android.view.*
import android.widget.*
import java.io.File
import kotlin.concurrent.thread
import kotlin.math.*

class EditorActivity : Activity() {
  private lateinit var drawing: DrawingView
  private lateinit var status: TextView
  private var sourceFile: File? = null
  private var saved = false
  private var revision = 0L
  private var saving = false
  private var ocrBusy = false
  private var toolPopup: PopupWindow? = null

  override fun onCreate(state: Bundle?) {
    val mode = getSharedPreferences("settings", 0).getInt("theme", 0)
    val dark =
      mode == 2 ||
        (mode == 0 &&
          (resources.configuration.uiMode and
            android.content.res.Configuration.UI_MODE_NIGHT_MASK) ==
            android.content.res.Configuration.UI_MODE_NIGHT_YES)
    if (dark) setTheme(android.R.style.Theme_Material_NoActionBar)
    else setTheme(R.style.AppTheme_LightComfort)
    super.onCreate(state)
    val path = intent.getStringExtra("path") ?: return finish()
    sourceFile = File(path)
    val source = BitmapFactory.decodeFile(path) ?: return finish()
    val box =
      LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(8, 8, 8, 8)
        setBackgroundColor(if (dark) 0xFF18181B.toInt() else 0xFFEDE9E2.toInt())
      }
    setContentView(box)
    status =
      TextView(this).apply {
        text = "Редактор · выберите область или добавьте пометки"
        textSize = 17f
        setPadding(8, 8, 8, 8)
      }
    box.addView(status)
    val tools = LinearLayout(this)
    val scroller = HorizontalScrollView(this)
    scroller.addView(tools)
    box.addView(scroller)
    drawing =
      DrawingView(this, source) { tool ->
        saved = false
        revision++
        status.text = tool
      }
    val kinds =
      listOf(
        "Выбрать" to "select",
        "Область" to "crop",
        "Стрелка" to "arrow",
        "Линия" to "line",
        "Рамка" to "rect",
        "Круг" to "circle",
        "Овал" to "oval",
        "Текст" to "text",
        "Карандаш" to "pen",
        "Маркер" to "marker",
        "Закрыть" to "cover",
        "Пиксели" to "pixel",
        "Номер" to "number",
      )
    kinds.forEach { (title, kind) ->
      tools.addView(
        ToolIconView(this, kind, title).apply {
          setOnClickListener {
            drawing.tool = kind
            for (i in 0 until tools.childCount) {
              tools.getChildAt(i).isSelected = tools.getChildAt(i) === this
              tools.getChildAt(i).invalidate()
            }
            status.text = title + if (kind == "crop") " · выделите сохраняемую часть" else ""
          }
        }
      )
    }
    drawing.regionSelected = { bounds -> showRegionTools(bounds) }
    val props = LinearLayout(this)
    val propScroll = HorizontalScrollView(this)
    propScroll.addView(props)
    box.addView(propScroll)
    props.addView(
      Button(this).apply {
        text = "Цвет"
        setOnClickListener {
          val colors =
            intArrayOf(
              Color.RED,
              0xFFC2410C.toInt(),
              Color.BLUE,
              Color.GREEN,
              Color.BLACK,
              Color.WHITE,
              Color.YELLOW,
            )
          AlertDialog.Builder(this@EditorActivity)
            .setTitle("Цвет")
            .setItems(
              arrayOf("Красный", "Оранжевый", "Синий", "Зелёный", "Чёрный", "Белый", "Жёлтый")
            ) { _, i ->
              drawing.color = colors[i]
            }
            .show()
        }
      }
    )
    props.addView(
      Button(this).apply {
        text = "Шрифт"
        setOnClickListener {
          AlertDialog.Builder(this@EditorActivity)
            .setItems(arrayOf("Без засечек", "С засечками", "Моноширинный")) { _, i ->
              drawing.fontName = listOf("sans-serif", "serif", "monospace")[i]
            }
            .show()
        }
      }
    )
    props.addView(
      Button(this).apply {
        text = "Отмена"
        setOnClickListener { drawing.undo() }
      }
    )
    props.addView(
      Button(this).apply {
        text = "Повтор"
        setOnClickListener { drawing.redo() }
      }
    )
    val sizeLabel = TextView(this).apply { text = "Размер текста / толщина: 36" }
    box.addView(sizeLabel)
    box.addView(
      SeekBar(this).apply {
        max = 120
        progress = 32
        setOnSeekBarChangeListener(
          object : SeekBar.OnSeekBarChangeListener {
            override fun onStartTrackingTouch(s: SeekBar?) {}

            override fun onStopTrackingTouch(s: SeekBar?) {}

            override fun onProgressChanged(s: SeekBar?, v: Int, fromUser: Boolean) {
              drawing.textSize = (v + 4).toFloat()
              drawing.strokeWidth = maxOf(2f, (v + 4) / 7f)
              sizeLabel.text = "Размер текста / толщина: ${v+4}"
            }
          }
        )
      }
    )
    box.addView(drawing, LinearLayout.LayoutParams(-1, 0, 1f))
    val actions = LinearLayout(this)
    box.addView(actions)
    if (intent.getBooleanExtra("regionOnly", false)) {
      status.text = "Выделите область для будущей видеозаписи"
      actions.addView(
        Button(this).apply {
          text = "Использовать область для видео"
          setOnClickListener { acceptRegion() }
        },
        LinearLayout.LayoutParams(-1, -2),
      )
    } else {
      actions.addView(
        Button(this).apply {
          text = "Сохранить"
          setOnClickListener { save(false) }
        },
        LinearLayout.LayoutParams(0, -2, 1f),
      )
      actions.addView(
        Button(this).apply {
          text = "Сохранить и отправить"
          setOnClickListener { save(true) }
        },
        LinearLayout.LayoutParams(0, -2, 1f),
      )
    }
    props.addView(
      Button(this).apply {
        text = "Править текст"
        setOnClickListener { drawing.editSelectedText() }
      }
    )
    props.addView(
      Button(this).apply {
        text = "Удалить"
        setOnClickListener { drawing.deleteSelected() }
      }
    )
    props.addView(
      Button(this).apply {
        text = "Текст OCR"
        setOnClickListener { recognize() }
      }
    )
    box.addView(
      TextView(this).apply {
        text =
          "«Закрыть» полностью заменяет пиксели. После сохранения исходник удаляется из временного хранилища."
        textSize = 12f
      }
    )
  }

  private fun acceptRegion() {
    val r = drawing.selectedRegion()
    getSharedPreferences("settings", 0)
      .edit()
      .putFloat("cropX", r.left)
      .putFloat("cropY", r.top)
      .putFloat("cropWidth", r.width())
      .putFloat("cropHeight", r.height())
      .apply()
    sourceFile?.delete()
    saved = true
    startActivity(Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP))
    finish()
  }

  private fun showRegionTools(bounds: RectF) {
    if (isFinishing || isDestroyed) return
    toolPopup?.dismiss()
    val bar =
      LinearLayout(this).apply {
        setPadding(4, 4, 4, 4)
        setBackgroundColor(0xFFFFF7ED.toInt())
      }
    for ((kind, label) in
      listOf(
        "arrow" to "Стрелка",
        "rect" to "Рамка",
        "oval" to "Овал",
        "text" to "Текст",
        "cover" to "Закрыть",
        "save" to "Сохранить",
      )) bar.addView(
      ToolIconView(this, kind, label).apply {
        setOnClickListener {
          toolPopup?.dismiss()
          if (kind == "save") {
            if (intent.getBooleanExtra("regionOnly", false)) acceptRegion() else save(false)
          } else {
            drawing.tool = kind
            status.text = label
          }
        }
      }
    )
    val size = (48 * resources.displayMetrics.density).toInt()
    val location = IntArray(2)
    drawing.getLocationOnScreen(location)
    val screenWidth = resources.displayMetrics.widthPixels
    val screenHeight = resources.displayMetrics.heightPixels
    val x = (location[0] + bounds.left).toInt().coerceIn(0, maxOf(0, screenWidth - size * 6))
    val below = (location[1] + bounds.bottom + 8).toInt()
    val y =
      if (below + size < screenHeight) below
      else maxOf(0, (location[1] + bounds.top - size - 8).toInt())
    toolPopup =
      PopupWindow(bar, minOf(screenWidth, size * 6 + 8), size + 8, false).apply {
        isOutsideTouchable = true
        setBackgroundDrawable(android.graphics.drawable.ColorDrawable(Color.TRANSPARENT))
        elevation = 12f
        showAtLocation(drawing, Gravity.TOP or Gravity.START, x, y)
      }
  }

  override fun onDestroy() {
    toolPopup?.dismiss()
    super.onDestroy()
  }

  private fun recognize() {
    if (ocrBusy) {
      Toast.makeText(this, "Дождитесь завершения распознавания", Toast.LENGTH_SHORT).show()
      return
    }
    ocrBusy = true
    status.text = "Распознавание на устройстве…"
    val bitmap = drawing.exportBitmap()
    thread {
      try {
        val text = LocalOcr.recognize(this, bitmap)
        runOnUiThread {
          if (isFinishing || isDestroyed) return@runOnUiThread
          status.text = "Распознавание завершено"
          val result =
            EditText(this).apply {
              setText(text)
              minLines = 4
            }
          AlertDialog.Builder(this)
            .setTitle("Текст на изображении")
            .setView(result)
            .setPositiveButton("Копировать") { _, _ ->
              getSystemService(android.content.ClipboardManager::class.java)
                .setPrimaryClip(
                  android.content.ClipData.newPlainText("Текст изображения", result.text.toString())
                )
            }
            .setNegativeButton("Закрыть", null)
            .show()
        }
      } catch (e: Throwable) {
        runOnUiThread {
          if (isFinishing || isDestroyed) return@runOnUiThread
          status.text = "OCR недоступен: ${e.message}"
        }
      } finally {
        runOnUiThread { ocrBusy = false }
        bitmap.recycle()
      }
    }
  }

  private fun save(share: Boolean) {
    if (saving) return
    saving = true
    val atRevision = revision
    val result = drawing.exportBitmap()
    status.text = "Сохранение…"
    thread {
      try {
        val fmt = intent.getStringExtra("imageFormat") ?: "png"
        val uri =
          MediaFiles.saveBitmap(
            this,
            result,
            fmt,
            intent.getIntExtra("imageQuality", 95),
            intent.getStringExtra("tree"),
          )
        result.recycle()
        sourceFile?.delete()
        runOnUiThread {
          saving = false
          saved = revision == atRevision
          if (isFinishing || isDestroyed) return@runOnUiThread
          status.text = "Снимок сохранён"
          Toast.makeText(this, "Снимок сохранён", Toast.LENGTH_SHORT).show()
          if (share) {
            val mime = if (fmt == "jpg") "image/jpeg" else "image/$fmt"
            startActivity(
              Intent.createChooser(
                Intent(Intent.ACTION_SEND)
                  .setType(mime)
                  .putExtra(Intent.EXTRA_STREAM, uri)
                  .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION),
                "Поделиться снимком",
              )
            )
          }
        }
      } catch (e: Exception) {
        result.recycle()
        runOnUiThread {
          saving = false
          if (!isFinishing && !isDestroyed) status.text = "Не удалось сохранить: ${e.message}"
        }
      }
    }
  }

  @Deprecated("Deprecated in Java")
  override fun onBackPressed() {
    if (saved) {
      super.onBackPressed()
      return
    }
    AlertDialog.Builder(this)
      .setTitle("Закрыть редактор?")
      .setMessage("Несохранённые изменения будут потеряны")
      .setPositiveButton("Закрыть") { _, _ ->
        sourceFile?.delete()
        finish()
      }
      .setNegativeButton("Продолжить", null)
      .show()
  }
}

class DrawingView(
  context: android.content.Context,
  private val source: Bitmap,
  private val changed: (String) -> Unit,
) : View(context) {
  var regionSelected: ((RectF) -> Unit)? = null
  var tool = "crop"
    set(value) {
      field = value
      selected = null
      invalidate()
    }

  var color = Color.RED
    set(value) {
      field = value
      selected?.let {
        if (it.color != value) {
          checkpoint()
          it.color = value
          changed("Цвет изменён")
          invalidate()
        }
      }
    }

  var strokeWidth = 5f
    set(value) {
      field = value
      selected?.let {
        if (it.width != value) {
          checkpoint()
          it.width = value
          changed("Толщина изменена")
          invalidate()
        }
      }
    }

  var textSize = 36f
    set(value) {
      field = value
      selected?.let {
        if (it.size != value) {
          checkpoint()
          it.size = value
          changed("Размер изменён")
          invalidate()
        }
      }
    }

  var fontName = "sans-serif"
    set(value) {
      field = value
      selected?.let {
        if (it.font != value) {
          checkpoint()
          it.font = value
          changed("Шрифт изменён")
          invalidate()
        }
      }
    }

  private data class Mark(
    val kind: String,
    var points: MutableList<PointF>,
    var color: Int,
    var width: Float,
    var size: Float,
    var font: String,
    var text: String = "",
  )

  private val marks = mutableListOf<Mark>()
  private val undoStates = mutableListOf<List<Mark>>()
  private val redoStates = mutableListOf<List<Mark>>()
  private var active: Mark? = null
  private var scale = 1f
  private var offsetX = 0f
  private var offsetY = 0f
  private var selected: Mark? = null
  private var moving: Mark? = null
  private var previous = PointF()
  private var number = 1
  private val p = Paint(Paint.ANTI_ALIAS_FLAG)

  init {
    setLayerType(LAYER_TYPE_SOFTWARE, null)
    contentDescription = "Изображение для пометок и выбора области"
  }

  private fun snapshot() =
    marks.map { it.copy(points = it.points.map { p -> PointF(p.x, p.y) }.toMutableList()) }

  private fun checkpoint() {
    undoStates += snapshot()
    if (undoStates.size > 100) undoStates.removeAt(0)
    redoStates.clear()
  }

  fun undo() {
    selected = null
    changed("Отмена")
    if (undoStates.isNotEmpty()) {
      redoStates += snapshot()
      marks.clear()
      marks.addAll(undoStates.removeAt(undoStates.lastIndex))
      invalidate()
    }
  }

  fun redo() {
    selected = null
    changed("Повтор")
    if (redoStates.isNotEmpty()) {
      undoStates += snapshot()
      marks.clear()
      marks.addAll(redoStates.removeAt(redoStates.lastIndex))
      invalidate()
    }
  }

  override fun onDraw(canvas: Canvas) {
    super.onDraw(canvas)
    scale = minOf(width.toFloat() / source.width, height.toFloat() / source.height)
    offsetX = (width - source.width * scale) / 2
    offsetY = (height - source.height * scale) / 2
    canvas.drawColor(0xFFE5E7EB.toInt())
    canvas.save()
    canvas.translate(offsetX, offsetY)
    canvas.scale(scale, scale)
    canvas.drawBitmap(source, 0f, 0f, null)
    marks.forEach { draw(canvas, it, false) }
    active?.let { draw(canvas, it, false) }
    selected?.let {
      val r = rect(it)
      r.inset(-maxOf(8f, it.width), -maxOf(8f, it.width))
      p.reset()
      p.color = Color.CYAN
      p.style = Paint.Style.STROKE
      p.strokeWidth = 2f / scale
      canvas.drawRect(r, p)
    }
    canvas.restore()
  }

  private fun rect(m: Mark): RectF {
    val a = m.points.first()
    val b = m.points.last()
    if (m.kind == "circle") {
      val radius = hypot(b.x - a.x, b.y - a.y)
      return RectF(a.x - radius, a.y - radius, a.x + radius, a.y + radius)
    }
    if (m.kind == "number") {
      val radius = m.size * .6f
      return RectF(a.x - radius, a.y - radius, a.x + radius, a.y + radius)
    }
    if (m.kind == "text") {
      val paint =
        Paint().apply {
          textSize = m.size
          typeface = Typeface.create(m.font, Typeface.NORMAL)
        }
      val lines = m.text.split('\n')
      return RectF(
        a.x,
        a.y - m.size,
        a.x + (lines.maxOfOrNull { paint.measureText(it) } ?: m.size),
        a.y + (lines.size - 1) * m.size * 1.2f,
      )
    }
    return RectF(min(a.x, b.x), min(a.y, b.y), max(a.x, b.x), max(a.y, b.y))
  }

  private fun draw(c: Canvas, m: Mark, export: Boolean) {
    val a = m.points.first()
    val b = m.points.last()
    p.reset()
    p.isAntiAlias = true
    p.color = m.color
    p.strokeWidth = m.width
    p.strokeCap = Paint.Cap.ROUND
    p.style = Paint.Style.STROKE
    when (m.kind) {
      "line" -> c.drawLine(a.x, a.y, b.x, b.y, p)
      "arrow" -> {
        c.drawLine(a.x, a.y, b.x, b.y, p)
        val angle = atan2(b.y - a.y, b.x - a.x)
        val len = maxOf(m.width * 4, 20f)
        c.drawLine(b.x, b.y, b.x - len * cos(angle - 0.5f), b.y - len * sin(angle - 0.5f), p)
        c.drawLine(b.x, b.y, b.x - len * cos(angle + 0.5f), b.y - len * sin(angle + 0.5f), p)
      }
      "rect" -> c.drawRect(rect(m), p)
      "crop" -> {
        if (!export) {
          p.color = 0xFFFF8A00.toInt()
          p.pathEffect = DashPathEffect(floatArrayOf(12f, 8f), 0f)
          c.drawRect(rect(m), p)
        }
      }
      "oval" -> c.drawOval(rect(m), p)
      "circle" -> {
        val r = hypot(b.x - a.x, b.y - a.y)
        c.drawCircle(a.x, a.y, r, p)
      }
      "cover" -> {
        p.style = Paint.Style.FILL
        c.drawRect(rect(m), p)
      }
      "pixel" -> {
        val r = rect(m)
        val x = r.left.toInt().coerceIn(0, source.width - 1)
        val y = r.top.toInt().coerceIn(0, source.height - 1)
        val w = maxOf(1, r.width().toInt()).coerceAtMost(source.width - x)
        val h = maxOf(1, r.height().toInt()).coerceAtMost(source.height - y)
        val crop = Bitmap.createBitmap(source, x, y, w, h)
        val small = Bitmap.createScaledBitmap(crop, maxOf(1, w / 16), maxOf(1, h / 16), false)
        p.isFilterBitmap = false
        c.drawBitmap(
          small,
          null,
          RectF(x.toFloat(), y.toFloat(), (x + w).toFloat(), (y + h).toFloat()),
          p,
        )
        if (crop !== source) crop.recycle()
        small.recycle()
      }
      "pen",
      "marker" -> {
        if (m.kind == "marker") {
          p.alpha = 90
          p.strokeWidth = m.width * 4
        }
        val path = Path()
        path.moveTo(a.x, a.y)
        m.points.drop(1).forEach { path.lineTo(it.x, it.y) }
        c.drawPath(path, p)
      }
      "text" -> {
        p.style = Paint.Style.FILL
        p.textSize = m.size
        p.typeface = Typeface.create(m.font, Typeface.NORMAL)
        m.text.split('\n').forEachIndexed { i, line ->
          c.drawText(line, a.x, a.y + i * m.size * 1.2f, p)
        }
      }
      "number" -> {
        p.style = Paint.Style.FILL
        c.drawCircle(a.x, a.y, m.size * 0.6f, p)
        p.color = Color.WHITE
        p.textAlign = Paint.Align.CENTER
        p.textSize = m.size
        p.typeface = Typeface.DEFAULT_BOLD
        c.drawText(m.text, a.x, a.y + m.size * 0.35f, p)
      }
    }
  }

  override fun onTouchEvent(event: MotionEvent): Boolean {
    val point =
      PointF(
        ((event.x - offsetX) / scale).coerceIn(0f, source.width.toFloat()),
        ((event.y - offsetY) / scale).coerceIn(0f, source.height.toFloat()),
      )
    when (event.action) {
      MotionEvent.ACTION_DOWN -> {
        parent.requestDisallowInterceptTouchEvent(true)
        if (tool == "text") {
          val input =
            EditText(context).apply {
              hint = "Введите текст"
              minLines = 2
            }
          AlertDialog.Builder(context)
            .setTitle("Текст")
            .setView(input)
            .setPositiveButton("Добавить") { _, _ ->
              if (input.text.isNotBlank()) {
                checkpoint()
                marks +=
                  Mark(
                    "text",
                    mutableListOf(point),
                    color,
                    strokeWidth,
                    textSize,
                    fontName,
                    input.text.toString(),
                  )
                changed("Текст добавлен")
                invalidate()
              }
            }
            .setNegativeButton("Отмена", null)
            .show()
          return true
        }
        if (tool == "select") {
          moving =
            marks.asReversed().firstOrNull { m ->
              val r = rect(m)
              r.inset(-maxOf(40f, m.size), -maxOf(40f, m.size))
              r.contains(point.x, point.y)
            }
          selected = moving
          if (moving != null) checkpoint()
          previous = point
          invalidate()
          return true
        }
        selected = null
        active =
          Mark(
            tool,
            mutableListOf(point, PointF(point.x, point.y)),
            color,
            strokeWidth,
            textSize,
            fontName,
            if (tool == "number") (number++).toString() else "",
          )
        return true
      }
      MotionEvent.ACTION_MOVE -> {
        if (tool == "select") {
          moving?.points?.forEach { it.offset(point.x - previous.x, point.y - previous.y) }
          previous = point
        } else
          active?.let {
            if (tool == "pen" || tool == "marker") it.points += point
            else it.points[it.points.lastIndex] = point
          }
        invalidate()
        return true
      }
      MotionEvent.ACTION_UP -> {
        if (tool == "select") {
          if (moving != null) changed("Объект перемещён")
          moving = null
          invalidate()
          return true
        }
        active?.let {
          checkpoint()
          if (it.kind == "crop") marks.removeAll { m -> m.kind == "crop" }
          marks += it
        }
        active = null
        changed("Изменения")
        if (tool == "crop") {
          marks
            .lastOrNull { it.kind == "crop" }
            ?.let {
              val r = rect(it)
              regionSelected?.invoke(
                RectF(
                  r.left * scale + offsetX,
                  r.top * scale + offsetY,
                  r.right * scale + offsetX,
                  r.bottom * scale + offsetY,
                )
              )
            }
        }
        invalidate()
        performClick()
        return true
      }
      MotionEvent.ACTION_CANCEL -> {
        active = null
        moving = null
        invalidate()
        return true
      }
    }
    return false
  }

  override fun performClick(): Boolean {
    super.performClick()
    return true
  }

  fun deleteSelected() {
    selected?.let {
      checkpoint()
      marks.remove(it)
      selected = null
      changed("Объект удалён")
      invalidate()
    }
  }

  fun editSelectedText() {
    val mark = selected
    if (mark == null || mark.kind != "text") {
      Toast.makeText(context, "Выберите текст инструментом «Выбрать»", Toast.LENGTH_SHORT).show()
      return
    }
    val input =
      EditText(context).apply {
        setText(mark.text)
        minLines = 2
      }
    AlertDialog.Builder(context)
      .setTitle("Изменить текст")
      .setView(input)
      .setPositiveButton("Сохранить") { _, _ ->
        checkpoint()
        mark.text = input.text.toString()
        changed("Текст изменён")
        invalidate()
      }
      .setNegativeButton("Отмена", null)
      .show()
  }

  fun selectedRegion(): RectF {
    val r =
      marks.lastOrNull { it.kind == "crop" }?.let { rect(it) }
        ?: RectF(0f, 0f, source.width.toFloat(), source.height.toFloat())
    val left = (r.left / source.width).coerceIn(0f, 0.99f)
    val top = (r.top / source.height).coerceIn(0f, 0.99f)
    return RectF(
      left,
      top,
      (r.right / source.width).coerceIn(left + 0.01f, 1f),
      (r.bottom / source.height).coerceIn(top + 0.01f, 1f),
    )
  }

  fun exportBitmap(): Bitmap {
    val flat = Bitmap.createBitmap(source.width, source.height, Bitmap.Config.ARGB_8888)
    val c = Canvas(flat)
    c.drawBitmap(source, 0f, 0f, null)
    marks.filter { it.kind != "crop" }.forEach { draw(c, it, true) }
    val crop = marks.lastOrNull { it.kind == "crop" }?.let { rect(it) } ?: return flat
    val x = crop.left.toInt().coerceIn(0, flat.width - 1)
    val y = crop.top.toInt().coerceIn(0, flat.height - 1)
    val w = crop.width().toInt().coerceIn(1, flat.width - x)
    val h = crop.height().toInt().coerceIn(1, flat.height - y)
    val result = Bitmap.createBitmap(flat, x, y, w, h)
    if (result !== flat) flat.recycle()
    return result
  }
}
