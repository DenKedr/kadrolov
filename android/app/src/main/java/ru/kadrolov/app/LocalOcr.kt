package ru.kadrolov.app

import android.content.Context
import android.graphics.Bitmap
import com.googlecode.tesseract.android.TessBaseAPI
import java.io.File

object LocalOcr {
  /** Bundled eng/rus tessdata_fast: no network permissions or image uploads. */
  fun recognize(context: Context, bitmap: Bitmap): String {
    val root = File(context.filesDir, "tesseract")
    val data = File(root, "tessdata")
    check(data.isDirectory || data.mkdirs()) { "Не удалось подготовить OCR" }
    for (lang in listOf("eng", "rus")) {
      val target = File(data, "$lang.traineddata")
      if (!target.isFile) {
        val part = File(data, "$lang.part")
        context.assets.open("tessdata/$lang.traineddata").use { input ->
          part.outputStream().use { input.copyTo(it) }
        }
        check(part.renameTo(target)) { "Не удалось установить язык $lang" }
      }
    }
    val api = TessBaseAPI()
    try {
      check(api.init(root.absolutePath, "rus+eng", TessBaseAPI.OEM_LSTM_ONLY)) {
        "Не удалось загрузить языки OCR"
      }
      api.setImage(bitmap)
      return api.getUTF8Text().orEmpty().trim()
    } finally {
      api.recycle()
    }
  }
}
