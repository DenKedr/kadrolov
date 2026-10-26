package ru.kadrolov.app

import android.Manifest
import android.app.*
import android.content.*
import android.content.pm.PackageManager
import android.graphics.Color
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.media.projection.MediaProjectionManager
import android.net.Uri
import android.os.*
import android.provider.Settings
import android.view.*
import android.widget.*

class MainActivity : Activity() {
  private val prefs by lazy { getSharedPreferences("settings", 0) }
  private lateinit var status: TextView
  private lateinit var audio: Spinner
  private lateinit var microphone: Spinner
  private var microphoneChoices: List<MicrophoneDevices.Choice> = emptyList()
  private var listeningToAudioDevices = false
  private val microphoneCallback =
    object : AudioDeviceCallback() {
      override fun onAudioDevicesAdded(devices: Array<out AudioDeviceInfo>) {
        refreshMicrophones()
      }

      override fun onAudioDevicesRemoved(devices: Array<out AudioDeviceInfo>) {
        refreshMicrophones()
      }
    }
  private lateinit var size: Spinner
  private lateinit var fps: Spinner
  private lateinit var quality: Spinner
  private lateinit var imageFormat: Spinner
  private lateinit var imageQuality: Spinner
  private lateinit var videoFormat: Spinner
  private lateinit var camera: CheckBox
  private lateinit var cameraShape: Spinner
  private lateinit var cameraPosition: Spinner
  private lateinit var cameraSize: Spinner
  private lateinit var cameraFacing: Spinner
  private lateinit var overlay: CheckBox
  private lateinit var folder: TextView
  private lateinit var controlRow: LinearLayout
  private lateinit var pause: Button
  private var pendingScreenshot = false
  private var pendingVideoRegion = false
  private val handler = Handler(Looper.getMainLooper())
  private val tick =
    object : Runnable {
      override fun run() {
        status.text =
          when (CaptureState.state) {
            "recording" -> "Идёт запись · ${CaptureService.time(CaptureState.elapsedMs)}"
            "paused" -> "Пауза · ${CaptureService.time(CaptureState.elapsedMs)}"
            "saving" -> "Сохранение видео…"
            "starting" -> "Подготовка…"
            else -> CaptureState.message.ifEmpty { "Всё остаётся на вашем устройстве" }
          }
        controlRow.visibility =
          if (CaptureState.state == "recording" || CaptureState.state == "paused") View.VISIBLE
          else View.GONE
        pause.text = if (CaptureState.state == "paused") "Продолжить" else "Пауза"
        handler.postDelayed(this, 500)
      }
    }

