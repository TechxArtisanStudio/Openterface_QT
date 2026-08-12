# Preferences Dialog Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Merge SettingDialog and AdvancedSettingsDialog into a unified Preferences dialog with per-page Apply/Revert/Cancel buttons for settings pages.

**Architecture:** Each settings page (LogPage, VideoPage, AudioPage, TargetControlPage, McpPage) gets its own 3-button bar at the bottom. The buttons use a snapshot mechanism: Revert restores widget values to the state when the page was opened. SettingDialog is extended to include all 9 pages (4 original + 5 migrated from AdvancedSettingsDialog). The global button bar is removed.

**Tech Stack:** Qt 6.5.3, C++, CMake

## Global Constraints

- Revert button semantics: snapshot mode (B) — record values when page opens, Revert restores to that snapshot. Apply does NOT update the snapshot.
- Button names: Apply (应用) / Revert (恢复) / Cancel (关闭)
- Settings pages get 3 buttons; action pages (Firmware, Control Chip Firmware, EDID, Virtual Keyboard) keep existing buttons
- All 9 pages merged into a single flat tree (no General/Advanced distinction)
- Class name remains `SettingDialog` (no rename)

---

## File Structure

**Files to modify:**
- `ui/preferences/settingdialog.h` — Add 5 migrated page members, remove global button members, add helper methods
- `ui/preferences/settingdialog.cpp` — Merge page creation logic, remove global buttons, update changePage()
- `ui/preferences/logpage.h` — Add snapshot methods and members
- `ui/preferences/logpage.cpp` — Implement snapshot + add button bar
- `ui/preferences/videopage.h` — Add snapshot methods and members
- `ui/preferences/videopage.cpp` — Implement snapshot + add button bar
- `ui/preferences/audiopage.h` — Add snapshot methods and members
- `ui/preferences/audiopage.cpp` — Implement snapshot + add button bar
- `ui/preferences/targetcontrolpage.h` — Add snapshot methods and members
- `ui/preferences/targetcontrolpage.cpp` — Implement snapshot + add button bar
- `ui/preferences/mcppage.h` — Add snapshot methods and members
- `ui/preferences/mcppage.cpp` — Implement snapshot + add button bar
- `ui/mainwindow.h` — Remove AdvancedSettingsDialog references
- `ui/mainwindow.cpp` — Merge signal connections, remove configureAdvancedSettings()
- `CMakeLists.txt` — Remove advancedsettingsdialog files

**Files to delete:**
- `ui/preferences/advancedsettingsdialog.h`
- `ui/preferences/advancedsettingsdialog.cpp`

**Files unchanged:**
- `ui/preferences/firmwarepage.h/cpp` — Action page, keeps existing buttons
- `ui/preferences/controlchipfirmwarepage.h/cpp` — Action page, keeps existing buttons
- `ui/preferences/edidconfigpage.h/cpp` — Action page, keeps existing buttons
- `ui/customkey/virtualkeyboardpage.h/cpp` — Action page, keeps existing buttons

---

### Task 1: Add Snapshot and Buttons to LogPage

**Files:**
- Modify: `ui/preferences/logpage.h:56-79`
- Modify: `ui/preferences/logpage.cpp:1-200`

**Interfaces:**
- Consumes: Existing LogPage widget members (coreCheckBox, serialCheckBox, etc.)
- Produces: `captureSnapshot()`, `revertToSnapshot()` methods; button bar with Apply/Revert/Cancel

- [ ] **Step 1: Add snapshot member variables to logpage.h**

Add after line 73 (after `QCheckBox *systemKeyBlockerCheckBox;`):

```cpp
    // Snapshot members for Revert functionality
    bool m_snap_coreLog;
    bool m_snap_serialLog;
    bool m_snap_uiLog;
    bool m_snap_hostLog;
    bool m_snap_deviceLog;
    bool m_snap_backendLog;
    bool m_snap_scriptLog;
    bool m_snap_storeLog;
    QString m_snap_logFilePath;
    bool m_snap_screenSaver;
    bool m_snap_hideKeyboardInput;
    bool m_snap_floatingWindow;
    int m_snap_floatingWindowOpacity;
    bool m_snap_systemKeyBlocker;
```

- [ ] **Step 2: Add snapshot method declarations to logpage.h**

Add after line 47 (after `void applyLogsettings();`):

```cpp
    void captureSnapshot();
    void revertToSnapshot();
```

- [ ] **Step 3: Implement captureSnapshot() in logpage.cpp**

Add at the end of the file:

