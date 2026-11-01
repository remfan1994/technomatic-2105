package vip.thatiam.technomatic2105;
import java.io.File;
public final class FlacHostTest {
  public static void main(String[] args) throws Exception {
    ExportMetadata m = new ExportMetadata(
      "track1", "Technomatic 2105", "Technomatic 2105",
      "AUGUST 15 2026", "No Channel",
      "Generated locally by Technomatic 2105", "2026-08-15",
      "technomatic2105-v1;seed=3290437499;seconds=18;gmask=0;gblend=0;gprimary=0;gmode=0",
      new String[] {"No Channel", "Chrome Pulse", "Velvet Circuit", "Glass Trap", "Dust Machine",
        "Liquid Grid", "Neon Drift", "Broken Speaker", "Deep Magnet", "Pixel Ritual",
        "Soft Voltage", "Heavy Orbit", "Cold Arcade"}, 10, 18);
    FlacExporter.encodeRawPcm16ToFlac(new File(args[0]), new File(args[1]), m, () -> false);
  }
}
