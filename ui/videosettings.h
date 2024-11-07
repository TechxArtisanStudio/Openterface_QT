#ifndef VIDEOSETTINGS_H
#define VIDEOSETTINGS_H

#include <QWidget>

namespace Ui {
class VideoSettings;
}

class VideoSettings : public QWidget
{
    Q_OBJECT

public:
    explicit VideoSettings(QWidget *parent = nullptr);
    ~VideoSettings();

private:
    Ui::VideoSettings *ui;
    // Remove any FPS slider related members
};

#endif // VIDEOSETTINGS_H