```cpp
void LogPage::captureSnapshot()
{
    m_snap_coreLog = coreCheckBox->isChecked();
    m_snap_serialLog = serialCheckBox->isChecked();
    m_snap_uiLog = uiCheckBox->isChecked();
    m_snap_hostLog = hostCheckBox->isChecked();
    m_snap_deviceLog = deviceCheckBox->isChecked();
    m_snap_backendLog = backendCheckBox->isChecked();
    m_snap_scriptLog = scriptCheckBox->isChecked();
    m_snap_storeLog = storeLogCheckBox->isChecked();
    m_snap_logFilePath = logFilePathLineEdit->text();
    m_snap_screenSaver = screenSaverCheckBox->isChecked();
    m_snap_hideKeyboardInput = hideKeyboardInputCheckBox->isChecked();
    m_snap_floatingWindow = floatingWindowCheckBox->isChecked();
    m_snap_floatingWindowOpacity = floatingWindowOpacitySlider->value();
    m_snap_systemKeyBlocker = systemKeyBlockerCheckBox->isChecked();
}
```

- [ ] **Step 4: Implement revertToSnapshot() in logpage.cpp**

Add after captureSnapshot():

```cpp
void LogPage::revertToSnapshot()
{
    coreCheckBox->setChecked(m_snap_coreLog);
    serialCheckBox->setChecked(m_snap_serialLog);
    uiCheckBox->setChecked(m_snap_uiLog);
    hostCheckBox->setChecked(m_snap_hostLog);
    deviceCheckBox->setChecked(m_snap_deviceLog);
    backendCheckBox->setChecked(m_snap_backendLog);
    scriptCheckBox->setChecked(m_snap_scriptLog);
    storeLogCheckBox->setChecked(m_snap_storeLog);
    logFilePathLineEdit->setText(m_snap_logFilePath);
    screenSaverCheckBox->setChecked(m_snap_screenSaver);
    hideKeyboardInputCheckBox->setChecked(m_snap_hideKeyboardInput);
    floatingWindowCheckBox->setChecked(m_snap_floatingWindow);
    floatingWindowOpacitySlider->setValue(m_snap_floatingWindowOpacity);
    systemKeyBlockerCheckBox->setChecked(m_snap_systemKeyBlocker);
}
```

- [ ] **Step 5: Call captureSnapshot() at the end of initLogSettings()**

In logpage.cpp, find the `initLogSettings()` method and add at the very end (before the closing brace):

```cpp
    captureSnapshot();
```

- [ ] **Step 6: Add button bar to LogPage in setupUI()**

At the end of `setupUI()` in logpage.cpp, add:

```cpp
    // Button bar: Apply / Revert / Cancel
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    QPushButton *applyButton = new QPushButton(tr("Apply"));
    QPushButton *revertButton = new QPushButton(tr("Revert"));
    QPushButton *cancelButton = new QPushButton(tr("Cancel"));
    
    applyButton->setFixedSize(80, 30);
    revertButton->setFixedSize(80, 30);
    cancelButton->setFixedSize(80, 30);
    
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(revertButton);
    buttonLayout->addWidget(cancelButton);
    
    logLayout->addLayout(buttonLayout);
    
    // Connect buttons
    connect(applyButton, &QPushButton::clicked, this, &LogPage::applyLogsettings);
    connect(revertButton, &QPushButton::clicked, this, &LogPage::revertToSnapshot);
    connect(cancelButton, &QPushButton::clicked, this, [this]() {
        QDialog *dlg = qobject_cast<QDialog*>(window());
        if (dlg) dlg->reject();
    });
```

- [ ] **Step 7: Build and test**

Run: `cmake --build build`
Expected: Build succeeds

Manual test: Open Preferences → General page → verify 3 buttons appear at bottom → change a checkbox → click Revert → verify checkbox restores → click Apply → verify setting saves → click Cancel → verify dialog closes

- [ ] **Step 8: Commit**

```bash
git add ui/preferences/logpage.h ui/preferences/logpage.cpp
git commit -m "feat(LogPage): add per-page Apply/Revert/Cancel buttons with snapshot mechanism

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 2: Add Snapshot and Buttons to VideoPage

**Files:**
- Modify: `ui/preferences/videopage.h:75-92`
- Modify: `ui/preferences/videopage.cpp:1-200`

**Interfaces:**
- Consumes: Existing VideoPage widget members (videoFormatBox, pixelFormatBox, etc.)
- Produces: `captureSnapshot()`, `revertToSnapshot()` methods; button bar

- [ ] **Step 1: Add snapshot member variables to videopage.h**

Add after line 78 (after `QComboBox *pixelFormatBox;`):

```cpp
    // Snapshot members
    int m_snap_videoFormatIndex;
    int m_snap_pixelFormatIndex;
    int m_snap_fpsIndex;
    QSize m_snap_resolution;
