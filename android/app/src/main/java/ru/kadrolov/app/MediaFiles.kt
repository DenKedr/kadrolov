package ru.kadrolov.app

import android.content.ContentValues
import android.content.Context
import android.graphics.Bitmap
import android.net.Uri
import android.os.Environment
import android.provider.DocumentsContract
import android.provider.MediaStore
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import org.json.JSONArray
import org.json.JSONObject

object MediaFiles {
  fun name(extension: String) =
    "Кадролов_" +
      SimpleDateFormat("yyyy-MM-dd_HH-mm-ss_SSS", Locale.US).format(Date()) +
      "." +
      extension

  fun save(c: Context, file: File, mime: String, name: String, tree: String?): Uri {
    val uri: Uri
    var pending = false
    if (!tree.isNullOrEmpty()) {
      val t = Uri.parse(tree)
      val parent =
        DocumentsContract.buildDocumentUriUsingTree(t, DocumentsContract.getTreeDocumentId(t))
      uri =
        DocumentsContract.createDocument(c.contentResolver, parent, mime, name)
          ?: error("Не удалось создать файл в выбранной папке")
    } else {
      val video = mime.startsWith("video/")
      val values =
        ContentValues().apply {
          put(MediaStore.MediaColumns.DISPLAY_NAME, name)
          put(MediaStore.MediaColumns.MIME_TYPE, mime)
          put(
            MediaStore.MediaColumns.RELATIVE_PATH,
            (if (video) Environment.DIRECTORY_MOVIES else Environment.DIRECTORY_PICTURES) +
              "/Кадролов",
          )
          put(MediaStore.MediaColumns.IS_PENDING, 1)
        }
      uri =
        c.contentResolver.insert(
          if (video) MediaStore.Video.Media.EXTERNAL_CONTENT_URI
          else MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
          values,
        ) ?: error("Не удалось создать файл")
      pending = true
    }
    try {
      c.contentResolver.openOutputStream(uri, "w").use { out ->
        checkNotNull(out) { "Папка недоступна" }
        file.inputStream().use { it.copyTo(out) }
      }
      if (pending)
        c.contentResolver.update(
          uri,
          ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) },
          null,
          null,
        )
      addHistory(c, uri, name, mime)
      return uri
    } catch (e: Exception) {
      runCatching { c.contentResolver.delete(uri, null, null) }
      throw e
    }
  }

  fun saveBitmap(c: Context, b: Bitmap, format: String, quality: Int, tree: String?): Uri {
    val ext = if (format == "jpg") "jpg" else if (format == "webp") "webp" else "png"
    val temp = File.createTempFile("export-", ".$ext", c.cacheDir)
    try {
      temp.outputStream().use { out ->
        check(
          b.compress(
            when (ext) {
              "jpg" -> Bitmap.CompressFormat.JPEG
              "webp" -> Bitmap.CompressFormat.WEBP
              else -> Bitmap.CompressFormat.PNG
            },
            quality.coerceIn(1, 100),
            out,
          )
        ) {
          "Не удалось сохранить изображение"
        }
      }
      return save(c, temp, if (ext == "jpg") "image/jpeg" else "image/$ext", name(ext), tree)
    } finally {
      temp.delete()
    }
  }

  fun history(c: Context): JSONArray =
    runCatching { JSONArray(c.getSharedPreferences("history", 0).getString("items", "[]")) }
      .getOrDefault(JSONArray())

  @Synchronized
  fun removeHistory(c: Context, uri: String) {
    val old = history(c)
    val keep = JSONArray()
    for (i in 0 until old.length()) {
      val entry = old.getJSONObject(i)
      if (entry.optString("uri") != uri) keep.put(entry)
    }
    c.getSharedPreferences("history", 0).edit().putString("items", keep.toString()).apply()
  }

  @Synchronized
  fun addHistory(c: Context, u: Uri, n: String, m: String) {
    val old = history(c)
    val all = JSONArray()
    all.put(
      JSONObject()
        .put("uri", u.toString())
        .put("name", n)
        .put("mime", m)
        .put("at", System.currentTimeMillis())
    )
    for (i in 0 until minOf(old.length(), 199)) all.put(old.get(i))
    c.getSharedPreferences("history", 0).edit().putString("items", all.toString()).apply()
  }
}
