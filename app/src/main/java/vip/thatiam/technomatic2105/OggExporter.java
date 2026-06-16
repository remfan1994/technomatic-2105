package vip.thatiam.technomatic2105;

import android.media.MediaCodec;
import android.media.MediaFormat;
import android.media.MediaMuxer;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;

final class OggExporter {
    interface CancellationToken {
        boolean isCancellationRequested();
    }

    private static final int SAMPLE_RATE = 48000;
    private static final int CHANNELS = 2;
    private static final int BYTES_PER_FRAME = CHANNELS * 2;
    private static final int OPUS_FRAME_BYTES = 960 * BYTES_PER_FRAME;
    private static final long CODEC_TIMEOUT_US = 1000L;
    private static final long NO_PROGRESS_TIMEOUT_MS = 30000L;

    private OggExporter() {
    }

    static void encodeRawPcm16ToOgg(File rawPcm, File output, CancellationToken token) throws IOException {
        if (rawPcm == null || output == null) throw new IOException("Missing export file.");
        if (!rawPcm.exists() || rawPcm.length() <= 0L) throw new IOException("Missing rendered PCM data.");
        if ((rawPcm.length() % BYTES_PER_FRAME) != 0L) throw new IOException("Rendered PCM data is not frame-aligned.");

        MediaCodec encoder = null;
        MediaMuxer muxer = null;
        FileInputStream input = null;
        boolean muxerStarted = false;
        boolean encoderStarted = false;
        int trackIndex = -1;
        long submittedFrames = 0L;
        long lastProgressMs = System.currentTimeMillis();

        try {
            checkCancelled(token);

            MediaFormat format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_OPUS, SAMPLE_RATE, CHANNELS);
            format.setInteger(MediaFormat.KEY_BIT_RATE, 128000);
            format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, OPUS_FRAME_BYTES);

            encoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_OPUS);
            encoder.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            encoder.start();
            encoderStarted = true;

            muxer = new MediaMuxer(output.getAbsolutePath(), MediaMuxer.OutputFormat.MUXER_OUTPUT_OGG);
            input = new FileInputStream(rawPcm);

            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            byte[] readBuffer = new byte[OPUS_FRAME_BYTES];
            boolean inputDone = false;
            boolean outputDone = false;

            while (!outputDone) {
                checkCancelled(token);
                boolean progressed = false;

                while (!inputDone) {
                    checkCancelled(token);
                    int inIndex = encoder.dequeueInputBuffer(CODEC_TIMEOUT_US);
                    if (inIndex < 0) break;

                    ByteBuffer in = encoder.getInputBuffer(inIndex);
                    if (in == null) throw new IOException("Encoder input buffer unavailable.");
                    in.clear();
                    int cap = Math.min(in.capacity(), readBuffer.length);
                    cap -= cap % BYTES_PER_FRAME;
                    int read = input.read(readBuffer, 0, cap);
                    long ptsUs = submittedFrames * 1000000L / SAMPLE_RATE;
                    if (read < 0) {
                        encoder.queueInputBuffer(inIndex, 0, 0, ptsUs, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                        inputDone = true;
                    } else {
                        read -= read % BYTES_PER_FRAME;
                        if (read <= 0) continue;
                        in.put(readBuffer, 0, read);
                        encoder.queueInputBuffer(inIndex, 0, read, ptsUs, 0);
                        submittedFrames += read / BYTES_PER_FRAME;
                    }
                    progressed = true;
                    lastProgressMs = System.currentTimeMillis();
                }

                boolean outputAvailable = true;
                while (outputAvailable) {
                    checkCancelled(token);
                    int outIndex = encoder.dequeueOutputBuffer(info, CODEC_TIMEOUT_US);
                    if (outIndex == MediaCodec.INFO_TRY_AGAIN_LATER) {
                        outputAvailable = false;
                    } else if (outIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                        if (muxerStarted) throw new IOException("Encoder format changed twice.");
                        trackIndex = muxer.addTrack(encoder.getOutputFormat());
                        muxer.start();
                        muxerStarted = true;
                        progressed = true;
                        lastProgressMs = System.currentTimeMillis();
                    } else if (outIndex >= 0) {
                        ByteBuffer out = encoder.getOutputBuffer(outIndex);
                        if (out != null && info.size > 0) {
                            if (!muxerStarted) throw new IOException("Muxer not started.");
                            out.position(info.offset);
                            out.limit(info.offset + info.size);
                            // Use the encoder's timestamps. The earlier manual 20 ms restamp
                            // caused device-dependent duration errors and stalled exports.
                            muxer.writeSampleData(trackIndex, out, info);
                        }
                        outputDone = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
                        encoder.releaseOutputBuffer(outIndex, false);
                        progressed = true;
                        lastProgressMs = System.currentTimeMillis();
                    }
                }

                if (!progressed && System.currentTimeMillis() - lastProgressMs > NO_PROGRESS_TIMEOUT_MS) {
                    throw new IOException("OGG encoder stopped making progress.");
                }
            }
        } finally {
            if (input != null) {
                try { input.close(); } catch (IOException ignored) {}
            }
            if (encoder != null) {
                try { if (encoderStarted) encoder.stop(); } catch (Exception ignored) {}
                try { encoder.release(); } catch (Exception ignored) {}
            }
            if (muxer != null) {
                try { if (muxerStarted) muxer.stop(); } catch (Exception ignored) {}
                try { muxer.release(); } catch (Exception ignored) {}
            }
        }
    }

    private static void checkCancelled(CancellationToken token) throws IOException {
        if (token != null && token.isCancellationRequested()) {
            throw new IOException("Export cancelled.");
        }
    }
}