```

- [ ] **Step 2: Add snapshot method declarations to videopage.h**

Add after line 60 (after `void applyVideoSettings();`):

```cpp
    void captureSnapshot();
    void revertToSnapshot();
```

- [ ] **Step 3: Implement captureSnapshot() in videopage.cpp**

Add at the end of the file:

```cpp
void VideoPage::captureSnapshot()
{
    m_snap_videoFormatIndex = videoFormatBox->currentIndex();
    m_snap_pixelFormatIndex = pixelFormatBox->currentIndex();
    m_snap_fpsIndex = fpsComboBox->currentIndex();
    m_snap_resolution = m_currentResolution;
}
```

- [ ] **Step 4: Implement revertToSnapshot() in videopage.cpp**

Add after captureSnapshot():

```cpp
void VideoPage::revertToSnapshot()
{
    videoFormatBox->setCurrentIndex(m_snap_videoFormatIndex);
    pixelFormatBox->setCurrentIndex(m_snap_pixelFormatIndex);
    fpsComboBox->setCurrentIndex(m_snap_fpsIndex);
    m_currentResolution = m_snap_resolution;
}
```

- [ ] **Step 5: Call captureSnapshot() at the end of initVideoSettings()**

In videopage.cpp, find `initVideoSettings()` and add at the very end:

```cpp
    captureSnapshot();
