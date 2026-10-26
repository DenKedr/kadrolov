import ru.kadrolov.app.FrameSchedule;
import ru.kadrolov.app.RecordingTimeline;
public final class StaticFrameSmoke {
 public static void main(String[] args){
  FrameSchedule frames=new FrameSchedule(10);RecordingTimeline clock=new RecordingTimeline();clock.start(0);
  if(frames.next(0,false,false)!=-1)throw new AssertionError("no source frame yet");
  if(frames.next(100_000_000L,true,false)!=100_000_000L)throw new AssertionError("first screen frame");
  // No subsequent screen invalidation: camera and static tail must still be encoded.
  if(frames.next(1_000_000_000L,true,false)!=1_000_000_000L)throw new AssertionError("static screen must keep ticking");
  clock.pause(1_000_000_000L);
  if(frames.next(6_000_000_000L,true,true)!=-1)throw new AssertionError("no paused frames");
  clock.resume(6_000_000_000L);
  if(frames.next(6_100_000_000L,true,false)!=6_100_000_000L)throw new AssertionError("resume fresh monotonic timestamp");
  if(clock.elapsedUs(6_100_000_000L)!=1_100_000L)throw new AssertionError("pause gap excluded");
  if(frames.finish(6_150_000_000L,true,false)!=6_150_000_000L)throw new AssertionError("encode exact final static tail");
  if(frames.finish(6_150_000_000L,true,false)!=-1)throw new AssertionError("no duplicate PTS");
  System.out.println("StaticFrameSmoke: 8 assertions passed");
 }
}
