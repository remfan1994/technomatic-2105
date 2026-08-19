package vip.thatiam.technomatic2105;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.Color;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
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
import java.util.Locale;

public final class MainActivity extends Activity {
    private static final int REQUEST_NOTIFICATIONS = 94;
    private static final int HISTORY_LIMIT = 20;
    private static final int DEFAULT_EXPORT_SECONDS = 180;
    private static final int MIN_EXPORT_SECONDS = 8;
    private static final int MAX_EXPORT_SECONDS = 3660;

    private static final String PREFS = "technomatic_2105";
    private static final String KEY_V23_CHANNEL_MODE = "v23_channel_mode";
    private static final String KEY_V23_EXPORT_SECONDS = "v23_export_seconds";
    private static final String KEY_V24_CHANNEL_MASK = "v24_channel_mask";
    private static final String KEY_V24_CHANNEL_BLEND = "v24_channel_blend";
    private static final String KEY_V24_CHANNEL_PRIMARY = "v24_channel_primary";

    private static final int SCREEN_MAIN = 0;
    private static final int SCREEN_CHANNEL = 1;
    private static final int SCREEN_ADVANCED = 2;

    private enum ExportFormat {
        OGG("OGG", ".ogg", "audio/ogg"),
        FLAC("FLAC", ".flac", "audio/flac");

        final String label;
        final String extension;
        final String mimeType;

        ExportFormat(String label, String extension, String mimeType) {
            this.label = label;
            this.extension = extension;
            this.mimeType = mimeType;
        }
    }

    private static final String[] CHANNEL_LABELS = new String[] {
            "No Channel",
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

    private static final String[] CHANNEL_DESCRIPTIONS = new String[] {
            "Pure unrestricted generation; no channel character is imposed.",
            "Hard chrome rhythm, bright motion, and decisive pulse pressure.",
            "Rounded electric flow, soft circuitry, and patient melodic current.",
            "Crystalline hooks, clipped low-end logic, and suspended reflections.",
            "Grainy motors, weathered voltage, and percussion with mechanical dust.",
            "Fluid bass motion through a precise but continuously shifting lattice.",
            "Luminous nocturnal drift with long melodic trails and open air.",
            "Fractured pressure, unstable edges, and deliberate electronic abrasion.",
            "Low-register gravity, magnetic bass pull, and dark harmonic mass.",
            "Tiny hard pulses organized as an evolving machine ritual.",
            "Mellow current, airy leads, spacious harmony, and restrained impact.",
            "Large circular bass motion with weight, momentum, and widening force.",
            "Icy hooks, exact timing, and bright synthetic architecture."
    };

    private final Handler statusHandler = new Handler(Looper.getMainLooper());
    private final ArrayList<String> trackHistory = new ArrayList<>();

    private int historyCursor = -1;
    private int currentScreen = SCREEN_MAIN;
    private boolean historyDirty = true;

    private Button startStopButton;
    private Button channelButton;
    private TextView elapsedText;
    private Button previousButton;
    private Button nextButton;
    private LinearLayout historyListContainer;
    private Button clearHistoryButton;

    private volatile boolean exportRunning = false;
    private volatile boolean exportCancelRequested = false;
    private Thread exportThread;
    private String lastExportPath = "";
    private volatile String exportStatusText = "";
    private Button exportOggButton;
    private Button exportFlacButton;
    private Button cancelExportButton;
    private TextView exportStatusView;
    private volatile ExportFormat activeExportFormat = ExportFormat.OGG;

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

        NativeAudio.setGenreBlendMode(loadChannelBlendMode());
        NativeAudio.setGenreMask(loadChannelMask());
        NativeAudio.setGenrePrimary(loadChannelPrimary());
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
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        if (currentScreen == SCREEN_CHANNEL) showChannelSelectorScreen();
        else if (currentScreen == SCREEN_ADVANCED) showAdvancedScreen();
        else showMainScreen();
    }

