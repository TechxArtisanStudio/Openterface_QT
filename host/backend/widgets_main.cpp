#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QMenuBar>
#include <QAction>
#include <QTimer>
#include <QMessageBox>
#include <QWindow>
#include <QFile>
#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#endif
#include <gst/video/videooverlay.h>

class VideoWidget : public QWidget
{
    Q_OBJECT
public:
    VideoWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("videoWidget");
        setStyleSheet("background-color: black; border: 2px solid white;");
        setMinimumSize(640, 480);
        
        // Create a label to show instructions
        QVBoxLayout *layout = new QVBoxLayout(this);
        QLabel *label = new QLabel("Camera Video Area\n\nClick 'Start Camera' to begin streaming", this);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet("color: white; font-size: 16px; background: transparent; border: none;");
        layout->addWidget(label);
        
        // Enable native window for video overlay
        setAttribute(Qt::WA_NativeWindow, true);
        setAttribute(Qt::WA_PaintOnScreen, true);
    }
    
    WId getWindowId() const {
        return winId();
    }
};

class VideoWindow : public QMainWindow
{
    Q_OBJECT

public:
    VideoWindow(QWidget *parent = nullptr);
    ~VideoWindow();

private slots:
    void startVideo();
    void stopVideo();
    void aboutApp();

private:
    void setupUI();
    void setupGStreamer();
    bool checkCameraAvailable();
    
    QWidget *centralWidget;
    QVBoxLayout *mainLayout;
    QHBoxLayout *buttonLayout;
    VideoWidget *videoWidget;
    QPushButton *playButton;
    QPushButton *stopButton;
    QLabel *infoLabel;
    
    GstElement *pipeline;
    GstElement *sink;
    bool isPlaying;
};

VideoWindow::VideoWindow(QWidget *parent)
    : QMainWindow(parent), pipeline(nullptr), sink(nullptr), isPlaying(false)
{
    setWindowTitle("GStreamer Qt6 Camera Viewer");
    setMinimumSize(800, 600);
    
    setupUI();
    
    // Check camera availability before setting up GStreamer
    if (checkCameraAvailable()) {
        setupGStreamer();
    } else {
        statusBar()->showMessage("No camera found at /dev/video0");
        playButton->setEnabled(false);
        infoLabel->setText("Camera not found - Please check that a camera is connected to /dev/video0");
        infoLabel->setStyleSheet("background-color: lightyellow; padding: 5px; margin: 5px; font-weight: bold;");
    }
}

VideoWindow::~VideoWindow()
{
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        if (sink) gst_object_unref(sink);
        gst_object_unref(pipeline);
    }
}

void VideoWindow::setupUI()
{
    // Create menu bar
    QMenuBar *menuBar = this->menuBar();
    QMenu *fileMenu = menuBar->addMenu("&File");
    QMenu *helpMenu = menuBar->addMenu("&Help");
    
    QAction *exitAction = fileMenu->addAction("E&xit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    
    QAction *aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, &VideoWindow::aboutApp);
    
    // Create central widget
    centralWidget = new QWidget;
    setCentralWidget(centralWidget);
    
    // Create main layout
    mainLayout = new QVBoxLayout(centralWidget);
    
    // Create info label
    infoLabel = new QLabel("GStreamer Qt6 Camera Viewer - Live camera feed will render directly in the widget below");
    infoLabel->setStyleSheet("background-color: lightblue; padding: 5px; margin: 5px; font-weight: bold;");
    mainLayout->addWidget(infoLabel);
    
    // Create video widget
    videoWidget = new VideoWidget;
    mainLayout->addWidget(videoWidget);
    
    // Create button layout
    buttonLayout = new QHBoxLayout;
    
    playButton = new QPushButton("Start Camera");
    stopButton = new QPushButton("Stop Camera");
    stopButton->setEnabled(false);
    
    connect(playButton, &QPushButton::clicked, this, &VideoWindow::startVideo);
    connect(stopButton, &QPushButton::clicked, this, &VideoWindow::stopVideo);
    
    buttonLayout->addWidget(playButton);
    buttonLayout->addWidget(stopButton);
    buttonLayout->addStretch();
    
    mainLayout->addLayout(buttonLayout);
    
    // Create status bar
    statusBar()->showMessage("GStreamer Qt6 Camera Viewer - Ready");
}

void VideoWindow::setupGStreamer()
{
    // Create pipeline with V4L2 camera source and video overlay sink
    pipeline = gst_parse_launch(
        "v4l2src device=/dev/video0 ! image/jpeg,width=1280,height=720,framerate=30/1 ! jpegdec ! videoconvert ! xvimagesink name=videosink", NULL);
        
    if (!pipeline) {
        QMessageBox::critical(this, "Error", 
            "Failed to create GStreamer pipeline.\n\n"
            "Make sure you have a camera connected to /dev/video0\n"
            "and that your user has permission to access it.");
        return;
    }

    // Get the video sink
    sink = gst_bin_get_by_name(GST_BIN(pipeline), "videosink");
    if (!sink) {
        QMessageBox::critical(this, "Error", "Failed to get video sink element");
        gst_object_unref(pipeline);
        pipeline = nullptr;
        return;
    }

    statusBar()->showMessage("Camera pipeline ready - Video will render in the widget above");
}

bool VideoWindow::checkCameraAvailable()
{
    // Simple check if camera device exists
    return QFile::exists("/dev/video0");
}

void VideoWindow::startVideo()
{
    if (!pipeline || !sink) {
        QMessageBox::warning(this, "Warning", "Pipeline not initialized");
        return;
    }
    
    // Set the window handle for video overlay
    WId windowId = videoWidget->getWindowId();
    if (windowId) {
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(sink), windowId);
    }
    
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        QMessageBox::critical(this, "Error", "Failed to start video playback");
        statusBar()->showMessage("Failed to start playback");
    } else {
        isPlaying = true;
        playButton->setEnabled(false);
        stopButton->setEnabled(true);
        statusBar()->showMessage("Camera streaming - Live video should appear in the widget above");
        
        // Auto-stop after 30 seconds for demo (you can remove this)
        QTimer::singleShot(30000, this, &VideoWindow::stopVideo);
    }
}

void VideoWindow::stopVideo()
{
    if (!pipeline) return;
    
    gst_element_set_state(pipeline, GST_STATE_PAUSED);
    isPlaying = false;
    playButton->setEnabled(true);
    stopButton->setEnabled(false);
    statusBar()->showMessage("Camera stopped");
}

void VideoWindow::aboutApp()
{
    QMessageBox::about(this, "About", 
        "GStreamer Qt6 Camera Viewer\n\n"
        "This application captures live video from a camera\n"
        "(/dev/video0) and displays it directly in a Qt widget\n"
        "using GStreamer video overlay.\n\n"
        "Camera Settings:\n"
        "- Resolution: 1280x720\n"
        "- Format: JPEG\n"
        "- Framerate: 30 FPS\n\n"
        "Built with Qt6 and GStreamer 1.0");
}

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    QApplication app(argc, argv);

    VideoWindow window;
    window.show();

    int result = app.exec();
    gst_deinit();
    return result;
}

#include "widgets_main.moc"
