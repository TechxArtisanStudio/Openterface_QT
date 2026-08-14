# Preference Dialog Improvements Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix SystemBlocker blocking input in dialogs, add button styling with dirty-state highlighting, and implement unsaved-changes protection in the preference dialog.

**Architecture:** SystemKeyBlocker will check if focus is in VideoPane before swallowing keys. PreferencePageBase extracts common button/styling/dirty logic for 5 settings pages. SettingDialog adds dirty-check on page switch and close.

**Tech Stack:** C++17, Qt 5.15, CMake/qmake, X11/XCB, Windows API

**Spec:** `docs/superpowers/specs/2026-08-14-preference-dialog-improvements-design.md`

## Global Constraints

- Only 5 settings pages use PreferencePageBase: LogPage, VideoPage, AudioPage, TargetControlPage, McpPage
- FirmwarePage, ControlChipFirmwarePage, EdidConfigPage, VirtualKeyboardPage are NOT modified
- Button styles: white with shadow (default), orange when dirty
- Unsaved changes dialog: Save/Discard/Cancel three-way choice

---

### Task 1: SystemKeyBlocker — Add Focus Target

**Files:**
- Modify: `SysKeyBlocker/SystemKeyBlocker.h`
- Modify: `SysKeyBlocker/SystemKeyBlocker.cpp`

**Interfaces:**
- Produces: `setFocusTarget(QWidget*)`, `focusTarget()`, `m_focusTarget`

- [ ] **Step 1: Add focus target to SystemKeyBlocker.h**

Add to SystemKeyBlocker.h after `setSwallowEnabled`:
```cpp
void setFocusTarget(QWidget *target) { m_focusTarget = target; }
QWidget *focusTarget() const { return m_focusTarget; }
```

Add private member:
```cpp
QWidget *m_focusTarget = nullptr;
```

- [ ] **Step 2: Verify compilation**

```bash
cd ~/project/Openterface_QT && mkdir -p build && cd build && cmake .. && make -j4 SysKeyBlocker 2>&1 | tail -20
```

- [ ] **Step 3: Commit**

```bash
git add SysKeyBlocker/SystemKeyBlocker.h SysKeyBlocker/SystemKeyBlocker.cpp
git commit -m "feat: add focus target to SystemKeyBlocker"
```

---

### Task 2: SystemKeyBlocker X11 — Restrict Swallow to VideoPane

**Files:**
- Modify: `SysKeyBlocker/SystemKeyBlocker_x11.cpp`

**Interfaces:**
- Consumes: `m_focusTarget` from Task 1
- Produces: X11 filter checks focusTarget instead of focusWindow

- [ ] **Step 1: Modify nativeEventFilter in SystemKeyBlocker_x11.cpp**

Replace the focus check (around line 338):
```cpp
// OLD:
if (!QGuiApplication::focusWindow())
    return false;

// NEW:
if (!m_blocker->focusTarget())
    return false;
QWidget *fw = QApplication::focusWidget();
if (!fw || !m_blocker->focusTarget()->isAncestorOf(fw))
    return false;
```

Add `#include <QApplication>` at top if not present.

- [ ] **Step 2: Verify compilation**

```bash
cd ~/project/Openterface_QT/build && make -j4 2>&1 | tail -20
```

- [ ] **Step 3: Commit**

```bash
git add SysKeyBlocker/SystemKeyBlocker_x11.cpp
git commit -m "fix: SystemKeyBlocker X11 only swallows when VideoPane has focus"
```

---

### Task 3: SystemKeyBlocker Windows — Restrict Swallow to VideoPane

**Files:**
- Modify: `SysKeyBlocker/SystemKeyBlocker_win.cpp`

**Interfaces:**
- Consumes: `m_focusTarget` from Task 1
- Produces: Windows hook checks focusTarget instead of foreground window

- [ ] **Step 1: Modify lowLevelKeyboardProc in SystemKeyBlocker_win.cpp**

