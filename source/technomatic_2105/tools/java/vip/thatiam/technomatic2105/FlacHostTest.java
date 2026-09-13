package vip.thatiam.technomatic2105;
import java.io.File;
public final class FlacHostTest {
  public static void main(String[] args) throws Exception {
    FlacExporter.Metadata m = new FlacExporter.Metadata(
      "track1", "Technomatic 2105", "Technomatic 2105",
      "AUGUST 15 2026", "No Channel",
      "Generated locally by Technomatic 2105; Seed: 3290437499; Start: 00:00:10; End: 00:00:18");
    FlacExporter.encodeRawPcm16ToFlac(new File(args[0]), new File(args[1]), m, () -> false);
  }
}
