package vip.thatiam.technomatic2105;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.media.AudioManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.List;

public final class MainActivity extends Activity {
    private static final int REQUEST_NOTIFICATIONS = 94;
    private static final int DEFAULT_TRACK_SECONDS = 180;
    private static final int MIN_TRACK_SECONDS = 8;
    private static final int MAX_TRACK_SECONDS = 999999;
    private static final String PREFS = "technomatic_2105";
    private static final String KEY_TRACK_SECONDS = "track_seconds";
    private static final String KEY_TRACK_SECONDS_RANDOM = "track_seconds_random";
    private static final String KEY_TRACK_SECONDS_INFINITE = "track_seconds_infinite";
    private static final String KEY_GENRE_RANDOM = "genre_random";
    private static final String KEY_GENRE_MASK = "genre_mask";
    private static final String KEY_GENRE_BLEND_MODE = "genre_blend_mode";

    private static final int SCREEN_MAIN = 0;
    private static final int SCREEN_GENRE = 1;
    private static final int GENRE_BLEND_POOL = 0;
    private static final int GENRE_BLEND_HYBRID = 1;

    private static final String[] GENRE_LABELS = new String[] {
            "Chrome Pulse",
            "Velvet Circuit",
            "Glass Trap",
            "Dust Machine",
            "Liquid Grid",
            "Neon Drift",
            "Broken Speaker",
            "Deep Magnet",
            "Pixel Ritual",
            "Soft Voltage",
            "Heavy Orbit",
            "Cold Arcade",
            "No Genre"
    };

    private Button startStopButton;
    private Button genreButton;
    private Button elapsedButton;
    private int currentScreen = SCREEN_MAIN;
    private boolean suppressGenreCallbacks = false;

    private final Handler statusHandler = new Handler(Looper.getMainLooper());

    private final Runnable statusTicker = new Runnable() {
        @Override
        public void run() {
            updateMainStatus();
            statusHandler.postDelayed(this, 1000L);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setVolumeControlStream(AudioManager.STREAM_MUSIC);
        requestNotificationPermissionIfNeeded();
        registerSystemBackHandler();
        NativeAudio.setGenreMask(activeGenreMask());
        NativeAudio.setGenreBlendMode(loadGenreBlendMode());
        NativeAudio.setPieceLengthSeconds(nativeTrackSeconds());
        showMainScreen();
    }

    @Override
    protected void onResume() {
        super.onResume();
        updateMainStatus();
        statusHandler.removeCallbacks(statusTicker);
        statusHandler.postDelayed(statusTicker, 1000L);
    }

    @Override
    protected void onPause() {
        statusHandler.removeCallbacks(statusTicker);
        super.onPause();
    }

    @Override
    public void onBackPressed() {
        if (currentScreen == SCREEN_GENRE) {
            showMainScreen();
            return;
        }
        super.onBackPressed();
    }

    private void registerSystemBackHandler() {
        if (Build.VERSION.SDK_INT < 33) return;
        getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,
                () -> {
                    if (currentScreen == SCREEN_GENRE) showMainScreen();
                    else finish();
                });
    }

    private void showMainScreen() {
        currentScreen = SCREEN_MAIN;
        LinearLayout controls = baseColumn(true);

        startStopButton = button(NativeAudio.isPlaying() ? "Stop" : "Start", 26.0f);
        startStopButton.setOnClickListener(view -> togglePlayback());

        Button next = button("Next", 18.0f);
        next.setOnClickListener(view -> skipToNext());

        genreButton = navButton(currentGenreText(), 15.0f);
        genreButton.setOnClickListener(view -> showGenreSelectorScreen());

        elapsedButton = navButton(currentElapsedText(), 15.0f);
        elapsedButton.setOnClickListener(view -> showDurationChooser());

        TextView notice = status("If you're not already vegetarian, you need to see Bloodguiltcurse.net");
        notice.setTextSize(12.0f);

        controls.addView(startStopButton, params(292, 70, 0));
        controls.addView(next, params(292, 54, 5));
        controls.addView(genreButton, params(320, 104, 16));
        controls.addView(elapsedButton, params(320, 96, 8));
        controls.addView(notice, params(320, -2, 20));

        setRoot(controls, false);
        updateMainStatus();
    }

