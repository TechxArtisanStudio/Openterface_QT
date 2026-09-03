# Terminal Idle Detection — Combined Heuristic Approach

## Overview

This feature detects whether a target terminal (connected via the Openterface KVM)
is waiting for user input by combining **three independent signals**:

1. **Cursor blink detection** — temporal frame differencing to find a small region toggling on/off at a fixed position
2. **Screen stability** — measuring total change ratio to distinguish idle from active output
3. **Shell prompt detection** — OCR on the bottom line to find common prompt patterns (`$ `, `# `, `> `, etc.)

No single signal is sufficient on its own. A cursor blinks during command output too;
a stable screen could be frozen; a prompt might not be recognized. By combining all three,
the system achieves robust detection across a wide range of terminal states.

**Status**: Test implementation (v2). Exposed as the `detect_cursor` MCP tool.

## Problem Statement

When an AI agent interacts with a remote terminal through the KVM, it needs to know:
- "Has this command finished running?"
- "Is the terminal ready for my next command?"
- "Where is the cursor so I can type?"

Without this knowledge, the agent must either:
- Wait an arbitrary amount of time (wasteful, unreliable)
- Parse OCR output looking for shell prompts (brittle, language-specific)
- Use vision models (expensive, slow)

## Why a Single Signal Isn't Enough

### Cursor Blink Alone → False Positives

A cursor blinks continuously — even while a command is running. During `sleep 30` or
`ping -c 100`, the cursor is still blinking at the prompt line. Detecting a blink alone
cannot distinguish "idle at prompt" from "waiting for slow command to finish."

### Screen Stability Alone → Ambiguous

A screen with a non-blinking cursor (or a frozen terminal) is perfectly stable. But so
is a terminal that's paused mid-output. Stability tells you "nothing is happening" but
not whether the terminal is *ready*.

### Prompt Detection Alone → Fragile

OCR on the bottom line can find `$ ` or `# `, but:
- Custom prompts (`➜ `, `λ `, emoji) won't match
- The last line might be the final output line, not the prompt
- OCR misreads characters (`5` instead of `$`, etc.)

### Combined → Robust

By requiring multiple signals to agree, we reduce false positives:

| Scenario | Cursor Blink | Screen Stable | Prompt Found | Verdict |
|----------|-------------|---------------|--------------|---------|
| Idle at shell prompt | ✅ (usually) | ✅ | ✅ (usually) | **idle** |
| Command still outputting | ❌ (or ✅ at prompt line) | ❌ | ❌ | **outputting** |
| Non-blinking cursor at prompt | ❌ | ✅ | ✅ | **likely_idle** |
| Frozen / paused terminal | ❌ | ✅ | ❌ | **likely_idle** (lower confidence) |

## Approach: Temporal Frame Differencing + Stability + OCR

### Signal 1: Cursor Blink Detection

In a terminal that's waiting for input, consecutive video frames differ in exactly
one small region — the cursor toggling on and off. Everything else is static.

```
  Frame 0: cursor ON  ─┐
  Frame 1: cursor OFF  ├─ diff shows change at cursor position
  Frame 2: cursor ON   ├─ diff shows change at SAME position  ← confirms cursor
  Frame 3: cursor OFF ─┘
```

vs. a terminal still outputting content:

```
  Frame 0: line 10 ─┐
  Frame 1: line 11  ├─ diff shows change at line 11
  Frame 2: line 12  ├─ diff shows change at line 12  ← DIFFERENT position
  Frame 3: line 13 ─┘
```

The key differentiator: a cursor **blinks at a fixed position**, while terminal output
**flows to different positions**.

### Signal 2: Screen Stability

Measure the total fraction of the screen that changed between consecutive frames.
This captures *all* changes, not just cursor-sized ones.

- **< 0.1% change**: screen is essentially static — cursor blink level or nothing
- **0.1% - 5%**: some content changing — could be slow output or a command nearing completion
- **> 5%**: active output — terminal is clearly producing content

