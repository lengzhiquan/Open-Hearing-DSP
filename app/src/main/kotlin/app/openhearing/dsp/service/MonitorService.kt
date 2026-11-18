package app.openhearing.dsp.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import app.openhearing.dsp.MainActivity
import app.openhearing.dsp.OpenHearingApp
import app.openhearing.dsp.R

class MonitorService : Service() {
  override fun onBind(intent: Intent?): IBinder? = null

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    val controller = (application as OpenHearingApp).controller
    createChannel()
    val notification = buildNotification()
    if (Build.VERSION.SDK_INT >= 34) {
      startForeground(NOTIF_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE)
    } else {
      startForeground(NOTIF_ID, notification)
    }
    controller.markServiceForeground(true)
    when (intent?.action) {
      ACTION_STOP -> {
        val snap = controller.state.value
        if (snap.running || snap.starting) controller.requestStop("已停止采集。")
        stopForeground(STOP_FOREGROUND_REMOVE)
        controller.markServiceForeground(false)
        stopSelf()
      }
      ACTION_START -> controller.requestStart()
    }
    return START_NOT_STICKY
  }

  override fun onTaskRemoved(rootIntent: Intent?) {
    val controller = (application as OpenHearingApp).controller
    controller.requestStop("应用被划掉，已停止采集。")
    stopForeground(STOP_FOREGROUND_REMOVE)
    controller.markServiceForeground(false)
    stopSelf()
    super.onTaskRemoved(rootIntent)
  }

  override fun onDestroy() {
    (application as OpenHearingApp).controller.markServiceForeground(false)
    super.onDestroy()
  }

  private fun createChannel() {
    val manager = getSystemService(NotificationManager::class.java)
    val channel = NotificationChannel(CHANNEL, getString(R.string.notif_channel), NotificationManager.IMPORTANCE_LOW)
    channel.description = "麦克风实验监听进行时显示"
    manager.createNotificationChannel(channel)
  }

  private fun buildNotification(): Notification {
    val open = PendingIntent.getActivity(
        this,
        1,
        Intent(this, MainActivity::class.java),
        PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
    )
    val stop = PendingIntent.getService(
        this,
        2,
        Intent(this, MonitorService::class.java).setAction(ACTION_STOP),
        PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
    )
    return NotificationCompat.Builder(this, CHANNEL)
        .setSmallIcon(R.drawable.ic_stat_mic)
        .setContentTitle(getString(R.string.notif_title))
        .setContentText(getString(R.string.notif_text))
        .setOngoing(true)
        .setCategory(Notification.CATEGORY_SERVICE)
        .setContentIntent(open)
        .addAction(R.drawable.ic_stat_mic, getString(R.string.notif_stop), stop)
        .build()
  }

  companion object {
    const val ACTION_START = "app.openhearing.dsp.START"
    const val ACTION_STOP = "app.openhearing.dsp.STOP"
    private const val CHANNEL = "monitor"
    private const val NOTIF_ID = 41
  }
}
