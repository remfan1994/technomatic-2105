package vip.thatiam.technomatic2105;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Exercises real encoded fixtures plus metadata/header boundary cases. */
public final class MetadataHostTest {
    static final String RECIPE = "technomatic2105-v1;seed=141414419;seconds=18;edited=0;gmask=0;"
            + "gblend=0;gprimary=0;gmode=0;cand=17;engine=30;intent=0;outputrev=2";
    static final String NOTES = "Original slow motion. Unicode: \u03b1 / \u6771\u4eac.\nSecond note line.";
    private static int checks;

    public static void main(String[] args) throws Exception {
        if (args.length != 3) throw new IllegalArgumentException("PCM source.ogg output-directory");
        File raw = new File(args[0]);
        File source = new File(args[1]);
        byte[] sourceBytes = Files.readAllBytes(source.toPath());
        File directory = new File(args[2]);
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Test directory unavailable.");

        ExportMetadata metadata = metadata("fixture", NOTES);
        check(metadata.comment.startsWith("Seed: 141414419\nEngine: 30\n"), "identity note");
        check(metadata.comment.endsWith("Range: 00:00:10-00:00:18"), "export range note");
        check(metadata.fields.get("SEED").equals("141414419"), "machine seed");
        check(metadata.fields.get("CHANNELS").equals("Retired"), "retired channel marker");
        check(metadata.fields.get("CHANNEL_PRIMARY").equals("None"), "no dominant channel");
        check(metadata.fields.get("CHANNEL_MASK").equals("0"), "neutral channel mask");
        check(metadata.fields.get("CHANNEL_BLEND").equals("none"), "neutral channel blend");
        check(metadata.fields.get("CHANNEL_PRIMARY_ID").equals("0"), "neutral primary id");
        check(metadata.fields.get("TECHNOMATIC_SONG_DATA").equals(RECIPE), "exact recipe including engine and output revision");
        check(metadata.fields.get("ENGINE_VERSION").equals("30"), "engine version tag");
        check(metadata.fields.get("OUTPUT_MASTER_VERSION").equals("2"), "captured output revision tag");
        check(ExportMetadata.identityNote(RECIPE).equals("Seed: 141414419\nEngine: 30"),
                "output revision leaves Notes identity unchanged");
        check(metadata.fields.get("COMPOSITION_INTENT").equals("slow"), "composition intent tag");
        check(metadata.fields.get("COMMENT").equals(metadata.fields.get("DESCRIPTION")), "comment aliases");
        try {
            metadata.fields.put("SEED", "0");
            throw new AssertionError("metadata must be immutable");
        } catch (UnsupportedOperationException expected) { checks++; }

        check(ExportMetadata.identityNote(null).startsWith("No current sound."), "empty current sound");
        check(ExportMetadata.identityNote("x;notseed=42").startsWith("No current sound."), "field boundary");
        check(ExportMetadata.identityNote("x;seed=4294967295;gmode=6")
                .equals("Seed: 4294967295"), "unsigned maximum with ignored legacy channel");
        check(ExportMetadata.identityNote("x;seed=4294967296").startsWith("No current sound."), "seed bounds");
        check(ExportMetadata.identityNote("x;seed=0;gmask=0;gmode=0")
                .equals("Seed: 0"), "valid zero seed parsing");
        check(ExportMetadata.identityNote("x;seed=55;gmask=544;gblend=1;gprimary=10;gmode=10")
                .equals("Seed: 55"), "legacy channel values have no identity effect");
        ExportMetadata blank = metadata("blank", "");
        check(!blank.comment.contains("\n\n"), "empty notes keep one generated identity");
        check(!blank.comment.contains("Original slow motion"), "old user notes absent from new snapshot");
        ExportMetadata next = new ExportMetadata("next", "a", "a", "b", "g", "personal note", "2026-10-18",
                RECIPE.replace("seed=141414419", "seed=27").replace("outputrev=2", "outputrev=3"), 0, 8);
        check(next.comment.startsWith("Seed: 27\nEngine: 30\n"), "new seed gets fresh identity");
        check(!next.comment.contains("141414419"), "previous generated identity cannot leak");
        check(metadata.fields.get("SEED").equals("141414419"), "original job snapshot survives Next");
        check(next.fields.get("OUTPUT_MASTER_VERSION").equals("3"), "new output revision comes from its own capture");
        check(metadata.fields.get("OUTPUT_MASTER_VERSION").equals("2"), "original output revision remains frozen");
        check(metadata.songData.equals(RECIPE), "original full recipe remains frozen");
        String legacy = "technomatic2105-v1;seed=141414419;gmask=4095;gblend=1;gprimary=6;gmode=6;engine=29";
        ExportMetadata retired = new ExportMetadata("legacy", "a", "a", "b", "g", "", "2026-10-18",
                legacy, 0, 8);
        check(retired.comment.equals("Seed: 141414419\nEngine: 29\nRange: 00:00:00-00:00:08"),
                "legacy recipe does not invent an active channel or engine version");
        check(retired.fields.get("TECHNOMATIC_SONG_DATA").equals(legacy), "raw recipe is not silently rewritten");
        check(retired.fields.get("CHANNELS").equals("Retired"), "legacy recipe has neutral export channel");
        check(retired.fields.get("CHANNEL_PRIMARY").equals("None"), "legacy dominant field neutralized");
        check(retired.fields.get("OUTPUT_MASTER_VERSION").isEmpty(), "missing legacy output revision is not invented");
        String legacyComments = new String(retired.vorbisCommentBlock("test", Arrays.asList(
                "OUTPUT_MASTER_VERSION=99")), StandardCharsets.UTF_8);
        check(!legacyComments.contains("OUTPUT_MASTER_VERSION="),
                "missing revision omits physical tag and removes stale source value");
        ExportMetadata withoutRevision = new ExportMetadata("fixture", "Technomatic 2105", "Technomatic 2105",
                "OCTOBER 18 2026", "Original design", NOTES, "2026-10-18",
                RECIPE.replace(";outputrev=2", ""), 10, 18);
        check(withoutRevision.comment.equals(metadata.comment), "output revision leaves user notes and comment unchanged");
        check(withoutRevision.fields.get("TECHNOMATIC_SONG_DATA").equals(RECIPE.replace(";outputrev=2", "")),
                "recipe without output revision stays verbatim");
        for (String badRevision : new String[] {";notoutputrev=1", ";outputrev=-1", ";outputrev=invalid"}) {
            ExportMetadata invalid = new ExportMetadata("x", "a", "a", "b", "g", "", "2026-10-18",
                    legacy + badRevision, 0, 8);
            check(invalid.fields.get("OUTPUT_MASTER_VERSION").isEmpty(),
                    "unusable output revision is not invented: " + badRevision);
        }
        String[] obsoleteLabels = {"Old Auto", "Old Named Channel"};
        ExportMetadata isolated = new ExportMetadata("x", "a", "a", "b", "g", "", "2026-10-18",
                RECIPE, obsoleteLabels, 10, 18);
        obsoleteLabels[1] = "Changed later";
        check(isolated.fields.get("CHANNEL_PRIMARY").equals("None"), "compatibility labels ignored");
        check(ExportMetadata.identityNote(RECIPE, null).equals(ExportMetadata.identityNote(RECIPE)),
                "old identity signature needs no labels");
        String handwritten = "141414419 all channels neon drift dominant\nMy old listening note.";
        ExportMetadata personal = metadata("personal", handwritten);
        check(personal.comment.contains(handwritten), "handwritten old channel notes preserved");
        check(personal.comment.startsWith("Seed: 141414419\nEngine: 30\n"), "fresh automatic identity remains separate");

        File flac = new File(directory, "tagged.flac");
        FlacExporter.encodeRawPcm16ToFlac(raw, flac, metadata, () -> false);
        File tagged = new File(directory, "tagged.ogg");
        OggMetadataWriter.write(source, tagged, metadata, () -> false);
        assertAudioPreserved(source, tagged);
        Tags original = tags(source);
        Tags result = tags(tagged);
        check(original.vendor.equals(result.vendor), "codec vendor preserved");
        check(result.comments.contains("COMMENT=" + metadata.comment), "actual embedded comment");
        check(result.comments.contains("SEED=141414419"), "actual embedded seed");
        check(result.comments.contains("OUTPUT_MASTER_VERSION=2"), "actual embedded output revision");
        check(result.comments.contains("TECHNOMATIC_SONG_DATA=" + RECIPE), "actual embedded full recipe with output revision");

        // Exact multiples of 255 need a final zero lace. Crossing 255 laces
        // changes the number of header pages and all later sequence numbers.
        for (int target : new int[] {65024, 65025, 65026, 130050}) {
            ExportMetadata large = sizedMetadata(target, original);
            File largeOutput = new File(directory, "boundary_" + target + ".ogg");
            OggMetadataWriter.write(source, largeOutput, large, () -> false);
            check(tags(largeOutput).packetSize == target, "exact OpusTags size " + target);
            assertAudioPreserved(source, largeOutput);
            File reduced = new File(directory, "reduced_" + target + ".ogg");
            OggMetadataWriter.write(largeOutput, reduced, blank, () -> false);
            assertAudioPreserved(source, reduced);
            check(tags(reduced).comments.contains("COMMENT=" + blank.comment), "replace rather than duplicate note");
            check(!tags(reduced).comments.contains("COMMENT=" + large.comment), "remove prior generated note");
        }

        // Unknown codec tags and RFC 7845 non-padding binary suffixes survive
        // replacement. Ordinary zero padding is discarded.
        File extended = new File(directory, "extended-input.ogg");
        byte[] extension = new byte[] {1, 44, 55, 66};
        writeWithTags(source, extended, makeTags(original.vendor,
                Arrays.asList("R128_TRACK_GAIN=-17", "PRIVATE_TEST=preserved", "TITLE=obsolete",
                        "CHANNELS=Neon Drift", "CHANNEL_MASK=32", "CHANNEL_BLEND=hybrid",
                        "CHANNEL_PRIMARY=Neon Drift", "CHANNEL_PRIMARY_ID=6",
                        "OUTPUT_MASTER_VERSION=99"), extension));
        File extensionOutput = new File(directory, "extended.ogg");
        OggMetadataWriter.write(extended, extensionOutput, metadata, () -> false);
        Tags extensionTags = tags(extensionOutput);
        check(extensionTags.comments.contains("R128_TRACK_GAIN=-17"), "preserve codec-specific gain");
        check(extensionTags.comments.contains("PRIVATE_TEST=preserved"), "preserve unknown tags");
        check(!extensionTags.comments.contains("TITLE=obsolete"), "replace owned fields");
        check(!extensionTags.comments.contains("CHANNELS=Neon Drift"), "replace old named channel tag");
        check(!extensionTags.comments.contains("CHANNEL_PRIMARY=Neon Drift"), "replace old dominant tag");
        check(extensionTags.comments.contains("CHANNELS=Retired"), "physical retired channel marker");
        check(extensionTags.comments.contains("CHANNEL_MASK=0"), "physical neutral channel mask");
        check(extensionTags.comments.contains("OUTPUT_MASTER_VERSION=2"), "physical output revision matches current capture");
        check(!extensionTags.comments.contains("OUTPUT_MASTER_VERSION=99"), "old output revision replaced without duplicate");
        check(Arrays.equals(extensionTags.suffix, extension), "preserve non-padding tag suffix");
        assertAudioPreserved(extended, extensionOutput);

        // AOSP OggWriter may finish cleanly without marking its last page EOS.
        // The metadata pass accepts that shape and preserves its flags/timing.
        List<byte[]> noEosPages = pages(source);
        byte[] lastPage = noEosPages.get(noEosPages.size() - 1);
        lastPage[5] &= ~4;
        put32(lastPage, 22, bitwiseCrc(lastPage));
        ByteArrayOutputStream noEosBytes = new ByteArrayOutputStream();
        for (byte[] page : noEosPages) noEosBytes.write(page);
        File noEosSource = new File(directory, "no-eos-input.ogg");
        Files.write(noEosSource.toPath(), noEosBytes.toByteArray());
        File noEosOutput = new File(directory, "no-eos.ogg");
        OggMetadataWriter.write(noEosSource, noEosOutput, metadata, () -> false);
        assertAudioPreserved(noEosSource, noEosOutput);

        byte[] damaged = Files.readAllBytes(source.toPath());
        damaged[damaged.length - 1] ^= 1;
        File corrupt = new File(directory, "bad-crc.ogg");
        Files.write(corrupt.toPath(), damaged);
        expectFailure(corrupt, new File(directory, "bad-crc-output.ogg"), metadata, () -> false, "checksum");
        File truncated = new File(directory, "truncated.ogg");
        Files.write(truncated.toPath(), Arrays.copyOf(damaged, damaged.length - 3));
        expectFailure(truncated, new File(directory, "truncated-output.ogg"), metadata, () -> false, "truncated");
        byte[] badLengthTags = makeTags(original.vendor, Arrays.asList("A=B"), new byte[0]);
        put32(badLengthTags, 8, 0x7fffffff);
        File malformed = new File(directory, "bad-length.ogg");
        writeWithTags(source, malformed, badLengthTags);
        expectFailure(malformed, new File(directory, "bad-length-output.ogg"), metadata, () -> false, "length");
        expectFailure(source, new File(directory, "cancel-output.ogg"), metadata, () -> true, "cancel");
        final int[] calls = {0};
        expectFailure(source, new File(directory, "mid-cancel-output.ogg"), metadata, () -> ++calls[0] >= 5, "cancel");
        File cancelledFlac = new File(directory, "cancelled.flac");
        try {
            FlacExporter.encodeRawPcm16ToFlac(raw, cancelledFlac, metadata, () -> true);
            throw new AssertionError("FLAC cancellation ignored");
        } catch (IOException expected) {
            check(expected.getMessage().contains("cancel"), "FLAC cancellation");
            check(!cancelledFlac.exists(), "no cancelled FLAC published");
        }
        // The same source must still parse after all success/failure/cancel paths.
        check(Arrays.equals(sourceBytes, Files.readAllBytes(source.toPath())), "source unchanged by every path");
        System.out.println("Metadata host checks passed: " + checks);
    }