    private void showGenreSelectorScreen() {
        currentScreen = SCREEN_GENRE;
        LinearLayout controls = baseColumn(false);
        controls.setGravity(Gravity.CENTER_HORIZONTAL);

        Button back = button("Back", 16.0f);
        back.setOnClickListener(view -> showMainScreen());

        CheckBox random = new CheckBox(this);
        random.setText("Random");
        random.setTextSize(18.0f);
        random.setTextColor(Color.WHITE);
        random.setGravity(Gravity.CENTER_VERTICAL);
        random.setChecked(isGenreRandom());

        RadioGroup blendGroup = new RadioGroup(this);
        blendGroup.setOrientation(RadioGroup.HORIZONTAL);
        blendGroup.setGravity(Gravity.CENTER);
        RadioButton pool = radio("Pool");
        RadioButton hybrid = radio("Hybrid");
        pool.setId(View.generateViewId());
        hybrid.setId(View.generateViewId());
        blendGroup.addView(pool, new RadioGroup.LayoutParams(dp(118), dp(48)));
        blendGroup.addView(hybrid, new RadioGroup.LayoutParams(dp(138), dp(48)));
        blendGroup.check(loadGenreBlendMode() == GENRE_BLEND_HYBRID ? hybrid.getId() : pool.getId());

        LinearLayout blendRow = new LinearLayout(this);
        blendRow.setOrientation(LinearLayout.HORIZONTAL);
        blendRow.setGravity(Gravity.CENTER);
        blendRow.addView(blendGroup, new LinearLayout.LayoutParams(dp(292), dp(52)));

        TextView message = status(genreSelectorMessage(random.isChecked(), selectedBlendMode(pool, hybrid)));

        List<CheckBox> checks = new ArrayList<>();
        int savedMask = loadGenreMask();
        for (int i = 0; i < GENRE_LABELS.length; ++i) {
            CheckBox box = new CheckBox(this);
            box.setText(GENRE_LABELS[i]);
            box.setTextSize(18.0f);
            box.setTextColor(Color.WHITE);
            box.setGravity(Gravity.CENTER_VERTICAL);
            box.setChecked((savedMask & (1 << i)) != 0);
            checks.add(box);
        }

        Runnable refreshVisuals = () -> {
            boolean randomOn = random.isChecked();
            int blendMode = selectedBlendMode(pool, hybrid);
            pool.setEnabled(!randomOn);
            hybrid.setEnabled(!randomOn);
            pool.setTextColor(randomOn ? 0xff777777 : Color.WHITE);
            hybrid.setTextColor(randomOn ? 0xff777777 : Color.WHITE);
            for (CheckBox box : checks) {
                box.setEnabled(true);
                box.setTextColor(randomOn ? 0xff777777 : Color.WHITE);
            }
            message.setText(genreSelectorMessage(randomOn, blendMode));
        };

        random.setOnClickListener(view -> {
            if (suppressGenreCallbacks) return;
            if (random.isChecked()) {
                suppressGenreCallbacks = true;
                for (CheckBox box : checks) box.setChecked(false);
                suppressGenreCallbacks = false;
            } else if (selectedMask(checks) == 0 && !checks.isEmpty()) {
                suppressGenreCallbacks = true;
                checks.get(0).setChecked(true);
                suppressGenreCallbacks = false;
            }
            refreshVisuals.run();
            applyGenreSelection(random, checks, selectedBlendMode(pool, hybrid));
        });

        blendGroup.setOnCheckedChangeListener((group, checkedId) -> {
            if (suppressGenreCallbacks) return;
            if (random.isChecked()) return;
            refreshVisuals.run();
            applyGenreSelection(random, checks, selectedBlendMode(pool, hybrid));
        });

        for (CheckBox box : checks) {
            box.setOnClickListener(view -> {
                if (suppressGenreCallbacks) return;
                if (random.isChecked()) {
                    suppressGenreCallbacks = true;
                    random.setChecked(false);
                    for (CheckBox other : checks) other.setChecked(false);
                    ((CheckBox) view).setChecked(true);
                    suppressGenreCallbacks = false;
                } else if (selectedMask(checks) == 0) {
                    suppressGenreCallbacks = true;
                    random.setChecked(true);
                    for (CheckBox other : checks) other.setChecked(false);
                    suppressGenreCallbacks = false;
                    Toast.makeText(this, "No genre left selected. Random is active.", Toast.LENGTH_SHORT).show();
                }
                refreshVisuals.run();
                applyGenreSelection(random, checks, selectedBlendMode(pool, hybrid));
            });
        }

        controls.addView(back, params(292, 50, 0));
        controls.addView(label("GENRE SELECTOR"), params(292, -2, 12));
        controls.addView(random, params(292, 52, 10));
        controls.addView(blendRow, params(292, 56, 2));
        controls.addView(message, params(320, -2, 8));
        for (CheckBox box : checks) controls.addView(box, params(292, 48, 4));

        refreshVisuals.run();

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(controls, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));
        setRoot(scroll, true);
    }

    private RadioButton radio(String text) {
        RadioButton button = new RadioButton(this);
        button.setText(text);
        button.setTextSize(16.0f);
        button.setTextColor(Color.WHITE);
        button.setGravity(Gravity.CENTER_VERTICAL);
        return button;
    }

    private int selectedBlendMode(RadioButton pool, RadioButton hybrid) {
        return hybrid.isChecked() ? GENRE_BLEND_HYBRID : GENRE_BLEND_POOL;
    }

    private String genreSelectorMessage(boolean random, int blendMode) {
        if (random) return "Random is active. Tap a genre below to focus generation.";
        return blendMode == GENRE_BLEND_HYBRID ? "Sounds hybridized from:" : "Sounds pooled from:";
    }

    private void applyGenreSelection(CheckBox random, List<CheckBox> checks, int blendMode) {
        boolean useRandom = random.isChecked();
        int mask = useRandom ? 0 : selectedMask(checks);
        if (!useRandom && mask == 0) {
            useRandom = true;
            suppressGenreCallbacks = true;
            random.setChecked(true);
            for (CheckBox box : checks) box.setChecked(false);
            suppressGenreCallbacks = false;
            mask = 0;
        }

        int oldMask = activeGenreMask();
        int oldBlend = loadGenreBlendMode();
        saveGenre(useRandom, mask, blendMode);
        int newMask = useRandom ? 0 : mask;
        NativeAudio.setGenreBlendMode(blendMode);
        NativeAudio.setGenreMask(newMask);

        if ((newMask != oldMask || blendMode != oldBlend) && NativeAudio.isPlaying()) {
            NativeAudio.forceNew();
        }
        updateMainStatusDelayed();
    }

    private int selectedMask(List<CheckBox> checks) {
        int mask = 0;
        for (int i = 0; i < checks.size(); ++i) if (checks.get(i).isChecked()) mask |= (1 << i);
        return mask;
    }

    private void showDurationChooser() {
        final String[] labels = new String[] {
                "30 sec",
                "1 min",
                "3 min",
                "5 min",
                "10 min",
                "20 min",
                "1 hour",
                "Custom",
                "Random",
                "Infinite"
        };
        new AlertDialog.Builder(this)
                .setTitle("Track length")
                .setItems(labels, (dialog, which) -> {
                    switch (which) {
                        case 0: setTrackDuration(30, false, false); break;
                        case 1: setTrackDuration(60, false, false); break;
                        case 2: setTrackDuration(180, false, false); break;
                        case 3: setTrackDuration(300, false, false); break;
                        case 4: setTrackDuration(600, false, false); break;
                        case 5: setTrackDuration(1200, false, false); break;
                        case 6: setTrackDuration(3600, false, false); break;
                        case 7: showCustomDurationDialog(); break;
                        case 8: setTrackDuration(0, true, false); break;
                        case 9: setTrackDuration(-1, false, true); break;
                        default: break;
                    }
                })
                .show();
    }

    private void showCustomDurationDialog() {
        int current = isTrackSecondsRandom() || isTrackSecondsInfinite() ? DEFAULT_TRACK_SECONDS : loadTrackSeconds();
        int currentMinutes = Math.min(60, current / 60);
        int currentSeconds = Math.min(60, current % 60);

        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(dp(18), dp(6), dp(18), dp(2));

        EditText minutes = editField(Integer.toString(currentMinutes), InputType.TYPE_CLASS_NUMBER);
        minutes.setHint("0-60");
        EditText seconds = editField(Integer.toString(currentSeconds), InputType.TYPE_CLASS_NUMBER);
        seconds.setHint("0-60");

        form.addView(label("Minutes"), params(240, -2, 6));
        form.addView(minutes, params(240, 54, 2));
        form.addView(label("Seconds"), params(240, -2, 8));
        form.addView(seconds, params(240, 54, 2));

        new AlertDialog.Builder(this)
                .setTitle("Custom length")
                .setView(form)
                .setPositiveButton("Set", (dialog, which) -> {
                    int m = clamp(parseInteger(textOf(minutes), 0), 0, 60);
                    int sec = clamp(parseInteger(textOf(seconds), 0), 0, 60);
                    int total = m * 60 + sec;
                    if (total < MIN_TRACK_SECONDS) {
                        total = MIN_TRACK_SECONDS;
                        Toast.makeText(this, "Minimum length is 8 seconds.", Toast.LENGTH_SHORT).show();
                    }
                    setTrackDuration(total, false, false);
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void setTrackDuration(int seconds, boolean random, boolean infinite) {
        if (!random && !infinite) seconds = clamp(seconds, MIN_TRACK_SECONDS, MAX_TRACK_SECONDS);
        getSharedPreferences(PREFS, MODE_PRIVATE)
                .edit()
                .putBoolean(KEY_TRACK_SECONDS_RANDOM, random)
                .putBoolean(KEY_TRACK_SECONDS_INFINITE, infinite)
                .putInt(KEY_TRACK_SECONDS, (random || infinite) ? DEFAULT_TRACK_SECONDS : seconds)
                .apply();
        NativeAudio.setPieceLengthSeconds(nativeTrackSeconds());
        updateMainStatus();
    }

    private void togglePlayback() {
        if (NativeAudio.isPlaying()) {
            Intent intent = new Intent(this, AudioService.class);
            intent.setAction(AudioService.ACTION_STOP);
            startService(intent);
        } else {
            startAudioService(AudioService.ACTION_START, true);
        }
        updateMainStatusDelayed();
    }

    private void skipToNext() {
        startAudioService(NativeAudio.isPlaying() ? AudioService.ACTION_NEXT : AudioService.ACTION_START, true);
        updateMainStatusDelayed();
    }

    private void startAudioService(String action, boolean sendState) {
        Intent intent = new Intent(this, AudioService.class);
        intent.setAction(action);
        if (sendState) {
            int seconds = nativeTrackSeconds();
            int mask = activeGenreMask();
            int blend = loadGenreBlendMode();
            NativeAudio.setPieceLengthSeconds(seconds);
            NativeAudio.setGenreBlendMode(blend);
            NativeAudio.setGenreMask(mask);
            intent.putExtra(AudioService.EXTRA_TRACK_SECONDS, seconds);
            intent.putExtra(AudioService.EXTRA_GENRE_MASK, mask);
            intent.putExtra(AudioService.EXTRA_GENRE_BLEND_MODE, blend);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(intent);
        else startService(intent);
    }

    private void updateMainStatusDelayed() {
        if (startStopButton != null) startStopButton.postDelayed(this::updateMainStatus, 220L);
    }

    private void updateMainStatus() {
        if (startStopButton != null) startStopButton.setText(NativeAudio.isPlaying() ? "Stop" : "Start");
        if (genreButton != null) genreButton.setText(currentGenreText());
        if (elapsedButton != null) elapsedButton.setText(currentElapsedText());
    }

    private String currentGenreText() {
        String value;
        if (isGenreRandom()) {
            int mode = NativeAudio.currentGenreMode();
            value = NativeAudio.isPlaying() && mode > 0 ? "Random -> " + genreModeLabel(mode) : "Random";
        } else if (loadGenreBlendMode() == GENRE_BLEND_HYBRID) {
            value = "Hybrid: " + selectedGenreSummary();
        } else if (NativeAudio.isPlaying()) {
            int mode = NativeAudio.currentGenreMode();
            value = mode > 0 ? genreModeLabel(mode) : selectedGenreSummary();
        } else {
            value = "Pool: " + selectedGenreSummary();
        }
        return "Current Genre - tap to choose\n" + value;
    }

    private String currentElapsedText() {
        int elapsed = (int) Math.max(0.0, NativeAudio.currentElapsedSeconds());
        String totalText;
        if (isTrackSecondsInfinite()) {
            totalText = "Infinite";
        } else if (isTrackSecondsRandom()) {
            int actual = Math.max(MIN_TRACK_SECONDS, NativeAudio.currentPieceLengthSeconds());
            totalText = "Random (" + formatDuration(actual) + ")";
        } else {
            totalText = formatDuration(loadTrackSeconds());
        }
        return "Duration - tap to change\nElapsed: " + formatDuration(elapsed) + " / " + totalText;
    }

    private String selectedGenreSummary() {
        if (isGenreRandom()) return "Random";
        int mask = loadGenreMask();
        if (mask == 0) return "Random";
        StringBuilder out = new StringBuilder();
        int count = 0;
        for (int i = 0; i < GENRE_LABELS.length; ++i) {
            if ((mask & (1 << i)) == 0) continue;
            if (count > 0) out.append(" + ");
            out.append(GENRE_LABELS[i]);
            ++count;
            if (count >= 3) {
                int extra = bitCount(mask) - count;
                if (extra > 0) out.append(" + ").append(extra);
                break;
            }
        }
        return out.length() > 0 ? out.toString() : "Random";
    }

    private String genreModeLabel(int mode) {
        if (mode >= 1 && mode <= GENRE_LABELS.length) return GENRE_LABELS[mode - 1];
        return selectedGenreSummary();
    }

    private int bitCount(int value) {
        int count = 0;
        while (value != 0) {
            value &= value - 1;
            ++count;
        }
        return count;
    }

    private String formatDuration(int totalSeconds) {
        if (totalSeconds < 0) return "Infinite";
        int hours = totalSeconds / 3600;
        int minutes = (totalSeconds / 60) % 60;
        int seconds = totalSeconds % 60;
        if (hours > 0) return String.format(java.util.Locale.US, "%d:%02d:%02d", hours, minutes, seconds);
        return String.format(java.util.Locale.US, "%d:%02d", minutes, seconds);
    }

    private int loadTrackSeconds() {
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        return clamp(prefs.getInt(KEY_TRACK_SECONDS, DEFAULT_TRACK_SECONDS), MIN_TRACK_SECONDS, MAX_TRACK_SECONDS);
    }

    private boolean isTrackSecondsRandom() {
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        return prefs.getBoolean(KEY_TRACK_SECONDS_RANDOM, false) && !prefs.getBoolean(KEY_TRACK_SECONDS_INFINITE, false);
    }

    private boolean isTrackSecondsInfinite() {
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        return prefs.getBoolean(KEY_TRACK_SECONDS_INFINITE, false);
    }

    private int nativeTrackSeconds() {
        if (isTrackSecondsInfinite()) return -1;
        return isTrackSecondsRandom() ? 0 : loadTrackSeconds();
    }

    private boolean isGenreRandom() {
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        boolean random = prefs.getBoolean(KEY_GENRE_RANDOM, true);
        if (!random && loadGenreMask() == 0) return true;
        return random;
    }

    private int loadGenreMask() {
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        return clamp(prefs.getInt(KEY_GENRE_MASK, 0), 0, (1 << GENRE_LABELS.length) - 1);
    }

    private int loadGenreBlendMode() {
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        return prefs.getInt(KEY_GENRE_BLEND_MODE, GENRE_BLEND_POOL) == GENRE_BLEND_HYBRID ? GENRE_BLEND_HYBRID : GENRE_BLEND_POOL;
    }

    private int activeGenreMask() {
        if (isGenreRandom()) return 0;
        int mask = loadGenreMask();
        return mask == 0 ? 0 : mask;
    }

    private void saveGenre(boolean random, int mask, int blendMode) {
        getSharedPreferences(PREFS, MODE_PRIVATE)
                .edit()
                .putBoolean(KEY_GENRE_RANDOM, random || mask == 0)
                .putInt(KEY_GENRE_MASK, random ? 0 : clamp(mask, 0, (1 << GENRE_LABELS.length) - 1))
                .putInt(KEY_GENRE_BLEND_MODE, blendMode == GENRE_BLEND_HYBRID ? GENRE_BLEND_HYBRID : GENRE_BLEND_POOL)
                .apply();
    }

    private int clamp(int value, int min, int max) {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }

    private int parseInteger(String text, int fallback) {
        if (text == null) return fallback;
        try {
            return Integer.parseInt(text.trim());
        } catch (NumberFormatException ex) {
            return fallback;
        }
    }

    private String textOf(EditText field) {
        return field != null && field.getText() != null ? field.getText().toString() : "";
    }

    private LinearLayout baseColumn(boolean center) {
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(center ? Gravity.CENTER : Gravity.CENTER_HORIZONTAL);
        column.setPadding(dp(12), dp(18), dp(12), dp(18));
        return column;
    }

    private Button button(String text, float size) {
        Button button = new Button(this);
        button.setText(text);
        button.setTextSize(size);
        button.setAllCaps(false);
        return button;
    }

    private Button navButton(String text, float size) {
        Button button = button(text, size);
        button.setTextColor(Color.WHITE);
        button.setGravity(Gravity.CENTER);
        button.setSingleLine(false);
        button.setMaxLines(4);
        button.setIncludeFontPadding(true);
        button.setMinHeight(0);
        button.setMinWidth(0);
        button.setPadding(dp(8), dp(6), dp(8), dp(6));
        return button;
    }

    private EditText editField(String text, int inputType) {
        EditText field = new EditText(this);
        field.setSingleLine(true);
        field.setGravity(Gravity.CENTER);
        field.setTextColor(Color.WHITE);
        field.setHintTextColor(0xff888888);
        field.setTextSize(18.0f);
        field.setInputType(inputType);
        field.setText(text == null ? "" : text);
        field.setSelectAllOnFocus(true);
        return field;
    }

    private TextView label(String text) {
        TextView label = new TextView(this);
        label.setText(text);
        label.setTextColor(Color.WHITE);
        label.setTextSize(13.0f);
        label.setGravity(Gravity.CENTER);
        return label;
    }

    private TextView status(String text) {
        TextView status = new TextView(this);
        status.setText(text);
        status.setTextColor(0xffaaaaaa);
        status.setTextSize(12.0f);
        status.setGravity(Gravity.CENTER);
        return status;
    }

    private LinearLayout.LayoutParams params(int widthDp, int heightDp, int topDp) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                dp(widthDp),
                heightDp < 0 ? LinearLayout.LayoutParams.WRAP_CONTENT : dp(heightDp));
        lp.topMargin = dp(topDp);
        lp.gravity = Gravity.CENTER_HORIZONTAL;
        return lp;
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private void setRoot(View view, boolean fill) {
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                fill ? FrameLayout.LayoutParams.MATCH_PARENT : FrameLayout.LayoutParams.WRAP_CONTENT,
                fill ? FrameLayout.LayoutParams.MATCH_PARENT : FrameLayout.LayoutParams.WRAP_CONTENT);
        lp.gravity = Gravity.CENTER;
        root.addView(view, lp);
        setContentView(root, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
    }

    private void requestNotificationPermissionIfNeeded() {
        if (Build.VERSION.SDK_INT >= 33 &&
                checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQUEST_NOTIFICATIONS);
        }
    }
}
