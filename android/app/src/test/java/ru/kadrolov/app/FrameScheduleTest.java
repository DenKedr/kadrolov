package ru.kadrolov.app;
import org.junit.Test;
import static org.junit.Assert.*;
public class FrameScheduleTest {
 @Test public void staticSourceStillProducesFramesAndFinalTail(){
  FrameSchedule f=new FrameSchedule(30);assertEquals(-1,f.next(0,false,false));
  assertEquals(100_000_000L,f.next(100_000_000L,true,false));
  assertEquals(2_000_000_000L,f.next(2_000_000_000L,true,false));
  assertEquals(2_005_000_000L,f.finish(2_005_000_000L,true,false));
  assertEquals(-1,f.finish(2_005_000_000L,true,false));
 }
 @Test public void pauseDoesNotEncodeCameraOrStaticFrames(){
  FrameSchedule f=new FrameSchedule(30);RecordingTimeline t=new RecordingTimeline();t.start(0);
  f.next(1_000_000_000L,true,false);t.pause(1_000_000_000L);
  assertEquals(-1,f.next(5_000_000_000L,true,true));t.resume(6_000_000_000L);
  assertEquals(6_100_000_000L,f.next(6_100_000_000L,true,false));assertEquals(1_100_000L,t.elapsedUs(6_100_000_000L));
 }
 @Test public void pacingDoesNotDoubleDropAtRoundedTimerIntervals(){
  FrameSchedule f=new FrameSchedule(30);f.next(0,true,false);assertEquals(34_000_000L,f.next(34_000_000L,true,false));
  assertTrue(f.delayNanos(34_000_000L)<33_000_000L);
  assertEquals(-1,f.next(35_000_000L,true,false));
 }
}
