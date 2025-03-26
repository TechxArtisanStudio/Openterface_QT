/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#ifndef GLOBAL_H
#define GLOBAL_H

#include <QString>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include "resources/version.h"

// inline QString getAppVersion() {
//     return QString(APP_VERSION);
// }

// #define APP_VERSION getAppVersion()

const int LOG_ = 100; // Add this line

class GlobalVar {
public:
    static GlobalVar& instance() {
        static GlobalVar instance;
        return instance;
    }
    bool isUseCustomInputResolution() const { return customInputResolution; }
    void setUseCustomInputResolution(bool value) { customInputResolution = value; }


    int getInputWidth() const { return input_width; }
    void setInputWidth(int width) { input_width = width; }

    int getInputHeight() const { return input_height; }
    void setInputHeight(int height) { input_height = height; }

    float getInputFps() const { return input_fps; }
    void setInputFps(float fps) { input_fps = fps; }

    float getInputAspectRatio() const { return input_aspect_ratio; }
    void setInputAspectRatio(float aspect_ratio) { input_aspect_ratio = aspect_ratio; }

    int getCaptureWidth() const { return capture_width; }
    void setCaptureWidth(int width) { capture_width = width; }

    int getCaptureHeight() const { return capture_height; }
    void setCaptureHeight(int height) { capture_height = height; }

    int getCaptureFps() const { return capture_fps; }
    void setCaptureFps(int fps) { capture_fps = fps; }

    int getWinWidth() const { return win_width; }
    void setWinWidth(int width) { win_width = width; }

    int getWinHeight() const { return win_height; }
    void setWinHeight(int height) { win_height = height; }

    int getMenuHeight() const { return menu_height; }
    void setMenuHeight(int height) { menu_height = height; }

    int getTitleHeight() const { return title_height; }
    void setTitleHeight(int height) { title_height = height; }

    int getStatusbarHeight() const { return statusbar_height; }
    void setStatusbarHeight(int height) { statusbar_height = height; }

    int getTopbarHeight() const {return title_height + menu_height;}

    int getAllbarHeight() const {return title_height + menu_height + statusbar_height ;}

    bool isAbsoluteMouseMode() const { return absolute_mouse_mode; }
    void setAbsoluteMouseMode(bool mode) { absolute_mouse_mode = mode; }

    std::string getCaptureCardFirmwareVersion() const { return captureCardFirmwareVersion; }
    void setCaptureCardFirmwareVersion(const std::string& version) { captureCardFirmwareVersion = version; }

    bool isSwitchOnTarget() const { return _isSwitchOnTarget; }
    void setSwitchOnTarget(bool onTarget) { _isSwitchOnTarget = onTarget; }

    bool isToolbarVisible() const { return toolbarVisible; }
    void setToolbarVisible(bool visible) { toolbarVisible = visible; }
    
    int getToolbarHeight() const { return toolbarHeight; }
    void setToolbarHeight(int height) { toolbarHeight = height; }

    bool isMouseAutoHideEnabled() const { return m_mouseAutoHide; }
    void setMouseAutoHide(bool autoHide) { m_mouseAutoHide = autoHide; }
private:
    GlobalVar() : input_width(1920), input_height(1080), capture_width(1920), capture_height(1080), capture_fps(30) {} // Private constructor
    ~GlobalVar() {} // Private destructor

    // Prevent copying
    GlobalVar(const GlobalVar&) = delete;
    GlobalVar& operator=(const GlobalVar&) = delete;

    // Prevent moving
    GlobalVar(GlobalVar&&) = delete;
    GlobalVar& operator=(GlobalVar&&) = delete;

    // The target device input resolution
    int input_width;
    int input_height;
    float input_fps;
    float input_aspect_ratio;

    // The capture card capture resolution
    int capture_width;
    int capture_height;
    float capture_fps;


    int win_width;
    int win_height;

    int menu_height;
    int title_height;
    int statusbar_height;

    bool absolute_mouse_mode = true;
    std::string captureCardFirmwareVersion;

    bool customInputResolution = false;

    bool _isSwitchOnTarget = true;

    bool toolbarVisible = true;
    int toolbarHeight = 0;

    bool m_mouseAutoHide = true;
};

#endif
