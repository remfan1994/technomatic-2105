package vip.thatiam.radiobreaker;

public final class NativeAudio {
    static {
        System.loadLibrary("radio_breaker");
    }

    private NativeAudio() {
    }

    public static native boolean start();
    public static native void stop();
    public static native void next();
    public static native void setPieceLengthSeconds(int seconds);
    public static native boolean isPlaying();
}