Replace the window focus check (around line 100-120):
```cpp
// OLD:
HWND foregroundWnd = GetForegroundWindow();
HWND hookedWnd = (HWND)s_self->m_hookedHwnd;
bool isOurWindowFocused = false;
if (foregroundWnd != nullptr && hookedWnd != nullptr) {
    if (foregroundWnd == hookedWnd) {
        isOurWindowFocused = true;
    } else if (IsChild(hookedWnd, foregroundWnd)) {
        isOurWindowFocused = true;
    } else if (IsChild(foregroundWnd, hookedWnd)) {
        isOurWindowFocused = true;
    }
}
if (!isOurWindowFocused) {
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// NEW:
if (!s_self->m_focusTarget) {
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
HWND focusHwnd = GetFocus();
HWND targetHwnd = (HWND)s_self->m_focusTarget->winId();
if (focusHwnd != targetHwnd && !IsChild(targetHwnd, focusHwnd)) {
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
```

- [ ] **Step 2: Verify compilation (Windows only, skip on Linux)**

Skip if building on Linux.

- [ ] **Step 3: Commit**

```bash
git add SysKeyBlocker/SystemKeyBlocker_win.cpp
git commit -m "fix: SystemKeyBlocker Windows only swallows when VideoPane has focus"
```

---

### Task 4: MainWindow — Initialize Focus Target

**Files:**
- Modify: `ui/mainwindow.cpp`

**Interfaces:**
- Consumes: `setFocusTarget()` from Task 1
- Produces: SystemKeyBlocker knows about VideoPane

- [ ] **Step 1: Add setFocusTarget call in mainwindow.cpp**

Find where videoPane is created (around line 131) and after it's initialized, add:
```cpp
SystemKeyBlocker::instance().setFocusTarget(videoPane);
```

Best place: in MainWindow constructor after `videoPane` is fully set up, or in `initUI()` method.

- [ ] **Step 2: Verify compilation**

```bash
cd ~/project/Openterface_QT/build && make -j4 2>&1 | tail -20
```

- [ ] **Step 3: Commit**

```bash
git add ui/mainwindow.cpp
git commit -m "feat: initialize SystemKeyBlocker focus target to VideoPane"
```

---

### Task 5: PreferencePageBase — Create Base Class

**Files:**
- Create: `ui/preferences/preferencepagebase.h`
- Create: `ui/preferences/preferencepagebase.cpp`

**Interfaces:**
- Produces: PreferencePageBase class with button management, dirty tracking, styling

- [ ] **Step 1: Create preferencepagebase.h**

```cpp
#ifndef PREFERENCEPAGEBASE_H
#define PREFERENCEPAGEBASE_H

#include <QWidget>
#include <QPushButton>
#include <QLayout>

class PreferencePageBase : public QWidget
{
    Q_OBJECT

public:
    explicit PreferencePageBase(QWidget *parent = nullptr);

    virtual void captureSnapshot() = 0;
    virtual void applySettings() = 0;
    virtual void revertToSnapshot() = 0;

    bool isDirty() const { return m_isDirty; }

signals:
    void dirtyChanged(bool dirty);

protected:
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_revertButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    void createButtonBar(QLayout *parentLayout);
    void markDirty();
    void clearDirty();
    void updateButtonStyles();

private:
    bool m_isDirty = false;

    static QString defaultButtonStyle();
    static QString dirtyButtonStyle();
};

#endif // PREFERENCEPAGEBASE_H
```

- [ ] **Step 2: Create preferencepagebase.cpp**

