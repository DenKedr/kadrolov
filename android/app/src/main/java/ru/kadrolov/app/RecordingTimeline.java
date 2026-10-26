package ru.kadrolov.app;
/** Monotonic session clock shared by video, audio and visible controls. */
public final class RecordingTimeline {
 private long start, pausedAt, excluded;
 private boolean paused;
 public synchronized void start(long now) {start=now;pausedAt=0;excluded=0;paused=false;}
 public synchronized void pause(long now) {if(!paused){pausedAt=now;paused=true;}}
 public synchronized void resume(long now) {if(paused){excluded+=Math.max(0,now-pausedAt);paused=false;}}
 public synchronized long elapsedUs(long now) {return Math.max(0,((paused?pausedAt:now)-start-excluded)/1000);}
 public synchronized boolean isPaused(){return paused;}
}
