package ru.kadrolov.app

import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Build

/**
 * Explicit microphone identity. Missing saved devices remain visible; they never select default.
 */
object MicrophoneDevices {
  data class Choice(val id: Int, val label: String, val available: Boolean = true)

  fun inputs(context: Context): List<AudioDeviceInfo> =
    context
      .getSystemService(AudioManager::class.java)
      .getDevices(AudioManager.GET_DEVICES_INPUTS)
      .filter { device ->
        device.isSource &&
          device.type != AudioDeviceInfo.TYPE_TELEPHONY &&
          device.type != AudioDeviceInfo.TYPE_REMOTE_SUBMIX &&
          device.type != AudioDeviceInfo.TYPE_FM_TUNER &&
          device.type != AudioDeviceInfo.TYPE_TV_TUNER
      }
      .sortedWith(compareBy({ typeName(it.type) }, { it.productName.toString() }, { it.id }))

  fun choices(context: Context, savedId: Int): List<Choice> {
    val choices = mutableListOf(Choice(0, "Системный микрофон по умолчанию"))
    inputs(context).forEach { device ->
      val type = typeName(device.type)
      val name = device.productName.toString().trim().ifEmpty { type }
      choices += Choice(device.id, "$name · $type · ID ${device.id}")
    }
    if (savedId != 0 && choices.none { it.id == savedId })
      choices += Choice(savedId, "Выбранный микрофон недоступен · ID $savedId", false)
    return choices
  }

  fun selected(context: Context, selectedId: Int): AudioDeviceInfo? {
    if (selectedId == 0) return null
    return inputs(context).firstOrNull { it.id == selectedId }
      ?: error(
        "Выбранный микрофон отключён или недоступен. Подключите его или измените выбор микрофона в настройках записи"
      )
  }

  private fun typeName(type: Int): String =
    when (type) {
      AudioDeviceInfo.TYPE_BUILTIN_MIC -> "встроенный микрофон"
      AudioDeviceInfo.TYPE_WIRED_HEADSET -> "проводная гарнитура"
      AudioDeviceInfo.TYPE_USB_DEVICE,
      AudioDeviceInfo.TYPE_USB_HEADSET -> "USB"
      AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> "Bluetooth"
      AudioDeviceInfo.TYPE_LINE_ANALOG -> "линейный вход"
      AudioDeviceInfo.TYPE_LINE_DIGITAL -> "цифровой вход"
      AudioDeviceInfo.TYPE_BUS -> "аудиовход"
      else ->
        if (Build.VERSION.SDK_INT >= 31 && type == AudioDeviceInfo.TYPE_BLE_HEADSET) "Bluetooth LE"
        else "аудиовход (тип $type)"
    }
}
