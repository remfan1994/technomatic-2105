package vip.thatiam.technomatic2105;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Replaces OpusTags in a completed, single-stream Ogg file without re-encoding
 * audio. Header paging follows RFC 7845 sections 3-5; page CRC follows RFC 3533.
 * Audio lacing, packet bytes, flags and granules are preserved verbatim.
 */
final class OggMetadataWriter {
    private static final int MAX_TAG_BYTES = 16 * 1024 * 1024;
    private static final int[] CRC_TABLE = makeCrcTable();

    private OggMetadataWriter() {}

    static void write(File inputFile, File outputFile, ExportMetadata metadata,
                      ExportCancellationToken token) throws IOException {
        if (inputFile == null || outputFile == null || metadata == null) {
            throw new IOException("Missing OGG metadata input.");
        }
        if (inputFile.getCanonicalFile().equals(outputFile.getCanonicalFile())) {
            throw new IOException("OGG metadata needs a separate output file.");
        }
        boolean completed = false;
        try (InputStream input = new BufferedInputStream(new FileInputStream(inputFile), 64 * 1024);
             OutputStream output = new BufferedOutputStream(new FileOutputStream(outputFile), 64 * 1024)) {
            checkCancelled(token);
            Page head = readPage(input);
            if (head == null || head.flags() != 2 || head.granule() != 0 || head.bodyLength() < 19
                    || !head.singleCompletePacket() || !head.bodyStartsWith("OpusHead")) {
                throw new IOException("Invalid Ogg Opus identification header.");
            }
            output.write(head.bytes);
            int serial = head.serial();
            int inputSequence = head.sequence() + 1;
            int outputSequence = inputSequence;

            ByteArrayOutputStream oldTags = new ByteArrayOutputStream();
            boolean continued = false;
            while (true) {
                checkCancelled(token);
                Page page = readPage(input);
                validatePage(page, serial, inputSequence++);
                if (page.flags() != (continued ? 1 : 0) || page.segmentCount() == 0) {
                    throw new IOException("Invalid Ogg Opus comment header page.");
                }
                for (int i = 0; i < page.segmentCount() - 1; i++) {
                    if (page.lace(i) != 255) {
                        throw new IOException("OpusTags must finish its own Ogg page.");
                    }
                }
                if (oldTags.size() > MAX_TAG_BYTES - page.bodyLength()) {
                    throw new IOException("OGG comment header is too large.");
                }
                oldTags.write(page.bytes, page.bodyOffset(), page.bodyLength());
                continued = page.lace(page.segmentCount() - 1) == 255;
                if (page.granule() != (continued ? -1L : 0L)) {
                    throw new IOException("Invalid Ogg comment header position.");
                }
                if (!continued) break;
            }

            ExistingComments old = new ExistingComments(oldTags.toByteArray());
            byte[] commentBlock = metadata.vorbisCommentBlock(old.vendor, old.comments);
            if (commentBlock.length > MAX_TAG_BYTES - 8 - old.suffix.length) {
                throw new IOException("OGG metadata is too large.");
            }
            ByteArrayOutputStream tags = new ByteArrayOutputStream(8 + commentBlock.length + old.suffix.length);
            tags.write("OpusTags".getBytes(StandardCharsets.US_ASCII));
            tags.write(commentBlock);
            tags.write(old.suffix);
            outputSequence = writeHeaderPacket(output, tags.toByteArray(), serial, outputSequence, token);

            boolean audioFound = false;
            boolean ended = false;
            continued = false;
            Page page;
            while ((page = readPage(input)) != null) {
                checkCancelled(token);
                validatePage(page, serial, inputSequence++);
                if (ended || (page.flags() & 2) != 0) throw new IOException("Unexpected additional Ogg stream.");
                if (page.segmentCount() > 0) {
                    if (((page.flags() & 1) != 0) != continued) {
                        throw new IOException("Incomplete Ogg audio packet.");
                    }
                    continued = page.lace(page.segmentCount() - 1) == 255;
                    audioFound = true;
                }
                ended = (page.flags() & 4) != 0;
                if (page.sequence() != outputSequence) {
                    put32(page.bytes, 18, outputSequence);
                    updateCrc(page.bytes);
                }
                outputSequence++;
                output.write(page.bytes);
            }
            // Android's OggWriter can omit the EOS flag. Preserve that flag and
            // all timing exactly; a truncated packet or an empty stream fails.
            if (!audioFound || continued) throw new IOException("Incomplete Ogg audio stream.");
            checkCancelled(token);
            output.flush();
            completed = true;
        } catch (IOException ex) {
            completed = false;
            throw ex;
        } finally {
            if (!completed && outputFile.exists() && !outputFile.delete()) outputFile.deleteOnExit();
        }
    }

