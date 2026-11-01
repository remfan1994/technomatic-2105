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
    private static final int DEFAULT_EXPORT_START_SECONDS = 0;
    private static final int DEFAULT_EXPORT_END_SECONDS = 180;
    private static final int MIN_EXPORT_SPAN_SECONDS = 8;
    private static final int MAX_EXPORT_SPAN_SECONDS = 3600;
    private static final int MAX_EXPORT_POSITION_SECONDS = 24 * 60 * 60;

    private static final String PREFS = "technomatic_2105";
    private static final String KEY_V23_EXPORT_SECONDS = "v23_export_seconds";
    private static final String KEY_V28_EXPORT_START_SECONDS = "v28_export_start_seconds";
    private static final String KEY_V28_EXPORT_END_SECONDS = "v28_export_end_seconds";
    private static final String KEY_V28_META_ARTIST = "v28_meta_artist";
    private static final String KEY_V28_META_ALBUM_ARTIST = "v28_meta_album_artist";
    private static final String KEY_V28_META_ALBUM = "v28_meta_album";
    private static final String KEY_V28_META_GENRE = "v28_meta_genre";
    private static final String KEY_V28_META_COMMENT = "v28_meta_comment";

    private static final int SCREEN_MAIN = 0;
    private static final int SCREEN_ADVANCED = 2;
    private static final int SCREEN_METADATA = 3;

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

    private final Handler statusHandler = new Handler(Looper.getMainLooper());
    private final ArrayList<String> trackHistory = new ArrayList<>();

    private int historyCursor = -1;
    private int currentScreen = SCREEN_MAIN;
    private boolean historyDirty = true;

    private Button startStopButton;
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
    private TextView metadataIdentityView;
    private final ArrayList<EditText> metadataInputFields = new ArrayList<>();
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

        // These preferences belonged to the retired channel selector. Preserve
        // all metadata and export settings while removing their old influence.
        getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                .remove("v23_channel_mode")
                .remove("v24_channel_mask")
                .remove("v24_channel_blend")
                .remove("v24_channel_primary")
                .apply();
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
        if (currentScreen == SCREEN_ADVANCED) showAdvancedScreen();
        else if (currentScreen == SCREEN_METADATA) showMetadataScreen();
        else showMainScreen();
    }

    @Override
    public void onBackPressed() {
        if (currentScreen == SCREEN_METADATA) {
            showAdvancedScreen();
            return;
        }
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
                    if (currentScreen == SCREEN_METADATA) showAdvancedScreen();
                    else if (currentScreen != SCREEN_MAIN) showMainScreen();
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

        TextView heading = status("Seed / listened");
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

    private void showAdvancedScreen() {
        if (currentScreen == SCREEN_METADATA) {
            metadataInputFields.clear();
            metadataIdentityView = null;
        }
        currentScreen = SCREEN_ADVANCED;
        int width = contentWidthDp();
        LinearLayout controls = baseColumn();

        Button back = button("Back", 15.0f);
        back.setOnClickListener(view -> showMainScreen());
        controls.addView(back, params(width, 46, 0));
        controls.addView(label("ADVANCED"), params(width, -2, 10));

        controls.addView(label("Load seed"), params(width, -2, 12));
        TextView loadExplanation = status("Enter a seed to regenerate its sound with the full music engine.");
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

        int exportStart = loadExportStartSeconds();
        int exportEnd = loadExportEndSeconds();
        Button exportStartButton = navButton(
                "Export start: " + formatDuration(exportStart) + "\nTap to change", 14.0f);
        exportStartButton.setOnClickListener(view -> showExportTimeDialog(true));
        controls.addView(exportStartButton, params(width, 60, 14));

        Button exportEndButton = navButton(
                "Export end: " + formatDuration(exportEnd) + "\nTap to change", 14.0f);
        exportEndButton.setOnClickListener(view -> showExportTimeDialog(false));
        controls.addView(exportEndButton, params(width, 60, 7));

        TextView rangeExplanation = status(
                "The live sound is endless. Export renders only the selected interval; maximum interval length is 1 hour.");
        rangeExplanation.setTextSize(11.5f);
        controls.addView(rangeExplanation, params(width, -2, 5));

        Button metadataButton = button("Metadata Editor", 14.0f);
        metadataButton.setOnClickListener(view -> showMetadataScreen());
        controls.addView(metadataButton, params(width, 44, 9));

        EditText exportName = editField("", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        exportName.setHint("filename, e.g. track1");
        controls.addView(label("Export filename / song title"), params(width, -2, 12));
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
                : idleExportStatus());
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

    private void showExportTimeDialog(boolean editingStart) {
        int current = editingStart ? loadExportStartSeconds() : loadExportEndSeconds();
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(dp(18), dp(6), dp(18), dp(2));

        EditText hours = editField(String.valueOf(current / 3600), InputType.TYPE_CLASS_NUMBER);
        EditText minutes = editField(String.valueOf((current / 60) % 60), InputType.TYPE_CLASS_NUMBER);
        EditText seconds = editField(String.valueOf(current % 60), InputType.TYPE_CLASS_NUMBER);
        form.addView(label("Hours (0-24)"), params(240, -2, 5));
        form.addView(hours, params(240, 50, 2));
        form.addView(label("Minutes (0-59)"), params(240, -2, 7));
        form.addView(minutes, params(240, 50, 2));
        form.addView(label("Seconds (0-59)"), params(240, -2, 7));
        form.addView(seconds, params(240, 50, 2));

        new AlertDialog.Builder(this)
                .setTitle(editingStart ? "Export start" : "Export end")
                .setView(form)
                .setPositiveButton("Set", (dialog, which) -> {
                    int h = clamp(parseInteger(textOf(hours), 0), 0, 24);
                    int m = clamp(parseInteger(textOf(minutes), 0), 0, 59);
                    int sec = clamp(parseInteger(textOf(seconds), 0), 0, 59);
                    int value = clamp(h * 3600 + m * 60 + sec, 0, MAX_EXPORT_POSITION_SECONDS);
                    if (editingStart) setExportStartTime(value);
                    else setExportEndTime(value);
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void showMetadataScreen() {
        // Configuration changes rebuild this screen in place. Preserve typing
        // during that redraw; leaving with Back still discards unsaved edits.
        ArrayList<String> draft = new ArrayList<>();
        int focusedField = -1;
        int selectionStart = 0;
        int selectionEnd = 0;
        if (currentScreen == SCREEN_METADATA) {
            for (int i = 0; i < metadataInputFields.size(); i++) {
                EditText field = metadataInputFields.get(i);
                draft.add(textOf(field));
                if (field.hasFocus()) {
                    focusedField = i;
                    selectionStart = Math.max(0, field.getSelectionStart());
                    selectionEnd = Math.max(0, field.getSelectionEnd());
                }
            }
        }
        metadataInputFields.clear();
        currentScreen = SCREEN_METADATA;
        int width = contentWidthDp();
        LinearLayout controls = baseColumn();

        Button back = button("Back", 15.0f);
        back.setOnClickListener(view -> showAdvancedScreen());
        controls.addView(back, params(width, 46, 0));
        controls.addView(label("EXPORT METADATA"), params(width, -2, 10));

        TextView note = status(
                "Title comes from the export filename. Blank Album uses the export date; blank Genre uses Generative music. Notes include the actual seed automatically in both OGG and FLAC.");
        note.setTextSize(11.5f);
        controls.addView(note, params(width, -2, 4));

        EditText artist = metadataField(controls, width, "Artist", KEY_V28_META_ARTIST, "Technomatic 2105");
        EditText albumArtist = metadataField(controls, width, "Album Artist", KEY_V28_META_ALBUM_ARTIST, "Technomatic 2105");
        EditText album = metadataField(controls, width, "Album", KEY_V28_META_ALBUM, "");
        EditText genre = metadataField(controls, width, "Genre", KEY_V28_META_GENRE, "");
        controls.addView(label("Notes"), params(width, -2, 9));
        metadataIdentityView = status(ExportMetadata.identityNote(NativeAudio.currentSongData()));
        metadataIdentityView.setTextColor(Color.WHITE);
        metadataIdentityView.setTextSize(13.0f);
        metadataIdentityView.setTextIsSelectable(true);
        controls.addView(metadataIdentityView, params(width, -2, 3));
        TextView automatic = status("The seed details above and the export range are added automatically. Add your own notes below.");
        automatic.setTextSize(11.5f);
        controls.addView(automatic, params(width, -2, 4));
        EditText comment = editField(metadataValue(KEY_V28_META_COMMENT,
                "Generated locally by Technomatic 2105"), InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_FLAG_MULTI_LINE | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        comment.setSingleLine(false);
        comment.setMinLines(3);
        comment.setGravity(Gravity.TOP | Gravity.START);
        comment.setSelectAllOnFocus(false);
        comment.setTextSize(14.0f);
        comment.setHint("Optional notes");
        controls.addView(comment, params(width, -2, 3));
        metadataInputFields.add(comment);
        if (draft.size() == metadataInputFields.size()) {
            for (int i = 0; i < draft.size(); i++) metadataInputFields.get(i).setText(draft.get(i));
        }

        Button save = button("Save Metadata", 15.0f);
        save.setOnClickListener(view -> {
            getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                    .putString(KEY_V28_META_ARTIST, textOf(artist).trim())
                    .putString(KEY_V28_META_ALBUM_ARTIST, textOf(albumArtist).trim())
                    .putString(KEY_V28_META_ALBUM, textOf(album).trim())
                    .putString(KEY_V28_META_GENRE, textOf(genre).trim())
                    .putString(KEY_V28_META_COMMENT, textOf(comment).trim())
                    .apply();
            Toast.makeText(this, "Metadata saved.", Toast.LENGTH_SHORT).show();
            showAdvancedScreen();
        });
        controls.addView(save, params(width, 48, 12));
        setScrollRoot(controls);
        if (focusedField >= 0 && focusedField < metadataInputFields.size()) {
            EditText field = metadataInputFields.get(focusedField);
            field.requestFocus();
            int length = field.length();
            field.setSelection(Math.min(selectionStart, length), Math.min(selectionEnd, length));
        }
    }

    private EditText metadataField(LinearLayout controls, int width, String labelText,
                                   String key, String fallback) {
        controls.addView(label(labelText), params(width, -2, 9));
        EditText field = editField(metadataValue(key, fallback),
                InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        controls.addView(field, params(width, 50, 2));
        metadataInputFields.add(field);
        return field;
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
        // Next is an explicit new-seed operation even while playback is stopped.
        // AudioService handles the stopped case by creating the new sound before
        // opening the stream.
        startAudioService(serviceIntent(AudioService.ACTION_NEXT));
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

        String data = "technomatic2105-v1;seed=" + value +
                ";seconds=" + DEFAULT_EXPORT_END_SECONDS + ";edited=0";
        playSongData(data, true);
        Toast.makeText(this, "Seed loaded.", Toast.LENGTH_SHORT).show();
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
            // Refresh the existing snapshot without duplicating the same rendition.
            // Native and foreground snapshots can arrive in either order, so keep
            // the greatest listened duration rather than allowing an older poll to
            // make history run backward.
            trackHistory.set(existing, mergeHistoryData(trackHistory.get(existing), data));
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
        // Retired channel values do not create a distinct v30 composition.
        return String.valueOf(seedFromSongData(data) & 0xffffffffL) + ":" +
                signedFieldFromSongData(data, "cand", 0);
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
                row.setPadding(dp(10), dp(2), dp(8), dp(2));
                row.setSingleLine(false);
                row.setMaxLines(2);
                row.setTextColor(index == historyCursor ? Color.WHITE : 0xffbbbbbb);
                row.setOnClickListener(view -> loadHistoryAt(index));
                row.setOnLongClickListener(view -> {
                    copySeedToClipboard(String.valueOf(seedFromSongData(data) & 0xffffffffL));
                    return true;
                });
                historyListContainer.addView(row, params(width, 54, 2));
            }
        }
        if (clearHistoryButton != null) clearHistoryButton.setEnabled(!trackHistory.isEmpty());
        historyDirty = false;
    }

    private String historyRowText(String data) {
        long seed = seedFromSongData(data) & 0xffffffffL;
        int listened = Math.max(0, signedFieldFromSongData(data, "listened", 0));
        return "Seed: " + seed + "\nListened: " + formatDuration(listened);
    }

    private String mergeHistoryData(String oldData, String newData) {
        int oldListened = Math.max(0, signedFieldFromSongData(oldData, "listened", 0));
        int newListened = Math.max(0, signedFieldFromSongData(newData, "listened", 0));
        return replaceSignedField(newData, "listened", Math.max(oldListened, newListened));
    }

    private String replaceSignedField(String data, String key, int value) {
        if (data == null || data.isEmpty()) return data;
        String needle = key + "=";
        int pos = data.indexOf(needle);
        String field = needle + value;
        if (pos < 0) return data + ";" + field;
        int end = pos + needle.length();
        if (end < data.length() && data.charAt(end) == '-') ++end;
        while (end < data.length()) {
            char c = data.charAt(end);
            if (c < '0' || c > '9') break;
            ++end;
        }
        return data.substring(0, pos) + field + data.substring(end);
    }

    private Intent serviceIntent(String action) {
        Intent intent = new Intent(this, AudioService.class);
        intent.setAction(action);
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
        if (elapsedText != null) elapsedText.setText(currentElapsedText());
        if (previousButton != null) previousButton.setEnabled(historyCursor > 0);
        if (nextButton != null) nextButton.setEnabled(true);
        if (currentScreen == SCREEN_METADATA && metadataIdentityView != null) {
            String identity = ExportMetadata.identityNote(NativeAudio.currentSongData());
            if (!identity.contentEquals(metadataIdentityView.getText())) metadataIdentityView.setText(identity);
        }
        rebuildHistoryList();
    }

    private String currentElapsedText() {
        int elapsed = (int) Math.max(0.0, NativeAudio.currentElapsedSeconds());
        return "Elapsed: " + formatDuration(elapsed);
    }


    private void startExport(String requestedName, ExportFormat format) {
        if (exportRunning) return;
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            Toast.makeText(this, "Saving audio to Music requires Android 10 or newer.", Toast.LENGTH_LONG).show();
            return;
        }
        String source = NativeAudio.currentSongData();
        if (source == null || source.isEmpty()) {
            Toast.makeText(this, "No sound is available to export.", Toast.LENGTH_SHORT).show();
            return;
        }

        final int startSeconds = loadExportStartSeconds();
        final int endSeconds = loadExportEndSeconds();
        final int durationSeconds = endSeconds - startSeconds;
        if (durationSeconds < MIN_EXPORT_SPAN_SECONDS ||
                durationSeconds > MAX_EXPORT_SPAN_SECONDS) {
            Toast.makeText(this, "Choose a valid export start and end.", Toast.LENGTH_LONG).show();
            return;
        }
        // End defines the finite musical boundary. The offline engine advances
        // from time zero, discards everything before Start, and generates its
        // conclusion at End without touching live playback.
        final String data = songDataWithDuration(source, endSeconds);
        final String displayName = exportDisplayName(requestedName, format);
        // Capture preferences, date, notes and the exact native recipe once on
        // the UI thread. Later edits and live playback cannot retag this job.
        final ExportMetadata metadata;
        try {
            metadata = exportMetadata(displayName, data, startSeconds, endSeconds);
        } catch (IllegalArgumentException ex) {
            Toast.makeText(this, "Load a sound before exporting.", Toast.LENGTH_LONG).show();
            return;
        }
        activeExportFormat = format;
        exportRunning = true;
        exportCancelRequested = false;
        exportStatusText = "Rendering " + formatDuration(startSeconds) + "-" +
                formatDuration(endSeconds) + " offline...";
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
                raw = File.createTempFile("technomatic_2105_export_", ".pcm", getCacheDir());
                encoded = File.createTempFile("technomatic_2105_export_", format.extension, getCacheDir());

                updateExportStatus("Rendering " + formatDuration(startSeconds) + "-" +
                        formatDuration(endSeconds) + " offline...");
                checkExportCancelled(token);
                if (!NativeAudio.exportPcm16RangeToFile(
                        data, startSeconds, endSeconds, raw.getAbsolutePath())) {
                    checkExportCancelled(token);
                    throw new java.io.IOException("Native range render failed.");
                }
                long expected = (long) durationSeconds * 48000L * 2L * 2L;
                if (raw.length() != expected) {
                    throw new java.io.IOException("Native render length mismatch: expected " +
                            expected + " bytes, got " + raw.length() + ".");
                }

                updateExportStatus("Encoding " + format.label + "...");
                if (format == ExportFormat.OGG) {
                    OggExporter.encodeRawPcm16ToOgg(raw, encoded, metadata, token);
                } else {
                    FlacExporter.encodeRawPcm16ToFlac(raw, encoded, metadata, token);
                }
                checkExportCancelled(token);
                if (!encoded.exists() || encoded.length() <= 0L) {
                    throw new java.io.IOException(
                            "Encoder produced an empty " + format.label + " file.");
                }

                updateExportStatus("Publishing to Music...");
                ExportResult result = publishAudioToMusic(
                        encoded, displayName, metadata, format, token);
                publicPath = result.displayPath;
                ok = true;
                message = "Exported " + formatDuration(startSeconds) + "-" +
                        formatDuration(endSeconds) + " to " + publicPath;
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

    private ExportMetadata exportMetadata(
            String displayName, String songData, int startSeconds, int endSeconds) {
        Date capturedDate = new Date();
        return new ExportMetadata(
                removeKnownAudioExtension(displayName),
                exportArtist(),
                exportAlbumArtist(),
                exportAlbum(capturedDate),
                exportGenre(),
                metadataValue(KEY_V28_META_COMMENT, "Generated locally by Technomatic 2105"),
                new SimpleDateFormat("yyyy-MM-dd", Locale.US).format(capturedDate),
                songData, startSeconds, endSeconds);
    }

    private ExportResult publishAudioToMusic(
            File encodedAudio,
            String displayName,
            ExportMetadata metadata,
            ExportFormat format,
            ExportCancellationToken token) throws java.io.IOException {
        checkExportCancelled(token);
        ContentResolver resolver = getContentResolver();
        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, displayName);
        values.put(MediaStore.MediaColumns.MIME_TYPE, format.mimeType);
        values.put(MediaStore.Audio.Media.TITLE, metadata.title);
        values.put("artist", metadata.artist);
        values.put("album_artist", metadata.albumArtist);
        values.put("album", metadata.album);
        values.put("genre", metadata.genre);
        values.put(MediaStore.Audio.Media.IS_MUSIC, 1);
        values.put(MediaStore.Audio.Media.DURATION,
                Math.max(0L, (long) (metadata.endSeconds - metadata.startSeconds) * 1000L));
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
            if (resolver.update(uri, done, null, null) != 1) {
                throw new java.io.IOException("Could not finalize the exported Music file.");
            }
            return new ExportResult(uri, Environment.DIRECTORY_MUSIC + "/" + displayName);
        } catch (Exception ex) {
            try { resolver.delete(uri, null, null); } catch (Exception ignored) {}
            if (ex instanceof java.io.IOException) throw (java.io.IOException) ex;
            throw new java.io.IOException(ex);
        }
    }

    private String exportAlbumName(Date date) {
        return new SimpleDateFormat("MMMM dd yyyy", Locale.US)
                .format(date).toUpperCase(Locale.US);
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
        boolean canExport = Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && !exportRunning;
        if (exportOggButton != null) exportOggButton.setEnabled(canExport);
        if (exportFlacButton != null) exportFlacButton.setEnabled(canExport);
        if (cancelExportButton != null) {
            cancelExportButton.setEnabled(exportRunning);
            cancelExportButton.setVisibility(exportRunning ? View.VISIBLE : View.GONE);
        }
        if (exportStatusView != null) {
            exportStatusView.setText(exportRunning
                    ? exportStatusForUi()
                    : idleExportStatus());
        }
    }

    private String idleExportStatus() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            return "Audio export to Music requires Android 10 or newer. Playback remains available.";
        }
        return "OGG is compact and lossy. FLAC is lossless and larger. Both embed your metadata and seed, and render independently of live playback.";
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
        if (isFinishing() || isDestroyed()) return;
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

    private int loadExportStartSeconds() {
        return clamp(getSharedPreferences(PREFS, MODE_PRIVATE)
                .getInt(KEY_V28_EXPORT_START_SECONDS, DEFAULT_EXPORT_START_SECONDS),
                0, MAX_EXPORT_POSITION_SECONDS - MIN_EXPORT_SPAN_SECONDS);
    }

    private int loadExportEndSeconds() {
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        int fallback = prefs.contains(KEY_V23_EXPORT_SECONDS)
                ? clamp(prefs.getInt(KEY_V23_EXPORT_SECONDS, DEFAULT_EXPORT_END_SECONDS),
                        MIN_EXPORT_SPAN_SECONDS, MAX_EXPORT_SPAN_SECONDS)
                : DEFAULT_EXPORT_END_SECONDS;
        int start = loadExportStartSeconds();
        int end = clamp(prefs.getInt(KEY_V28_EXPORT_END_SECONDS, fallback),
                MIN_EXPORT_SPAN_SECONDS, MAX_EXPORT_POSITION_SECONDS);
        if (end < start + MIN_EXPORT_SPAN_SECONDS) {
            end = Math.min(MAX_EXPORT_POSITION_SECONDS, start + MIN_EXPORT_SPAN_SECONDS);
        }
        if (end - start > MAX_EXPORT_SPAN_SECONDS) end = start + MAX_EXPORT_SPAN_SECONDS;
        return end;
    }

    private void saveExportRange(int start, int end) {
        start = clamp(start, 0, MAX_EXPORT_POSITION_SECONDS - MIN_EXPORT_SPAN_SECONDS);
        end = clamp(end, start + MIN_EXPORT_SPAN_SECONDS, MAX_EXPORT_POSITION_SECONDS);
        if (end - start > MAX_EXPORT_SPAN_SECONDS) end = start + MAX_EXPORT_SPAN_SECONDS;
        getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                .putInt(KEY_V28_EXPORT_START_SECONDS, start)
                .putInt(KEY_V28_EXPORT_END_SECONDS, end)
                .apply();
        if (currentScreen == SCREEN_ADVANCED) showAdvancedScreen();
    }

    private void setExportStartTime(int start) {
        start = clamp(start, 0, MAX_EXPORT_POSITION_SECONDS - MIN_EXPORT_SPAN_SECONDS);
        int end = loadExportEndSeconds();
        if (end < start + MIN_EXPORT_SPAN_SECONDS) {
            int oldSpan = Math.max(DEFAULT_EXPORT_END_SECONDS,
                    loadExportEndSeconds() - loadExportStartSeconds());
            end = Math.min(MAX_EXPORT_POSITION_SECONDS,
                    start + Math.min(MAX_EXPORT_SPAN_SECONDS, oldSpan));
        }
        if (end - start > MAX_EXPORT_SPAN_SECONDS) end = start + MAX_EXPORT_SPAN_SECONDS;
        saveExportRange(start, end);
    }

    private void setExportEndTime(int end) {
        int start = loadExportStartSeconds();
        int requested = end;
        end = clamp(end, start + MIN_EXPORT_SPAN_SECONDS, MAX_EXPORT_POSITION_SECONDS);
        if (end - start > MAX_EXPORT_SPAN_SECONDS) {
            end = start + MAX_EXPORT_SPAN_SECONDS;
            Toast.makeText(this, "Maximum export interval is 1 hour.", Toast.LENGTH_LONG).show();
        } else if (requested < start + MIN_EXPORT_SPAN_SECONDS) {
            Toast.makeText(this, "Export end must be at least 8 seconds after start.", Toast.LENGTH_LONG).show();
        }
        saveExportRange(start, end);
    }

    private String metadataValue(String key, String fallback) {
        String value = getSharedPreferences(PREFS, MODE_PRIVATE).getString(key, fallback);
        return value == null ? fallback : value;
    }

    private String exportArtist() {
        String value = metadataValue(KEY_V28_META_ARTIST, "Technomatic 2105").trim();
        return value.isEmpty() ? "Technomatic 2105" : value;
    }

    private String exportAlbumArtist() {
        String value = metadataValue(KEY_V28_META_ALBUM_ARTIST, "Technomatic 2105").trim();
        return value.isEmpty() ? exportArtist() : value;
    }

    private String exportAlbum(Date date) {
        String value = metadataValue(KEY_V28_META_ALBUM, "").trim();
        return value.isEmpty() ? exportAlbumName(date) : value;
    }

    private String exportGenre() {
        String value = metadataValue(KEY_V28_META_GENRE, "").trim();
        return value.isEmpty() ? "Generative music" : value;
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
