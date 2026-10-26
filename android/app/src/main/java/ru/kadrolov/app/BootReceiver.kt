package ru.kadrolov.app

import android.Manifest
import android.app.*
import android.content.*
import android.content.pm.PackageManager
import android.os.Build

/** Boot only prepares a visible shortcut. It never starts projection or a camera/mic. */
class BootReceiver : BroadcastReceiver() {
  override fun onReceive(context: Context, intent: Intent) {
    if (
      intent.action == Intent.ACTION_BOOT_COMPLETED ||
        intent.action == Intent.ACTION_MY_PACKAGE_REPLACED
    )
      showShortcut(context)
  }

  companion object {
    fun showShortcut(c: Context) {
      val manager = c.getSystemService(NotificationManager::class.java)
      if (!c.getSharedPreferences("settings", 0).getBoolean("bootShortcut", false)) {
        manager.cancel(3)
        return
      }
      if (
        Build.VERSION.SDK_INT >= 33 &&
          c.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) !=
            PackageManager.PERMISSION_GRANTED
      )
        return
      manager.createNotificationChannel(
        NotificationChannel("quick", "Быстрый доступ", NotificationManager.IMPORTANCE_LOW)
      )
      fun pending(mode: String) =
        PendingIntent.getActivity(
          c,
          mode.hashCode(),
          Intent(c, MainActivity::class.java)
            .putExtra("quick", mode)
            .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP),
          PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
      val notification =
        Notification.Builder(c, "quick")
          .setSmallIcon(android.R.drawable.ic_menu_camera)
          .setContentTitle("Кадролов готов")
          .setContentText("Снимок или запись — с вашим разрешением")
          .setContentIntent(pending("open"))
          .addAction(Notification.Action.Builder(null, "Снимок", pending("screenshot")).build())
          .addAction(Notification.Action.Builder(null, "Запись", pending("record")).build())
          .build()
      manager.notify(3, notification)
    }
  }
}