```cpp
#include "preferencepagebase.h"
#include <QHBoxLayout>
#include <QDialog>

PreferencePageBase::PreferencePageBase(QWidget *parent)
    : QWidget(parent)
{
}

void PreferencePageBase::createButtonBar(QLayout *parentLayout)
{
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_applyButton = new QPushButton(tr("Apply"), this);
    m_revertButton = new QPushButton(tr("Revert"), this);
    m_cancelButton = new QPushButton(tr("Cancel"), this);

    m_applyButton->setObjectName("applyButton");
    m_revertButton->setObjectName("revertButton");
    m_cancelButton->setObjectName("cancelButton");

    m_applyButton->setFixedSize(80, 30);
    m_revertButton->setFixedSize(80, 30);
    m_cancelButton->setFixedSize(80, 30);

    buttonLayout->addWidget(m_applyButton);
    buttonLayout->addWidget(m_revertButton);
    buttonLayout->addWidget(m_cancelButton);

    parentLayout->addLayout(buttonLayout);

    connect(m_applyButton, &QPushButton::clicked, this, [this]() {
        applySettings();
        captureSnapshot();
        clearDirty();
    });

    connect(m_revertButton, &QPushButton::clicked, this, [this]() {
        revertToSnapshot();
        markDirty();
    });

    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        QDialog *dlg = qobject_cast<QDialog*>(window());
        if (dlg) dlg->reject();
    });

    updateButtonStyles();
}

void PreferencePageBase::markDirty()
{
    if (!m_isDirty) {
        m_isDirty = true;
        updateButtonStyles();
        emit dirtyChanged(true);
    }
}

void PreferencePageBase::clearDirty()
{
    if (m_isDirty) {
        m_isDirty = false;
        updateButtonStyles();
        emit dirtyChanged(false);
    }
}

void PreferencePageBase::updateButtonStyles()
{
    if (m_applyButton) {
        m_applyButton->setProperty("dirty", m_isDirty);
        m_applyButton->style()->unpolish(m_applyButton);
        m_applyButton->style()->polish(m_applyButton);

        if (m_isDirty) {
            m_applyButton->setStyleSheet(dirtyButtonStyle());
        } else {
            m_applyButton->setStyleSheet(defaultButtonStyle());
        }
    }
    if (m_revertButton) {
        m_revertButton->setStyleSheet(defaultButtonStyle());
    }
    if (m_cancelButton) {
        m_cancelButton->setStyleSheet(defaultButtonStyle());
    }
}

QString PreferencePageBase::defaultButtonStyle()
{
    return QStringLiteral(
        "QPushButton {"
        "  background-color: #ffffff;"
        "  border: 1px solid #cccccc;"
        "  border-radius: 4px;"
        "  padding: 4px 16px;"
        "  min-width: 80px;"
        "  min-height: 28px;"
        "  color: #333333;"
        "}"
        "QPushButton:hover {"
        "  background-color: #f0f0f0;"
        "  border-color: #999999;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #e0e0e0;"
        "}"
    );
}

QString PreferencePageBase::dirtyButtonStyle()
{
    return QStringLiteral(
        "QPushButton#applyButton {"
        "  background-color: #ff8c00;"
        "  border: 1px solid #e07000;"
        "  color: white;"
        "  font-weight: bold;"
        "}"
        "QPushButton#applyButton:hover {"
        "  background-color: #ff9920;"
        "}"
    );
}
```

- [ ] **Step 3: Add to CMakeLists.txt or .pro file**

Add to `openterfaceQT.pro`:
```
HEADERS += ui/preferences/preferencepagebase.h
SOURCES += ui/preferences/preferencepagebase.cpp
```

Or to `CMakeLists.txt` in the appropriate section.

- [ ] **Step 4: Verify compilation**

```bash
cd ~/project/Openterface_QT/build && cmake .. && make -j4 2>&1 | tail -20
```

- [ ] **Step 5: Commit**

```bash
git add ui/preferences/preferencepagebase.h ui/preferences/preferencepagebase.cpp openterfaceQT.pro CMakeLists.txt
git commit -m "feat: add PreferencePageBase class for settings pages"
```

---

### Task 6: Migrate LogPage to PreferencePageBase

**Files:**
- Modify: `ui/preferences/logpage.h`
- Modify: `ui/preferences/logpage.cpp`

**Interfaces:**
- Consumes: PreferencePageBase from Task 5
- Produces: LogPage inherits base, uses createButtonBar, marks dirty on changes

- [ ] **Step 1: Modify logpage.h**

Change inheritance:
```cpp
// OLD:
class LogPage : public QWidget

// NEW:
#include "preferencepagebase.h"
class LogPage : public PreferencePageBase
```