The 0.1% threshold is critical: a single line of terminal output on a 1080p screen
changes ~0.1-0.5% of pixels. A cursor blink changes ~0.01-0.03%. Setting the threshold
at 0.1% cleanly separates cursor-level changes from content-level changes.

### Signal 3: Shell Prompt Detection

Crop the bottom 15% of the screen (where the prompt line lives), preprocess for
terminal OCR (grayscale, adaptive threshold, sharpen), and run Tesseract with
`PSM_SINGLE_LINE`. Match the result against known prompt patterns:

- Bash/sh: `$ `, `# `, `user@host:~$ `, `user@host:~# `
- C shell/tcsh: `% `, `host% `
- PowerShell: `PS C:\> `, `PS> `
- Python REPL: `>>> `, `... `
- Node.js REPL: `> `
- MySQL: `mysql> `, `-> `
- Fish: `dir> `

## Algorithm

```
Input: 3+ frames captured at ~350ms intervals

Phase 1: Compute per-frame diffs
  For each consecutive frame pair (f[i], f[i+1]):
    a. Convert both to grayscale (cv::Mat)
    b. Compute pixel difference: diff = cv::absdiff(gray_a, gray_b)
    c. Threshold at 25/255 (filters H.264 compression noise)
    d. Morphological close (3x3 kernel) to merge nearby pixels
    e. Find contours of changed regions
    f. Keep only "small" regions (< 2% of screen, cursor-sized)
    g. Record centroid (via image moments) and bounding box of each region

Phase 2: Screen stability
  For each consecutive frame pair, compute total change ratio using a generous
  10% maxRatio (to capture ALL changes, not just cursor-sized). Take the maximum
  across all pairs. Screen is "stable" if total change < 0.1%.

Phase 3: Shell prompt detection
  Crop bottom 15% of last frame, preprocess, OCR, match against prompt patterns.

Phase 4: Cursor blink matching
  For each small region in diff[0]:
    a. Check if diff[1] has a small region at the same position
       (centroid distance < 30px, bounding boxes overlap within 10px margin)
    b. If yes, check if diff[2] also has a region at the same position
    c. Count how many consecutive diffs show the same position
  Best candidate = position appearing in most consecutive diffs.

Phase 5: Combine signals (decision matrix)
  If screen NOT stable → "outputting"
    (double-check: if cursor blink found but changes at OTHER positions → outputting)
  If screen stable + cursor blink + prompt → "idle" (confidence: cursor_conf + 0.05)
  If screen stable + cursor blink         → "idle" (confidence: cursor_conf)
  If screen stable + prompt               → "likely_idle" (confidence: 0.75)
  If screen stable only                   → "likely_idle" (confidence: 0.50)
  If nothing                              → "unknown"

Cursor confidence from blink matching:
  2 consecutive diffs match: 0.65 (could be coincidence)
  3 diffs match:             0.90 (very likely cursor)
  All diffs match:           0.95 (maximum confidence)
  Bonus +0.05 if region has cursor-like aspect ratio (0.3-1.5 or wide+short)
```

## Usage

### MCP Tool: `detect_cursor`

```json
{
  "name": "detect_cursor",
  "arguments": {
    "samples": 5,
    "interval_ms": 350
  }
}
```

**Parameters:**
| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `samples` | 5 | 3-8 | Number of frames to capture. 5 gives 4 diffs across 1.4s, enough to catch slow blink rates (~2s period). |
| `interval_ms` | 350 | 200-1000 | Milliseconds between captures. 350ms avoids synchronizing with typical 500ms/1000ms/1200ms blink periods. |

**Total detection time**: `(samples - 1) × interval_ms` — about 1.4 seconds with defaults.