    @Override
    public void onBackPressed() {
        if (currentScreen != SCREEN_MAIN) {
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
                    if (currentScreen != SCREEN_MAIN) showMainScreen();
                    else finish();
                });
    }

    private void showMainScreen() {
        currentScreen = SCREEN_MAIN;
        boolean landscape = isLandscape();
        int screenWidth = Math.max(320, getResources().getConfiguration().screenWidthDp);
        int available = Math.max(292, screenWidth - 24);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(landscape ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);
        root.setGravity(Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        root.setPadding(dp(10), dp(10), dp(10), dp(8));
        root.setBackgroundColor(Color.BLACK);

        if (landscape) {
            int controlsWidth = Math.max(300, Math.min(430, (available * 44) / 100));
            int historyWidth = Math.max(280, available - controlsWidth - 10);

            ScrollView controlsScroll = new ScrollView(this);
            controlsScroll.setFillViewport(false);
            controlsScroll.addView(buildMainControls(controlsWidth, true), new ScrollView.LayoutParams(
                    ScrollView.LayoutParams.MATCH_PARENT,
                    ScrollView.LayoutParams.WRAP_CONTENT));
            root.addView(controlsScroll, new LinearLayout.LayoutParams(dp(controlsWidth), LinearLayout.LayoutParams.MATCH_PARENT));

            LinearLayout.LayoutParams historyLp = new LinearLayout.LayoutParams(dp(historyWidth), LinearLayout.LayoutParams.MATCH_PARENT);
            historyLp.leftMargin = dp(10);
            root.addView(buildHistoryPanel(historyWidth), historyLp);
        } else {
            int width = Math.max(292, Math.min(560, available));
            root.addView(buildMainControls(width, false), new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT));
            LinearLayout.LayoutParams historyLp = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f);
            historyLp.topMargin = dp(8);
            root.addView(buildHistoryPanel(width), historyLp);
        }

        setContentView(root, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        historyDirty = true;
        updateMainStatus();
    }

    private LinearLayout buildMainControls(int widthDp, boolean compact) {
        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.VERTICAL);
        controls.setGravity(Gravity.TOP | Gravity.CENTER_HORIZONTAL);

        startStopButton = button(NativeAudio.isPlaying() ? "Stop" : "Start", compact ? 23.0f : 28.0f);
        startStopButton.setOnClickListener(view -> togglePlayback());
        controls.addView(startStopButton, params(widthDp, compact ? 54 : 70, 0));

        channelButton = navButton(currentChannelText(), compact ? 13.5f : 14.5f);
        channelButton.setOnClickListener(view -> showChannelSelectorScreen());
        controls.addView(channelButton, params(widthDp, compact ? 45 : 48, 7));

        elapsedText = label(currentElapsedText());
        elapsedText.setTextSize(compact ? 13.5f : 14.5f);
        elapsedText.setGravity(Gravity.CENTER);
        controls.addView(elapsedText, params(widthDp, compact ? 34 : 38, 4));

        LinearLayout transport = new LinearLayout(this);
        transport.setOrientation(LinearLayout.HORIZONTAL);
        transport.setGravity(Gravity.CENTER);
        int cell = (widthDp - 16) / 3;

        previousButton = button("Previous", 13.0f);
        previousButton.setOnClickListener(view -> previousSound());
        Button restartButton = button("Restart", 13.0f);
        restartButton.setOnClickListener(view -> restartCurrentSound());
        nextButton = button("Next", 13.0f);
        nextButton.setOnClickListener(view -> nextSound());

        transport.addView(previousButton, new LinearLayout.LayoutParams(dp(cell), dp(compact ? 40 : 44)));
        LinearLayout.LayoutParams middle = new LinearLayout.LayoutParams(dp(cell), dp(compact ? 40 : 44));
        middle.leftMargin = dp(8);
        transport.addView(restartButton, middle);
        LinearLayout.LayoutParams right = new LinearLayout.LayoutParams(dp(cell), dp(compact ? 40 : 44));
        right.leftMargin = dp(8);
        transport.addView(nextButton, right);
        controls.addView(transport, params(widthDp, compact ? 40 : 44, 7));

        TextView notice = status("If you're not already vegetarian, you need to see Bloodguiltcurse.net");
        notice.setTextSize(compact ? 10.5f : 12.0f);
        controls.addView(notice, params(widthDp, -2, 8));

        Button advanced = button("Advanced", 13.5f);
        advanced.setOnClickListener(view -> showAdvancedScreen());
        controls.addView(advanced, params(widthDp, compact ? 40 : 44, 8));
        return controls;
    }

    private LinearLayout buildHistoryPanel(int widthDp) {
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setGravity(Gravity.TOP | Gravity.CENTER_HORIZONTAL);

        TextView title = label("Track Listing (latest 20)");
        title.setTextSize(12.5f);
        panel.addView(title, params(widthDp, -2, 0));

        TextView hint = status("(tap to load; long press to copy seed number)");
        hint.setTextSize(10.8f);
        panel.addView(hint, params(widthDp, -2, 2));

        TextView heading = status("Channel");
        heading.setGravity(Gravity.LEFT);
        heading.setTextSize(11.5f);
        panel.addView(heading, params(widthDp, -2, 2));

        ScrollView historyScroll = new ScrollView(this);
        historyScroll.setFillViewport(false);
        historyListContainer = new LinearLayout(this);
        historyListContainer.setOrientation(LinearLayout.VERTICAL);
        historyListContainer.setGravity(Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        historyScroll.addView(historyListContainer, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));
        LinearLayout.LayoutParams scrollLp = new LinearLayout.LayoutParams(dp(widthDp), 0, 1.0f);
        scrollLp.topMargin = dp(4);
        panel.addView(historyScroll, scrollLp);

        clearHistoryButton = button("Clear History", 13.0f);
        clearHistoryButton.setOnClickListener(view -> clearTrackHistory());
        panel.addView(clearHistoryButton, params(widthDp, 40, 6));
        return panel;
    }

    private void showChannelSelectorScreen() {
        currentScreen = SCREEN_CHANNEL;
        int width = contentWidthDp();
        LinearLayout controls = baseColumn();

        Button back = button("Back", 15.0f);
        back.setOnClickListener(view -> showMainScreen());
        controls.addView(back, params(width, 46, 0));
        controls.addView(label("CHANNEL SELECTOR"), params(width, -2, 10));

        TextView explanation = status(
                "No Channel is unrestricted. Named channels add 50% channel character. " +
                "Hybrid Channels keeps the first selected channel dominant and uses later selections as secondary influences.");
        explanation.setTextSize(11.5f);
        controls.addView(explanation, params(width, -2, 7));

        final boolean[] updating = new boolean[] {false};
        final CheckBox noChannel = checkBox("No Channel - " + CHANNEL_DESCRIPTIONS[0]);
        final CheckBox hybrid = checkBox("Hybrid Channels");
        final ArrayList<CheckBox> channelBoxes = new ArrayList<>();

        controls.addView(noChannel, params(width, 54, 8));
        controls.addView(hybrid, params(width, 46, 4));

        int initialMask = loadChannelMask();
        int initialBlend = loadChannelBlendMode();
        int initialPrimary = normalizedPrimary(initialMask, loadChannelPrimary());
        noChannel.setChecked(initialMask == 0);
        hybrid.setChecked(initialBlend == 1 && initialMask != 0);

        for (int mode = 1; mode < CHANNEL_LABELS.length; ++mode) {
            CheckBox box = checkBox(channelLine(mode, mode == initialPrimary && initialBlend == 1));
            box.setTag(mode);
            box.setChecked((initialMask & channelMask(mode)) != 0);
            channelBoxes.add(box);
            controls.addView(box, params(width, 58, 3));
        }

        Runnable refreshLabels = () -> {
            int mask = currentMask(channelBoxes);
            int primary = normalizedPrimary(mask, loadChannelPrimary());
            boolean isHybrid = hybrid.isChecked() && mask != 0;
            for (CheckBox box : channelBoxes) {
                int mode = (Integer) box.getTag();
                box.setText(channelLine(mode, isHybrid && mode == primary));
            }
        };

        noChannel.setOnCheckedChangeListener((buttonView, checked) -> {
            if (updating[0]) return;
            if (!checked) {
                // No Channel cannot be left empty. Selecting a named channel clears it
                // through the guarded channel listener below.
                if (currentMask(channelBoxes) == 0) {
                    updating[0] = true;
                    noChannel.setChecked(true);
                    updating[0] = false;
                    Toast.makeText(this, "Select a channel before leaving No Channel.", Toast.LENGTH_SHORT).show();
                }
                return;
            }
            updating[0] = true;
            hybrid.setChecked(false);
            for (CheckBox box : channelBoxes) box.setChecked(false);
            updating[0] = false;
            applyChannelState(0, 0, 0);
            refreshLabels.run();
            Toast.makeText(this, "No Channel: unrestricted generation.", Toast.LENGTH_SHORT).show();
        });

        hybrid.setOnCheckedChangeListener((buttonView, checked) -> {
            if (updating[0]) return;
            int mask = currentMask(channelBoxes);
            if (checked && mask == 0) {
                updating[0] = true;
                hybrid.setChecked(false);
                noChannel.setChecked(true);
                updating[0] = false;
                Toast.makeText(this, "Select the dominant channel first, then enable Hybrid Channels.", Toast.LENGTH_LONG).show();
                refreshLabels.run();
                return;
            }
            int primary = normalizedPrimary(mask, loadChannelPrimary());
            if (!checked && Integer.bitCount(mask) > 1) {
                // Leaving Hybrid keeps the dominant channel and removes secondary influences.
                mask = primary > 0 ? channelMask(primary) : 0;
                updating[0] = true;
                for (CheckBox box : channelBoxes) {
                    int mode = (Integer) box.getTag();
                    box.setChecked(mode == primary);
                }
                updating[0] = false;
            }
            applyChannelState(mask, checked && mask != 0 ? 1 : 0, primary);
            refreshLabels.run();
        });

        for (CheckBox box : channelBoxes) {
            box.setOnCheckedChangeListener((buttonView, checked) -> {
                if (updating[0]) return;
                int mode = (Integer) buttonView.getTag();
                int mask = currentMask(channelBoxes);
                int primary = loadChannelPrimary();

                updating[0] = true;
                noChannel.setChecked(false);
                if (checked) {
                    if (!hybrid.isChecked()) {
                        for (CheckBox other : channelBoxes) {
                            if (other != buttonView) other.setChecked(false);
                        }
                        mask = channelMask(mode);
                        primary = mode;
                    } else {
                        mask |= channelMask(mode);
                        if (primary <= 0 || (mask & channelMask(primary)) == 0) primary = mode;
                    }
                } else {
                    mask &= ~channelMask(mode);
                    if (mask == 0) {
                        hybrid.setChecked(false);
                        noChannel.setChecked(true);
                        primary = 0;
                    } else if (mode == primary) {
                        primary = firstSelectedMode(mask);
                    }
                }
                updating[0] = false;

                int blend = hybrid.isChecked() && mask != 0 ? 1 : 0;
                applyChannelState(mask, blend, primary);
                refreshLabels.run();
            });
        }

        setScrollRoot(controls);
    }


    private void showAdvancedScreen() {
        currentScreen = SCREEN_ADVANCED;
        int width = contentWidthDp();
        LinearLayout controls = baseColumn();

        Button back = button("Back", 15.0f);
        back.setOnClickListener(view -> showMainScreen());
        controls.addView(back, params(width, 46, 0));
        controls.addView(label("ADVANCED"), params(width, -2, 10));

        controls.addView(label("Load seed"), params(width, -2, 12));
        TextView loadExplanation = status("Enter a seed to regenerate its sound under the selected channel.");
        loadExplanation.setTextSize(11.5f);
        controls.addView(loadExplanation, params(width, -2, 3));

        EditText seedInput = editField("", InputType.TYPE_CLASS_NUMBER);
        seedInput.setHint("0 to 4294967295");
        controls.addView(seedInput, params(width, 50, 3));

        Button loadButton = button("Load", 15.0f);
        loadButton.setOnClickListener(view -> loadSeed(textOf(seedInput)));
        controls.addView(loadButton, params(width, 46, 7));

        String seedText = currentSeedText();
        Button seedButton = navButton("Current seed: " + seedText + "\nTap to copy", 14.5f);
        seedButton.setOnClickListener(view -> copySeedToClipboard(seedText));
        controls.addView(seedButton, params(width, 62, 13));

        TextView seedExplanation = status("The seed is the number that regenerates the current sound identity.");
        seedExplanation.setTextSize(11.5f);
        controls.addView(seedExplanation, params(width, -2, 5));

        Button exportDuration = navButton(
                "Export duration: " + formatDuration(loadExportSeconds()) + "\nTap to change", 14.0f);
        exportDuration.setOnClickListener(view -> showExportDurationChooser());
        controls.addView(exportDuration, params(width, 60, 14));

        EditText exportName = editField("", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        exportName.setHint("filename, e.g. track1");
        controls.addView(label("Export filename"), params(width, -2, 12));
        controls.addView(exportName, params(width, 50, 2));

        LinearLayout exportRow = new LinearLayout(this);
        exportRow.setOrientation(LinearLayout.HORIZONTAL);
        exportOggButton = button("Export OGG", 14.5f);
        exportFlacButton = button("Export FLAC", 14.5f);
        LinearLayout.LayoutParams oggHalf = new LinearLayout.LayoutParams(0, dp(48), 1.0f);
        oggHalf.setMarginEnd(dp(4));
        exportRow.addView(exportOggButton, oggHalf);
        LinearLayout.LayoutParams flacHalf = new LinearLayout.LayoutParams(0, dp(48), 1.0f);
        flacHalf.setMarginStart(dp(4));
        exportRow.addView(exportFlacButton, flacHalf);
        exportOggButton.setOnClickListener(view -> startExport(textOf(exportName), ExportFormat.OGG));
        exportFlacButton.setOnClickListener(view -> startExport(textOf(exportName), ExportFormat.FLAC));
        controls.addView(exportRow, params(width, 48, 9));

        cancelExportButton = button("Cancel Export", 15.0f);
        cancelExportButton.setOnClickListener(view -> requestExportCancel());
        controls.addView(cancelExportButton, params(width, 48, 7));

        exportStatusView = status(exportRunning
                ? exportStatusForUi()
                : "OGG is compact and lossy. FLAC is lossless and larger. Both exports are offline snapshots and do not restart playback.");
        exportStatusView.setTextSize(11.5f);
        controls.addView(exportStatusView, params(width, -2, 7));

        if (!lastExportPath.isEmpty()) {
            TextView last = status("Last export: " + lastExportPath);
            last.setTextSize(11.5f);
            controls.addView(last, params(width, -2, 7));
        }

        updateExportUi();
        setScrollRoot(controls);
    }

    private void showExportDurationChooser() {
        String[] labels = new String[] {
                "30 sec", "1 min", "3 min", "5 min", "10 min", "20 min", "1 hour", "Custom"
        };
        new AlertDialog.Builder(this)
                .setTitle("Export duration")
                .setItems(labels, (dialog, which) -> {
                    switch (which) {
                        case 0: setExportDuration(30); break;
                        case 1: setExportDuration(60); break;
                        case 2: setExportDuration(180); break;
                        case 3: setExportDuration(300); break;
                        case 4: setExportDuration(600); break;
                        case 5: setExportDuration(1200); break;
                        case 6: setExportDuration(3600); break;
                        case 7: showCustomExportDurationDialog(); break;
                        default: break;
                    }
                })
                .show();
    }

    private void showCustomExportDurationDialog() {
        int current = loadExportSeconds();
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(dp(18), dp(6), dp(18), dp(2));

        EditText minutes = editField(String.valueOf(Math.min(60, current / 60)), InputType.TYPE_CLASS_NUMBER);
        EditText seconds = editField(String.valueOf(Math.min(60, current % 60)), InputType.TYPE_CLASS_NUMBER);
        form.addView(label("Minutes (0-60)"), params(240, -2, 5));
        form.addView(minutes, params(240, 50, 2));
        form.addView(label("Seconds (0-60)"), params(240, -2, 7));
        form.addView(seconds, params(240, 50, 2));

        new AlertDialog.Builder(this)
                .setTitle("Custom export duration")
                .setView(form)
                .setPositiveButton("Set", (dialog, which) -> {
                    int m = clamp(parseInteger(textOf(minutes), 0), 0, 60);
                    int s = clamp(parseInteger(textOf(seconds), 0), 0, 60);
                    int total = m * 60 + s;
                    if (total < MIN_EXPORT_SECONDS) {
                        total = MIN_EXPORT_SECONDS;
                        Toast.makeText(this, "Minimum export length is 8 seconds.", Toast.LENGTH_SHORT).show();
                    }
                    setExportDuration(total);
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

    private void previousSound() {
        syncTrackHistory();
        if (historyCursor <= 0 || trackHistory.isEmpty()) {
            Toast.makeText(this, "No previous sound yet.", Toast.LENGTH_SHORT).show();
            return;
        }
        loadHistoryAt(historyCursor - 1);
    }

    private void restartCurrentSound() {
        String data = NativeAudio.currentSongData();
        if (data == null || data.isEmpty()) return;
        playSongData(data, false);
    }

    private void nextSound() {
        syncTrackHistory();
        if (historyCursor >= 0 && historyCursor < trackHistory.size() - 1) {
            loadHistoryAt(historyCursor + 1);
            return;
        }
        startAudioService(serviceIntent(NativeAudio.isPlaying() ? AudioService.ACTION_NEXT : AudioService.ACTION_START));
        updateMainStatusDelayed();
    }

    private void loadHistoryAt(int index) {
        if (index < 0 || index >= trackHistory.size()) return;
        historyCursor = index;
        historyDirty = true;
        playSongData(trackHistory.get(index), false);
    }

    private void playSongData(String data, boolean addToHistory) {
        if (data == null || data.isEmpty()) return;
        if (addToHistory) appendHistory(data);
        Intent intent = serviceIntent(AudioService.ACTION_LOAD_SOUND);
        intent.putExtra(AudioService.EXTRA_SONG_DATA, data);
        startAudioService(intent);
        updateMainStatusDelayed();
    }

    private void loadSeed(String text) {
        String trimmed = text == null ? "" : text.trim();
        if (trimmed.isEmpty()) {
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

        int mask = loadChannelMask();
        int blend = loadChannelBlendMode();
        int primary = loadChannelPrimary();
        String data = "technomatic2105-v1;seed=" + value +
                ";seconds=" + DEFAULT_EXPORT_SECONDS +
                ";edited=0;gmask=" + mask +
                ";gblend=" + blend +
                ";gprimary=" + primary +
                ";gmode=" + primary;
        playSongData(data, true);
        Toast.makeText(this, "Seed loaded.", Toast.LENGTH_SHORT).show();
    }

    private void applyChannelState(int mask, int blend, int primary) {
        mask = clamp(mask, 0, 4095);
        blend = blend == 1 && mask != 0 ? 1 : 0;
        primary = normalizedPrimary(mask, primary);
        saveChannelState(mask, blend, primary);
        NativeAudio.setGenreBlendMode(blend);
        NativeAudio.setGenreMask(mask);
        NativeAudio.setGenrePrimary(primary);
        if (NativeAudio.isPlaying()) {
            Intent intent = serviceIntent(AudioService.ACTION_START);
            intent.putExtra(AudioService.EXTRA_FORCE_RESTART, true);
            startAudioService(intent);
        }
        updateMainStatusDelayed();
    }

    private void syncTrackHistory() {
        String snapshot = NativeAudio.historyData();
        if (snapshot != null && !snapshot.isEmpty()) {
            String[] lines = snapshot.split("\n");
            for (String raw : lines) {
                String data = raw == null ? "" : raw.trim();
                if (!data.isEmpty()) appendHistory(data);
            }
        }

        String current = NativeAudio.currentSongData();
        if (NativeAudio.isPlaying() && current != null && !current.isEmpty()) {
            appendHistory(current);
            int index = findHistoryIdentity(current);
            if (index >= 0 && historyCursor != index) {
                historyCursor = index;
                historyDirty = true;
            }
        }
    }

    private void appendHistory(String data) {
        if (data == null || data.isEmpty()) return;
        int existing = findHistoryIdentity(data);
        if (existing >= 0) {
            // Refresh the existing snapshot without duplicating the same sound.
            trackHistory.set(existing, data);
            historyCursor = existing;
            historyDirty = true;
            return;
        }
        trackHistory.add(data);
        while (trackHistory.size() > HISTORY_LIMIT) {
            trackHistory.remove(0);
            if (historyCursor > 0) --historyCursor;
        }
        historyCursor = trackHistory.size() - 1;
        historyDirty = true;
    }

    private int findHistoryIdentity(String data) {
        String key = soundIdentityKey(data);
        for (int i = 0; i < trackHistory.size(); ++i) {
            if (key.equals(soundIdentityKey(trackHistory.get(i)))) return i;
        }
        return -1;
    }

    private String soundIdentityKey(String data) {
        return String.valueOf(seedFromSongData(data) & 0xffffffffL) + ":" +
                signedFieldFromSongData(data, "cand", 0) + ":" +
                signedFieldFromSongData(data, "gmask", 0) + ":" +
                signedFieldFromSongData(data, "gblend", 0) + ":" +
                signedFieldFromSongData(data, "gprimary", signedFieldFromSongData(data, "gmode", 0));
    }

    private void clearTrackHistory() {
        NativeAudio.clearHistory();
        trackHistory.clear();
        historyCursor = -1;
        String current = NativeAudio.currentSongData();
        if (NativeAudio.isPlaying() && current != null && !current.isEmpty()) appendHistory(current);
        historyDirty = true;
        rebuildHistoryList();
        Toast.makeText(this, "History cleared.", Toast.LENGTH_SHORT).show();
    }

    private void rebuildHistoryList() {
        if (historyListContainer == null || !historyDirty) return;
        historyListContainer.removeAllViews();
        int width = historyPanelWidthDp();

        if (trackHistory.isEmpty()) {
            TextView empty = status("No tracks yet.");
            historyListContainer.addView(empty, params(width, -2, 8));
        } else {
            for (int i = trackHistory.size() - 1; i >= 0; --i) {
                final int index = i;
                final String data = trackHistory.get(i);
                Button row = button(historyRowText(data), 12.5f);
                row.setGravity(Gravity.CENTER_VERTICAL | Gravity.LEFT);
                row.setPadding(dp(10), 0, dp(8), 0);
                row.setSingleLine(true);
                row.setMaxLines(1);
                row.setTextColor(index == historyCursor ? Color.WHITE : 0xffbbbbbb);
                row.setOnClickListener(view -> loadHistoryAt(index));
                row.setOnLongClickListener(view -> {
                    copySeedToClipboard(String.valueOf(seedFromSongData(data) & 0xffffffffL));
                    return true;
                });
                historyListContainer.addView(row, params(width, 38, 2));
            }
        }
        if (clearHistoryButton != null) clearHistoryButton.setEnabled(!trackHistory.isEmpty());
        historyDirty = false;
    }

    private String historyRowText(String data) {
        return channelNameFromSongData(data);
    }

    private String channelNameFromSongData(String data) {
        int mask = signedFieldFromSongData(data, "gmask", 0);
        int blend = signedFieldFromSongData(data, "gblend", 0);
        int primary = signedFieldFromSongData(data, "gprimary",
                signedFieldFromSongData(data, "gmode", 0));
        if (blend == 1 && Integer.bitCount(mask) > 1) {
            primary = normalizedPrimary(mask, primary);
            int secondaryCount = Math.max(1, Integer.bitCount(mask) - 1);
            return "Hybrid: " + CHANNEL_LABELS[primary] + " + " + secondaryCount;
        }
        int mode = signedFieldFromSongData(data, "gmode", -1);
        if (mode >= 0 && mode < CHANNEL_LABELS.length) return CHANNEL_LABELS[mode];
        if (Integer.bitCount(mask) == 1) {
            for (int i = 0; i < 12; ++i) if ((mask & (1 << i)) != 0) return CHANNEL_LABELS[i + 1];
        }
        return "No Channel";
    }

    private Intent serviceIntent(String action) {
        Intent intent = new Intent(this, AudioService.class);
        intent.setAction(action);
        int mask = loadChannelMask();
        int blend = loadChannelBlendMode();
        int primary = loadChannelPrimary();
        NativeAudio.setGenreBlendMode(blend);
        NativeAudio.setGenreMask(mask);
        NativeAudio.setGenrePrimary(primary);
        intent.putExtra(AudioService.EXTRA_GENRE_MASK, mask);
        intent.putExtra(AudioService.EXTRA_GENRE_BLEND_MODE, blend);
        intent.putExtra(AudioService.EXTRA_GENRE_PRIMARY, primary);
        return intent;
    }

    private void startAudioService(Intent intent) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(intent);
        else startService(intent);
    }

    private void updateMainStatusDelayed() {
        statusHandler.postDelayed(this::updateMainStatus, 260L);
    }

    private void updateMainStatus() {
        syncTrackHistory();
        if (startStopButton != null) startStopButton.setText(NativeAudio.isPlaying() ? "Stop" : "Start");
        if (channelButton != null) channelButton.setText(currentChannelText());
        if (elapsedText != null) elapsedText.setText(currentElapsedText());
        if (previousButton != null) previousButton.setEnabled(historyCursor > 0);
        if (nextButton != null) nextButton.setEnabled(true);
        rebuildHistoryList();
    }

    private String currentChannelText() {
        return "Channel: " + currentChannelValue() + "  >";
    }

    private String currentChannelValue() {
        String data = NativeAudio.currentSongData();
        if (NativeAudio.isPlaying() && data != null && !data.isEmpty()) return channelNameFromSongData(data);
        int mask = loadChannelMask();
        int blend = loadChannelBlendMode();
        int primary = normalizedPrimary(mask, loadChannelPrimary());
        if (mask == 0) return CHANNEL_LABELS[0];
        if (blend == 1 && Integer.bitCount(mask) > 1) {
            return "Hybrid: " + CHANNEL_LABELS[primary] + " + " + (Integer.bitCount(mask) - 1);
        }
        return CHANNEL_LABELS[primary];
    }

    private String currentElapsedText() {
        int elapsed = (int) Math.max(0.0, NativeAudio.currentElapsedSeconds());
        return "Elapsed: " + formatDuration(elapsed);
    }


    private void startExport(String requestedName, ExportFormat format) {
        if (exportRunning) return;
        String source = NativeAudio.currentSongData();
        if (source == null || source.isEmpty()) {
            Toast.makeText(this, "No sound is available to export.", Toast.LENGTH_SHORT).show();
            return;
        }

        int seconds = loadExportSeconds();
        String data = songDataWithDuration(source, seconds);
        activeExportFormat = format;
        exportRunning = true;
        exportCancelRequested = false;
        exportStatusText = "Rendering captured sound offline...";
        updateExportUi();
        Toast.makeText(this, format.label + " export started in the background.", Toast.LENGTH_SHORT).show();

        exportThread = new Thread(() -> {
            File raw = null;
            File encoded = null;
            boolean ok = false;
            String message;
            String publicPath = "";
            ExportCancellationToken token =
                    () -> exportCancelRequested || Thread.currentThread().isInterrupted();
            try {
                String displayName = exportDisplayName(requestedName, format);
                raw = File.createTempFile("technomatic_2105_export_", ".pcm", getCacheDir());
                encoded = File.createTempFile("technomatic_2105_export_", format.extension, getCacheDir());

                updateExportStatus("Rendering captured sound offline...");
                checkExportCancelled(token);
                if (!NativeAudio.exportPcm16ToFile(data, seconds, raw.getAbsolutePath())) {
                    checkExportCancelled(token);
                    throw new java.io.IOException("Native render failed.");
                }
                long expected = (long) seconds * 48000L * 2L * 2L;
                if (raw.length() != expected) {
                    throw new java.io.IOException("Native render length mismatch: expected " +
                            expected + " bytes, got " + raw.length() + ".");
                }

                updateExportStatus("Encoding " + format.label + "...");
                if (format == ExportFormat.OGG) {
                    OggExporter.encodeRawPcm16ToOgg(raw, encoded, token);
                } else {
                    FlacExporter.encodeRawPcm16ToFlac(
                            raw, encoded, flacMetadata(displayName, data), token);
                }
                checkExportCancelled(token);
                if (!encoded.exists() || encoded.length() <= 0L) {
                    throw new java.io.IOException(
                            "Encoder produced an empty " + format.label + " file.");
                }

                updateExportStatus("Publishing to Music...");
                ExportResult result = publishAudioToMusic(
                        encoded, displayName, data, format, token);
                publicPath = result.displayPath;
                ok = true;
                message = "Exported to " + publicPath;
            } catch (Exception ex) {
                message = safeMessage(ex);
                if (!message.toLowerCase(Locale.US).contains("cancel")) {
                    message = "Export failed: " + message;
                }
            } finally {
                if (raw != null && raw.exists()) raw.delete();
                if (encoded != null && encoded.exists()) encoded.delete();
            }

            boolean finalOk = ok;
            String finalMessage = message;
            String finalPublicPath = publicPath;
            runOnUiThread(() -> {
                exportRunning = false;
                exportCancelRequested = false;
                exportThread = null;
                exportStatusText = "";
                if (finalOk) lastExportPath = finalPublicPath;
                Toast.makeText(this, finalMessage, Toast.LENGTH_LONG).show();
                showExportResultDialog(finalOk, finalMessage);
                updateExportUi();
            });
        }, "Technomatic" + format.label + "Export");
        exportThread.start();
    }

    private FlacExporter.Metadata flacMetadata(String displayName, String songData) {
        String baseName = removeKnownAudioExtension(displayName);
        long seed = seedFromSongData(songData) & 0xffffffffL;
        return new FlacExporter.Metadata(
                baseName + " [" + seed + "]",
                "Technomatic 2105",
                "Technomatic 2105",
                exportAlbumName(),
                channelNameFromSongData(songData),
                "Generated locally by Technomatic 2105; Seed: " + seed);
    }

    private ExportResult publishAudioToMusic(
            File encodedAudio,
            String displayName,
            String songData,
            ExportFormat format,
            ExportCancellationToken token) throws java.io.IOException {
        checkExportCancelled(token);
        ContentResolver resolver = getContentResolver();
        ContentValues values = new ContentValues();
        String baseName = removeKnownAudioExtension(displayName);
        String title = baseName + " [" +
                (seedFromSongData(songData) & 0xffffffffL) + "]";

        values.put(MediaStore.MediaColumns.DISPLAY_NAME, displayName);
        values.put(MediaStore.MediaColumns.MIME_TYPE, format.mimeType);
        values.put(MediaStore.Audio.Media.TITLE, title);
        values.put("artist", "Technomatic 2105");
        values.put("album", exportAlbumName());
        values.put("genre", channelNameFromSongData(songData));
        values.put(MediaStore.Audio.Media.IS_MUSIC, 1);
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_MUSIC);
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);

        Uri uri = resolver.insert(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, values);
        if (uri == null) throw new java.io.IOException("Could not create MediaStore record.");

        try {
            try (InputStream input = new FileInputStream(encodedAudio);
                 OutputStream output = resolver.openOutputStream(uri, "w")) {
                if (output == null) {
                    throw new java.io.IOException("Could not open public Music output stream.");
                }
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
            return new ExportResult(uri, Environment.DIRECTORY_MUSIC + "/" + displayName);
        } catch (Exception ex) {
            try { resolver.delete(uri, null, null); } catch (Exception ignored) {}
            if (ex instanceof java.io.IOException) throw (java.io.IOException) ex;
            throw new java.io.IOException(ex);
        }
    }

    private String exportAlbumName() {
        return new SimpleDateFormat("MMMM dd yyyy", Locale.US)
                .format(new Date()).toUpperCase(Locale.US);
    }

    private void requestExportCancel() {
        if (!exportRunning) return;
        exportCancelRequested = true;
        exportStatusText = "Cancelling export...";
        NativeAudio.cancelExportRender();
        Thread thread = exportThread;
        if (thread != null) thread.interrupt();
        updateExportUi();
    }

    private void updateExportStatus(String text) {
        exportStatusText = text == null ? "" : text;
        runOnUiThread(this::updateExportUi);
    }

    private void updateExportUi() {
        if (exportOggButton != null) exportOggButton.setEnabled(!exportRunning);
        if (exportFlacButton != null) exportFlacButton.setEnabled(!exportRunning);
        if (cancelExportButton != null) {
            cancelExportButton.setEnabled(exportRunning);
            cancelExportButton.setVisibility(exportRunning ? View.VISIBLE : View.GONE);
        }
        if (exportStatusView != null) {
            exportStatusView.setText(exportRunning
                    ? exportStatusForUi()
                    : "OGG is compact and lossy. FLAC is lossless and larger. Both exports are offline snapshots and do not restart playback.");
        }
    }

    private String exportStatusForUi() {
        String phase = exportStatusText == null || exportStatusText.isEmpty()
                ? "Exporting " + activeExportFormat.label + "..."
                : exportStatusText;
        return phase + " Live playback is independent. Tap Cancel Export to stop this job.";
    }

    private String exportDisplayName(String requestedName, ExportFormat format) {
        String base = requestedName == null ? "" : requestedName.trim();
        base = removeKnownAudioExtension(base);
        StringBuilder cleaned = new StringBuilder();
        for (int i = 0; i < base.length(); ++i) {
            char c = base.charAt(i);
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ') {
                cleaned.append(c == ' ' ? '_' : c);
            } else {
                cleaned.append('_');
            }
        }
        while (cleaned.indexOf("__") >= 0) {
            int p = cleaned.indexOf("__");
            cleaned.deleteCharAt(p);
        }
        String result = cleaned.toString();
        if (result.isEmpty()) {
            result = "technomatic_2105_" + new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(new Date());
        }
        if (result.length() > 96) result = result.substring(0, 96);
        return result + format.extension;
    }

    private String removeKnownAudioExtension(String name) {
        String value = name == null ? "" : name.trim();
        String lower = value.toLowerCase(Locale.US);
        if (lower.endsWith(".flac")) return value.substring(0, value.length() - 5).trim();
        if (lower.endsWith(".ogg")) return value.substring(0, value.length() - 4).trim();
        return value;
    }

    private void showExportResultDialog(boolean ok, String message) {
        boolean cancelled = message != null && message.toLowerCase(Locale.US).contains("cancel");
        new AlertDialog.Builder(this)
                .setTitle(ok ? "Export complete" : (cancelled ? "Export cancelled" : "Export failed"))
                .setMessage(message)
                .setPositiveButton("OK", null)
                .show();
    }

    private static void checkExportCancelled(ExportCancellationToken token) throws java.io.IOException {
        if (token != null && token.isCancellationRequested()) throw new java.io.IOException("Export cancelled.");
    }

    private String safeMessage(Exception ex) {
        String message = ex == null ? "unknown error" : ex.getMessage();
        if (message == null || message.trim().isEmpty()) return ex == null ? "unknown error" : ex.getClass().getSimpleName();
        return message;
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

    private String currentSeedText() {
        return String.valueOf(seedFromSongData(NativeAudio.currentSongData()) & 0xffffffffL);
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
            value = value * 10L + (c - '0');
            if (value > 0xffffffffL) return 0L;
            ++i;
        }
        return any ? value : 0L;
    }


    private int signedFieldFromSongData(String data, String key, int fallback) {
        if (data == null || key == null || key.isEmpty()) return fallback;
        String needle = key + "=";
        int pos = data.indexOf(needle);
        if (pos < 0) return fallback;
        int i = pos + needle.length();
        int sign = 1;
        if (i < data.length() && data.charAt(i) == '-') {
            sign = -1;
            ++i;
        }
        int value = 0;
        boolean any = false;
        while (i < data.length()) {
            char c = data.charAt(i);
            if (c < '0' || c > '9') break;
            any = true;
            value = value * 10 + (c - '0');
            if (value > 100000000) return fallback;
            ++i;
        }
        return any ? value * sign : fallback;
    }

    private String songDataWithDuration(String data, int seconds) {
        if (data == null || data.isEmpty()) return data;
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

    private int loadChannelMask() {
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        if (prefs.contains(KEY_V24_CHANNEL_MASK)) {
            return clamp(prefs.getInt(KEY_V24_CHANNEL_MASK, 0), 0, 4095);
        }
        int legacyMode = clamp(prefs.getInt(KEY_V23_CHANNEL_MODE, 0), 0, CHANNEL_LABELS.length - 1);
        return channelMask(legacyMode);
    }

    private int loadChannelBlendMode() {
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        int mask = loadChannelMask();
        int blend = prefs.contains(KEY_V24_CHANNEL_BLEND)
                ? prefs.getInt(KEY_V24_CHANNEL_BLEND, 0) : 0;
        return blend == 1 && mask != 0 ? 1 : 0;
    }

    private int loadChannelPrimary() {
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        int mask = loadChannelMask();
        int legacyMode = clamp(prefs.getInt(KEY_V23_CHANNEL_MODE, 0), 0, CHANNEL_LABELS.length - 1);
        int primary = prefs.contains(KEY_V24_CHANNEL_PRIMARY)
                ? prefs.getInt(KEY_V24_CHANNEL_PRIMARY, legacyMode) : legacyMode;
        return normalizedPrimary(mask, primary);
    }

    private void saveChannelState(int mask, int blend, int primary) {
        mask = clamp(mask, 0, 4095);
        primary = normalizedPrimary(mask, primary);
        blend = blend == 1 && mask != 0 ? 1 : 0;
        getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                .putInt(KEY_V24_CHANNEL_MASK, mask)
                .putInt(KEY_V24_CHANNEL_BLEND, blend)
                .putInt(KEY_V24_CHANNEL_PRIMARY, primary)
                .apply();
    }

    private int channelMask(int mode) {
        return mode <= 0 ? 0 : 1 << (clamp(mode, 1, 12) - 1);
    }

    private int firstSelectedMode(int mask) {
        for (int mode = 1; mode < CHANNEL_LABELS.length; ++mode) {
            if ((mask & channelMask(mode)) != 0) return mode;
        }
        return 0;
    }

    private int normalizedPrimary(int mask, int primary) {
        mask = clamp(mask, 0, 4095);
        if (mask == 0) return 0;
        if (primary > 0 && primary < CHANNEL_LABELS.length &&
                (mask & channelMask(primary)) != 0) return primary;
        return firstSelectedMode(mask);
    }

    private int currentMask(ArrayList<CheckBox> boxes) {
        int mask = 0;
        for (CheckBox box : boxes) {
            if (!box.isChecked() || !(box.getTag() instanceof Integer)) continue;
            mask |= channelMask((Integer) box.getTag());
        }
        return mask;
    }

    private String channelLine(int mode, boolean primary) {
        String marker = primary ? " (dominant)" : "";
        return CHANNEL_LABELS[mode] + marker + " - " + CHANNEL_DESCRIPTIONS[mode];
    }


    private int loadExportSeconds() {
        return clamp(getSharedPreferences(PREFS, MODE_PRIVATE)
                .getInt(KEY_V23_EXPORT_SECONDS, DEFAULT_EXPORT_SECONDS), MIN_EXPORT_SECONDS, MAX_EXPORT_SECONDS);
    }

    private void setExportDuration(int seconds) {
        getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                .putInt(KEY_V23_EXPORT_SECONDS, clamp(seconds, MIN_EXPORT_SECONDS, MAX_EXPORT_SECONDS))
                .apply();
        if (currentScreen == SCREEN_ADVANCED) showAdvancedScreen();
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
        int safe = Math.max(0, totalSeconds);
        int hours = safe / 3600;
        int minutes = (safe / 60) % 60;
        int seconds = safe % 60;
        if (hours > 0) return String.format(Locale.US, "%d:%02d:%02d", hours, minutes, seconds);
        return String.format(Locale.US, "%d:%02d", minutes, seconds);
    }

    private LinearLayout baseColumn() {
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        column.setPadding(dp(12), dp(12), dp(12), dp(16));
        column.setBackgroundColor(Color.BLACK);
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

    private CheckBox checkBox(String text) {
        CheckBox box = new CheckBox(this);
        box.setText(text);
        box.setTextColor(Color.WHITE);
        box.setButtonTintList(android.content.res.ColorStateList.valueOf(Color.WHITE));
        box.setTextSize(14.0f);
        box.setGravity(Gravity.CENTER_VERTICAL | Gravity.LEFT);
        box.setPadding(dp(4), dp(3), dp(4), dp(3));
        box.setMaxLines(2);
        return box;
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
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                dp(widthDp),
                heightDp < 0 ? LinearLayout.LayoutParams.WRAP_CONTENT : dp(heightDp));
        params.topMargin = dp(topDp);
        params.gravity = Gravity.CENTER_HORIZONTAL;
        return params;
    }

    private int contentWidthDp() {
        int screen = getResources().getConfiguration().screenWidthDp;
        if (screen <= 0) screen = 360;
        int max = isLandscape() ? 760 : 560;
        return Math.max(292, Math.min(max, screen - 28));
    }

    private int historyPanelWidthDp() {
        if (!isLandscape()) return contentWidthDp();
        int screen = Math.max(320, getResources().getConfiguration().screenWidthDp);
        int available = Math.max(292, screen - 24);
        int controlsWidth = Math.max(300, Math.min(430, (available * 44) / 100));
        return Math.max(280, available - controlsWidth - 10);
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
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.WRAP_CONTENT);
        params.gravity = Gravity.TOP | Gravity.CENTER_HORIZONTAL;
        root.addView(view, params);
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