    private static int writeHeaderPacket(OutputStream output, byte[] packet, int serial,
                                         int sequence, ExportCancellationToken token) throws IOException {
        int fullSegments = packet.length / 255;
        // A multiple of 255 still needs a zero-length terminating segment.
        int totalSegments = fullSegments + 1;
        int bodyOffset = 0;
        for (int segment = 0; segment < totalSegments; ) {
            checkCancelled(token);
            int segmentCount = Math.min(255, totalSegments - segment);
            boolean last = segment + segmentCount == totalSegments;
            int bodySize = last ? packet.length - bodyOffset : segmentCount * 255;
            byte[] page = new byte[27 + segmentCount + bodySize];
            page[0] = 'O'; page[1] = 'g'; page[2] = 'g'; page[3] = 'S';
            page[5] = (byte) (segment == 0 ? 0 : 1);
            put64(page, 6, last ? 0L : -1L);
            put32(page, 14, serial);
            put32(page, 18, sequence++);
            page[26] = (byte) segmentCount;
            for (int i = 0; i < segmentCount; i++) {
                page[27 + i] = (byte) (segment + i < fullSegments ? 255 : packet.length % 255);
            }
            System.arraycopy(packet, bodyOffset, page, 27 + segmentCount, bodySize);
            updateCrc(page);
            output.write(page);
            bodyOffset += bodySize;
            segment += segmentCount;
        }
        return sequence;
    }

    private static Page readPage(InputStream input) throws IOException {
        int first = input.read();
        if (first < 0) return null;
        byte[] header = new byte[27];
        header[0] = (byte) first;
        readFully(input, header, 1, 26);
        if (header[0] != 'O' || header[1] != 'g' || header[2] != 'g' || header[3] != 'S'
                || header[4] != 0 || (header[5] & ~7) != 0) {
            throw new IOException("Invalid Ogg page header.");
        }
        int count = header[26] & 0xff;
        byte[] laces = new byte[count];
        readFully(input, laces, 0, count);
        int bodySize = 0;
        for (byte lace : laces) bodySize += lace & 0xff;
        byte[] bytes = new byte[27 + count + bodySize];
        System.arraycopy(header, 0, bytes, 0, 27);
        System.arraycopy(laces, 0, bytes, 27, count);
        readFully(input, bytes, 27 + count, bodySize);
        if (get32(bytes, 22) != crc(bytes)) throw new IOException("Ogg checksum mismatch.");
        return new Page(bytes);
    }

    private static void validatePage(Page page, int serial, int sequence) throws IOException {
        if (page == null) throw new IOException("Truncated Ogg stream.");
        if (page.serial() != serial || page.sequence() != sequence) {
            throw new IOException("Unexpected Ogg stream or missing page.");
        }
    }

    private static void readFully(InputStream in, byte[] bytes, int offset, int count) throws IOException {
        int end = offset + count;
        while (offset < end) {
            int read = in.read(bytes, offset, end - offset);
            if (read < 0) throw new IOException("Truncated Ogg page.");
            offset += read;
        }
    }

    private static int[] makeCrcTable() {
        int[] table = new int[256];
        for (int i = 0; i < table.length; i++) {
            int value = i << 24;
            for (int bit = 0; bit < 8; bit++) value = (value << 1) ^ (value < 0 ? 0x04c11db7 : 0);
            table[i] = value;
        }
        return table;
    }

