import ru.kadrolov.app.RecordingTimeline;
public class TimelineSmoke {
 public static void main(String[] args) {
  RecordingTimeline t=new RecordingTimeline(); t.start(1_000_000_000L);
  if(t.elapsedUs(2_000_000_000L)!=1_000_000L)throw new AssertionError("elapsed before pause");
  t.pause(2_000_000_000L);
  if(t.elapsedUs(7_000_000_000L)!=1_000_000L)throw new AssertionError("pause must freeze");
  t.resume(7_000_000_000L);
  if(t.elapsedUs(8_000_000_000L)!=2_000_000L)throw new AssertionError("exclude pause");
  t.pause(9_000_000_000L); t.pause(10_000_000_000L); t.resume(11_000_000_000L);
  if(t.elapsedUs(12_000_000_000L)!=4_000_000L)throw new AssertionError("idempotent pause");
  System.out.println("TimelineSmoke: 4 assertions passed");
 }
}
