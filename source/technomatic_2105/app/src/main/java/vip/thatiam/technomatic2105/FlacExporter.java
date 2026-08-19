package vip.thatiam.technomatic2105;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.List;

/**
 * Deterministic lossless FLAC encoder for Technomatic's export format:
 * 48 kHz, stereo, signed 16-bit PCM. It writes constant or verbatim FLAC
 * subframes, avoiding an external codec and preserving the rendered PCM exactly.
 */
final class FlacExporter {
    static final int SAMPLE_RATE = 48000;
    static final int CHANNELS = 2;
    static final int BITS_PER_SAMPLE = 16;
    static final int BYTES_PER_FRAME = CHANNELS * (BITS_PER_SAMPLE / 8);
    private static final int BLOCK_SIZE = 4096;
    private static final int IO_BUFFER_SIZE = 64 * 1024;

    static final class Metadata {
        final String title;
        final String artist;
        final String albumArtist;
        final String album;
        final String genre;
        final String comment;

        Metadata(String title, String artist, String albumArtist,
                 String album, String genre, String comment) {
            this.title = safe(title);
            this.artist = safe(artist);
            this.albumArtist = safe(albumArtist);
            this.album = safe(album);
            this.genre = safe(genre);
            this.comment = safe(comment);
        }

        private static String safe(String value) {
            return value == null ? "" : value;
        }
    }

    private FlacExporter() {}

    static void encodeRawPcm16ToFlac(
            File rawPcm,
            File output,
            Metadata metadata,
            ExportCancellationToken token) throws IOException {
        validateInput(rawPcm, output);
        final long totalSamples = rawPcm.length() / BYTES_PER_FRAME;
        if (totalSamples <= 0L || totalSamples > 0xfffffffffL) {
            throw new IOException("FLAC sample count is outside the supported range.");
        }

        checkCancelled(token);
        byte[] pcmMd5 = md5(rawPcm, token);
        byte[] commentBlock = makeVorbisComment(metadata);

        try (BufferedInputStream input = new BufferedInputStream(
                     new FileInputStream(rawPcm), IO_BUFFER_SIZE);
             BufferedOutputStream outputStream = new BufferedOutputStream(
                     new FileOutputStream(output), IO_BUFFER_SIZE)) {
            outputStream.write('f');
            outputStream.write('L');
            outputStream.write('a');
            outputStream.write('C');

            writeStreamInfo(outputStream, totalSamples, pcmMd5);
            writeMetadataBlock(outputStream, true, 4, commentBlock);

            byte[] interleaved = new byte[BLOCK_SIZE * BYTES_PER_FRAME];
            long framesRemaining = totalSamples;
            int frameNumber = 0;
            while (framesRemaining > 0L) {
                checkCancelled(token);
                int frames = (int) Math.min(BLOCK_SIZE, framesRemaining);
                int bytes = frames * BYTES_PER_FRAME;
                readFully(input, interleaved, bytes);
                writeFrame(outputStream, interleaved, frames, frameNumber);
                framesRemaining -= frames;
                frameNumber++;
            }
            outputStream.flush();
        } catch (IOException ex) {
            if (output.exists() && !output.delete()) output.deleteOnExit();
            throw ex;
        }

        if (!output.exists() || output.length() <= 0L) {
            throw new IOException("FLAC encoder produced an empty file.");
        }
    }

    private static void validateInput(File rawPcm, File output) throws IOException {
        if (rawPcm == null || output == null) throw new IOException("Missing export file.");
        if (!rawPcm.exists() || rawPcm.length() <= 0L) {
            throw new IOException("Missing rendered PCM data.");
        }
        if ((rawPcm.length() % BYTES_PER_FRAME) != 0L) {
            throw new IOException("Rendered PCM data is not frame-aligned.");
        }
    }

    private static byte[] md5(File file, ExportCancellationToken token) throws IOException {
        final MessageDigest digest;
        try {
            digest = MessageDigest.getInstance("MD5");
        } catch (NoSuchAlgorithmException ex) {
            throw new IOException("MD5 is unavailable.", ex);
        }
        try (BufferedInputStream input = new BufferedInputStream(
                new FileInputStream(file), IO_BUFFER_SIZE)) {
            byte[] buffer = new byte[IO_BUFFER_SIZE];
            int read;
            while ((read = input.read(buffer)) >= 0) {
                checkCancelled(token);
                if (read > 0) digest.update(buffer, 0, read);
            }
        }
        return digest.digest();
    }

    private static void writeStreamInfo(OutputStream out, long totalSamples, byte[] md5)
            throws IOException {
        ByteArrayOutputStream data = new ByteArrayOutputStream(34);
        writeU16BE(data, BLOCK_SIZE);
        writeU16BE(data, BLOCK_SIZE);
        writeU24BE(data, 0);
        writeU24BE(data, 0);
        long packed = ((long) SAMPLE_RATE << 44)
                | ((long) (CHANNELS - 1) << 41)
                | ((long) (BITS_PER_SAMPLE - 1) << 36)
                | (totalSamples & 0xfffffffffL);
        writeU64BE(data, packed);
        data.write(md5 != null && md5.length == 16 ? md5 : new byte[16]);
        writeMetadataBlock(out, false, 0, data.toByteArray());
    }