  override fun onCreate(savedInstanceState: Bundle?) {
    val mode = prefs.getInt("theme", 0)
    val dark =
      mode == 2 ||
        (mode == 0 &&
          (resources.configuration.uiMode and
            android.content.res.Configuration.UI_MODE_NIGHT_MASK) ==
            android.content.res.Configuration.UI_MODE_NIGHT_YES)
    if (dark) setTheme(android.R.style.Theme_Material_NoActionBar)
    else setTheme(R.style.AppTheme_LightComfort)
    super.onCreate(savedInstanceState)
    buildUi()
    if (
      Build.VERSION.SDK_INT >= 33 &&
        checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) !=
          PackageManager.PERMISSION_GRANTED
    )
      requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 90)
    handleQuick(intent)
  }

  private fun dark() =
    prefs.getInt("theme", 0) == 2 ||
      (prefs.getInt("theme", 0) == 0 &&
        (resources.configuration.uiMode and android.content.res.Configuration.UI_MODE_NIGHT_MASK) ==
          android.content.res.Configuration.UI_MODE_NIGHT_YES)

  private fun buildUi() {
    val scroll = ScrollView(this)
    val box =
      LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(20), dp(20), dp(20), dp(32))
        setBackgroundColor(if (dark()) 0xFF18181B.toInt() else 0xFFEDE9E2.toInt())
      }
    scroll.addView(box)
    setContentView(scroll)
    fun label(t: String, n: Float = 16f) =
      TextView(this).apply {
        text = t
        textSize = n
        setTextColor(if (dark()) Color.WHITE else Color.rgb(31, 41, 55))
        setPadding(0, dp(8), 0, dp(8))
        box.addView(this)
      }
    label("Кадролов", 30f)
    label("Снимки и запись экрана", 18f)
    status = label("Всё остаётся на вашем устройстве")
    val primary = LinearLayout(this)
    box.addView(primary)
    primary.addView(
      Button(this).apply {
        text = "Снимок"
        textSize = 20f
        setOnClickListener {
          pendingVideoRegion = false
          begin(true)
        }
      },
      LinearLayout.LayoutParams(0, dp(72), 1f),
    )
    primary.addView(
      Button(this).apply {
        text = "Запись"
        textSize = 20f
        setOnClickListener {
          pendingVideoRegion = false
          begin(false)
        }
      },
      LinearLayout.LayoutParams(0, dp(72), 1f),
    )
    label(
      "Источник выбирается в системном окне Android. Для снимка область можно обрезать в редакторе.",
      14f,
    )
    box.addView(
      Button(this).apply {
        text = "Выбрать область видео"
        setOnClickListener {
          pendingVideoRegion = true
          begin(true)
        }
      }
    )
    box.addView(
      Button(this).apply {
        text = "Видео: весь выбранный источник"
        setOnClickListener {
          prefs
            .edit()
            .remove("cropX")
            .remove("cropY")
            .remove("cropWidth")
            .remove("cropHeight")
            .apply()
          Toast.makeText(this@MainActivity, "Область видео сброшена", Toast.LENGTH_SHORT).show()
        }
      }
    )
    controlRow = LinearLayout(this)
    box.addView(controlRow)
    pause =
      Button(this).apply {
        text = "Пауза"
        setOnClickListener { send(if (CaptureState.state == "paused") "resume" else "pause") }
      }
    controlRow.addView(pause, LinearLayout.LayoutParams(0, dp(52), 1f))
    controlRow.addView(
      Button(this).apply {
        text = "Стоп"
        setOnClickListener { send("stop") }
      },
      LinearLayout.LayoutParams(0, dp(52), 1f),
    )
    controlRow.visibility = View.GONE
    fun spinner(title: String, values: List<String>, key: String): Spinner {
      label(title)
      return Spinner(this).apply {
        adapter =
          ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item, values)
        setSelection(prefs.getInt(key, 0).coerceIn(0, values.lastIndex))
        box.addView(this, LinearLayout.LayoutParams(-1, dp(48)))
        onItemSelectedListener =
          object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onNothingSelected(p: android.widget.AdapterView<*>?) {}

            override fun onItemSelected(
              p: android.widget.AdapterView<*>?,
              v: View?,
              position: Int,
              id: Long,
            ) {
              prefs.edit().putInt(key, position).apply()
            }
          }
      }
    }
    val themes = spinner("Тема", listOf("Как в системе", "Светлая", "Тёмная"), "theme")
    themes.onItemSelectedListener =
      object : AdapterView.OnItemSelectedListener {
        override fun onNothingSelected(p: AdapterView<*>?) {}

        override fun onItemSelected(p: AdapterView<*>?, v: View?, position: Int, id: Long) {
          if (prefs.getInt("theme", 0) != position) {
            prefs.edit().putInt("theme", position).apply()
            recreate()
          }
        }
      }
    audio =
      spinner(
        "Звук видео",
        listOf("Без звука", "Звук устройства", "Микрофон", "Устройство и микрофон"),
        "audio",
      )
    label(
      "Android разрешает запись системного звука только для приложений, которые её не запрещают. Для звука нужен системный доступ к аудио.",
      13f,
    )
    label("Микрофон для записи")
    microphone = Spinner(this)
    microphone.contentDescription = "Устройство микрофона"
    box.addView(microphone, LinearLayout.LayoutParams(-1, dp(48)))
    refreshMicrophones()
    box.addView(
      Button(this).apply {
        text = "Обновить список микрофонов"
        setOnClickListener { refreshMicrophones() }
      }
    )
    label(
      "Выбор применяется только к режимам «Микрофон» и «Устройство и микрофон». Системный звук не меняется. Если выбранное устройство отключится, запись остановится без подмены микрофона.",
      13f,
    )
    size =
      spinner(
        "Размер видео",
        listOf("До 1080p — обычный", "До 720p — меньше файл", "Исходный размер"),
        "size",
      )
    quality =
      spinner(
        "Качество видео",
        listOf("Обычное · 8 Мбит/с", "Высокое · 16 Мбит/с", "Компактное · 3 Мбит/с"),
        "quality",
      )
    fps = spinner("Частота кадров", listOf("30 кадров/с", "24 кадра/с", "60 кадров/с"), "fps")
    videoFormat =
      spinner("Формат видео", listOf("MP4 · H.264 / AAC", "WebM · VP8 / Opus"), "videoFormat")
    label(
      "Набор кодировщиков зависит от устройства. Если выбранный формат недоступен, программа сообщит об этом до записи.",
      13f,
    )
    camera =
      CheckBox(this).apply {
        text = "Камера поверх видео · нижний правый угол"
        isChecked = prefs.getBoolean("camera", false)
        setOnCheckedChangeListener { _, checked ->
          prefs.edit().putBoolean("camera", checked).apply()
        }
      }
    box.addView(camera)
    cameraFacing = spinner("Камера", listOf("Фронтальная", "Основная"), "cameraFacing")
    cameraShape = spinner("Форма камеры", listOf("Прямоугольник", "Круг"), "cameraShape")
    cameraPosition =
      spinner(
        "Положение камеры",
        listOf("Справа внизу", "Слева внизу", "Справа вверху", "Слева вверху"),
        "cameraPosition",
      )
    cameraSize =
      spinner(
        "Размер камеры",
        listOf("Обычный · 25% ширины", "Маленький · 18%", "Большой · 35%"),
        "cameraSize",
      )
    imageFormat =
      spinner(
        "Формат снимков",
        listOf("PNG — без потерь", "JPEG — меньше размер", "WebP"),
        "imageFormat",
      )
    imageQuality =
      spinner(
        "Качество JPEG / WebP",
        listOf("Высокое · 95%", "Максимальное · 100%", "Компактное · 75%"),
        "imageQuality",
      )
    label("Курсор и круги кликов", 17f)
    label(
      "Android не даёт приложению свободно скрывать курсор и отслеживать касания в чужих приложениях. Эти параметры управляются системой; скрытого перехвата касаний в Кадролове нет.",
      13f,
    )
    overlay =
      CheckBox(this).apply {
        text = "Плавающая мини-панель: пауза, продолжить, стоп"
        isChecked = prefs.getBoolean("overlay", false)
        setOnCheckedChangeListener { _, checked ->
          prefs.edit().putBoolean("overlay", checked).apply()
          if (checked && !Settings.canDrawOverlays(this@MainActivity)) {
            AlertDialog.Builder(this@MainActivity)
              .setTitle("Панель поверх других приложений")
              .setMessage(
                "Для мини-панели разрешите Кадролову показываться поверх других приложений. Без этого запись управляется уведомлением и кнопками здесь."
              )
              .setPositiveButton("Открыть настройки") { _, _ ->
                startActivity(
                  Intent(
                    Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                    Uri.parse("package:$packageName"),
                  )
                )
              }
              .setNegativeButton("Позже", null)
              .show()
          }
        }
      }
    box.addView(overlay)
    box.addView(
      CheckBox(this).apply {
        text = "Готовить быстрый доступ после перезагрузки"
        isChecked = prefs.getBoolean("bootShortcut", false)
        setOnCheckedChangeListener { _, checked ->
          prefs.edit().putBoolean("bootShortcut", checked).apply()
          BootReceiver.showShortcut(this@MainActivity)
          if (
            checked && !getSystemService(NotificationManager::class.java).areNotificationsEnabled()
          ) {
            AlertDialog.Builder(this@MainActivity)
              .setMessage(
                "Для быстрого доступа после перезагрузки разрешите уведомления Кадролова."
              )
              .setPositiveButton("Настройки") { _, _ ->
                startActivity(
                  Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS)
                    .putExtra(Settings.EXTRA_APP_PACKAGE, packageName)
                )
              }
              .setNegativeButton("Позже", null)
              .show()
          }
        }
      }
    )
    label("Папка сохранения", 17f)
    folder = label("")
    refreshFolder()
    box.addView(
      Button(this).apply {
        text = "Выбрать папку"
        setOnClickListener {
          startActivityForResult(
            Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
              .addFlags(
                Intent.FLAG_GRANT_READ_URI_PERMISSION or
                  Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
                  Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
              ),
            102,
          )
        }
      }
    )
    box.addView(
      Button(this).apply {
        text = "Вернуть стандартные папки"
        setOnClickListener {
          prefs.edit().remove("tree").apply()
          refreshFolder()
        }
      }
    )
    box.addView(
      Button(this).apply {
        text = "Мои файлы"
        setOnClickListener { showHistory() }
      }
    )
    box.addView(
      Button(this).apply {
        text = "Незавершённые записи"
        setOnClickListener { recoverRecordings() }
      }
    )
    box.addView(
      Button(this).apply {
        text = "О программе и лицензии"
        setOnClickListener { showLicenses() }
      }
    )
  }

  private fun refreshMicrophones() {
    if (!::microphone.isInitialized) return
    val saved = prefs.getInt("microphoneDeviceId", 0)
    microphoneChoices = MicrophoneDevices.choices(this, saved)
    microphone.onItemSelectedListener = null
    microphone.adapter =
      object :
        ArrayAdapter<String>(
          this,
          android.R.layout.simple_spinner_dropdown_item,
          microphoneChoices.map { it.label },
        ) {
        override fun isEnabled(position: Int) =
          microphoneChoices.getOrNull(position)?.available == true
      }
    microphone.setSelection(
      microphoneChoices.indexOfFirst { it.id == saved }.coerceAtLeast(0),
      false,
    )
    microphone.onItemSelectedListener =
      object : AdapterView.OnItemSelectedListener {
        override fun onNothingSelected(parent: AdapterView<*>?) {}

        override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
          microphoneChoices.getOrNull(position)?.let { choice ->
            if (choice.available) prefs.edit().putInt("microphoneDeviceId", choice.id).apply()
          }
        }
      }
  }

  override fun onNewIntent(intent: Intent) {
    super.onNewIntent(intent)
    setIntent(intent)
    handleQuick(intent)
  }

  private fun handleQuick(i: Intent?) {
    val mode = i?.getStringExtra("quick") ?: return
    i.removeExtra("quick")
    handler.post {
      when (mode) {
        "screenshot" -> {
          pendingVideoRegion = false
          begin(true)
        }
        "record" -> {
          pendingVideoRegion = false
          begin(false)
        }
      }
    }
  }

  override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
    if (event.isCtrlPressed && event.isAltPressed) {
      when (keyCode) {
        KeyEvent.KEYCODE_S -> {
          pendingVideoRegion = false
          begin(true)
          return true
        }
        KeyEvent.KEYCODE_R -> {
          pendingVideoRegion = false
          begin(false)
          return true
        }
      }
    }
    return super.onKeyDown(keyCode, event)
  }

  private fun begin(screenshot: Boolean, confirmed: Boolean = false) {
    if (!screenshot && !confirmed) {
      val summary =
        "${audio.selectedItem}\nМикрофон: ${microphone.selectedItem}\n${size.selectedItem} · ${fps.selectedItem}\n${videoFormat.selectedItem}\nКамера: ${if(camera.isChecked)"включена"else"выключена"}\nОбласть: ${if(prefs.contains("cropWidth"))"выбранный прямоугольник"else"системно выбранный источник"}\n\n${folder.text}\n\nПосле выбора источника — 3 секунды до начала. Стоп и пауза доступны в уведомлении."
      AlertDialog.Builder(this)
        .setTitle("Настройки записи")
        .setMessage(summary)
        .setPositiveButton("Выбрать источник") { _, _ -> begin(false, true) }
        .setNegativeButton("Вернуться", null)
        .show()
      return
    }
    if (CaptureState.state != "idle") {
      Toast.makeText(this, "Сначала остановите текущую запись", Toast.LENGTH_SHORT).show()
      return
    }
    pendingScreenshot = screenshot
    val permissions = mutableListOf<String>()
    if (
      !screenshot &&
        audio.selectedItemPosition != 0 &&
        checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED
    )
      permissions += Manifest.permission.RECORD_AUDIO
    if (
      !screenshot &&
        camera.isChecked &&
        checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED
    )
      permissions += Manifest.permission.CAMERA
    if (permissions.isNotEmpty()) {
      requestPermissions(permissions.toTypedArray(), 100)
      return
    }
    requestProjection()
  }

  private fun requestProjection() {
    if (!pendingScreenshot && audio.selectedItemPosition >= 2) {
      try {
        MicrophoneDevices.selected(this, prefs.getInt("microphoneDeviceId", 0))
      } catch (e: Exception) {
        refreshMicrophones()
        AlertDialog.Builder(this)
          .setTitle("Микрофон недоступен")
          .setMessage(e.message)
          .setPositiveButton("Изменить выбор", null)
          .show()
        return
      }
    }
    startActivityForResult(
      getSystemService(MediaProjectionManager::class.java).createScreenCaptureIntent(),
      101,
    )
  }

  override fun onRequestPermissionsResult(
    requestCode: Int,
    permissions: Array<out String>,
    grantResults: IntArray,
  ) {
    super.onRequestPermissionsResult(requestCode, permissions, grantResults)
    if (requestCode == 90) BootReceiver.showShortcut(this)
    if (requestCode == 100) {
      if (grantResults.isNotEmpty() && grantResults.all { it == PackageManager.PERMISSION_GRANTED })
        requestProjection()
      else
        AlertDialog.Builder(this)
          .setMessage(
            "Разрешение не предоставлено. Отключите звук или камеру, либо разрешите выбранные функции в настройках Android."
          )
          .setPositiveButton("Понятно", null)
          .show()
    }
  }

  @Suppress("DEPRECATION")
  override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
    super.onActivityResult(requestCode, resultCode, data)
    if (requestCode == 102 && resultCode == RESULT_OK) {
      data?.data?.let { uri ->
        try {
          contentResolver.takePersistableUriPermission(
            uri,
            Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION,
          )
          prefs.edit().putString("tree", uri.toString()).apply()
          refreshFolder()
        } catch (e: Exception) {
          Toast.makeText(this, "Не удалось сохранить доступ к папке", Toast.LENGTH_LONG).show()
        }
      }
    }
    if (requestCode == 101) {
      if (resultCode != RESULT_OK || data == null) {
        status.text = "Захват отменён"
        return
      }
      val options =
        CaptureOptions(
          screenshot = pendingScreenshot,
          audio =
            if (pendingScreenshot) "none"
            else listOf("none", "system", "mic", "both")[audio.selectedItemPosition],
          maxHeight = listOf(1080, 720, 8192)[size.selectedItemPosition],
          fps = listOf(30, 24, 60)[fps.selectedItemPosition],
          bitrate = listOf(8_000_000, 16_000_000, 3_000_000)[quality.selectedItemPosition],
          overlay = overlay.isChecked,
          destinationTree = prefs.getString("tree", null),
          imageFormat = listOf("png", "jpg", "webp")[imageFormat.selectedItemPosition],
          imageQuality = listOf(95, 100, 75)[imageQuality.selectedItemPosition],
          videoFormat = listOf("mp4", "webm")[videoFormat.selectedItemPosition],
          camera = !pendingScreenshot && camera.isChecked,
          selectVideoRegion = pendingVideoRegion,
          cropX = prefs.getFloat("cropX", 0f),
          cropY = prefs.getFloat("cropY", 0f),
          cropWidth = prefs.getFloat("cropWidth", 1f),
          cropHeight = prefs.getFloat("cropHeight", 1f),
          cameraShape = listOf("rect", "circle")[cameraShape.selectedItemPosition],
          cameraPosition =
            listOf("bottomRight", "bottomLeft", "topRight", "topLeft")[
              cameraPosition.selectedItemPosition],
          cameraScale = listOf(.25f, .18f, .35f)[cameraSize.selectedItemPosition],
          cameraFacing = listOf("front", "back")[cameraFacing.selectedItemPosition],
          microphoneDeviceId = prefs.getInt("microphoneDeviceId", 0),
        )
      startForegroundService(
        options.writeTo(Intent(this, CaptureService::class.java)).putExtra("projectionData", data)
      )
      moveTaskToBack(true)
    }
  }

  private fun refreshFolder() {
    folder.text =
      prefs.getString("tree", null)?.let {
        "Выбранная папка: ${Uri.decode(it.substringAfterLast('/'))}"
      } ?: "Снимки: Изображения/Кадролов\nВидео: Фильмы/Кадролов"
  }

  private fun showLicenses() {
    val files = assets.list("licenses")?.filter { it.endsWith(".txt") }.orEmpty()
    AlertDialog.Builder(this)
      .setTitle("Кадролов 0.1 · локальная работа")
      .setItems((listOf("О программе") + files).toTypedArray()) { _, i ->
        val content =
          if (i == 0)
            "Снимки, запись, OCR и обработка работают на вашем устройстве. Серверов, аккаунта и разрешения INTERNET в приложении нет. Разрешения захвата, камеры и звука выдаются Android отдельно."
          else assets.open("licenses/" + files[i - 1]).bufferedReader().use { it.readText() }
        val scroll = ScrollView(this)
        scroll.addView(
          TextView(this).apply {
            text = content
            textSize = 14f
            setPadding(24, 16, 24, 16)
          }
        )
        AlertDialog.Builder(this)
          .setTitle(if (i == 0) "О программе" else files[i - 1])
          .setView(scroll)
          .setPositiveButton("Закрыть", null)
          .show()
      }
      .setNegativeButton("Закрыть", null)
      .show()
  }

  private fun showHistory() {
    val items = MediaFiles.history(this)
    if (items.length() == 0) {
      AlertDialog.Builder(this)
        .setMessage("Здесь появятся сохранённые снимки и видео")
        .setPositiveButton("Понятно", null)
        .show()
      return
    }
    val source = (0 until items.length()).map { items.getJSONObject(it) }.toMutableList()
    val shown = source.toMutableList()
    val box =
      LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(16, 8, 16, 8)
      }
    val search =
      EditText(this).apply {
        hint = "Поиск по имени"
        isSingleLine = true
      }
    box.addView(search)
    val list = ListView(this)
    box.addView(list, LinearLayout.LayoutParams(-1, dp(380)))
    val pool = java.util.concurrent.Executors.newFixedThreadPool(2)
    val adapter =
      object : BaseAdapter() {
        override fun getCount() = shown.size

        override fun getItem(i: Int) = shown[i]

        override fun getItemId(i: Int) = i.toLong()

        override fun getView(i: Int, recycled: View?, parent: ViewGroup?): View {
          val row =
            LinearLayout(this@MainActivity).apply {
              gravity = Gravity.CENTER_VERTICAL
              setPadding(8, 8, 8, 8)
            }
          val image =
            ImageView(this@MainActivity).apply {
              scaleType = ImageView.ScaleType.CENTER_CROP
              setImageResource(
                if (shown[i].getString("mime").startsWith("video/"))
                  android.R.drawable.ic_media_play
                else android.R.drawable.ic_menu_gallery
              )
            }
          row.addView(image, LinearLayout.LayoutParams(dp(56), dp(56)))
          row.addView(
            TextView(this@MainActivity).apply {
              text = shown[i].getString("name")
              setPadding(16, 0, 0, 0)
            },
            LinearLayout.LayoutParams(0, -2, 1f),
          )
          val uri = Uri.parse(shown[i].getString("uri"))
          if (!pool.isShutdown)
            pool.execute {
              val thumbnail =
                runCatching {
                    contentResolver.loadThumbnail(uri, android.util.Size(112, 112), null)
                  }
                  .getOrNull()
              if (thumbnail != null)
                runOnUiThread { if (!isDestroyed) image.setImageBitmap(thumbnail) }
            }
          return row
        }
      }
    list.adapter = adapter
    search.addTextChangedListener(
      object : android.text.TextWatcher {
        override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}

        override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
          shown.clear()
          shown.addAll(
            source.filter {
              it.getString("name").contains(s?.toString().orEmpty(), ignoreCase = true)
            }
          )
          adapter.notifyDataSetChanged()
        }

        override fun afterTextChanged(s: android.text.Editable?) {}
      }
    )
    list.setOnItemClickListener { _, _, i, _ ->
      val entry = shown[i]
      val uri = Uri.parse(entry.getString("uri"))
      val mime = entry.getString("mime")
      val actions = mutableListOf("Открыть", "Поделиться")
      if (mime.startsWith("video/")) actions += "Обрезать видео"
      actions += "Убрать из истории"
      actions += "Удалить файл"
      AlertDialog.Builder(this)
        .setTitle(entry.getString("name"))
        .setItems(actions.toTypedArray()) { _, index ->
          try {
            when (actions[index]) {
              "Открыть" ->
                startActivity(
                  Intent(Intent.ACTION_VIEW)
                    .setDataAndType(uri, mime)
                    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                )
              "Поделиться" ->
                startActivity(
                  Intent.createChooser(
                    Intent(Intent.ACTION_SEND)
                      .setType(mime)
                      .putExtra(Intent.EXTRA_STREAM, uri)
                      .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION),
                    "Поделиться файлом",
                  )
                )
              "Обрезать видео" -> showTrim(uri, mime == "video/webm")
              "Убрать из истории" -> {
                MediaFiles.removeHistory(this, uri.toString())
                source.remove(entry)
                shown.remove(entry)
                adapter.notifyDataSetChanged()
              }
              "Удалить файл" ->
                AlertDialog.Builder(this)
                  .setTitle("Удалить файл с устройства?")
                  .setMessage(entry.getString("name"))
                  .setPositiveButton("Удалить") { _, _ ->
                    try {
                      val deleted =
                        if (android.provider.DocumentsContract.isDocumentUri(this, uri))
                          android.provider.DocumentsContract.deleteDocument(contentResolver, uri)
                        else contentResolver.delete(uri, null, null) > 0
                      check(deleted) { "Не удалось удалить файл" }
                      MediaFiles.removeHistory(this, uri.toString())
                      source.remove(entry)
                      shown.remove(entry)
                      adapter.notifyDataSetChanged()
                    } catch (e: Exception) {
                      Toast.makeText(this, e.message ?: "Ошибка удаления", Toast.LENGTH_LONG).show()
                    }
                  }
                  .setNegativeButton("Отмена", null)
                  .show()
            }
          } catch (e: Exception) {
            Toast.makeText(
                this,
                "Файл недоступен или нет приложения для открытия",
                Toast.LENGTH_LONG,
              )
              .show()
          }
        }
        .show()
    }
    AlertDialog.Builder(this)
      .setTitle("Мои файлы")
      .setView(box)
      .setNegativeButton("Закрыть", null)
      .create()
      .apply {
        setOnDismissListener { pool.shutdown() }
        show()
      }
  }

  private fun showTrim(uri: Uri, webm: Boolean) {
    val box =
      LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(24, 12, 24, 12)
      }
    val begin =
      EditText(this).apply {
        hint = "Начало, секунды"
        inputType =
          android.text.InputType.TYPE_CLASS_NUMBER or
            android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL
        setText("0")
      }
    val end =
      EditText(this).apply {
        hint = "Конец, секунды"
        inputType =
          android.text.InputType.TYPE_CLASS_NUMBER or
            android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL
      }
    box.addView(begin)
    box.addView(end)
    box.addView(
      TextView(this).apply {
        text =
          "Начало привязывается к предыдущему ключевому кадру. Исходник останется без изменений."
      }
    )
    AlertDialog.Builder(this)
      .setTitle("Обрезать видео")
      .setView(box)
      .setPositiveButton("Сохранить копию") { _, _ ->
        val from = begin.text.toString().replace(',', '.').toDoubleOrNull()
        val to = end.text.toString().replace(',', '.').toDoubleOrNull()
        if (from == null || to == null || from < 0 || to <= from) {
          Toast.makeText(this, "Укажите начало и конец отрезка", Toast.LENGTH_LONG).show()
          return@setPositiveButton
        }
        CaptureState.message = "Обрезка на устройстве…"
        kotlin.concurrent.thread {
          try {
            VideoTrim.trim(
              this,
              uri,
              (from * 1000).toLong(),
              (to * 1000).toLong(),
              webm,
              prefs.getString("tree", null),
            )
            runOnUiThread { CaptureState.message = "Обрезанная копия сохранена" }
          } catch (e: Exception) {
            runOnUiThread {
              AlertDialog.Builder(this)
                .setMessage("Не удалось обрезать: ${e.message}")
                .setPositiveButton("Понятно", null)
                .show()
            }
          }
        }
      }
      .setNegativeButton("Отмена", null)
      .show()
  }

  private fun recoverRecordings() {
    if (CaptureState.state != "idle") {
      Toast.makeText(this, "Сначала остановите запись", Toast.LENGTH_SHORT).show()
      return
    }
    val files =
      cacheDir
        .listFiles()
        ?.filter {
          it.name.startsWith("kadrolov-record-") &&
            (it.extension == "mp4" || it.extension == "webm") &&
            it.length() > 0
        }
        .orEmpty()
    if (files.isEmpty()) {
      AlertDialog.Builder(this)
        .setMessage("Незавершённых записей нет")
        .setPositiveButton("Понятно", null)
        .show()
      return
    }
    AlertDialog.Builder(this)
      .setTitle("Временные записи")
      .setItems(files.map { it.name + " · " + it.length() / 1024 + " КБ" }.toTypedArray()) {
        _,
        index ->
        val f = files[index]
        AlertDialog.Builder(this)
          .setMessage(
            "Попробовать сохранить найденную запись? После аварийного завершения некоторые MP4 могут быть непригодны к восстановлению."
          )
          .setPositiveButton("Сохранить") { _, _ ->
            kotlin.concurrent.thread {
              try {
                val metadata = android.media.MediaMetadataRetriever()
                try {
                  metadata.setDataSource(f.absolutePath)
                  check(
                    (metadata
                      .extractMetadata(android.media.MediaMetadataRetriever.METADATA_KEY_DURATION)
                      ?.toLongOrNull() ?: 0) > 0
                  ) {
                    "В записи нет законченного контейнера"
                  }
                } finally {
                  metadata.release()
                }
                MediaFiles.save(
                  this,
                  f,
                  if (f.extension == "webm") "video/webm" else "video/mp4",
                  MediaFiles.name(f.extension),
                  prefs.getString("tree", null),
                )
                f.delete()
                runOnUiThread { Toast.makeText(this, "Запись сохранена", Toast.LENGTH_LONG).show() }
              } catch (e: Exception) {
                runOnUiThread {
                  AlertDialog.Builder(this)
                    .setMessage("Не удалось восстановить: ${e.message}")
                    .setPositiveButton("Понятно", null)
                    .show()
                }
              }
            }
          }
          .setNegativeButton("Отмена", null)
          .show()
      }
      .setNegativeButton("Закрыть", null)
      .show()
  }

  private fun send(a: String) {
    startService(Intent(this, CaptureService::class.java).setAction(a))
  }

  private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

  override fun onResume() {
    super.onResume()
    refreshMicrophones()
    if (!listeningToAudioDevices) {
      getSystemService(AudioManager::class.java)
        .registerAudioDeviceCallback(microphoneCallback, handler)
      listeningToAudioDevices = true
    }
    handler.post(tick)
  }

  override fun onPause() {
    if (listeningToAudioDevices) {
      getSystemService(AudioManager::class.java).unregisterAudioDeviceCallback(microphoneCallback)
      listeningToAudioDevices = false
    }
    handler.removeCallbacks(tick)
    super.onPause()
  }
}