**Response:**
```json
{
  "detected": true,
  "status": "idle",
  "confidence": 0.9,
  "description": "Terminal is idle. Blinking cursor at pixel(960, 540) MCP(2048, 2048), screen stable (0.00% change), shell prompt detected (\"$\").",
  "cursor_position": {
    "pixel_x": 960,
    "pixel_y": 540,
    "mcp_x": 2048,
    "mcp_y": 2048,
    "width": 10,
    "height": 20
  },
  "signals": {
    "cursor_blink": true,
    "screen_stable": true,
    "total_change_ratio": 0.0,
    "prompt_detected": true,
    "prompt_text": "$"
  },
  "frames_analyzed": 5,
  "total_duration_ms": 1400
}
```

**Status values:**
| Status | Meaning | Signals |
|--------|---------|---------|
| `idle` | Terminal waiting for input — high confidence | Stable + cursor blink (± prompt) |
| `likely_idle` | Probably waiting for input — moderate confidence | Stable, but cursor blink or prompt not confirmed |
| `outputting` | Terminal producing output — not ready | Change ratio > 0.1% |
| `unknown` | Insufficient data or error | < 3 frames or other error |

**Individual signals** (in `signals` object):
| Signal | Type | Description |
|--------|------|-------------|
| `cursor_blink` | bool | true if a blinking cursor was detected at a fixed position |
| `screen_stable` | bool | true if total change ratio < 0.1% |
| `total_change_ratio` | float | Fraction of screen that changed between frames (0.0 to 1.0) |
| `prompt_detected` | bool | true if a shell prompt pattern was found on the last line |
| `prompt_text` | string | The detected prompt text (only present if `prompt_detected` is true) |

### Recommended Workflow for AI Agents

```
1. Send a command (e.g., keyboard_type_text with "ls -la\n")
2. Wait 1-2 seconds for output to start
3. Call detect_cursor
4. If status == "idle" → terminal is ready, send next command
5. If status == "likely_idle" → probably ready; proceed or wait 1s and retry for higher confidence
6. If status == "outputting" → wait 1 more second, try again
7. If status == "unknown" → check signals object for details; may need more frames
```

## Implementation Details

### Files Changed

| File | Change |
|------|--------|
| `server/mcp/screenAnalyzer.h` | Added `CursorDetectionResult` (with 3 signal fields), `ChangeRegion` structs; declared `detectCursorFromFrames()`, `findSmallChanges()`, `isSamePosition()`, `detectShellPrompt()` |
| `server/mcp/screenAnalyzer.cpp` | Implemented combined detection algorithm (~550 lines) |
| `server/mcp/mcpConstants.h` | Added `MCP_TOOL_DETECT_CURSOR` constant; removed duplicate `MCP_TOOL_SCREEN_TO_MARKDOWN` |
| `server/mcp/mcpToolHandler.h` | Declared `toolDetectCursor()` |
| `server/mcp/mcpToolHandler.cpp` | Added tool definition, dispatch, and implementation (~80 lines) |

### Architecture

The design separates concerns:

```
McpToolHandler::toolDetectCursor()        ← Timing + frame capture
  ├── Captures frames at intervals via CameraManager::getLatestOriginalFrame()
  ├── Calls QThread::msleep() between captures
  └── Passes frames to ScreenAnalyzer

ScreenAnalyzer::detectCursorFromFrames()  ← Combines 3 signals
  ├── findSmallChanges() per frame pair → cursor blink detection
  ├── findSmallChanges() with 10% maxRatio → screen stability
  ├── detectShellPrompt() on last frame → OCR prompt detection
  └── Decision matrix → final status + confidence

ScreenAnalyzer::findSmallChanges()        ← Low-level OpenCV diff
  ├── cv::absdiff + threshold + morphology + contours
  ├── Image moments for accurate centroids
  └── Returns QList<ChangeRegion>

ScreenAnalyzer::detectShellPrompt()       ← OCR-based prompt detection
  ├── Crop bottom 15% of screen
  ├── preprocessForTerminal() (OpenCV)
  ├── Tesseract PSM_SINGLE_LINE
  └── Regex match against known prompt patterns
```