    private static ExportMetadata metadata(String title, String notes) {
        return new ExportMetadata(title, "Technomatic 2105", "Technomatic 2105", "OCTOBER 18 2026",
                "Original design", notes, "2026-10-18", RECIPE, 10, 18);
    }

    private static ExportMetadata sizedMetadata(int size, Tags original) throws IOException {
        ExportMetadata minimum = metadata("boundary", "x");
        int base = 8 + minimum.vorbisCommentBlock(original.vendor, original.comments).length;
        int remaining = size - base;
        if (remaining < 0) throw new AssertionError("boundary too small");
        char[] padding = new char[1 + remaining / 2];
        Arrays.fill(padding, 'x');
        ExportMetadata result = metadata(remaining % 2 == 0 ? "boundary" : "boundaryx", new String(padding));
        check(8 + result.vorbisCommentBlock(original.vendor, original.comments).length == size, "sized fixture");
        return result;
    }

    private static void expectFailure(File source, File output, ExportMetadata metadata,
                                      ExportCancellationToken token, String word) throws Exception {
        try {
            OggMetadataWriter.write(source, output, metadata, token);
            throw new AssertionError("Expected failure: " + word);
        } catch (IOException expected) {
            check(expected.getMessage().toLowerCase(java.util.Locale.US).contains(word), "failure: " + word);
            check(!output.exists(), "partial output removed: " + word);
        }
    }

