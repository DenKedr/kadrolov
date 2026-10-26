package ru.kadrolov.app;
import org.junit.Test;
import static org.junit.Assert.*;
public class RecordingTimelineTest {
 @Test public void pauseFreezesAndResumeExcludesGap(){
  RecordingTimeline t=new RecordingTimeline();t.start(1_000_000_000L);
  t.pause(2_000_000_000L);assertEquals(1_000_000,t.elapsedUs(8_000_000_000L));
  t.resume(8_000_000_000L);assertEquals(2_000_000,t.elapsedUs(9_000_000_000L));
 }
 @Test public void repeatedPauseDoesNotResetBoundary(){
  RecordingTimeline t=new RecordingTimeline();t.start(0);t.pause(10_000);t.pause(20_000);t.resume(30_000);
  assertEquals(20,t.elapsedUs(40_000));
 }
 @Test public void elapsedNeverNegative(){RecordingTimeline t=new RecordingTimeline();t.start(5000);assertEquals(0,t.elapsedUs(1000));}
}