Add override to methods:
```cpp
void captureSnapshot() override;
void applySettings() override;  // renamed from applyLogsettings
void revertToSnapshot() override;
```

Remove local button pointers (applyButton, revertButton, cancelButton) if declared.

- [ ] **Step 2: Modify logpage.cpp**

Remove local button creation (lines ~176-186):
```cpp
// DELETE these lines:
QPushButton *applyButton = new QPushButton(tr("Apply"));
QPushButton *revertButton = new QPushButton(tr("Revert"));
QPushButton *cancelButton = new QPushButton(tr("Cancel"));
applyButton->setFixedSize(80, 30);
revertButton->setFixedSize(80, 30);
cancelButton->setFixedSize(80, 30);
buttonLayout->addWidget(applyButton);
buttonLayout->addWidget(revertButton);
buttonLayout->addWidget(cancelButton);
```

Replace with:
```cpp
createButtonBar(logLayout);
```

Rename `applyLogsettings()` to `applySettings()`.

Add dirty tracking — connect all setting changes to markDirty():
```cpp
connect(coreCheckBox, &QCheckBox::toggled, this, [this]{ markDirty(); });
connect(serialCheckBox, &QCheckBox::toggled, this, [this]{ markDirty(); });
connect(uiCheckBox, &QCheckBox::toggled, this, [this]{ markDirty(); });
connect(hostCheckBox, &QCheckBox::toggled, this, [this]{ markDirty(); });
connect(deviceCheckBox, &QCheckBox::toggled, this, [this]{ markDirty(); });
connect(backendCheckBox, &QCheckBox::toggled, this, [this]{ markDirty(); });
connect(scriptCheckBox, &QCheckBox::toggled, this, [this]{ markDirty(); });
connect(storeLogCheckBox, &QCheckBox::toggled, this, [this]{ markDirty(); });
connect(logFilePathLineEdit, &QLineEdit::textChanged, this, [this]{ markDirty(); });
connect(screenSaverCheckBox, &QCheckBox::toggled, this, [this]{ markDirty(); });
connect(hideKeyboardInputCheckBox, &QCheckBox::toggled, this, [this]{ markDirty(); });
connect(floatingWindowCheckBox, &QCheckBox::toggled, this, [this]{ markDirty(); });
connect(floatingWindowOpacitySlider, &QSlider::valueChanged, this, [this]{ markDirty(); });
connect(systemKeyBlockerCheckBox, &QCheckBox::toggled, this, [this]{ markDirty(); });
```

Remove old button connections (applyButton, revertButton, cancelButton connects).

- [ ] **Step 3: Verify compilation**

```bash
cd ~/project/Openterface_QT/build && make -j4 2>&1 | tail -20
```

- [ ] **Step 4: Commit**

```bash
git add ui/preferences/logpage.h ui/preferences/logpage.cpp
git commit -m "refactor: migrate LogPage to PreferencePageBase"
```

---

### Task 7: Migrate VideoPage to PreferencePageBase

**Files:**
- Modify: `ui/preferences/videopage.h`
- Modify: `ui/preferences/videopage.cpp`

- [ ] **Step 1: Modify videopage.h**

Change inheritance to PreferencePageBase, add override to captureSnapshot/revertToSnapshot, add applySettings().

- [ ] **Step 2: Modify videopage.cpp**

Remove local button creation, replace with `createButtonBar(layout)`, rename apply method to `applySettings()`, connect changes to `markDirty()`.

- [ ] **Step 3: Verify compilation**

```bash
cd ~/project/Openterface_QT/build && make -j4 2>&1 | tail -20
```

- [ ] **Step 4: Commit**

```bash
git add ui/preferences/videopage.h ui/preferences/videopage.cpp
git commit -m "refactor: migrate VideoPage to PreferencePageBase"
```

---

### Task 8: Migrate AudioPage to PreferencePageBase

**Files:**
- Modify: `ui/preferences/audiopage.h`
- Modify: `ui/preferences/audiopage.cpp`

- [ ] **Step 1: Modify audiopage.h**

