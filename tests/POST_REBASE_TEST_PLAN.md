# Test Plan: Post-Rebase Merge Validation

## Context

The `feature/mcp-server-improvements` branch was rebased onto the latest `upstream/main`. The rebase resolved conflicts across ~20 files, and the user's local (uncommitted) changes were restored on top. This test plan validates that the merge didn't break anything and that all the integrated features work together correctly.

**Scope of changes:**
- **Upstream (newly rebased in):** firmware MCP tools, tree-based log page, palette-aware preference buttons with drop shadow, SystemKeyBlocker Qt focus improvements, soak test enhancements
- **Local (user's in-progress work):** AI Chat feature (ChatWindow, ChatManager, ChatScreenCapture, ChatSettingsPage, etc.), MCP `screen_to_markdown` tool with Tesseract OCR, configurable paste settings in KeyboardManager, GStreamer backend changes

**Date:** 2026-08-22

---

## Phase 1: Smoke Tests (App Startup & Basic UI)

These verify the rebase didn't break the fundamentals.

| # | Test | Expected Result | Risk Area |
|---|------|-----------------|-----------|
| 1.1 | Launch `openterfaceQT` natively | App starts, main window visible, video pane shows camera feed (if device connected) | Startup conflicts in `mainwindow.cpp` |
| 1.2 | Open **Preferences** dialog (toolbar or menu) | Dialog opens, all pages render without crash | `settingdialog.cpp` had merge conflicts |
| 1.3 | Navigate each Preferences page: Log, Video, Audio, MCP, Target Control, Firmware, EDID | Each page loads, no missing widgets or styling glitches | Multiple preference pages had conflict markers |
| 1.4 | Verify Apply/Revert/Cancel buttons in any preference page | Buttons visible with drop shadow; Apply disabled when clean; orange when dirty; Revert restores snapshot | `preferencepagebase.cpp` had 4 conflict markers resolved |
| 1.5 | Close app cleanly (no crash on exit) | Process exits 0, no segfault | Cleanup/destroy order |

---

## Phase 2: Log Page (Tree-Based Rewrite)

The log page was rewritten from individual checkboxes to a tree view. This was the heaviest conflict area.

| # | Test | Expected Result |
|---|------|-----------------|
| 2.1 | Open Preferences → Log | Tree view shows all log categories grouped (Serial, Input, HID/Chip, Device, Camera/Backend, Audio, Scripts, Server, System, UI) |
| 2.2 | Toggle a category checkbox | Checkbox state changes; Apply button turns orange (dirty) |
| 2.3 | Change a category's log level dropdown | Level updates; dirty state triggered |
| 2.4 | Click **Apply** | Settings persist; log filter rules take effect immediately (visible in app output); Apply button returns to default state |
| 2.5 | Modify settings, then click **Revert** | Values restore to snapshot; Apply becomes disabled |
| 2.6 | Modify settings, then click **Cancel** | Dialog closes; changes discarded |
| 2.7 | Enable file logging, browse to a path, apply | Log file created at specified path |
| 2.8 | Toggle "Inhibit Screen Saver" | Screen saver behavior changes accordingly |
| 2.9 | Toggle "Enable System Key Blocker" | System key blocker state updates |
| 2.10 | Re-launch app after saving log settings | Previously configured log levels persist |

---

## Phase 3: AI Chat Feature (Local In-Progress Work)

| # | Test | Expected Result | Risk Area |
|---|------|-----------------|-----------|
| 3.1 | Toggle Chat Window (toolbar/menu) | Chat window appears as a panel in main window | `mainwindow.cpp` integration |
| 3.2 | Open Chat Settings page in Preferences | Page renders with API endpoint, model, API key fields | `ChatSettingsPage.cpp` |
| 3.3 | Configure a valid OpenAI-compatible endpoint, send a test message | Response appears in chat bubble list | `ChatApiClient.cpp` |
| 3.4 | Send a message with an invalid API key | Error message displayed, no crash | Error handling |
| 3.5 | Screen capture in chat (if available) | Screenshot attached to message | `ChatScreenCapture.cpp` depends on `CameraManager` changes |
| 3.6 | Send multi-turn conversation | History preserved, context carried forward | `ChatConversationBuilder.cpp` |
| 3.7 | Close and reopen app | Chat history persists (if persistence enabled) | `ChatPersistence.cpp` |
| 3.8 | Toggle chat visibility, close app, reopen | Chat visibility state persists | `GlobalSetting` |

---

## Phase 4: MCP Server

| # | Test | Expected Result | Risk Area |
|---|------|-----------------|-----------|
| 4.1 | Start app with MCP server enabled (Preferences → MCP) | Server starts on configured port | `mcpServer.cpp` |
| 4.2 | Connect via SSE transport (`/sse` endpoint) | SSE session established | `mcpSseTransport.cpp` |
| 4.3 | Call `firmware_check` tool via MCP client | Returns firmware status (latest/upgradable/version info) | New tool from upstream, merged into `mcpToolHandler.cpp` |
| 4.4 | Call `screen_to_markdown` tool (with feature enabled) | Returns OCR-analyzed markdown of current screen | New tool from local changes; depends on Tesseract |
| 4.5 | Call `screen_to_markdown` with feature disabled in settings | Returns error message directing user to enable it | Settings integration |
| 4.6 | Call `capture_screen` tool | Returns base64 image | Existing tool, verify still works |
| 4.7 | Exercise all mouse/keyboard tools via MCP client | Each responds correctly | `mcpToolHandler.cpp` dispatch had conflict resolved |

---

## Phase 5: Video / Camera / GStreamer

| # | Test | Expected Result | Risk Area |
|---|------|-----------------|-----------|
| 5.1 | Open Preferences → Video | Resolution/FPS/pixel format dropdowns populated from camera capabilities | `videopage.cpp` had 5 conflict markers |
| 5.2 | Change resolution, click Apply | Video feed updates to new resolution without crash | Capture thread restart logic had conflicts |
| 5.3 | Change FPS, click Apply | Video feed updates, frame rate changes | Same |
| 5.4 | Rapidly switch resolutions | App doesn't crash; previous capture thread fully terminates before new one starts | Thread-termination fix was conflict area |
| 5.5 | Verify GStreamer backend still works (if using gstreamer) | Video renders via gstreamer pipeline | `gstreamerbackendhandler.cpp` had local changes |

---

## Phase 6: Keyboard & Paste

| # | Test | Expected Result | Risk Area |
|---|------|-----------------|-----------|
| 6.1 | Paste text into target (via toolbar/keyboard shortcut) | Characters typed to target sequentially | `KeyboardManager::handlePastingCharacters` had major conflict |
| 6.2 | Paste with configurable batch size/typing delay (check `GlobalSetting` values) | Paste respects configured batch size and inter-batch delay | Uses `getChatBatchSize()` / `getChatTypingDelayMs()` from local changes |
| 6.3 | Paste text with special characters (uppercase, symbols) | Shift/AltGr modifiers applied correctly | Same method |
| 6.4 | SystemKeyBlocker enabled, focus on video pane | System keys (Win, Alt-Tab, etc.) are blocked | `SystemKeyBlocker_win.cpp` focus-check logic had conflict |
| 6.5 | SystemKeyBlocker enabled, focus on a dialog (e.g. Preferences) | System keys pass through to dialog | Same — Qt focus system check |

---

## Phase 7: Soak Test (Automated)

| # | Test | Expected Result |
|---|------|-----------------|
| 7.1 | `./tests/gui_soak_test.sh --native 5 15` (5 min soak, 15s interval) | Runs 5 minutes, produces report in `tests/soak_test_logs/` with PASS/FAIL status |
| 7.2 | Check report contents | Report includes memory stats, crash count, focus loop count, FD/thread leak counts, screenshots |
| 7.3 | Verify no "unknown variable" errors in script | Script uses upstream's comprehensive monitoring functions |

---

## Phase 8: Regression — Edge Cases from Conflict Resolution

These are specific to areas where conflicts were resolved and subtle breakage could hide:

| # | Test | Expected Result |
|---|------|-----------------|
| 8.1 | `cmake/SourceFiles.cmake` — run a cmake build (if applicable) | Configures without parse error |
| 8.2 | PreferencePageBase button styling in both light and dark system themes | Palette-based styles adapt correctly (not hardcoded white) |
| 8.3 | Log page: enable a category, restart app | Category state persisted and restored |
| 8.4 | Target Control page: toggle custom USB descriptors, apply | Settings persist |
| 8.5 | Video page: switch between FFmpeg and GStreamer backends (if both available) | Each backend works independently |

---

## Test Execution Order

1. **Phase 1** — Smoke (must pass first; blocks everything else)
2. **Phase 2** — Log page (highest conflict density)
3. **Phase 5** — Video (thread-safety risk from conflict resolution)
4. **Phase 6** — Keyboard/paste (logic conflict in handlePastingCharacters)
5. **Phase 3** — AI Chat (largest new feature)
6. **Phase 4** — MCP Server
7. **Phase 7** — Soak test (automated, runs last since it's long)
8. **Phase 8** — Regression edge cases (spot checks)

## Sign-off Criteria

- All Phase 1 smoke tests pass
- No crashes or memory leaks during Phase 7 soak test (5+ min)
- All new MCP tools (`firmware_check`, `screen_to_markdown`) callable via MCP client
- AI Chat can send/receive at least one message end-to-end
- Apply/Revert/Cancel work correctly on all preference pages
- Paste to target works with the configurable batch/delay settings

## Notes for Testers

- Phases 1-6 are manual tests — run the app and exercise each scenario
- Phase 7 is automated — run the soak test script and inspect the generated report
- Phase 8 is spot-check regression — focus on areas where conflicts were resolved
- If a test fails, note which phase and test number, plus any console output or crash logs