    private static byte[] makeVorbisComment(Metadata metadata) throws IOException {
        Metadata m = metadata == null
                ? new Metadata("", "Technomatic 2105", "Technomatic 2105", "", "", "")
                : metadata;
        List<String> fields = new ArrayList<>();
        addTag(fields, "TITLE", m.title);
        addTag(fields, "ARTIST", m.artist);
        addTag(fields, "ALBUMARTIST", m.albumArtist);
        addTag(fields, "ALBUM", m.album);
        addTag(fields, "GENRE", m.genre);
        addTag(fields, "COMMENT", m.comment);

        ByteArrayOutputStream data = new ByteArrayOutputStream(256);
        byte[] vendor = "Technomatic 2105 v27".getBytes(StandardCharsets.UTF_8);
        writeU32LE(data, vendor.length);
        data.write(vendor);
        writeU32LE(data, fields.size());
        for (String field : fields) {
            byte[] bytes = field.getBytes(StandardCharsets.UTF_8);
            writeU32LE(data, bytes.length);
            data.write(bytes);
        }
        return data.toByteArray();
    }

    private static void addTag(List<String> fields, String key, String value) {
        if (value != null && !value.isEmpty()) fields.add(key + "=" + value);
    }

    private static void writeMetadataBlock(
            OutputStream out, boolean last, int type, byte[] data) throws IOException {
        if (data.length > 0xffffff) throw new IOException("FLAC metadata is too large.");
        out.write((last ? 0x80 : 0x00) | (type & 0x7f));
        writeU24BE(out, data.length);
        out.write(data);
    }

    private static void writeFrame(
            OutputStream out, byte[] interleaved, int frames, int frameNumber) throws IOException {
        ByteArrayOutputStream frame = new ByteArrayOutputStream(frames * BYTES_PER_FRAME + 32);
        ByteArrayOutputStream header = new ByteArrayOutputStream(16);
        header.write(0xff);
        header.write(0xf8);
        header.write(0x7a);
        header.write(0x18);
        writeUtf8Number(header, frameNumber);
        writeU16BE(header, frames - 1);
        byte[] headerBytes = header.toByteArray();
        frame.write(headerBytes);
        frame.write(crc8(headerBytes));

        for (int channel = 0; channel < CHANNELS; ++channel) {
            int first = sampleAt(interleaved, 0, channel);
            boolean constant = true;
            for (int i = 1; i < frames; ++i) {
                if (sampleAt(interleaved, i, channel) != first) {
                    constant = false;
                    break;
                }
            }
            if (constant) {
                frame.write(0x00);
                writeU16BE(frame, first & 0xffff);
            } else {
                frame.write(0x02);
                for (int i = 0; i < frames; ++i) {
                    writeU16BE(frame, sampleAt(interleaved, i, channel) & 0xffff);
                }
            }
        }

        byte[] frameBytes = frame.toByteArray();
        out.write(frameBytes);
        writeU16BE(out, crc16(frameBytes));
    }

    private static int sampleAt(byte[] data, int frame, int channel) {
        int offset = frame * BYTES_PER_FRAME + channel * 2;
        int value = (data[offset] & 0xff) | (data[offset + 1] << 8);
        return (short) value;
    }

    private static void readFully(BufferedInputStream input, byte[] buffer, int length)
            throws IOException {
        int offset = 0;
        while (offset < length) {
            int read = input.read(buffer, offset, length - offset);
            if (read < 0) throw new IOException("Unexpected end of rendered PCM data.");
            offset += read;
        }
    }

    private static void checkCancelled(ExportCancellationToken token) throws IOException {
        if (token != null && token.isCancellationRequested()) {
            throw new IOException("Export cancelled.");
        }
    }

    private static int crc8(byte[] data) {
        int crc = 0;
        for (byte datum : data) {
            crc ^= datum & 0xff;
            for (int bit = 0; bit < 8; ++bit) {
                crc = ((crc & 0x80) != 0) ? ((crc << 1) ^ 0x07) : (crc << 1);
                crc &= 0xff;
            }
        }
        return crc;
    }

    private static int crc16(byte[] data) {
        int crc = 0;
        for (byte datum : data) {
            crc ^= (datum & 0xff) << 8;
            for (int bit = 0; bit < 8; ++bit) {
                crc = ((crc & 0x8000) != 0) ? ((crc << 1) ^ 0x8005) : (crc << 1);
                crc &= 0xffff;
            }
        }
        return crc;
    }

    private static void writeUtf8Number(OutputStream out, int value) throws IOException {
        if (value < 0) throw new IOException("Invalid FLAC frame number.");
        if (value <= 0x7f) {
            out.write(value);
        } else if (value <= 0x7ff) {
            out.write(0xc0 | (value >>> 6));
            out.write(0x80 | (value & 0x3f));
        } else if (value <= 0xffff) {
            out.write(0xe0 | (value >>> 12));
            out.write(0x80 | ((value >>> 6) & 0x3f));
            out.write(0x80 | (value & 0x3f));
        } else if (value <= 0x1fffff) {
            out.write(0xf0 | (value >>> 18));
            out.write(0x80 | ((value >>> 12) & 0x3f));
            out.write(0x80 | ((value >>> 6) & 0x3f));
            out.write(0x80 | (value & 0x3f));
        } else {
            throw new IOException("FLAC frame number is too large.");
        }
    }

    private static void writeU16BE(OutputStream out, int value) throws IOException {
        out.write((value >>> 8) & 0xff);
        out.write(value & 0xff);
    }

    private static void writeU24BE(OutputStream out, int value) throws IOException {
        out.write((value >>> 16) & 0xff);
        out.write((value >>> 8) & 0xff);
        out.write(value & 0xff);
    }

    private static void writeU32LE(OutputStream out, int value) throws IOException {
        out.write(value & 0xff);
        out.write((value >>> 8) & 0xff);
        out.write((value >>> 16) & 0xff);
        out.write((value >>> 24) & 0xff);
    }

    private static void writeU64BE(OutputStream out, long value) throws IOException {
        for (int shift = 56; shift >= 0; shift -= 8) {
            out.write((int) ((value >>> shift) & 0xffL));
        }
    }
}
