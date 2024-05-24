#ifndef GLOBAL_H
#define GLOBAL_H

const int LOG_ = 100; // Add this line

class GlobalVar {
public:
    static GlobalVar& instance() {
        static GlobalVar instance;
        return instance;
    }

    int getVideoWidth() const { return video_width; }
    void setVideoWidth(int width) { video_width = width; }

    int getVideoHeight() const { return video_height; }
    void setVideoHeight(int height) { video_height = height; }

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

private:
    GlobalVar() : video_width(1920), video_height(1080) {} // Private constructor
    ~GlobalVar() {} // Private destructor

    // Prevent copying
    GlobalVar(const GlobalVar&) = delete;
    GlobalVar& operator=(const GlobalVar&) = delete;

    // Prevent moving
    GlobalVar(GlobalVar&&) = delete;
    GlobalVar& operator=(GlobalVar&&) = delete;

    int video_x;
    int video_y;

    int video_width;
    int video_height;

    int win_width;
    int win_height;

    int menu_height;
    int title_height;
    int statusbar_height;
};

#endif