    private static int crc(byte[] bytes) {
        int value = 0;
        for (int i = 0; i < bytes.length; i++) {
            int datum = i >= 22 && i < 26 ? 0 : bytes[i] & 0xff;
            value = (value << 8) ^ CRC_TABLE[((value >>> 24) ^ datum) & 0xff];
        }
        return value;
    }

    private static void updateCrc(byte[] bytes) { put32(bytes, 22, crc(bytes)); }

    private static int get32(byte[] bytes, int offset) {
        return (bytes[offset] & 0xff) | ((bytes[offset + 1] & 0xff) << 8)
                | ((bytes[offset + 2] & 0xff) << 16) | ((bytes[offset + 3] & 0xff) << 24);
    }

    private static void put32(byte[] bytes, int offset, int value) {
        for (int i = 0; i < 4; i++) bytes[offset + i] = (byte) (value >>> (i * 8));
    }

    private static void put64(byte[] bytes, int offset, long value) {
        for (int i = 0; i < 8; i++) bytes[offset + i] = (byte) (value >>> (i * 8));
    }

    private static void checkCancelled(ExportCancellationToken token) throws IOException {
        if (token != null && token.isCancellationRequested()) throw new IOException("Export cancelled.");
    }

    private static final class Page {
        final byte[] bytes;
        Page(byte[] bytes) { this.bytes = bytes; }
        int flags() { return bytes[5] & 0xff; }
        int serial() { return get32(bytes, 14); }
        int sequence() { return get32(bytes, 18); }
        int segmentCount() { return bytes[26] & 0xff; }
        int lace(int i) { return bytes[27 + i] & 0xff; }
        int bodyOffset() { return 27 + segmentCount(); }
        int bodyLength() { return bytes.length - bodyOffset(); }
        long granule() {
            long value = 0L;
            for (int i = 0; i < 8; i++) value |= (bytes[6 + i] & 0xffL) << (i * 8);
            return value;
        }
        boolean singleCompletePacket() {
            if (segmentCount() == 0 || lace(segmentCount() - 1) == 255) return false;
            for (int i = 0; i < segmentCount() - 1; i++) if (lace(i) != 255) return false;
            return true;
        }
        boolean bodyStartsWith(String value) {
            if (bodyLength() < value.length()) return false;
            for (int i = 0; i < value.length(); i++) {
                if (bytes[bodyOffset() + i] != value.charAt(i)) return false;
            }
            return true;
        }
    }

    private static final class ExistingComments {
        final String vendor;
        final List<String> comments = new ArrayList<>();
        final byte[] suffix;
        private final byte[] packet;
        private int position = 8;

        ExistingComments(byte[] packet) throws IOException {
            this.packet = packet;
            if (packet.length < 16 || !"OpusTags".equals(new String(packet, 0, 8, StandardCharsets.US_ASCII))) {
                throw new IOException("Missing OpusTags header.");
            }
            vendor = readString();
            int count = readLength();
            if (count > (packet.length - position) / 4) throw new IOException("Invalid OpusTags comment count.");
            for (int i = 0; i < count; i++) comments.add(readString());
            // RFC 7845: preserve a non-padding binary extension whose first
            // octet has bit zero set. Ordinary padding may be discarded.
            suffix = position < packet.length && (packet[position] & 1) != 0
                    ? Arrays.copyOfRange(packet, position, packet.length) : new byte[0];
        }

        private int readLength() throws IOException {
            if (position > packet.length - 4) throw new IOException("Truncated OpusTags field.");
            long value = get32(packet, position) & 0xffffffffL;
            position += 4;
            if (value > packet.length - position) throw new IOException("Invalid OpusTags field length.");
            return (int) value;
        }

        private String readString() throws IOException {
            int size = readLength();
            String value = new String(packet, position, size, StandardCharsets.UTF_8);
            position += size;
            return value;
        }
    }
}
