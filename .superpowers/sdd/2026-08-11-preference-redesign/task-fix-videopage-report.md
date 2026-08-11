# Task: fix(VideoPage): expand snapshot to cover all persisted fields

## Status
Done

## Commit
fix(VideoPage): expand snapshot to cover all persisted fields

## Test
Build passes cleanly. No runtime test available; visual verification via Revert button required.

## Fields now covered by snapshot/revert

### Previously covered (4)
1. videoFormatIndex (capture resolution combo)
2. pixelFormatIndex (pixel format combo)
3. fpsIndex (framerate combo)
4. resolution (m_currentResolution)

### Newly added (10)
5. hwAccelIndex — hardware acceleration combo
6. scalingQualityIndex — image/scaling quality combo
7. antialiasing — video antialiasing checkbox
8. textAntialiasing — text antialiasing checkbox
9. smoothTransform — smooth transform checkbox
10. mediaBackendIndex — media backend combo (triggers onMediaBackendChanged on restore)
11. overrideSettings — custom resolution override checkbox
12. customWidth — custom input width
13. customHeight — custom input height
14. gstSinkPriority — GStreamer sink priority text

## Files changed
- ui/preferences/videopage.h — added 10 new m_snap_* members
- ui/preferences/videopage.cpp — expanded captureSnapshot() and revertToSnapshot()