`ScreenAnalyzer` doesn't depend on `CameraManager` — it takes pre-captured frames.
This makes it:
- **Testable**: pass synthetic frames to verify the algorithm
- **Reusable**: could be called from anywhere that has frame data
- **Decoupled**: analysis logic is independent of capture hardware

### Thresholds and Tuning

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| Diff threshold | 25/255 | Higher than `detectChangedRegion`'s 20 to reduce noise. Cursor pixels typically change 100+ (dark↔light). |
| Morphology kernel | 3×3 | Small — cursor changes are tiny, don't merge unrelated regions. |
| Cursor max change ratio | 2% | Cursor is ~0.01% of screen; 2% gives generous headroom while filtering content changes. |
| Stability max change ratio | 10% | Used to capture ALL changes for stability measurement, not just cursor-sized. |
| Stability threshold | 0.1% (0.001f) | Below one line of terminal output (~0.1-0.5%), above cursor noise (~0.01-0.03%). |
| Min region size | 3×3 px | Filters single-pixel noise from H.264 compression. |
| Max cursor size | 80×50 px | Larger than any reasonable cursor. |
| Cursor validation | 4×4 min, 80×50 max | After matching, validate the accumulated region is cursor-sized. |
| Centroid tolerance | 30 px | Accounts for H.264 edge shifting and minor cursor movement. |
| BBox overlap margin | 10 px | Generous overlap check for bounding boxes. |
| Sampling interval | 350 ms | Avoids phase-locking with 500ms/1000ms/1200ms blink periods. 5 frames × 350ms = 1.4s total, gives 4 diffs. |
| Prompt crop height | 15% of screen (~14% actual) | Bottom strip where the prompt line lives. Min 30px. |

## Test Results

Tested against a real terminal through the KVM:

| Scenario | Status | Cursor Blink | Screen Stable | Change Ratio | Prompt | Notes |
|----------|--------|-------------|---------------|--------------|--------|-------|
| Idle at shell prompt | `idle` or `likely_idle` | ✅ (intermittent) | ✅ | 0.00% | ❌ | Cursor blink detected when timing aligns with blink phase |
| During `ping -c 100` | `outputting` | ❌ | ❌ | ~4.4% | ❌ | Correctly detects active output (188 changes at other positions) |
| During `sleep 30` | `likely_idle` | ❌ | ✅ | 0.00% | ❌ | Cursor blinks but not caught in sampling window; screen stable |
| After Ctrl+C from ping | `idle` | ✅ | ✅ | 0.00% | ❌ | Cursor blink caught at pixel(322,596), 70% confidence |

**Key findings:**
- Screen stability is the most reliable signal — always correct in tests
- Cursor blink detection works but is intermittent (depends on blink phase vs. sampling)
- Prompt detection (`prompt=False` in all tests) is not working yet — likely needs
  tuning of OCR preprocessing or regex patterns for the specific terminal

## Limitations

### Known Limitations

1. **Prompt detection not working**: The OCR of the bottom strip + regex matching
   has not successfully detected prompts in testing. Possible causes:
   - Preprocessing not optimized for the specific terminal's font/rendering
   - Prompt format not matching any known pattern
   - Bottom 15% crop may miss the prompt if terminal has a status bar or non-standard layout
   This is the weakest signal and needs further investigation.

2. **Cannot detect non-blinking cursors** (without prompt): The cursor blink signal
   requires temporal change. A solid/steady cursor produces no diffs. Screen stability
   alone gives `likely_idle` with lower confidence (0.5). Future work: add spatial
   cursor shape detection (block/underscore/I-beam) for single-frame detection.

3. **Cursor blink detection is timing-dependent**: If the sampling interval happens
   to align with the cursor blink period (e.g., exactly 500ms for a 1Hz cursor), we
   might always catch the cursor in the same state. The 350ms default is chosen to be
   slightly off from common blink rates, but this is not guaranteed.

