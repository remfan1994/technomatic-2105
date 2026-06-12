package vip.thatiam.radiobreaker;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.media.AudioManager;
import android.os.Build;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private static final int REQUEST_NOTIFICATIONS = 94;
    private static final int DEFAULT_TRACK_SECONDS = 1200;
    private static final int MIN_TRACK_SECONDS = 8;
    private static final int MAX_TRACK_SECONDS = 999999;
    private static final String PREFS = "radio_breaker";
    private static final String KEY_TRACK_SECONDS = "track_seconds";

    private Button playButton;
    private Button nextButton;
    private EditText durationInput;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setVolumeControlStream(AudioManager.STREAM_MUSIC);
        requestNotificationPermissionIfNeeded();

        playButton = new Button(this);
        playButton.setTextSize(28.0f);
        playButton.setAllCaps(false);
        playButton.setOnClickListener(view -> togglePlayback());

        nextButton = new Button(this);
        nextButton.setTextSize(22.0f);
        nextButton.setAllCaps(false);
        nextButton.setText("NEXT");
        nextButton.setOnClickListener(view -> skipToNext());

        TextView durationLabel = new TextView(this);
        durationLabel.setText("TRACK SECONDS");
        durationLabel.setTextColor(Color.WHITE);
        durationLabel.setTextSize(14.0f);
        durationLabel.setGravity(Gravity.CENTER);

        durationInput = new EditText(this);
        durationInput.setSingleLine(true);
        durationInput.setGravity(Gravity.CENTER);
        durationInput.setTextColor(Color.WHITE);
        durationInput.setHintTextColor(0xff888888);
        durationInput.setTextSize(22.0f);
        durationInput.setInputType(InputType.TYPE_CLASS_NUMBER);
        durationInput.setText(Integer.toString(loadTrackSeconds()));
        durationInput.setSelectAllOnFocus(true);

        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.VERTICAL);
        controls.setGravity(Gravity.CENTER);

        int buttonWidth = (int) (210.0f * getResources().getDisplayMetrics().density);
        int buttonHeight = (int) (92.0f * getResources().getDisplayMetrics().density);
        int fieldHeight = (int) (62.0f * getResources().getDisplayMetrics().density);
        int gap = (int) (16.0f * getResources().getDisplayMetrics().density);
        int smallGap = (int) (7.0f * getResources().getDisplayMetrics().density);

        LinearLayout.LayoutParams playLp = new LinearLayout.LayoutParams(buttonWidth, buttonHeight);
        LinearLayout.LayoutParams nextLp = new LinearLayout.LayoutParams(buttonWidth, buttonHeight);
        LinearLayout.LayoutParams labelLp = new LinearLayout.LayoutParams(buttonWidth, LinearLayout.LayoutParams.WRAP_CONTENT);
        LinearLayout.LayoutParams durationLp = new LinearLayout.LayoutParams(buttonWidth, fieldHeight);
        LinearLayout.LayoutParams spacerLp = new LinearLayout.LayoutParams(buttonWidth, smallGap);
        nextLp.topMargin = gap;
        labelLp.topMargin = gap;

        controls.addView(playButton, playLp);
        controls.addView(nextButton, nextLp);
        controls.addView(durationLabel, labelLp);
        controls.addView(durationInput, durationLp);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT);
        lp.gravity = Gravity.CENTER;
        root.addView(controls, lp);
        setContentView(root, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        updateButton();
    }

    @Override
    protected void onResume() {
        super.onResume();
        updateButton();
    }

    private void togglePlayback() {
        if (NativeAudio.isPlaying()) {
            Intent intent = new Intent(this, AudioService.class);
            intent.setAction(AudioService.ACTION_STOP);
            startService(intent);
        } else {
            startAudioService(AudioService.ACTION_START);
        }
        playButton.postDelayed(this::updateButton, 250L);
    }

    private void skipToNext() {
        startAudioService(NativeAudio.isPlaying() ? AudioService.ACTION_NEXT : AudioService.ACTION_START);
        nextButton.postDelayed(this::updateButton, 250L);
    }

    private void startAudioService(String action) {
        int seconds = readAndStoreTrackSeconds();
        NativeAudio.setPieceLengthSeconds(seconds);

        Intent intent = new Intent(this, AudioService.class);
        intent.setAction(action);
        intent.putExtra(AudioService.EXTRA_TRACK_SECONDS, seconds);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent);
        } else {
            startService(intent);
        }
    }

    private int loadTrackSeconds() {
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        return clampTrackSeconds(prefs.getInt(KEY_TRACK_SECONDS, DEFAULT_TRACK_SECONDS));
    }

    private int readAndStoreTrackSeconds() {
        int seconds = DEFAULT_TRACK_SECONDS;
        if (durationInput != null) {
            try {
                String raw = durationInput.getText() != null ? durationInput.getText().toString().trim() : "";
                if (!raw.isEmpty()) {
                    seconds = Integer.parseInt(raw);
                }
            } catch (NumberFormatException ignored) {
                seconds = DEFAULT_TRACK_SECONDS;
            }
        }
        seconds = clampTrackSeconds(seconds);
        if (durationInput != null) {
            durationInput.setText(Integer.toString(seconds));
            durationInput.setSelection(durationInput.getText().length());
        }
        getSharedPreferences(PREFS, MODE_PRIVATE)
                .edit()
                .putInt(KEY_TRACK_SECONDS, seconds)
                .apply();
        return seconds;
    }

    private int clampTrackSeconds(int seconds) {
        if (seconds < MIN_TRACK_SECONDS) return MIN_TRACK_SECONDS;
        if (seconds > MAX_TRACK_SECONDS) return MAX_TRACK_SECONDS;
        return seconds;
    }

    private void updateButton() {
        if (playButton != null) {
            playButton.setText(NativeAudio.isPlaying() ? "PAUSE" : "PLAY");
        }
        if (nextButton != null) {
            nextButton.setEnabled(true);
        }
    }

    private void requestNotificationPermissionIfNeeded() {
        if (Build.VERSION.SDK_INT >= 33 &&
                checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQUEST_NOTIFICATIONS);
        }
    }
}