Change inheritance to PreferencePageBase, add override to captureSnapshot/revertToSnapshot, add applySettings().

- [ ] **Step 2: Modify audiopage.cpp**

Remove local button creation, replace with `createButtonBar(layout)`, rename apply method to `applySettings()`, connect changes to `markDirty()`.

- [ ] **Step 3: Verify compilation**

```bash
cd ~/project/Openterface_QT/build && make -j4 2>&1 | tail -20
```

- [ ] **Step 4: Commit**

```bash
git add ui/preferences/audiopage.h ui/preferences/audiopage.cpp
git commit -m "refactor: migrate AudioPage to PreferencePageBase"
```

---

### Task 9: Migrate TargetControlPage to PreferencePageBase

**Files:**
- Modify: `ui/preferences/targetcontrolpage.h`
- Modify: `ui/preferences/targetcontrolpage.cpp`

- [ ] **Step 1: Modify targetcontrolpage.h**

Change inheritance to PreferencePageBase, add override to captureSnapshot/revertToSnapshot, add applySettings().

- [ ] **Step 2: Modify targetcontrolpage.cpp**

Remove local button creation, replace with `createButtonBar(layout)`, rename apply method to `applySettings()`, connect changes to `markDirty()`.

- [ ] **Step 3: Verify compilation**

```bash
cd ~/project/Openterface_QT/build && make -j4 2>&1 | tail -20
```

- [ ] **Step 4: Commit**

```bash
git add ui/preferences/targetcontrolpage.h ui/preferences/targetcontrolpage.cpp
git commit -m "refactor: migrate TargetControlPage to PreferencePageBase"
```

---

### Task 10: Migrate McpPage to PreferencePageBase

**Files:**
- Modify: `ui/preferences/mcppage.h`
- Modify: `ui/preferences/mcppage.cpp`

- [ ] **Step 1: Modify mcppage.h**

Change inheritance to PreferencePageBase, add override to captureSnapshot/revertToSnapshot, add applySettings().

- [ ] **Step 2: Modify mcppage.cpp**

Remove local button creation, replace with `createButtonBar(layout)`, rename apply method to `applySettings()`, connect changes to `markDirty()`.

- [ ] **Step 3: Verify compilation**

```bash
cd ~/project/Openterface_QT/build && make -j4 2>&1 | tail -20
```

- [ ] **Step 4: Commit**

```bash
git add ui/preferences/mcppage.h ui/preferences/mcppage.cpp
git commit -m "refactor: migrate McpPage to PreferencePageBase"
```

---

### Task 11: SettingDialog — Add Unsaved Changes Protection

**Files:**
- Modify: `ui/preferences/settingdialog.h`
- Modify: `ui/preferences/settingdialog.cpp`

**Interfaces:**
- Consumes: PreferencePageBase from Task 5, all migrated pages from Tasks 6-10
- Produces: dirty check on page switch and close, save/discard/cancel dialog

- [ ] **Step 1: Modify settingdialog.h**

Add includes and members:
```cpp
#include "preferencepagebase.h"

private:
    QList<PreferencePageBase*> m_pages;
    
    bool hasUnsavedChanges() const;
    void applyAllDirtyPages();
    QMessageBox::StandardButton promptSaveDiscardCancel();
    PreferencePageBase* currentPage() const;
    
protected:
    void closeEvent(QCloseEvent *event) override;
```

- [ ] **Step 2: Modify settingdialog.cpp — constructor**

Initialize m_pages list:
```cpp
m_pages << logPage << videoPage << audioPage << targetControlPage << mcpPage;
```

- [ ] **Step 3: Implement hasUnsavedChanges()**

```cpp
bool SettingDialog::hasUnsavedChanges() const
{
    for (auto *page : m_pages) {
        if (page->isDirty()) return true;
    }
    return false;
}
```

- [ ] **Step 4: Implement applyAllDirtyPages()**

```cpp
void SettingDialog::applyAllDirtyPages()
{
    for (auto *page : m_pages) {
        if (page->isDirty()) {
            page->applySettings();
            page->captureSnapshot();
            page->clearDirty();
        }
    }
}
```

