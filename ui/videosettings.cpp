#include "videosettings.h"
#include "ui_videosettings.h"

VideoSettings::VideoSettings(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::VideoSettings)
{
    ui->setupUi(this);
    // Remove any FPS slider setup or connections
}

VideoSettings::~VideoSettings()
{
    delete ui;
}

// Remove any FPS slider related methods