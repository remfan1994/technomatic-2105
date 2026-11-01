package vip.thatiam.technomatic2105;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** Immutable, format-independent metadata captured before an export starts. */
final class ExportMetadata {
    static final String APPLICATION = "Technomatic 2105";
    private static final int MAX_COMMENT_BYTES = 16 * 1024 * 1024 - 1;

    final String title;
    final String artist;
    final String albumArtist;
    final String album;
    final String genre;
    final String comment;
    final String songData;
    final int startSeconds;
    final int endSeconds;
    final Map<String, String> fields;

    ExportMetadata(String title, String artist, String albumArtist,
                   String album, String genre, String userNotes, String exportDate,
                   String songData, int startSeconds, int endSeconds) {
        this.title = safe(title);
        this.artist = safe(artist);
        this.albumArtist = safe(albumArtist);
        this.album = safe(album);
        this.genre = safe(genre);
        this.songData = safe(songData);
        this.startSeconds = startSeconds;
        this.endSeconds = endSeconds;

        Identity identity = new Identity(this.songData);
        if (!identity.hasSeed) throw new IllegalArgumentException("No current sound identity.");
        String notes = safe(userNotes).trim();
        // Only user notes are persisted by the editor. Seed/engine/range are
        // derived here for each job, so Next/Load cannot leave stale identity.
        this.comment = identity.summary + (notes.isEmpty() ? "" : "\n" + notes)
                + "\nRange: " + formatTime(startSeconds) + "-" + formatTime(endSeconds);

        LinkedHashMap<String, String> tags = new LinkedHashMap<>();
        tags.put("TITLE", this.title);
        tags.put("ARTIST", this.artist);
        tags.put("ALBUMARTIST", this.albumArtist);
        tags.put("ALBUM", this.album);
        tags.put("GENRE", this.genre);
        tags.put("DATE", safe(exportDate));
        // COMMENT is widely displayed by players; DESCRIPTION is the name
        // recommended by the Vorbis comment specification for a text note.
        tags.put("COMMENT", this.comment);
        tags.put("DESCRIPTION", this.comment);
        tags.put("SEED", Long.toString(identity.seed));
        // Channels were retired in v30. Keep the old fields explicitly neutral
        // so older readers cannot mistake a legacy recipe for an active choice.
        tags.put("CHANNELS", "Retired");
        tags.put("CHANNEL_MASK", "0");
        tags.put("CHANNEL_BLEND", "none");
        tags.put("CHANNEL_PRIMARY", "None");
        tags.put("CHANNEL_PRIMARY_ID", "0");
        tags.put("EXPORT_START_SECONDS", Integer.toString(startSeconds));
        tags.put("EXPORT_END_SECONDS", Integer.toString(endSeconds));
        long engine = field(this.songData, "engine", -1);
        tags.put("ENGINE_VERSION", engine < 0 ? "" : Long.toString(engine));
        long outputRevision = field(this.songData, "outputrev", -1);
        // Output processing can change without changing the composition engine.
        // Blank values remove stale tags but do not invent a legacy revision.
        tags.put("OUTPUT_MASTER_VERSION", outputRevision < 0 ? "" : Long.toString(outputRevision));
        long intent = field(this.songData, "intent", -1);
        String[] intents = {"slow", "relaxed groove", "driving", "intense"};
        tags.put("COMPOSITION_INTENT", intent >= 0 && intent < intents.length ? intents[(int) intent] : "");
        tags.put("COMPOSITION_INTENT_ID", intent >= 0 && intent < intents.length ? Long.toString(intent) : "");
        tags.put("TECHNOMATIC_SONG_DATA", this.songData);
        tags.put("ENCODER", APPLICATION);
        this.fields = Collections.unmodifiableMap(tags);
    }

    /** Compatibility constructor for older in-package integrations; labels are ignored. */
    @Deprecated
    ExportMetadata(String title, String artist, String albumArtist,
                   String album, String genre, String userNotes, String exportDate,
                   String songData, String[] ignoredChannelLabels, int startSeconds, int endSeconds) {
        this(title, artist, albumArtist, album, genre, userNotes, exportDate,
                songData, startSeconds, endSeconds);
    }

    static String identityNote(String songData) {
        Identity identity = new Identity(songData);
        return identity.hasSeed ? identity.summary : "No current sound. Start or load a seed first.";
    }

    /** Compatibility entry point; labels have no effect after channel retirement. */
    @Deprecated
    static String identityNote(String songData, String[] ignoredChannelLabels) {
        return identityNote(songData);
    }

    /** The comment payload shared by FLAC block 4 and the OpusTags packet. */
    byte[] vorbisCommentBlock(String vendor, List<String> existingComments) throws IOException {
        List<String> comments = new ArrayList<>();
        for (Map.Entry<String, String> entry : fields.entrySet()) {
            if (!entry.getValue().isEmpty()) comments.add(entry.getKey() + "=" + entry.getValue());
        }
        // Preserve codec-specific fields such as R128_TRACK_GAIN. User-facing
        // fields owned by this snapshot replace older values, including blanks.
        if (existingComments != null) {
            for (String field : existingComments) {
                int equals = field.indexOf('=');
                String name = (equals < 0 ? field : field.substring(0, equals)).toUpperCase(Locale.US);
                if (!fields.containsKey(name)) comments.add(field);
            }
        }

        ByteArrayOutputStream out = new ByteArrayOutputStream(1024);
        writeString(out, safe(vendor));
        writeU32LE(out, comments.size());
        for (String field : comments) writeString(out, field);
        if (out.size() > MAX_COMMENT_BYTES) throw new IOException("Export metadata is too large.");
        return out.toByteArray();
    }

    private static void writeString(ByteArrayOutputStream out, String value) throws IOException {
        if (value.length() > MAX_COMMENT_BYTES) throw new IOException("Export metadata is too large.");
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        if (bytes.length > MAX_COMMENT_BYTES - out.size() - 4) {
            throw new IOException("Export metadata is too large.");
        }
        writeU32LE(out, bytes.length);
        out.write(bytes, 0, bytes.length);
    }

    private static void writeU32LE(ByteArrayOutputStream out, int value) {
        out.write(value & 0xff);
        out.write((value >>> 8) & 0xff);
        out.write((value >>> 16) & 0xff);
        out.write((value >>> 24) & 0xff);
    }

    private static String formatTime(int seconds) {
        int value = Math.max(0, seconds);
        return String.format(Locale.US, "%02d:%02d:%02d", value / 3600, (value / 60) % 60, value % 60);
    }

    private static String safe(String value) {
        return value == null ? "" : value;
    }

    private static long field(String data, String key, long fallback) {
        if (data == null) return fallback;
        for (String item : data.split(";")) {
            if (!item.startsWith(key + "=")) continue;
            try {
                return Long.parseLong(item.substring(key.length() + 1));
            } catch (NumberFormatException ignored) {
                return fallback;
            }
        }
        return fallback;
    }

    private static final class Identity {
        final long seed;
        final boolean hasSeed;
        final String summary;

        Identity(String songData) {
            seed = field(songData, "seed", -1L);
            hasSeed = seed >= 0L && seed <= 0xffffffffL;
            long engine = field(songData, "engine", -1L);
            summary = "Seed: " + seed + (engine < 0 ? "" : "\nEngine: " + engine);
        }
    }
}