    private static void assertAudioPreserved(File before, File after) throws IOException {
        List<byte[]> first = pages(before);
        List<byte[]> second = pages(after);
        check(Arrays.equals(first.get(0), second.get(0)), "OpusHead page byte-identical");
        int startA = audioStart(first);
        int startB = audioStart(second);
        check(first.size() - startA == second.size() - startB, "same audio page count");
        for (int i = 0; i < first.size() - startA; i++) {
            byte[] a = first.get(startA + i).clone();
            byte[] b = second.get(startB + i).clone();
            // Sequence and its dependent checksum are the only allowed changes.
            Arrays.fill(a, 18, 26, (byte) 0);
            Arrays.fill(b, 18, 26, (byte) 0);
            check(Arrays.equals(a, b), "audio bytes/granules/lacing/flags preserved");
        }
    }

    private static List<byte[]> pages(File file) throws IOException {
        byte[] bytes = Files.readAllBytes(file.toPath());
        List<byte[]> result = new ArrayList<>();
        int sequence = -1;
        for (int offset = 0; offset < bytes.length; ) {
            if (bytes.length - offset < 27) throw new AssertionError("short page");
            int count = bytes[offset + 26] & 255;
            int size = 27 + count;
            for (int i = 0; i < count; i++) size += bytes[offset + 27 + i] & 255;
            if (offset + size > bytes.length) throw new AssertionError("page overrun");
            byte[] page = Arrays.copyOfRange(bytes, offset, offset + size);
            check(read32(page, 22) == bitwiseCrc(page), "independent page CRC");
            int current = read32(page, 18);
            if (!result.isEmpty()) check(current == sequence + 1, "consecutive page sequence");
            sequence = current;
            result.add(page);
            offset += size;
        }
        return result;
    }