- [ ] **Step 5: Implement promptSaveDiscardCancel()**

```cpp
QMessageBox::StandardButton SettingDialog::promptSaveDiscardCancel()
{
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Unsaved Changes"));
    msgBox.setText(tr("You have unsaved changes."));
    msgBox.setInformativeText(tr("Do you want to save your changes?"));
    msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Save);
    return msgBox.exec();
}
```

- [ ] **Step 6: Implement currentPage()**

```cpp
PreferencePageBase* SettingDialog::currentPage() const
{
    if (m_currentPageIndex >= 0 && m_currentPageIndex < m_pages.size()) {
        return m_pages[m_currentPageIndex];
    }
    return nullptr;
}
```

- [ ] **Step 7: Modify changePage() — add dirty check**

Before switching pages:
```cpp
if (newPageIndex != -1 && newPageIndex != m_currentPageIndex) {
    if (hasUnsavedChanges()) {
        auto result = promptSaveDiscardCancel();
        if (result == QMessageBox::Save) {
            applyAllDirtyPages();
        } else if (result == QMessageBox::Cancel) {
            settingTree->blockSignals(true);
            settingTree->setCurrentItem(previous);
            settingTree->blockSignals(false);
            return;
        }
    }
    stackedWidget->setCurrentIndex(newPageIndex);
    m_currentPageIndex = newPageIndex;
}
```

- [ ] **Step 8: Implement closeEvent()**

```cpp
void SettingDialog::closeEvent(QCloseEvent *event)
{
    if (hasUnsavedChanges()) {
        auto result = promptSaveDiscardCancel();
        if (result == QMessageBox::Save) {
            applyAllDirtyPages();
            event->accept();
        } else if (result == QMessageBox::Cancel) {
            event->ignore();
        } else {
            event->accept();
        }
    } else {
        event->accept();
    }
}
```

- [ ] **Step 9: Add includes**

```cpp
#include <QMessageBox>
#include <QCloseEvent>
```

- [ ] **Step 10: Verify compilation**

```bash
cd ~/project/Openterface_QT/build && make -j4 2>&1 | tail -20
```

- [ ] **Step 11: Commit**

```bash
git add ui/preferences/settingdialog.h ui/preferences/settingdialog.cpp
git commit -m "feat: add unsaved changes protection to SettingDialog"
```

---

### Task 12: Final Integration Test

- [ ] **Step 1: Build full project**

```bash
cd ~/project/Openterface_QT/build && make -j4 2>&1 | tail -30
```

- [ ] **Step 2: Test SystemKeyBlocker fix**

1. Launch app
2. Enable SystemKeyBlocker in General/Log page
3. Open Preferences (Ctrl+P)
4. Try typing in text fields (e.g., log file path)
5. Verify: typing works normally

- [ ] **Step 3: Test button styling**

1. Open Preferences
2. Verify: Apply/Revert/Cancel buttons have white background with subtle shadow
3. Modify a setting (e.g., toggle a checkbox)
4. Verify: Apply button turns orange
5. Click Apply
6. Verify: Apply returns to white
7. Click Revert
8. Verify: Apply turns orange again

- [ ] **Step 4: Test unsaved changes dialog**

1. Modify a setting (Apply turns orange)
2. Try to switch to another page
3. Verify: dialog appears asking to save
4. Click Cancel — verify: stays on same page
5. Click Discard — verify: switches without saving
6. Modify again, try to close dialog
7. Verify: dialog appears
8. Click Save — verify: applies and closes

- [ ] **Step 5: Final commit**

```bash
git add .
git commit -m "feat: complete preference dialog improvements"
git push origin dev_260624_MCPserver
```

---

## Summary

**Total tasks:** 12
**Estimated time:** 4-6 hours
**Key files created:** 2 (preferencepagebase.h/cpp)
**Key files modified:** 12 (SystemKeyBlocker: 3, mainwindow: 1, pages: 5×2, settingdialog: 2)
