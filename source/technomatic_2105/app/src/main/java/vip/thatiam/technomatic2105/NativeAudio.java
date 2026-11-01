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
    /** Retired channel API: no musical effect; numeric results are always zero. */
    @Deprecated
    public static native void setGenreMask(int mask);
    /** Retired channel API: no musical effect; numeric results are always zero. */
    @Deprecated
    public static native void setGenreBlendMode(int mode);
    /** Retired channel API: no musical effect; numeric results are always zero. */
    @Deprecated
    public static native void setGenrePrimary(int mode);
    /** Retired channel API: no musical effect; numeric results are always zero. */
    @Deprecated
    public static native void setGenreStateAndRerenderCurrent(int mask, int mode, int primary);
    public static native String currentSongData();
    public static native String historyData();
    public static native void clearHistory();
    public static native boolean loadSongData(String data);
    public static native boolean exportPcm16ToFile(String data, int seconds, String path);
    public static native boolean exportPcm16RangeToFile(String data, int startSeconds, int endSeconds, String path);
    public static native void cancelExportRender();
    /** Retired channel API: no musical effect; numeric results are always zero. */
    @Deprecated
    public static native int currentGenreMask();
    /** Retired channel API: no musical effect; numeric results are always zero. */
    @Deprecated
    public static native int currentGenreBlendMode();
    /** Retired channel API: no musical effect; numeric results are always zero. */
    @Deprecated
    public static native int currentGenrePrimary();
    /** Retired channel API: no musical effect; numeric results are always zero. */
    @Deprecated
    public static native int currentGenreMode();
    public static native double currentElapsedSeconds();
    public static native boolean isPlaying();
}