    private static int audioStart(List<byte[]> pages) {
        for (int i = 1; i < pages.size(); i++) {
            byte[] page = pages.get(i);
            int count = page[26] & 255;
            if (count > 0 && (page[26 + count] & 255) != 255) return i + 1;
        }
        throw new AssertionError("no complete comment packet");
    }

    private static Tags tags(File file) throws IOException {
        List<byte[]> pages = pages(file);
        ByteArrayOutputStream packet = new ByteArrayOutputStream();
        for (int i = 1; i < audioStart(pages); i++) {
            byte[] page = pages.get(i);
            int body = 27 + (page[26] & 255);
            packet.write(page, body, page.length - body);
        }
        return new Tags(packet.toByteArray());
    }

    private static final class Tags {
        final String vendor;
        final List<String> comments = new ArrayList<>();
        final byte[] suffix;
        final int packetSize;
        Tags(byte[] packet) {
            packetSize = packet.length;
            int vendorSize = read32(packet, 8);
            vendor = new String(packet, 12, vendorSize, StandardCharsets.UTF_8);
            int count = read32(packet, 12 + vendorSize);
            int offset = 16 + vendorSize;
            for (int i = 0; i < count; i++) {
                int size = read32(packet, offset);
                offset += 4;
                comments.add(new String(packet, offset, size, StandardCharsets.UTF_8));
                offset += size;
            }
            suffix = Arrays.copyOfRange(packet, offset, packet.length);
        }
    }

