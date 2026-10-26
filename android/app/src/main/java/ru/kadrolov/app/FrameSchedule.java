package ru.kadrolov.app;
/** Monotonic output pacing independent of screen invalidation/camera frame callbacks. */
public final class FrameSchedule {
 private final long interval;
 private long deadline=0,last=-1;
 public FrameSchedule(int fps){if(fps<1||fps>240)throw new IllegalArgumentException("fps");interval=1_000_000_000L/fps;}
 public long next(long now,boolean ready,boolean paused){
  if(!ready||paused||now<deadline||now<=last)return -1;
  if(last<0)deadline=now+interval;else deadline+=Math.max(1L,(now-deadline)/interval+1L)*interval;
  last=now;return now;
 }
 public long delayNanos(long now){return Math.max(0L,deadline-now);}
 public long finish(long now,boolean ready,boolean paused){
  if(!ready||paused||now<=last)return -1;
  last=now;return now;
 }
}
