# AI Chat Tools Configuration - Tree Structure Refactoring

## Overview
Refactored the AI Chat Tools Configuration section in Preferences to use a hierarchical tree structure, matching the design pattern used in the Logging page.

## Changes Made

### UI Structure
**Before:** Nested QGroupBox layout with individual checkboxes
- Flat list of tool groups with manual indentation
- Each group had a separate QGroupBox with border styling
- Required custom margin/padding management

**After:** QTreeView with QStandardItemModel
- Hierarchical, expandable/collapsible tree structure
- Consistent with logging page design
- Better visual hierarchy and organization

### Tree Structure
The tools are now organized as follows:

```
Tools Configuration
├── [Select All checkbox]
└── Tree View
    ├── Screen Tools (expandable)
    │   ├── Screen Capture (capture_screen)
    │   └── Screen to Markdown (screen_to_markdown)
    ├── Mouse Tools (expandable)
    │   ├── Move Mouse (move_mouse)
    │   ├── Left Click (left_click)
    │   ├── Right Click (right_click)
    │   ├── Double Click (double_click)
    │   └── Left Drag (left_drag)
    ├── Keyboard Tools (expandable)
    │   ├── Type Text (type_text)
    │   ├── Press Key (press_key)
    │   └── Repeat Key (repeat_key)
    ├── Recording Tools (expandable)
    │   ├── Start Recording (start_recording)
    │   └── Stop Recording (stop_recording)
    └── System/Host Tools (expandable)
        ├── Set Target System (set_target_system)
        └── Run Bash (run_bash)
```

### Features

#### Tree View Capabilities
- **Expandable/Collapsible Groups**: Click group headers to expand/collapse
- **Two-Column Layout**: 
  - Column 1: Tool name
  - Column 2: Tool ID (e.g., `capture_screen`, `move_mouse`)
- **Checkable Items**: Both groups and individual tools can be checked/unchecked
- **Group Propagation**: Checking a group automatically checks/unchecks all child tools
- **Select All Sync**: Master checkbox reflects state of all tools
- **Bold Group Headers**: Visual distinction for group items

#### State Management
- Group check states automatically update based on children:
  - All children checked → Group checked
  - Some children checked → Group partially checked
  - No children checked → Group unchecked
- Select All checkbox updates based on all tool states
- Dirty state tracking for Apply/Revert functionality
- Snapshot/revert preserves tree state correctly

### Code Changes

#### Files Modified
1. **ui/chat/ChatSettingsPage.h**
   - Removed individual checkbox member variables for tools
   - Added `QTreeView *m_toolsTreeView`
   - Added `QStandardItemModel *m_toolsModel`
   - Added `populateToolsTree()` method
   - Removed helper methods: `updateGroupCheckState()`, `propagateGroupState()`, `updateAllToolsState()`

2. **ui/chat/ChatSettingsPage.cpp**
   - Replaced nested QGroupBox creation with tree model population
   - Implemented `populateToolsTree()` to build hierarchical structure
   - Updated `initChatSettings()` to load tool states into tree
   - Updated `applySettings()` to save tool states from tree
   - Updated `captureSnapshot()` to capture tree state
   - Updated `revertToSnapshot()` to restore tree state
   - Updated `valuesMatchSnapshot()` to compare tree state
   - Replaced individual checkbox signal connections with tree model `itemChanged` signal
   - Implemented group propagation logic in model change handler

#### Key Implementation Details

**Tree Population:**
```cpp
void ChatSettingsPage::populateToolsTree()
{
    // Create groups with checkable items
    QStandardItem *screenGroup = new QStandardItem(tr("Screen Tools"));
    screenGroup->setCheckable(true);
    screenGroup->setCheckState(Qt::Checked);
    screenGroup->setEditable(false);
    
    // Add child tools with tool ID in UserRole
    QStandardItem *tool = new QStandardItem(tr("Screen Capture"));
    tool->setCheckable(true);
    tool->setData("capture_screen", Qt::UserRole + 1);
    
    screenGroup->appendRow({tool, descItem});
    m_toolsModel->appendRow(screenGroup);
}
```

**Group Propagation:**
```cpp
connect(m_toolsModel, &QStandardItemModel::itemChanged, this, [this](QStandardItem* item){
    if (item && item->rowCount() > 0 && item->isCheckable()) {
        // Propagate check state to all children
        Qt::CheckState groupState = item->checkState();
        for (int c = 0; c < item->rowCount(); ++c) {
            QStandardItem* child = item->child(c, 0);
            if (child && child->isCheckable()) {
                child->setCheckState(groupState);
            }
        }
    }
    // Update select all checkbox
    // ...
});
```

**Tool State Access:**
```cpp
// Get tool state by tool ID
auto getToolCheck = [this](const QString &toolId) -> bool {
    for (int g = 0; g < m_toolsModel->rowCount(); ++g) {
        QStandardItem *group = m_toolsModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *toolItem = group->child(c, 0);
            if (toolItem->data(Qt::UserRole + 1).toString() == toolId) {
                return toolItem->checkState() == Qt::Checked;
            }
        }
    }
    return false;
};
```

### Benefits

1. **Better Organization**: Hierarchical structure makes it easier to understand tool categories
2. **Consistent Design**: Matches the logging page UI pattern
3. **Improved UX**: Expandable/collapsible groups reduce visual clutter
4. **Cleaner Code**: Tree model is more maintainable than nested layouts
5. **Scalable**: Easy to add new tool groups and tools
6. **Professional Appearance**: Two-column layout with tool IDs provides better documentation

### Testing Checklist

- [x] Tree view displays correctly with all groups and tools
- [x] Groups can be expanded/collapsed
- [x] Checking a group checks all children
- [x] Unchecking a group unchecks all children
- [x] Individual tools can be toggled independently
- [x] Select All checkbox works correctly
- [x] Group check states update when children change
- [x] Tool states load from settings correctly
- [x] Tool states save to settings correctly
- [x] Snapshot/revert works correctly
- [x] Dirty state tracking works correctly
- [x] Build completes without errors or warnings

### Migration Notes

- No data migration needed - tool settings are stored by tool ID in GlobalSetting
- Existing tool configurations are automatically loaded into the new tree structure
- Tool IDs remain unchanged (e.g., `capture_screen`, `move_mouse`)
- UI is backward compatible with existing settings