    private static byte[] makeTags(String vendor, List<String> comments, byte[] suffix) throws IOException {
        ByteArrayOutputStream packet = new ByteArrayOutputStream();
        packet.write("OpusTags".getBytes(StandardCharsets.US_ASCII));
        writeString(packet, vendor);
        write32(packet, comments.size());
        for (String field : comments) writeString(packet, field);
        packet.write(suffix);
        return packet.toByteArray();
    }

    private static void writeWithTags(File source, File output, byte[] packet) throws IOException {
        if (packet.length >= 65025) throw new IllegalArgumentException("test fixture tags too large");
        List<byte[]> sourcePages = pages(source);
        int count = packet.length / 255 + 1;
        byte[] tagPage = new byte[27 + count + packet.length];
        System.arraycopy(sourcePages.get(1), 0, tagPage, 0, 27);
        tagPage[26] = (byte) count;
        for (int i = 0; i < count; i++) tagPage[27 + i] = (byte) Math.min(255, packet.length - i * 255);
        System.arraycopy(packet, 0, tagPage, 27 + count, packet.length);
        put32(tagPage, 22, bitwiseCrc(tagPage));
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        bytes.write(sourcePages.get(0));
        bytes.write(tagPage);
        int sequence = read32(tagPage, 18) + 1;
        for (int i = audioStart(sourcePages); i < sourcePages.size(); i++) {
            byte[] page = sourcePages.get(i).clone();
            put32(page, 18, sequence++);
            put32(page, 22, bitwiseCrc(page));
            bytes.write(page);
        }
        Files.write(output.toPath(), bytes.toByteArray());
    }

    private static void writeString(ByteArrayOutputStream out, String text) throws IOException {
        byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
        write32(out, bytes.length);
        out.write(bytes);
    }

    private static void write32(ByteArrayOutputStream out, int value) {
        for (int shift = 0; shift < 32; shift += 8) out.write((value >>> shift) & 255);
    }

    private static int read32(byte[] bytes, int offset) {
        int result = 0;
        for (int i = 0; i < 4; i++) result |= (bytes[offset + i] & 255) << (8 * i);
        return result;
    }

    private static void put32(byte[] bytes, int offset, int value) {
        for (int i = 0; i < 4; i++) bytes[offset + i] = (byte) (value >>> (8 * i));
    }

    // Deliberately bitwise to check the production table implementation.
    private static int bitwiseCrc(byte[] bytes) {
        int value = 0;
        for (int i = 0; i < bytes.length; i++) {
            value ^= (i >= 22 && i < 26 ? 0 : bytes[i] & 255) << 24;
            for (int bit = 0; bit < 8; bit++) value = (value & 0x80000000) != 0
                    ? (value << 1) ^ 0x04c11db7 : value << 1;
        }
        return value;
    }

    private static void check(boolean condition, String name) {
        if (!condition) throw new AssertionError(name);
        checks++;
    }
}