```

- [ ] **Step 6: Add button bar to VideoPage in setupUI()**

At the end of `setupUI()` in videopage.cpp, add:

```cpp
    // Button bar: Apply / Revert / Cancel
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    QPushButton *applyButton = new QPushButton(tr("Apply"));
    QPushButton *revertButton = new QPushButton(tr("Revert"));
    QPushButton *cancelButton = new QPushButton(tr("Cancel"));
    
    applyButton->setFixedSize(80, 30);
    revertButton->setFixedSize(80, 30);
    cancelButton->setFixedSize(80, 30);
    
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(revertButton);
    buttonLayout->addWidget(cancelButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Connect buttons
    connect(applyButton, &QPushButton::clicked, this, &VideoPage::applyVideoSettings);
    connect(revertButton, &QPushButton::clicked, this, &VideoPage::revertToSnapshot);
    connect(cancelButton, &QPushButton::clicked, this, [this]() {
        QDialog *dlg = qobject_cast<QDialog*>(window());
        if (dlg) dlg->reject();
    });
```

- [ ] **Step 7: Build and test**

Run: `cmake --build build`
Expected: Build succeeds

Manual test: Open Preferences → Video page → verify 3 buttons → change resolution → click Revert → verify restores

- [ ] **Step 8: Commit**

```bash
git add ui/preferences/videopage.h ui/preferences/videopage.cpp
git commit -m "feat(VideoPage): add per-page Apply/Revert/Cancel buttons with snapshot mechanism

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 3: Add Snapshot and Buttons to AudioPage

**Files:**
- Modify: `ui/preferences/audiopage.h:50-70`
- Modify: `ui/preferences/audiopage.cpp:1-200`

**Interfaces:**
- Consumes: Existing AudioPage widget members (audioCodecBox, audioSampleRateBox, etc.)
- Produces: `captureSnapshot()`, `revertToSnapshot()` methods; button bar

- [ ] **Step 1: Add snapshot member variables to audiopage.h**

Add after line 69 (after `QLabel *volumeValueLabel;`):

```cpp
    // Snapshot members
    int m_snap_audioCodecIndex;
    int m_snap_sampleRate;
    int m_snap_quality;
    int m_snap_containerFormatIndex;
    int m_snap_audioDeviceIndex;
    int m_snap_audioBitrate;
    bool m_snap_enableAudio;
    int m_snap_volume;
```

- [ ] **Step 2: Add snapshot method declarations to audiopage.h**

Add after line 45 (after `void saveSettings();`):

```cpp
    void captureSnapshot();
    void revertToSnapshot();
```

- [ ] **Step 3: Implement captureSnapshot() in audiopage.cpp**

Add at the end of the file:

```cpp
void AudioPage::captureSnapshot()
{
    m_snap_audioCodecIndex = audioCodecBox->currentIndex();
    m_snap_sampleRate = audioSampleRateBox->value();
    m_snap_quality = qualitySlider->value();
    m_snap_containerFormatIndex = containerFormatBox->currentIndex();
    m_snap_audioDeviceIndex = audioDeviceComboBox->currentIndex();
    m_snap_audioBitrate = audioBitrateBox->value();
    m_snap_enableAudio = enableAudioCheckBox->isChecked();
    m_snap_volume = volumeSlider->value();
}
```

- [ ] **Step 4: Implement revertToSnapshot() in audiopage.cpp**

Add after captureSnapshot():

```cpp
void AudioPage::revertToSnapshot()
{
    audioCodecBox->setCurrentIndex(m_snap_audioCodecIndex);
    audioSampleRateBox->setValue(m_snap_sampleRate);
    qualitySlider->setValue(m_snap_quality);
    containerFormatBox->setCurrentIndex(m_snap_containerFormatIndex);
    audioDeviceComboBox->setCurrentIndex(m_snap_audioDeviceIndex);
    audioBitrateBox->setValue(m_snap_audioBitrate);
    enableAudioCheckBox->setChecked(m_snap_enableAudio);
    volumeSlider->setValue(m_snap_volume);
}
```

- [ ] **Step 5: Call captureSnapshot() at the end of loadSettings()**

In audiopage.cpp, find `loadSettings()` and add at the very end:

```cpp
    captureSnapshot();
```

- [ ] **Step 6: Add button bar to AudioPage in setupUI()**

At the end of `setupUI()` in audiopage.cpp, add:

```cpp
    // Button bar: Apply / Revert / Cancel
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    QPushButton *applyButton = new QPushButton(tr("Apply"));
    QPushButton *revertButton = new QPushButton(tr("Revert"));
    QPushButton *cancelButton = new QPushButton(tr("Cancel"));
    
    applyButton->setFixedSize(80, 30);
    revertButton->setFixedSize(80, 30);
    cancelButton->setFixedSize(80, 30);
    
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(revertButton);
    buttonLayout->addWidget(cancelButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Connect buttons
    connect(applyButton, &QPushButton::clicked, this, &AudioPage::saveSettings);
    connect(revertButton, &QPushButton::clicked, this, &AudioPage::revertToSnapshot);
    connect(cancelButton, &QPushButton::clicked, this, [this]() {
        QDialog *dlg = qobject_cast<QDialog*>(window());
        if (dlg) dlg->reject();
    });
```

- [ ] **Step 7: Build and test**

Run: `cmake --build build`
Expected: Build succeeds

Manual test: Open Preferences → Audio page → verify 3 buttons → change volume → click Revert → verify restores

- [ ] **Step 8: Commit**

```bash
git add ui/preferences/audiopage.h ui/preferences/audiopage.cpp
git commit -m "feat(AudioPage): add per-page Apply/Revert/Cancel buttons with snapshot mechanism

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 4: Add Snapshot and Buttons to TargetControlPage

**Files:**
- Modify: `ui/preferences/targetcontrolpage.h:51-83`
- Modify: `ui/preferences/targetcontrolpage.cpp:1-200`

**Interfaces:**
- Consumes: Existing TargetControlPage widget members (VIDLineEdit, PIDLineEdit, operatingModeGroup, etc.)
- Produces: `captureSnapshot()`, `revertToSnapshot()` methods; button bar

- [ ] **Step 1: Add snapshot member variables to targetcontrolpage.h**

Add after line 82 (after `int originalOperatingMode;`):

```cpp
    // Snapshot members
    bool m_snap_vidChecked;
    bool m_snap_pidChecked;
    QString m_snap_vidText;
    QString m_snap_pidText;
    QString m_snap_vidDescriptor;
    QString m_snap_pidDescriptor;
    bool m_snap_usbSerialNumberChecked;
    QString m_snap_serialNumberText;
    bool m_snap_usbCustomStringChecked;
    int m_snap_operatingMode;
```

- [ ] **Step 2: Add snapshot method declarations to targetcontrolpage.h**

Add after line 48 (after `void initHardwareSetting();`):

```cpp
    void captureSnapshot();
    void revertToSnapshot();
```

- [ ] **Step 3: Implement captureSnapshot() in targetcontrolpage.cpp**

Add at the end of the file:

```cpp
void TargetControlPage::captureSnapshot()
{
    m_snap_vidChecked = VIDCheckBox->isChecked();
    m_snap_pidChecked = PIDCheckBox->isChecked();
    m_snap_vidText = VIDLineEdit->text();
    m_snap_pidText = PIDLineEdit->text();
    m_snap_vidDescriptor = VIDDescriptorLineEdit->text();
    m_snap_pidDescriptor = PIDDescriptorLineEdit->text();
    m_snap_usbSerialNumberChecked = USBSerialNumberCheckBox->isChecked();
    m_snap_serialNumberText = serialNumberLineEdit->text();
    m_snap_usbCustomStringChecked = USBCustomStringDescriptorCheckBox->isChecked();
    m_snap_operatingMode = operatingModeGroup->checkedId();
}
```

- [ ] **Step 4: Implement revertToSnapshot() in targetcontrolpage.cpp**

Add after captureSnapshot():

```cpp
void TargetControlPage::revertToSnapshot()
{
    VIDCheckBox->setChecked(m_snap_vidChecked);
    PIDCheckBox->setChecked(m_snap_pidChecked);
    VIDLineEdit->setText(m_snap_vidText);
    PIDLineEdit->setText(m_snap_pidText);
    VIDDescriptorLineEdit->setText(m_snap_vidDescriptor);
    PIDDescriptorLineEdit->setText(m_snap_pidDescriptor);
    USBSerialNumberCheckBox->setChecked(m_snap_usbSerialNumberChecked);
    serialNumberLineEdit->setText(m_snap_serialNumberText);
    USBCustomStringDescriptorCheckBox->setChecked(m_snap_usbCustomStringChecked);
    
    // Restore operating mode radio button
    QAbstractButton *button = operatingModeGroup->button(m_snap_operatingMode);
    if (button) button->setChecked(true);
}
```

- [ ] **Step 5: Call captureSnapshot() at the end of initHardwareSetting()**

In targetcontrolpage.cpp, find `initHardwareSetting()` and add at the very end:

```cpp
    captureSnapshot();
```

- [ ] **Step 6: Add button bar to TargetControlPage in setupUI()**

At the end of `setupUI()` in targetcontrolpage.cpp, add:

```cpp
    // Button bar: Apply / Revert / Cancel
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    QPushButton *applyButton = new QPushButton(tr("Apply"));
    QPushButton *revertButton = new QPushButton(tr("Revert"));
    QPushButton *cancelButton = new QPushButton(tr("Cancel"));
    
    applyButton->setFixedSize(80, 30);
    revertButton->setFixedSize(80, 30);
    cancelButton->setFixedSize(80, 30);
    
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(revertButton);
    buttonLayout->addWidget(cancelButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Connect buttons
    connect(applyButton, &QPushButton::clicked, this, &TargetControlPage::applyHardwareSetting);
    connect(revertButton, &QPushButton::clicked, this, &TargetControlPage::revertToSnapshot);
    connect(cancelButton, &QPushButton::clicked, this, [this]() {
        QDialog *dlg = qobject_cast<QDialog*>(window());
        if (dlg) dlg->reject();
    });
```

- [ ] **Step 7: Build and test**

Run: `cmake --build build`
Expected: Build succeeds

Manual test: Open Preferences → Target Control page → verify 3 buttons → change VID → click Revert → verify restores

- [ ] **Step 8: Commit**

```bash
git add ui/preferences/targetcontrolpage.h ui/preferences/targetcontrolpage.cpp
git commit -m "feat(TargetControlPage): add per-page Apply/Revert/Cancel buttons with snapshot mechanism

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 5: Add Snapshot and Buttons to McpPage

**Files:**
- Modify: `ui/preferences/mcppage.h:65-81`
- Modify: `ui/preferences/mcppage.cpp:1-305`

**Interfaces:**
- Consumes: Existing McpPage widget members (m_enableCheckBox, m_transportCombo, etc.)
- Produces: `captureSnapshot()`, `revertToSnapshot()` methods; button bar

- [ ] **Step 1: Add snapshot member variables to mcppage.h**

Add after line 80 (after `QSpinBox *m_sseMaxSessionsSpin;`):

```cpp
    // Snapshot members
    bool m_snap_enableChecked;
    int m_snap_transportIndex;
    int m_snap_ssePort;
    int m_snap_sseBindPresetIndex;
    QString m_snap_sseBindCustom;
    QString m_snap_ssePathSse;
    QString m_snap_ssePathMessages;
    int m_snap_sseKeepalive;
    int m_snap_sseSessionTimeout;
    int m_snap_sseCleanupInterval;
    int m_snap_sseMaxSessions;
```

- [ ] **Step 2: Add snapshot method declarations to mcppage.h**

Add after line 55 (after `void applyMcpSettings();`):

```cpp
    void captureSnapshot();
    void revertToSnapshot();
```

- [ ] **Step 3: Implement captureSnapshot() in mcppage.cpp**

Add at the end of the file:

```cpp
void McpPage::captureSnapshot()
{
    m_snap_enableChecked = m_enableCheckBox->isChecked();
    m_snap_transportIndex = m_transportCombo->currentIndex();
    m_snap_ssePort = m_ssePortSpin->value();
    m_snap_sseBindPresetIndex = m_sseBindPresetCombo->currentIndex();
    m_snap_sseBindCustom = m_sseBindCustomEdit->text();
    m_snap_ssePathSse = m_ssePathSseEdit->text();
    m_snap_ssePathMessages = m_ssePathMessagesEdit->text();
    m_snap_sseKeepalive = m_sseKeepaliveSpin->value();
    m_snap_sseSessionTimeout = m_sseSessionTimeoutSpin->value();
    m_snap_sseCleanupInterval = m_sseCleanupIntervalSpin->value();
    m_snap_sseMaxSessions = m_sseMaxSessionsSpin->value();
}
```

- [ ] **Step 4: Implement revertToSnapshot() in mcppage.cpp**

Add after captureSnapshot():

```cpp
void McpPage::revertToSnapshot()
{
    m_enableCheckBox->setChecked(m_snap_enableChecked);
    m_transportCombo->setCurrentIndex(m_snap_transportIndex);
    m_ssePortSpin->setValue(m_snap_ssePort);
    m_sseBindPresetCombo->setCurrentIndex(m_snap_sseBindPresetIndex);
    m_sseBindCustomEdit->setText(m_snap_sseBindCustom);
    m_ssePathSseEdit->setText(m_snap_ssePathSse);
    m_ssePathMessagesEdit->setText(m_snap_ssePathMessages);
    m_sseKeepaliveSpin->setValue(m_snap_sseKeepalive);
    m_sseSessionTimeoutSpin->setValue(m_snap_sseSessionTimeout);
    m_sseCleanupIntervalSpin->setValue(m_snap_sseCleanupInterval);
    m_sseMaxSessionsSpin->setValue(m_snap_sseMaxSessions);
    
    // Trigger UI state updates
    onTransportModeChanged(m_snap_transportIndex);
    onBindAddressPresetChanged(m_snap_sseBindPresetIndex);
}
```

- [ ] **Step 5: Call captureSnapshot() at the end of initMcpSettings()**

In mcppage.cpp, find `initMcpSettings()` and add at the very end:

```cpp
    captureSnapshot();
```

- [ ] **Step 6: Add button bar to McpPage in setupUI()**

At the end of `setupUI()` in mcppage.cpp, add:

```cpp
    // Button bar: Apply / Revert / Cancel
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    QPushButton *applyButton = new QPushButton(tr("Apply"));
    QPushButton *revertButton = new QPushButton(tr("Revert"));
    QPushButton *cancelButton = new QPushButton(tr("Cancel"));
    
    applyButton->setFixedSize(80, 30);
    revertButton->setFixedSize(80, 30);
    cancelButton->setFixedSize(80, 30);
    
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(revertButton);
    buttonLayout->addWidget(cancelButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Connect buttons
    connect(applyButton, &QPushButton::clicked, this, &McpPage::applyMcpSettings);
    connect(revertButton, &QPushButton::clicked, this, &McpPage::revertToSnapshot);
    connect(cancelButton, &QPushButton::clicked, this, [this]() {
        QDialog *dlg = qobject_cast<QDialog*>(window());
        if (dlg) dlg->reject();
    });
```

- [ ] **Step 7: Build and test**

Run: `cmake --build build`
Expected: Build succeeds

Manual test: Open Preferences → MCP page → verify 3 buttons → change port → click Revert → verify restores

- [ ] **Step 8: Commit**

```bash
git add ui/preferences/mcppage.h ui/preferences/mcppage.cpp
git commit -m "feat(McpPage): add per-page Apply/Revert/Cancel buttons with snapshot mechanism

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 6: Merge AdvancedSettingsDialog into SettingDialog

**Files:**
- Modify: `ui/preferences/settingdialog.h:40-96`
- Modify: `ui/preferences/settingdialog.cpp:59-263`

**Interfaces:**
- Consumes: FirmwarePage, ControlChipFirmwarePage, McpPage, EdidConfigPage, VirtualKeyboardPage
- Produces: SettingDialog with 9 pages, no global buttons, new getter methods

- [ ] **Step 1: Add migrated page members to settingdialog.h**

Add after line 80 (after `TargetControlPage *targetControlPage;`):

```cpp
    FirmwarePage *firmwarePage;
    ControlChipFirmwarePage *controlChipFirmwarePage;
    McpPage *mcpPage;
    EdidConfigPage *edidConfigPage;
    VirtualKeyboardPage *virtualKeyboardPage;
```

- [ ] **Step 2: Add includes to settingdialog.h**

Add after line 44 (after `#include "audiopage.h"`):

```cpp
#include "firmwarepage.h"
#include "controlchipfirmwarepage.h"
#include "mcppage.h"
#include "edidconfigpage.h"
#include "../customkey/virtualkeyboardpage.h"
```

- [ ] **Step 3: Remove global button members from settingdialog.h**

Delete line 82: `QWidget *buttonWidget;`

Delete lines 93-95:
```cpp
    void createButtons();
    void applyAccrodingPage();
    void handleOkButton();
```

- [ ] **Step 4: Add getter methods to settingdialog.h**

Add after line 66 (after `LogPage* getLogPage();`):

```cpp
    McpPage* getMcpPage();
    FirmwarePage* getFirmwarePage();
    VirtualKeyboardPage* getVirtualKeyboardPage();
```

- [ ] **Step 5: Update settingdialog.cpp constructor to create migrated pages**

In the constructor initializer list (lines 59-73), add after line 68:

```cpp
    , firmwarePage(new FirmwarePage(this))
    , controlChipFirmwarePage(new ControlChipFirmwarePage(this))
    , mcpPage(new McpPage(this))
    , edidConfigPage(new EdidConfigPage(this))
    , virtualKeyboardPage(new VirtualKeyboardPage(this))
```

Remove line 69: `, buttonWidget(new QWidget(this))`

- [ ] **Step 6: Remove createButtons() call from constructor**

Delete line 78: `createButtons();`

- [ ] **Step 7: Update createSettingTree() to include all 9 pages**

Replace lines 125-129 with:

```cpp
    QStringList names = {
        tr("General"),              // 0
        tr("Video"),                // 1
        tr("Audio"),                // 2
        tr("Target Control"),       // 3
        tr("MCP"),                  // 4
        tr("Video Firmware"),       // 5
        tr("Control Chip Firmware"),// 6
        tr("EDID Configuration"),   // 7
        tr("Virtual Keyboard")      // 8
    };
```

- [ ] **Step 8: Update createPages() to add migrated pages**

After line 147 (after `addScrollablePage(targetControlPage);`), add:

```cpp
    addScrollablePage(mcpPage);
    addScrollablePage(firmwarePage);
    addScrollablePage(controlChipFirmwarePage);
    addScrollablePage(edidConfigPage);
    addScrollablePage(virtualKeyboardPage);
```

- [ ] **Step 9: Update changePage() to handle 9 pages**

Replace lines 200-208 with:

```cpp
    if (itemText == tr("General")) newPageIndex = 0;
    else if (itemText == tr("Video")) newPageIndex = 1;
    else if (itemText == tr("Audio")) newPageIndex = 2;
    else if (itemText == tr("Target Control")) newPageIndex = 3;
    else if (itemText == tr("MCP")) newPageIndex = 4;
    else if (itemText == tr("Video Firmware")) newPageIndex = 5;
    else if (itemText == tr("Control Chip Firmware")) newPageIndex = 6;
    else if (itemText == tr("EDID Configuration")) newPageIndex = 7;
    else if (itemText == tr("Virtual Keyboard")) newPageIndex = 8;
```

- [ ] **Step 10: Add mcpPage->initMcpSettings() call in constructor**

After line 91 (after `targetControlPage->initHardwareSetting();`), add:

```cpp
    mcpPage->initMcpSettings();
```

- [ ] **Step 11: Remove createButtons(), handleOkButton(), applyAccrodingPage() methods**

Delete the entire `createButtons()` method (lines 150-168).
Delete the entire `applyAccrodingPage()` method (lines 224-243).
Delete the entire `handleOkButton()` method (lines 245-250).

- [ ] **Step 12: Implement new getter methods in settingdialog.cpp**

Add at the end of the file:

```cpp
McpPage* SettingDialog::getMcpPage() {
    return mcpPage;
}

FirmwarePage* SettingDialog::getFirmwarePage() {
    return firmwarePage;
}

VirtualKeyboardPage* SettingDialog::getVirtualKeyboardPage() {
    return virtualKeyboardPage;
}
```

- [ ] **Step 13: Build and test**

Run: `cmake --build build`
Expected: Build succeeds

Manual test: Open Preferences → verify all 9 pages appear in left tree → click each page → verify switching works → verify no global buttons at bottom → verify each settings page has its own 3 buttons

- [ ] **Step 14: Commit**

```bash
git add ui/preferences/settingdialog.h ui/preferences/settingdialog.cpp
git commit -m "refactor(SettingDialog): merge AdvancedSettingsDialog pages into unified Preferences

- Add FirmwarePage, ControlChipFirmwarePage, McpPage, EdidConfigPage, VirtualKeyboardPage
- Remove global OK/Apply/Cancel buttons
- Each settings page now has its own Apply/Revert/Cancel buttons
- Expand tree to 9 pages

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 7: Update MainWindow to Use Merged Dialog

**Files:**
- Modify: `ui/mainwindow.h`
- Modify: `ui/mainwindow.cpp:796-862`

**Interfaces:**
- Consumes: SettingDialog with merged pages
- Produces: Unified configureSettings() method, removed configureAdvancedSettings()

- [ ] **Step 1: Remove AdvancedSettingsDialog references from mainwindow.h**

Find and delete:
- `class AdvancedSettingsDialog;` (forward declaration)
- `AdvancedSettingsDialog *advancedSettingsDialog;` (member variable)
- `void configureAdvancedSettings();` (method declaration)

- [ ] **Step 2: Remove #include from mainwindow.cpp**

Delete the line:
```cpp
#include "ui/preferences/advancedsettingsdialog.h"
```

- [ ] **Step 3: Update configureSettings() to include migrated signal connections**

In mainwindow.cpp, find `configureSettings()` (line 796). After the existing LogPage and VideoPage connections (around line 825), add:

```cpp
        // Migrated from AdvancedSettingsDialog
        McpPage* mcpPage = settingDialog->getMcpPage();
        connect(mcpPage, &McpPage::mcpSettingsChanged, this, &MainWindow::onMcpSettingsApplied);

        FirmwarePage* firmwarePage = settingDialog->getFirmwarePage();
        connect(firmwarePage, &FirmwarePage::firmwareUpdateCompleted,
                this, []() { QApplication::quit(); });
```

- [ ] **Step 4: Delete configureAdvancedSettings() method**

Delete the entire `configureAdvancedSettings()` method (lines 838-862).

- [ ] **Step 5: Update menu connection**

Find where the "Advanced Settings" menu item is connected to `configureAdvancedSettings()` and change it to `configureSettings()`.

Search for:
```cpp
connect(..., &QAction::triggered, this, &MainWindow::configureAdvancedSettings);
```

Replace with:
```cpp
connect(..., &QAction::triggered, this, &MainWindow::configureSettings);
```

- [ ] **Step 6: Build and test**

Run: `cmake --build build`
Expected: Build succeeds

Manual test: Click Settings menu → verify only one "Preferences" option → open it → verify all 9 pages work → test MCP settings change triggers server restart → test firmware update completes

- [ ] **Step 7: Commit**

```bash
git add ui/mainwindow.h ui/mainwindow.cpp
git commit -m "refactor(MainWindow): use unified SettingDialog for all preferences

- Merge signal connections from AdvancedSettingsDialog
- Remove configureAdvancedSettings() method
- Update menu to use single configureSettings()

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 8: Delete AdvancedSettingsDialog Files and Update CMakeLists

**Files:**
- Delete: `ui/preferences/advancedsettingsdialog.h`
- Delete: `ui/preferences/advancedsettingsdialog.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Remove files from CMakeLists.txt**

In CMakeLists.txt, find and delete these two lines from the source file list:

```cmake
ui/preferences/advancedsettingsdialog.cpp
ui/preferences/advancedsettingsdialog.h
```

- [ ] **Step 2: Delete the files**

Run:
```bash
rm ui/preferences/advancedsettingsdialog.h
rm ui/preferences/advancedsettingsdialog.cpp
```

- [ ] **Step 3: Build and test**

Run: `cmake --build build`
Expected: Build succeeds (no references to deleted files)

Manual test: Open Preferences → verify all functionality still works → verify no compilation errors or warnings related to deleted files

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git add -u ui/preferences/advancedsettingsdialog.h ui/preferences/advancedsettingsdialog.cpp
git commit -m "refactor: delete AdvancedSettingsDialog after merging into SettingDialog

- Remove advancedsettingsdialog.h and advancedsettingsdialog.cpp
- Update CMakeLists.txt to exclude deleted files

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Self-Review Checklist

**Spec coverage:**
- ✅ Per-page Apply/Revert/Cancel buttons for settings pages (Tasks 1-5)
- ✅ Snapshot mechanism with B-scheme semantics (Tasks 1-5)
- ✅ Merge all 9 pages into unified dialog (Task 6)
- ✅ Remove global buttons (Task 6)
- ✅ Update MainWindow signal connections (Task 7)
- ✅ Delete AdvancedSettingsDialog files (Task 8)

**Placeholder scan:** No TBD/TODO found. All steps have concrete code or commands.

**Type consistency:** Method names (`captureSnapshot`, `revertToSnapshot`) and button names (Apply/Revert/Cancel) are consistent across all tasks.
