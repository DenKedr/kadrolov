package ru.kadrolov.app

import android.content.Intent

data class CaptureOptions(
  val screenshot: Boolean = false,
  val audio: String = "none",
  val maxHeight: Int = 1080,
  val fps: Int = 30,
  val bitrate: Int = 8_000_000,
  val overlay: Boolean = false,
  val destinationTree: String? = null,
  val imageFormat: String = "png",
  val imageQuality: Int = 95,
  val videoFormat: String = "mp4",
  val camera: Boolean = false,
  val selectVideoRegion: Boolean = false,
  val cropX: Float = 0f,
  val cropY: Float = 0f,
  val cropWidth: Float = 1f,
  val cropHeight: Float = 1f,
  val cameraShape: String = "rect",
  val cameraPosition: String = "bottomRight",
  val cameraScale: Float = 0.25f,
  val cameraFacing: String = "front",
  val microphoneDeviceId: Int = 0,
) {
  fun writeTo(i: Intent): Intent =
    i.putExtra("screenshot", screenshot)
      .putExtra("audio", audio)
      .putExtra("maxHeight", maxHeight)
      .putExtra("fps", fps)
      .putExtra("bitrate", bitrate)
      .putExtra("overlay", overlay)
      .putExtra("tree", destinationTree)
      .putExtra("imageFormat", imageFormat)
      .putExtra("imageQuality", imageQuality)
      .putExtra("videoFormat", videoFormat)
      .putExtra("camera", camera)
      .putExtra("selectVideoRegion", selectVideoRegion)
      .putExtra("cropX", cropX)
      .putExtra("cropY", cropY)
      .putExtra("cropWidth", cropWidth)
      .putExtra("cropHeight", cropHeight)
      .putExtra("cameraShape", cameraShape)
      .putExtra("cameraPosition", cameraPosition)
      .putExtra("cameraScale", cameraScale)
      .putExtra("cameraFacing", cameraFacing)
      .putExtra("microphoneDeviceId", microphoneDeviceId)

  companion object {
    fun from(i: Intent) =
      CaptureOptions(
        i.getBooleanExtra("screenshot", false),
        i.getStringExtra("audio") ?: "none",
        i.getIntExtra("maxHeight", 1080),
        i.getIntExtra("fps", 30),
        i.getIntExtra("bitrate", 8_000_000),
        i.getBooleanExtra("overlay", false),
        i.getStringExtra("tree"),
        i.getStringExtra("imageFormat") ?: "png",
        i.getIntExtra("imageQuality", 95),
        i.getStringExtra("videoFormat") ?: "mp4",
        i.getBooleanExtra("camera", false),
        i.getBooleanExtra("selectVideoRegion", false),
        i.getFloatExtra("cropX", 0f),
        i.getFloatExtra("cropY", 0f),
        i.getFloatExtra("cropWidth", 1f),
        i.getFloatExtra("cropHeight", 1f),
        i.getStringExtra("cameraShape") ?: "rect",
        i.getStringExtra("cameraPosition") ?: "bottomRight",
        i.getFloatExtra("cameraScale", 0.25f),
        i.getStringExtra("cameraFacing") ?: "front",
        i.getIntExtra("microphoneDeviceId", 0),
      )
  }
}
