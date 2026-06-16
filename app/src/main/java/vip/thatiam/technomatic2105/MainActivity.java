package vip.thatiam.technomatic2105;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipboardManager;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.Color;
import android.media.AudioManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.MediaStore;
import android.net.Uri;
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

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public final class MainActivity extends Activity {
    private static final int REQUEST_NOTIFICATIONS = 94;
    private static final int DEFAULT_TRACK_SECONDS = 180;
    private static final int MIN_TRACK_SECONDS = 8;
    private static final int MAX_TRACK_SECONDS = 999999;
    private static final int HISTORY_LIMIT = 50;

    private static final String PREFS = "technomatic_2105";
    private static final String KEY_TRACK_SECONDS = "track_seconds";
    private static final String KEY_TRACK_SECONDS_RANDOM = "track_seconds_random";
    private static final String KEY_TRACK_SECONDS_INFINITE = "track_seconds_infinite";
    private static final String KEY_GENRE_RANDOM = "genre_random";
    private static final String KEY_GENRE_MASK = "genre_mask";
    private static final String KEY_GENRE_BLEND_MODE = "genre_blend_mode";

    private static final int SCREEN_MAIN = 0;
    private static final int SCREEN_GENRE = 1;
    private static final int SCREEN_ADVANCED = 2;

    private static final int GENRE_BLEND_POOL = 0;
    private static final int GENRE_BLEND_HYBRID = 1;

    // UI order differs from native bit order so No Genre can sit at the top.
    private static final String[] GENRE_LABELS = new String[] {
            "-- No Genre --",
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
            "Cold Arcade"
    };

    private static final String[] GENRE_DESCRIPTIONS = new String[] {
            "Raw engine behavior outside named restraints.",
            "Hard chrome rhythm with bright machine motion.",
            "Smooth electric flow and rounded melodic current.",
            "Crystal hooks over clipped low-end logic.",
            "Grainy motor percussion and old-voltage haze.",
            "Fluid bass lines inside a coded lattice.",
            "Slow luminous motion with nocturnal surface glow.",
            "Fractured pressure, rough edges, unstable pulses.",
            "Low gravity, bass pull, dark harmonic mass.",
            "Tiny hard pulses arranged like an electronic rite.",
            "Mellow currents, airy leads, restrained percussion.",
            "Large circular bass motion with weight and momentum.",
            "Icy square hooks and precise mechanical timing."
    };

    private static final int[] GENRE_MODES = new int[] {
            13, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
    };

    private final Handler statusHandler = new Handler(Looper.getMainLooper());
    private final ArrayList<String> history = new ArrayList<>();

    private Button startStopButton;
    private Button previousButton;
    private Button genreButton;
    private Button elapsedButton;
    private int currentScreen = SCREEN_MAIN;
    private boolean suppressGenreCallbacks = false;
    private volatile boolean exportRunning = false;
    private volatile boolean exportCancelRequested = false;
    private Thread exportThread = null;
    private String lastObservedSongData = "";
    private String lastExportPath = "";
    private String exportSourceSongData = "";
    private volatile String exportStatusText = "";

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
        if (currentScreen == SCREEN_GENRE || currentScreen == SCREEN_ADVANCED) {
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
                    if (currentScreen == SCREEN_GENRE || currentScreen == SCREEN_ADVANCED) {
                        showMainScreen();
                    } else {
                        finish();
                    }
                });
    }

    private void showMainScreen() {
        currentScreen = SCREEN_MAIN;
        LinearLayout controls = baseColumn();
        int w = contentWidthDp();
        boolean wide = isLandscape();

        startStopButton = button(NativeAudio.isPlaying() ? "Stop" : "Start", 30.0f);
        startStopButton.setOnClickListener(view -> togglePlayback());
        controls.addView(startStopButton, params(w, wide ? 68 : 84, 0));

        genreButton = navButton(currentGenreText(), 15.0f);
        genreButton.setOnClickListener(view -> showGenreSelectorScreen());
        elapsedButton = navButton(currentElapsedText(), 15.0f);
        elapsedButton.setOnClickListener(view -> showDurationChooser());

        if (wide) {
            LinearLayout infoRow = new LinearLayout(this);
            infoRow.setOrientation(LinearLayout.HORIZONTAL);
            infoRow.setGravity(Gravity.CENTER);
            LinearLayout.LayoutParams infoLpA = new LinearLayout.LayoutParams(dp((w - 8) / 2), dp(60));
            LinearLayout.LayoutParams infoLpB = new LinearLayout.LayoutParams(dp((w - 8) / 2), dp(60));
            infoLpB.leftMargin = dp(8);
            infoRow.addView(genreButton, infoLpA);
            infoRow.addView(elapsedButton, infoLpB);
            controls.addView(infoRow, params(w, 60, 8));
        } else {
            controls.addView(genreButton, params(w, 52, 8));
            controls.addView(elapsedButton, params(w, 52, 6));
        }

        LinearLayout transportRow = new LinearLayout(this);
        transportRow.setOrientation(LinearLayout.HORIZONTAL);
        transportRow.setGravity(Gravity.CENTER);
        previousButton = button("Previous", 14.0f);
        previousButton.setOnClickListener(view -> previousSound());
        Button restart = button("Restart", 14.0f);
        restart.setOnClickListener(view -> restartCurrentSound());
        Button next = button("Next", 14.0f);
        next.setOnClickListener(view -> nextSound());
        int cell = (w - 16) / 3;
        transportRow.addView(previousButton, new LinearLayout.LayoutParams(dp(cell), dp(50)));
        LinearLayout.LayoutParams mid = new LinearLayout.LayoutParams(dp(cell), dp(50));
        mid.leftMargin = dp(8);
        transportRow.addView(restart, mid);
        LinearLayout.LayoutParams right = new LinearLayout.LayoutParams(dp(cell), dp(50));
        right.leftMargin = dp(8);
        transportRow.addView(next, right);
        controls.addView(transportRow, params(w, 50, 10));

        TextView notice = status("If you're not already vegetarian, you need to see Bloodguiltcurse.net");
        notice.setTextSize(12.0f);
        controls.addView(notice, params(w, -2, 14));

        Button advanced = button("Advanced", 14.0f);
        advanced.setOnClickListener(view -> showAdvancedScreen());
        controls.addView(advanced, params(w, 46, 12));

        setScrollRoot(controls);
        updateMainStatus();
    }

    private void showGenreSelectorScreen() {
        currentScreen = SCREEN_GENRE;
        LinearLayout controls = baseColumn();
        controls.setGravity(Gravity.CENTER_HORIZONTAL);
        int w = contentWidthDp();

        Button back = button("Back", 15.0f);
        back.setOnClickListener(view -> showMainScreen());
        controls.addView(back, params(w, 46, 0));
        controls.addView(label("GENRE SELECTOR"), params(w, -2, 10));

        CheckBox random = new CheckBox(this);
        random.setText("Random");
        random.setTextSize(18.0f);
        random.setTextColor(Color.WHITE);
        random.setGravity(Gravity.CENTER_VERTICAL);
        random.setChecked(isGenreRandom());
        controls.addView(random, params(w, 48, 8));

        RadioGroup blendGroup = new RadioGroup(this);
        blendGroup.setOrientation(RadioGroup.HORIZONTAL);
        blendGroup.setGravity(Gravity.CENTER);
        RadioButton pool = radio("Pool");
        RadioButton hybrid = radio("Hybrid");
        pool.setId(View.generateViewId());
        hybrid.setId(View.generateViewId());
        blendGroup.addView(pool, new RadioGroup.LayoutParams(dp((w - 8) / 2), dp(44)));
        blendGroup.addView(hybrid, new RadioGroup.LayoutParams(dp((w - 8) / 2), dp(44)));
        blendGroup.check(loadGenreBlendMode() == GENRE_BLEND_HYBRID ? hybrid.getId() : pool.getId());
        controls.addView(blendGroup, params(w, 48, 2));

        TextView message = status(genreSelectorMessage(random.isChecked(), selectedBlendMode(pool, hybrid)));
        controls.addView(message, params(w, -2, 6));

        List<CheckBox> checks = new ArrayList<>();
        List<TextView> descriptions = new ArrayList<>();
        int savedMask = loadGenreMask();
        for (int i = 0; i < GENRE_LABELS.length; ++i) {
            LinearLayout item = new LinearLayout(this);
            item.setOrientation(LinearLayout.VERTICAL);
            item.setPadding(dp(4), dp(3), dp(4), dp(3));
            CheckBox box = new CheckBox(this);
            box.setText(GENRE_LABELS[i]);
            box.setTextSize(17.0f);
            box.setTextColor(Color.WHITE);
            box.setGravity(Gravity.CENTER_VERTICAL);
            box.setChecked((savedMask & genreBitForUiIndex(i)) != 0);
            TextView desc = status(GENRE_DESCRIPTIONS[i]);
            desc.setGravity(Gravity.LEFT);
            desc.setTextSize(11.5f);
            item.addView(box, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(36)));
            item.addView(desc, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
            checks.add(box);
            descriptions.add(desc);
            controls.addView(item, params(w, -2, 3));
        }

        Runnable refreshVisuals = () -> {
            boolean randomOn = random.isChecked();
            int blendMode = selectedBlendMode(pool, hybrid);
            pool.setTextColor(randomOn ? 0xff888888 : Color.WHITE);
            hybrid.setTextColor(randomOn ? 0xff888888 : Color.WHITE);
            for (int i = 0; i < checks.size(); ++i) {
                checks.get(i).setTextColor(randomOn ? 0xff777777 : Color.WHITE);
                descriptions.get(i).setTextColor(randomOn ? 0xff666666 : 0xffaaaaaa);
            }
            message.setText(genreSelectorMessage(randomOn, blendMode));
        };

        random.setOnClickListener(view -> {
            if (suppressGenreCallbacks) return;
            if (random.isChecked()) {
                suppressGenreCallbacks = true;
                for (CheckBox box : checks) box.setChecked(false);
                suppressGenreCallbacks = false;
            } else if (selectedMask(checks) == 0 && checks.size() > 1) {
                suppressGenreCallbacks = true;
                checks.get(1).setChecked(true);
                suppressGenreCallbacks = false;
            }
            refreshVisuals.run();
            applyGenreSelection(random, checks, selectedBlendMode(pool, hybrid));
        });

        blendGroup.setOnCheckedChangeListener((group, checkedId) -> {
            if (suppressGenreCallbacks) return;
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

        refreshVisuals.run();
        setScrollRoot(controls);
    }

    private void showAdvancedScreen() {
        currentScreen = SCREEN_ADVANCED;
        LinearLayout controls = baseColumn();
        controls.setGravity(Gravity.CENTER_HORIZONTAL);
        int w = contentWidthDp();

        Button back = button("Back", 15.0f);
        back.setOnClickListener(view -> showMainScreen());
        controls.addView(back, params(w, 46, 0));
        controls.addView(label("ADVANCED"), params(w, -2, 10));

        String seedText = currentSeedText();
        Button seedButton = navButton("Seed: " + seedText + "\nTap to copy", 15.0f);
        seedButton.setOnClickListener(view -> copySeedToClipboard(seedText));
        controls.addView(seedButton, params(w, 64, 12));

        TextView seedExplain = status("Seed: the number that regenerates the current sound.");
        seedExplain.setTextSize(11.5f);
        controls.addView(seedExplain, params(w, -2, 6));

        EditText seedInput = editField("", InputType.TYPE_CLASS_NUMBER);
        seedInput.setHint("enter seed");
        controls.addView(label("Load seed"), params(w, -2, 12));
        TextView loadExplain = status("Load seed: enter a seed to hear that generated sound again.");
        loadExplain.setTextSize(11.5f);
        controls.addView(loadExplain, params(w, -2, 4));
        controls.addView(seedInput, params(w, 50, 2));

        Button load = button("Load", 15.0f);
        load.setOnClickListener(view -> loadSeed(textOf(seedInput)));
        controls.addView(load, params(w, 46, 8));

        Button export = button(exportRunning ? "Cancel Export" : "Export to OGG", 15.0f);
        export.setOnClickListener(view -> {
            if (exportRunning) requestExportCancel("Export cancellation requested.");
            else exportCurrentToOgg();
        });
        controls.addView(export, params(w, 48, 14));

        TextView note = status(exportRunning
                ? exportStatusForUi()
                : "Export snapshots the current sound and saves it to Music/Technomatic2105 without restarting playback.");
        note.setTextSize(11.5f);
        controls.addView(note, params(w, -2, 8));

        if (lastExportPath.length() > 0) {
            TextView exported = status("Last export: " + lastExportPath);
            exported.setTextSize(11.5f);
            controls.addView(exported, params(w, -2, 8));
        }

        setScrollRoot(controls);
    }

    private void showDurationChooser() {
        final String[] labels = new String[] {
                "30 sec", "1 min", "3 min", "5 min", "10 min", "20 min", "1 hour", "Custom", "Random", "Infinite"
        };
        new AlertDialog.Builder(this)
                .setTitle("Duration")
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
                        case 8: setTrackDuration(DEFAULT_TRACK_SECONDS, true, false); break;
                        case 9: setTrackDuration(DEFAULT_TRACK_SECONDS, false, true); break;
                        default: break;
                    }
                })
                .show();
    }

    private void showCustomDurationDialog() {
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(dp(18), dp(6), dp(18), dp(2));
        EditText minutes = editField("3", InputType.TYPE_CLASS_NUMBER);
        EditText seconds = editField("0", InputType.TYPE_CLASS_NUMBER);
        form.addView(label("Minutes (0-60)"), params(240, -2, 6));
        form.addView(minutes, params(240, 50, 2));
        form.addView(label("Seconds (0-60)"), params(240, -2, 8));
        form.addView(seconds, params(240, 50, 2));

        new AlertDialog.Builder(this)
                .setTitle("Custom length")
                .setView(form)
                .setPositiveButton("Set", (dialog, which) -> {
                    int m = clamp(parseInteger(textOf(minutes), 0), 0, 60);
                    int s = clamp(parseInteger(textOf(seconds), 0), 0, 60);
                    int total = m * 60 + s;
                    if (total < MIN_TRACK_SECONDS) {
                        total = MIN_TRACK_SECONDS;
                        Toast.makeText(this, "Minimum length is 8 seconds.", Toast.LENGTH_SHORT).show();
                    }
                    setTrackDuration(total, false, false);
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void togglePlayback() {
        if (NativeAudio.isPlaying()) {
            startService(serviceIntent(AudioService.ACTION_STOP));
        } else {
            startAudioService(serviceIntent(AudioService.ACTION_START));
        }
        updateMainStatusDelayed();
    }

    private void nextSound() {
        rememberCurrentForHistory();
        startAudioService(serviceIntent(NativeAudio.isPlaying() ? AudioService.ACTION_NEXT : AudioService.ACTION_START));
        updateMainStatusDelayed();
    }

    private void restartCurrentSound() {
        String data = NativeAudio.currentSongData();
        if (data.length() == 0) return;
        playSongData(data, false);
    }

    private void previousSound() {
        if (history.isEmpty()) {
            Toast.makeText(this, "No previous sound yet.", Toast.LENGTH_SHORT).show();
            updateMainStatus();
            return;
        }
        String current = NativeAudio.currentSongData();
        String previous = history.remove(history.size() - 1);
        if (current.length() > 0 && !current.equals(previous)) lastObservedSongData = previous;
        playSongData(previous, false);
    }

    private void playSongData(String data, boolean pushCurrentToHistory) {
        if (data == null || data.length() == 0) return;
        if (pushCurrentToHistory) rememberCurrentForHistory();
        Intent intent = serviceIntent(AudioService.ACTION_LOAD_SOUND);
        intent.putExtra(AudioService.EXTRA_SONG_DATA, data);
        startAudioService(intent);
        lastObservedSongData = data;
        updateMainStatusDelayed();
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
        NativeAudio.setGenreMask(newMask);
        NativeAudio.setGenreBlendMode(blendMode);

        if (newMask != oldMask || blendMode != oldBlend) {
            rememberCurrentForHistory();
            if (NativeAudio.isPlaying()) {
                Intent intent = serviceIntent(AudioService.ACTION_START);
                intent.putExtra(AudioService.EXTRA_FORCE_RESTART, true);
                startAudioService(intent);
            } else {
                NativeAudio.setGenreStateAndForceNew(newMask, blendMode);
            }
        }
        updateMainStatusDelayed();
    }

    private int exportDurationSeconds() {
        int seconds;
        if (isTrackSecondsRandom()) {
            seconds = NativeAudio.currentPieceLengthSeconds();
        } else {
            seconds = nativeTrackSeconds();
        }
        if (seconds < MIN_TRACK_SECONDS) seconds = MIN_TRACK_SECONDS;
        if (seconds > MAX_TRACK_SECONDS) seconds = MAX_TRACK_SECONDS;
        return seconds;
    }

    private void exportCurrentToOgg() {
        if (exportRunning) {
            requestExportCancel("Export cancellation requested.");
            return;
        }
        if (isTrackSecondsInfinite() || NativeAudio.currentPieceLengthSeconds() < 0) {
            Toast.makeText(this, "Set a finite duration before export.", Toast.LENGTH_LONG).show();
            return;
        }
        if (Build.VERSION.SDK_INT < 29) {
            Toast.makeText(this, "OGG export requires Android 10 or later.", Toast.LENGTH_LONG).show();
            return;
        }

        final String sourceData = NativeAudio.currentSongData();
        if (sourceData == null || sourceData.length() == 0) {
            Toast.makeText(this, "Start a sound before exporting.", Toast.LENGTH_LONG).show();
            return;
        }

        final int seconds = exportDurationSeconds();
        final String data = songDataWithDuration(sourceData, seconds);
        exportRunning = true;
        exportCancelRequested = false;
        lastExportPath = "";
        exportSourceSongData = data;
        exportStatusText = "Rendering captured sound offline...";
        showAdvancedScreen();
        Toast.makeText(this, "Export started in the background.", Toast.LENGTH_SHORT).show();

        exportThread = new Thread(() -> {
            File raw = null;
            File tempOgg = null;
            boolean ok = false;
            String message;
            String publicPath = "";
            Uri publicUri = null;
            final OggExporter.CancellationToken token = () -> exportCancelRequested || Thread.currentThread().isInterrupted();
            try {
                String stamp = new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(new Date());
                String displayName = "technomatic_2105_" + stamp + ".ogg";
                raw = File.createTempFile("technomatic_2105_export_", ".pcm", getCacheDir());
                tempOgg = File.createTempFile("technomatic_2105_export_", ".ogg", getCacheDir());
                updateExportStatus("Rendering captured sound offline...");
                checkExportCancelled(token);
                if (!NativeAudio.exportPcm16ToFile(data, seconds, raw.getAbsolutePath())) {
                    checkExportCancelled(token);
                    throw new java.io.IOException("Native render failed.");
                }
                checkExportCancelled(token);
                long expectedRawBytes = (long) seconds * 48000L * 2L * 2L;
                long actualRawBytes = raw.length();
                if (actualRawBytes != expectedRawBytes) {
                    throw new java.io.IOException("Native render length mismatch: expected " + expectedRawBytes + " bytes, got " + actualRawBytes + ".");
                }
                updateExportStatus("Encoding OGG...");
                OggExporter.encodeRawPcm16ToOgg(raw, tempOgg, token);
                checkExportCancelled(token);
                if (!tempOgg.exists() || tempOgg.length() <= 0L) {
                    throw new java.io.IOException("Encoder produced an empty OGG file.");
                }
                updateExportStatus("Publishing to Music/Technomatic2105...");
                ExportResult result = publishOggToMusic(tempOgg, displayName, token);
                publicPath = result.displayPath;
                publicUri = result.uri;
                ok = true;
                message = "Exported to " + publicPath;
            } catch (Exception ex) {
                message = safeMessage(ex);
                if (!message.toLowerCase(Locale.US).contains("cancel")) {
                    message = "Export failed: " + message;
                }
            } finally {
                if (raw != null && raw.exists()) raw.delete();
                if (tempOgg != null && tempOgg.exists()) tempOgg.delete();
            }
            final boolean finalOk = ok;
            final String finalMessage = message;
            final String finalPublicPath = publicPath;
            runOnUiThread(() -> {
                exportRunning = false;
                exportCancelRequested = false;
                exportThread = null;
                exportSourceSongData = "";
                exportStatusText = "";
                if (finalOk) {
                    lastExportPath = finalPublicPath;
                }
                Toast.makeText(this, finalMessage, Toast.LENGTH_LONG).show();
                showExportResultDialog(finalOk, finalMessage);
                if (currentScreen == SCREEN_ADVANCED) showAdvancedScreen();
            });
        }, "TechnomaticOggExport");
        exportThread.start();
    }

    private ExportResult publishOggToMusic(File encodedOgg, String displayName, OggExporter.CancellationToken token) throws java.io.IOException {
        checkExportCancelled(token);
        final String relativeDir = Environment.DIRECTORY_MUSIC + "/Technomatic2105";
        ContentResolver resolver = getContentResolver();
        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, displayName);
        values.put(MediaStore.MediaColumns.MIME_TYPE, "audio/ogg");
        values.put(MediaStore.Audio.Media.TITLE, displayName.replace(".ogg", ""));
        values.put(MediaStore.Audio.Media.IS_MUSIC, 1);
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, relativeDir);
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);

        Uri uri = resolver.insert(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, values);
        if (uri == null) throw new java.io.IOException("Could not create MediaStore record.");

        try {
            try (InputStream input = new FileInputStream(encodedOgg);
                 OutputStream output = resolver.openOutputStream(uri, "w")) {
                if (output == null) throw new java.io.IOException("Could not open public Music output stream.");
                byte[] buffer = new byte[64 * 1024];
                int read;
                while ((read = input.read(buffer)) >= 0) {
                    checkExportCancelled(token);
                    if (read > 0) output.write(buffer, 0, read);
                }
                output.flush();
            }

            checkExportCancelled(token);
            ContentValues done = new ContentValues();
            done.put(MediaStore.MediaColumns.IS_PENDING, 0);
            resolver.update(uri, done, null, null);
            return new ExportResult(uri, relativeDir + "/" + displayName);
        } catch (Exception ex) {
            try { resolver.delete(uri, null, null); } catch (Exception ignored) {}
            if (ex instanceof java.io.IOException) throw (java.io.IOException) ex;
            throw new java.io.IOException(ex);
        }
    }

    private void requestExportCancel(String message) {
        if (!exportRunning) return;
        exportCancelRequested = true;
        exportSourceSongData = "";
        exportStatusText = "Cancelling export...";
        NativeAudio.cancelExportRender();
        Thread thread = exportThread;
        if (thread != null) thread.interrupt();
        Toast.makeText(this, message, Toast.LENGTH_SHORT).show();
        if (currentScreen == SCREEN_ADVANCED) showAdvancedScreen();
    }

    private void updateExportStatus(String text) {
        exportStatusText = text == null ? "" : text;
        if (currentScreen == SCREEN_ADVANCED) {
            runOnUiThread(() -> {
                if (currentScreen == SCREEN_ADVANCED) showAdvancedScreen();
            });
        }
    }

    private String exportStatusForUi() {
        String phase = exportStatusText == null || exportStatusText.length() == 0
                ? "Exporting OGG..."
                : exportStatusText;
        return phase + " Live playback is independent. Tap Cancel Export to stop this job.";
    }

    private static void checkExportCancelled(OggExporter.CancellationToken token) throws java.io.IOException {
        if (token != null && token.isCancellationRequested()) throw new java.io.IOException("Export cancelled.");
    }

    private void showExportResultDialog(boolean ok, String message) {
        boolean cancelled = message != null && message.toLowerCase(Locale.US).contains("cancel");
        new AlertDialog.Builder(this)
                .setTitle(ok ? "Export complete" : (cancelled ? "Export cancelled" : "Export failed"))
                .setMessage(message)
                .setPositiveButton("OK", null)
                .show();
    }

    private String safeMessage(Exception ex) {
        String msg = ex == null ? "unknown error" : ex.getMessage();
        if (msg == null || msg.trim().length() == 0) return ex == null ? "unknown error" : ex.getClass().getSimpleName();
        return msg;
    }

    private static final class ExportResult {
        final Uri uri;
        final String displayPath;

        ExportResult(Uri uri, String displayPath) {
            this.uri = uri;
            this.displayPath = displayPath;
        }
    }

    private void copySeedToClipboard(String seed) {
        ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (clipboard != null) clipboard.setPrimaryClip(ClipData.newPlainText("Technomatic 2105 seed", seed));
        Toast.makeText(this, "Seed copied.", Toast.LENGTH_SHORT).show();
    }

    private void loadSeed(String text) {
        String trimmed = text == null ? "" : text.trim();
        if (trimmed.length() == 0) {
            Toast.makeText(this, "Enter a seed first.", Toast.LENGTH_SHORT).show();
            return;
        }
        long value;
        try {
            value = Long.parseLong(trimmed);
        } catch (NumberFormatException ex) {
            Toast.makeText(this, "Invalid seed.", Toast.LENGTH_SHORT).show();
            return;
        }
        if (value < 0L || value > 0xffffffffL) {
            Toast.makeText(this, "Seed must be 0 to 4294967295.", Toast.LENGTH_SHORT).show();
            return;
        }
        int seconds = isTrackSecondsInfinite() ? DEFAULT_TRACK_SECONDS : Math.max(MIN_TRACK_SECONDS, nativeTrackSeconds());
        String data = "technomatic2105-v1;seed=" + value + ";seconds=" + seconds +
                ";edited=0;gmask=" + activeGenreMask() + ";gblend=" + loadGenreBlendMode();
        playSongData(data, true);
        Toast.makeText(this, "Seed loaded.", Toast.LENGTH_SHORT).show();
    }

    private void rememberCurrentForHistory() {
        String data = NativeAudio.currentSongData();
        if (data == null || data.length() == 0) return;
        if (!history.isEmpty() && data.equals(history.get(history.size() - 1))) return;
        history.add(data);
        while (history.size() > HISTORY_LIMIT) history.remove(0);
        lastObservedSongData = data;
    }

    private void observeSongDataChange() {
        if (!NativeAudio.isPlaying() && lastObservedSongData.length() == 0) return;
        String data = NativeAudio.currentSongData();
        if (data == null || data.length() == 0) return;
        if (lastObservedSongData.length() == 0) {
            lastObservedSongData = data;
            return;
        }
        if (!data.equals(lastObservedSongData)) {
            // Export is an offline render of the source generator data captured when Export starts.
            // Live playback may legitimately advance before encoding/publishing finishes, especially
            // for short tracks with early outro transitions, so do not cancel export here.
            if (history.isEmpty() || !lastObservedSongData.equals(history.get(history.size() - 1))) {
                history.add(lastObservedSongData);
                while (history.size() > HISTORY_LIMIT) history.remove(0);
            }
            lastObservedSongData = data;
        }
    }

    private Intent serviceIntent(String action) {
        Intent intent = new Intent(this, AudioService.class);
        intent.setAction(action);
        int seconds = nativeTrackSeconds();
        int mask = activeGenreMask();
        int blend = loadGenreBlendMode();
        NativeAudio.setPieceLengthSeconds(seconds);
        NativeAudio.setGenreBlendMode(blend);
        NativeAudio.setGenreMask(mask);
        intent.putExtra(AudioService.EXTRA_TRACK_SECONDS, seconds);
        intent.putExtra(AudioService.EXTRA_GENRE_MASK, mask);
        intent.putExtra(AudioService.EXTRA_GENRE_BLEND_MODE, blend);
        return intent;
    }

    private void startAudioService(Intent intent) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(intent);
        else startService(intent);
    }

    private void updateMainStatusDelayed() {
        if (startStopButton != null) startStopButton.postDelayed(this::updateMainStatus, 260L);
    }

    private void updateMainStatus() {
        observeSongDataChange();
        if (startStopButton != null) startStopButton.setText(NativeAudio.isPlaying() ? "Stop" : "Start");
        if (genreButton != null) genreButton.setText(currentGenreText());
        if (elapsedButton != null) elapsedButton.setText(currentElapsedText());
        if (previousButton != null) previousButton.setEnabled(!history.isEmpty());
    }

    private String currentGenreText() {
        return "Genre: " + currentGenreValue() + "  >";
    }

    private String currentGenreValue() {
        int mode = NativeAudio.currentGenreMode();
        if (loadGenreBlendMode() == GENRE_BLEND_HYBRID && !isGenreRandom() && bitCount(activeGenreMask()) > 1) {
            return "Hybrid: " + selectedGenreSummary();
        }
        if (NativeAudio.isPlaying() && mode > 0) return genreModeLabel(mode);
        if (!isGenreRandom()) {
            if (loadGenreBlendMode() == GENRE_BLEND_POOL && bitCount(activeGenreMask()) > 1) return "Pool: " + selectedGenreSummary();
            return selectedGenreSummary();
        }
        return "Random";
    }

    private String currentElapsedText() {
        int elapsed = (int) Math.max(0.0, NativeAudio.currentElapsedSeconds());
        String totalText;
        if (isTrackSecondsInfinite()) {
            totalText = "Infinite";
        } else if (isTrackSecondsRandom()) {
            int actual = Math.max(MIN_TRACK_SECONDS, NativeAudio.currentPieceLengthSeconds());
            totalText = "Random " + formatDuration(actual);
        } else {
            totalText = formatDuration(loadTrackSeconds());
        }
        return "Elapsed: " + formatDuration(elapsed) + " / " + totalText + "  >";
    }

    private String currentSeedText() {
        long seed = seedFromSongData(NativeAudio.currentSongData());
        return String.valueOf(seed & 0xffffffffL);
    }

    private String songDataWithDuration(String data, int seconds) {
        if (data == null || data.length() == 0) return data;
        int pos = data.indexOf("seconds=");
        if (pos < 0) return data + ";seconds=" + seconds;
        int start = pos + "seconds=".length();
        int end = start;
        while (end < data.length()) {
            char c = data.charAt(end);
            if (c < '0' || c > '9') break;
            ++end;
        }
        return data.substring(0, start) + seconds + data.substring(end);
    }

    private long seedFromSongData(String data) {
        if (data == null) return 0L;
        String needle = "seed=";
        int pos = data.indexOf(needle);
        if (pos < 0) return 0L;
        int i = pos + needle.length();
        long value = 0L;
        boolean any = false;
        while (i < data.length()) {
            char c = data.charAt(i);
            if (c < '0' || c > '9') break;
            any = true;
            value = value * 10L + (long) (c - '0');
            if (value > 0xffffffffL) return 0L;
            ++i;
        }
        return any ? value : 0L;
    }

    private String selectedGenreSummary() {
        if (isGenreRandom()) return "Random";
        int mask = loadGenreMask();
        if (mask == 0) return "Random";
        StringBuilder out = new StringBuilder();
        int count = 0;
        for (int i = 0; i < GENRE_LABELS.length; ++i) {
            if ((mask & genreBitForUiIndex(i)) == 0) continue;
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
        for (int i = 0; i < GENRE_MODES.length; ++i) {
            if (GENRE_MODES[i] == mode) return GENRE_LABELS[i];
        }
        return selectedGenreSummary();
    }

    private String genreSelectorMessage(boolean random, int blendMode) {
        if (random) return "Random uses the full engine.";
        return blendMode == GENRE_BLEND_HYBRID ? "Sounds hybridized from:" : "Sounds pooled from:";
    }

    private int selectedMask(List<CheckBox> checks) {
        int mask = 0;
        for (int i = 0; i < checks.size(); ++i) if (checks.get(i).isChecked()) mask |= genreBitForUiIndex(i);
        return mask;
    }

    private int selectedBlendMode(RadioButton pool, RadioButton hybrid) {
        return hybrid.isChecked() ? GENRE_BLEND_HYBRID : GENRE_BLEND_POOL;
    }

    private int genreBitForUiIndex(int uiIndex) {
        int mode = GENRE_MODES[uiIndex];
        if (mode <= 0) return 0;
        return 1 << (mode - 1);
    }

    private int bitCount(int value) {
        int count = 0;
        while (value != 0) {
            value &= value - 1;
            ++count;
        }
        return count;
    }

    private int loadTrackSeconds() {
        return clamp(getSharedPreferences(PREFS, MODE_PRIVATE).getInt(KEY_TRACK_SECONDS, DEFAULT_TRACK_SECONDS), MIN_TRACK_SECONDS, MAX_TRACK_SECONDS);
    }

    private boolean isTrackSecondsRandom() {
        return getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean(KEY_TRACK_SECONDS_RANDOM, false);
    }

    private boolean isTrackSecondsInfinite() {
        return getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean(KEY_TRACK_SECONDS_INFINITE, false);
    }

    private int nativeTrackSeconds() {
        if (isTrackSecondsInfinite()) return -1;
        if (isTrackSecondsRandom()) return 0;
        return loadTrackSeconds();
    }

    private boolean isGenreRandom() {
        return getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean(KEY_GENRE_RANDOM, true);
    }

    private int loadGenreMask() {
        return clamp(getSharedPreferences(PREFS, MODE_PRIVATE).getInt(KEY_GENRE_MASK, 0), 0, (1 << 13) - 1);
    }

    private int loadGenreBlendMode() {
        int mode = getSharedPreferences(PREFS, MODE_PRIVATE).getInt(KEY_GENRE_BLEND_MODE, GENRE_BLEND_POOL);
        return mode == GENRE_BLEND_HYBRID ? GENRE_BLEND_HYBRID : GENRE_BLEND_POOL;
    }

    private int activeGenreMask() {
        if (isGenreRandom()) return 0;
        return loadGenreMask();
    }

    private void saveGenre(boolean random, int mask, int blendMode) {
        getSharedPreferences(PREFS, MODE_PRIVATE)
                .edit()
                .putBoolean(KEY_GENRE_RANDOM, random || mask == 0)
                .putInt(KEY_GENRE_MASK, random ? 0 : clamp(mask, 0, (1 << 13) - 1))
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

    private String formatDuration(int totalSeconds) {
        if (totalSeconds < 0) return "Infinite";
        int hours = totalSeconds / 3600;
        int minutes = (totalSeconds / 60) % 60;
        int seconds = totalSeconds % 60;
        if (hours > 0) return String.format(Locale.US, "%d:%02d:%02d", hours, minutes, seconds);
        return String.format(Locale.US, "%d:%02d", minutes, seconds);
    }

    private LinearLayout baseColumn() {
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        column.setPadding(dp(12), dp(12), dp(12), dp(16));
        return column;
    }

    private Button button(String text, float size) {
        Button button = new Button(this);
        button.setText(text);
        button.setTextSize(size);
        button.setAllCaps(false);
        button.setSingleLine(false);
        button.setMaxLines(3);
        button.setMinHeight(0);
        button.setMinWidth(0);
        return button;
    }

    private Button navButton(String text, float size) {
        Button button = button(text, size);
        button.setTextColor(Color.WHITE);
        button.setGravity(Gravity.CENTER);
        button.setMaxLines(2);
        button.setIncludeFontPadding(false);
        button.setPadding(dp(6), dp(4), dp(6), dp(4));
        return button;
    }

    private RadioButton radio(String text) {
        RadioButton radio = new RadioButton(this);
        radio.setText(text);
        radio.setTextSize(16.0f);
        radio.setTextColor(Color.WHITE);
        radio.setGravity(Gravity.CENTER);
        radio.setButtonTintList(android.content.res.ColorStateList.valueOf(Color.WHITE));
        return radio;
    }

    private EditText editField(String text, int inputType) {
        EditText field = new EditText(this);
        field.setSingleLine(true);
        field.setGravity(Gravity.CENTER);
        field.setTextColor(Color.WHITE);
        field.setHintTextColor(0xff888888);
        field.setTextSize(17.0f);
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

    private int contentWidthDp() {
        int screen = getResources().getConfiguration().screenWidthDp;
        if (screen <= 0) screen = 360;
        int max = isLandscape() ? 760 : 560;
        return Math.max(292, Math.min(max, screen - 28));
    }

    private boolean isLandscape() {
        return getResources().getConfiguration().orientation == Configuration.ORIENTATION_LANDSCAPE;
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private void setScrollRoot(View view) {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(false);
        scroll.addView(view, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));
        setRoot(scroll);
    }

    private void setRoot(View view) {
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.WRAP_CONTENT);
        lp.gravity = Gravity.TOP | Gravity.CENTER_HORIZONTAL;
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