4. **False positives from GUI elements**: A blinking element in a GUI application
   (loading spinner, notification LED, caret in a text field) could be detected as
   a "cursor". The size/position heuristics reduce this but don't eliminate it.

5. **H.264 compression**: The MS2109/MS2130 video chip uses H.264 encoding, which
   introduces small per-pixel variations even on static frames. The threshold (25/255)
   and morphological close handle this, but very subtle cursor blinks (low contrast)
   may be missed.

6. **Resolution-dependent**: The absolute size thresholds (80×50 px max) may need
   adjustment for very high resolutions (4K+) where a character is larger in pixels,
   or very low resolutions (640×480) where it's smaller.

### What It Cannot Do

- Read the terminal content (use `screen_to_markdown` with `mode: "terminal"` for that)
- Detect if a *specific* command finished (use `detect_cursor` + `screen_to_markdown` together)
- Work on graphical applications (cursor blink is terminal-specific)
- Detect cursor shape or style (block, underscore, I-beam)
- Detect custom/non-standard shell prompts without regex pattern updates

## Future Work

### Fix Prompt Detection
The most impactful improvement. Investigate why `detectShellPrompt()` returns false:
- Add debug logging of the raw OCR text from the bottom strip
- Test with different preprocessing parameters
- Expand regex patterns for custom prompts (PowerStar, Oh-My-Zsh themes, etc.)
- Consider using `PSM_RAW_LINE` instead of `PSM_SINGLE_LINE`

### Spatial Cursor Detection (Single Frame)
Detect cursor shape (block/underscore/I-beam) in a single frame using contour analysis.
Would complement the temporal approach by handling non-blinking cursors. Could reuse
the existing `detectButtonsVisually()` contour pipeline with cursor-specific size filters.

### Adaptive Thresholds
Auto-detect terminal resolution and adjust size thresholds proportionally. A 4K terminal
has characters that are 2-3× larger in pixels than 1080p.

### Blink Rate Estimation
Instead of just detecting presence/absence, measure the actual blink frequency. This
could help distinguish cursors from other blinking elements (e.g., a 1Hz cursor vs
a 2Hz notification LED).

### Integration with AI Chat
Wire `detect_cursor` into the AI chat tool execution pipeline so agents can
automatically poll for terminal readiness between commands, with configurable
timeout and retry logic.

## Testing

### Manual Testing

1. Start the KVM app with a terminal on the target
2. Connect an MCP client (Claude Desktop, etc.)
3. Call `detect_cursor` when the terminal is idle at a shell prompt
4. Expected: `status: "idle"` or `"likely_idle"`, `signals.screen_stable: true`
5. Run a long command (e.g., `ping -c 100 google.com`) and immediately call `detect_cursor`
6. Expected: `status: "outputting"`, `signals.screen_stable: false`
7. Wait for the command to finish (Ctrl+C or natural end), call `detect_cursor` again
8. Expected: `status: "idle"`, cursor may or may not be detected depending on blink phase

### Edge Cases to Test

- Cursor at different screen positions (corners, edges, center)
- Different terminal themes (dark/light, various cursor colors)
- Different cursor styles (block, underscore, I-beam if supported)
- Different blink rates (system settings may vary)
- High-resolution vs low-resolution targets
- Terminal with split panes (cursor in non-active pane)
- GUI application with a text field (false positive test)
- Terminal with custom prompt (Oh-My-Zsh, Powerline, Starship)

## References

- [OpenCV Frame Differencing](https://docs.opencv.org/4.x/d7/df3/group/imgproc__motion.html)
- [Tesseract OCR Terminal Mode](https://tesseract-ocr.github.io/tessdoc/ImproveQuality.html)
- [MCP Server Documentation](MCP_SERVER.md)
- `screenAnalyzer.h/cpp` — implementation details in code comments
