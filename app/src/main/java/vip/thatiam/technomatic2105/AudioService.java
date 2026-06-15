package vip.thatiam.technomatic2105;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;

public final class AudioService extends Service {
    public static final String ACTION_START = "vip.thatiam.technomatic2105.START";
    public static final String ACTION_STOP = "vip.thatiam.technomatic2105.STOP";
    public static final String ACTION_NEXT = "vip.thatiam.technomatic2105.NEXT";
    public static final String EXTRA_TRACK_SECONDS = "vip.thatiam.technomatic2105.TRACK_SECONDS";
    public static final String EXTRA_GENRE_MASK = "vip.thatiam.technomatic2105.GENRE_MASK";
    public static final String EXTRA_GENRE_BLEND_MODE = "vip.thatiam.technomatic2105.GENRE_BLEND_MODE";

    private static final String CHANNEL_ID = "technomatic_2105_playback";
    private static final int NOTIFICATION_ID = 2105;

    private AudioManager audioManager;
    private AudioFocusRequest focusRequest;
    private PowerManager.WakeLock wakeLock;
    private boolean foreground;

    private final AudioManager.OnAudioFocusChangeListener focusListener = focusChange -> {
        if (focusChange == AudioManager.AUDIOFOCUS_LOSS ||
                focusChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT) {
            stopPlayback();
            stopSelf();
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        audioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        createNotificationChannel();

        PowerManager powerManager = (PowerManager) getSystemService(Context.POWER_SERVICE);
        if (powerManager != null) {
            wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "Technomatic2105:Audio");
            wakeLock.setReferenceCounted(false);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent != null ? intent.getAction() : ACTION_START;
        if (intent != null && intent.hasExtra(EXTRA_TRACK_SECONDS)) {
            int seconds = intent.getIntExtra(EXTRA_TRACK_SECONDS, 180);
            if (seconds < -1) seconds = 180;
            if (seconds > 0 && seconds < 8) seconds = 8;
            if (seconds > 999999) seconds = 999999;
            NativeAudio.setPieceLengthSeconds(seconds);
        }
        if (intent != null && intent.hasExtra(EXTRA_GENRE_BLEND_MODE)) {
            int mode = intent.getIntExtra(EXTRA_GENRE_BLEND_MODE, 0);
            NativeAudio.setGenreBlendMode(mode);
        }
        if (intent != null && intent.hasExtra(EXTRA_GENRE_MASK)) {
            int mask = intent.getIntExtra(EXTRA_GENRE_MASK, 0);
            NativeAudio.setGenreMask(mask);
        }
        if (ACTION_STOP.equals(action)) {
            stopPlayback();
            stopSelf();
            return START_NOT_STICKY;
        }

        if (ACTION_NEXT.equals(action)) {
            if (NativeAudio.isPlaying()) {
                NativeAudio.next();
            } else {
                startPlayback();
            }
            return START_STICKY;
        }

        startPlayback();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        stopPlayback();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void startPlayback() {
        Notification notification = buildNotification();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }
        foreground = true;

        if (!requestAudioFocus()) {
            stopPlayback();
            stopSelf();
            return;
        }

        if (NativeAudio.start()) {
            acquireWakeLock();
        } else {
            stopPlayback();
            stopSelf();
        }
    }

    private void stopPlayback() {
        NativeAudio.stop();
        releaseWakeLock();
        abandonAudioFocus();
        if (foreground) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                stopForeground(STOP_FOREGROUND_REMOVE);
            } else {
                stopForeground(true);
            }
            foreground = false;
        }
    }

    private boolean requestAudioFocus() {
        if (audioManager == null) return true;

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            AudioAttributes attributes = new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build();
            focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                    .setAudioAttributes(attributes)
                    .setOnAudioFocusChangeListener(focusListener)
                    .build();
            return audioManager.requestAudioFocus(focusRequest) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        }

        return audioManager.requestAudioFocus(
                focusListener,
                AudioManager.STREAM_MUSIC,
                AudioManager.AUDIOFOCUS_GAIN) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
    }

    private void abandonAudioFocus() {
        if (audioManager == null) return;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && focusRequest != null) {
            audioManager.abandonAudioFocusRequest(focusRequest);
            focusRequest = null;
        } else {
            audioManager.abandonAudioFocus(focusListener);
        }
    }

    private void acquireWakeLock() {
        if (wakeLock != null && !wakeLock.isHeld()) {
            wakeLock.acquire();
        }
    }

    private void releaseWakeLock() {
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return;
        NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager == null) return;
        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                getString(R.string.notification_channel_name),
                NotificationManager.IMPORTANCE_LOW);
        channel.setDescription(getString(R.string.notification_channel_description));
        manager.createNotificationChannel(channel);
    }

    private Notification buildNotification() {
        Intent openIntent = new Intent(this, MainActivity.class);
        PendingIntent openPendingIntent = PendingIntent.getActivity(
                this,
                0,
                openIntent,
                pendingIntentFlags());

        Intent stopIntent = new Intent(this, AudioService.class);
        stopIntent.setAction(ACTION_STOP);
        PendingIntent stopPendingIntent = PendingIntent.getService(
                this,
                1,
                stopIntent,
                pendingIntentFlags());

        Intent nextIntent = new Intent(this, AudioService.class);
        nextIntent.setAction(ACTION_NEXT);
        PendingIntent nextPendingIntent = PendingIntent.getService(
                this,
                2,
                nextIntent,
                pendingIntentFlags());

        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);

        return builder
                .setSmallIcon(R.drawable.ic_stat_technomatic_2105)
                .setContentTitle(getString(R.string.notification_title))
                .setContentText(getString(R.string.notification_text))
                .setContentIntent(openPendingIntent)
                .setOngoing(true)
                .setCategory(Notification.CATEGORY_SERVICE)
                .addAction(android.R.drawable.ic_media_next, "Next", nextPendingIntent)
                .addAction(android.R.drawable.ic_media_pause, "Stop", stopPendingIntent)
                .build();
    }

    private int pendingIntentFlags() {
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            flags |= PendingIntent.FLAG_IMMUTABLE;
        }
        return flags;
    }
}
