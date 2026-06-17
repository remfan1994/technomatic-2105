package vip.thatiam.technomatic2105;

public final class NativeAudio {
    static {
        System.loadLibrary("technomatic_2105");
    }

    private NativeAudio() {
    }

    public static native boolean start();
    public static native void stop();
    public static native void next();
    public static native void forceNew();
    public static native void setPieceLengthSeconds(int seconds);
    public static native void setGenreMask(int mask);
    public static native void setGenreBlendMode(int mode);
    public static native void setGenreStateAndForceNew(int mask, int mode);
    public static native String currentSongData();
    public static native String historyData();
    public static native void clearHistory();
    public static native boolean loadSongData(String data);
    public static native boolean exportPcm16ToFile(String data, int seconds, String path);
    public static native void cancelExportRender();
    public static native int currentGenreMask();
    public static native int currentGenreBlendMode();
    public static native int currentGenreMode();
    public static native double currentElapsedSeconds();
    public static native int currentPieceLengthSeconds();
    public static native boolean isPlaying();
}
