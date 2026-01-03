/*
* ========================================================================== *
*                                                                            *
*    Window Control Manager Example                                          *
*    Demonstrates usage of WindowControlManager for auto-hiding toolbar      *
*                                                                            *
* ========================================================================== *
*/

#include <QApplication>
#include <QMainWindow>
#include <QToolBar>
#include <QAction>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <QDebug>
#include "ui/windowcontrolmanager.h"

class ExampleMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    ExampleMainWindow(QWidget *parent = nullptr) : QMainWindow(parent)
    {
        setWindowTitle("Window Control Manager Example");
        resize(800, 600);
        
        // Create central widget with content
        QWidget *centralWidget = new QWidget(this);
        QVBoxLayout *layout = new QVBoxLayout(centralWidget);
        
        QLabel *instructionLabel = new QLabel(
            "<h2>Window Control Manager Demo</h2>"
            "<p><b>Instructions:</b></p>"
            "<ul>"
            "<li>Maximize this window to activate auto-hide</li>"
            "<li>The toolbar will hide after 10 seconds of inactivity</li>"
            "<li>Move your mouse to the top edge to show the toolbar</li>"
            "<li>The toolbar will hide again after 10 seconds</li>"
            "<li>Restore the window to normal size to disable auto-hide</li>"
            "</ul>",
            this
        );
        instructionLabel->setWordWrap(true);
        layout->addWidget(instructionLabel);
        
        statusLabel = new QLabel("Status: Normal mode", this);
        statusLabel->setStyleSheet("QLabel { padding: 10px; background-color: #e0e0e0; }");
        layout->addWidget(statusLabel);
        
        setCentralWidget(centralWidget);
        
        // Create toolbar with some actions
        QToolBar *toolbar = new QToolBar("Main Toolbar", this);
        toolbar->setMovable(false);
        
        QAction *action1 = toolbar->addAction("Action 1");
        QAction *action2 = toolbar->addAction("Action 2");
        QAction *action3 = toolbar->addAction("Action 3");
        
        connect(action1, &QAction::triggered, this, [this]() {
            statusLabel->setText("Status: Action 1 triggered");
        });
        
        connect(action2, &QAction::triggered, this, [this]() {
            statusLabel->setText("Status: Action 2 triggered");
        });
        
        connect(action3, &QAction::triggered, this, [this]() {
            statusLabel->setText("Status: Action 3 triggered");
        });
        
        addToolBar(Qt::TopToolBarArea, toolbar);
        
        // Create Window Control Manager
        windowControlManager = new WindowControlManager(this, toolbar, this);
        
        // Configure behavior
        windowControlManager->setAutoHideEnabled(true);
        windowControlManager->setAutoHideDelay(10000);  // 10 seconds
        windowControlManager->setEdgeDetectionThreshold(5);  // 5 pixels
        windowControlManager->setAnimationDuration(300);  // 300ms
        
        // Connect signals
        connect(windowControlManager, &WindowControlManager::toolbarVisibilityChanged,
                this, &ExampleMainWindow::onToolbarVisibilityChanged);
        
        connect(windowControlManager, &WindowControlManager::autoHideTriggered,
                this, &ExampleMainWindow::onAutoHideTriggered);
        
        connect(windowControlManager, &WindowControlManager::edgeHoverDetected,
                this, &ExampleMainWindow::onEdgeHoverDetected);
        
        // Create menu bar for testing
        QMenuBar *menuBar = new QMenuBar(this);
        QMenu *fileMenu = menuBar->addMenu("File");
        fileMenu->addAction("New");
        fileMenu->addAction("Open");
        fileMenu->addAction("Save");
        fileMenu->addSeparator();
        fileMenu->addAction("Exit", this, &QMainWindow::close);
        
        QMenu *viewMenu = menuBar->addMenu("View");
        QAction *toggleToolbarAction = viewMenu->addAction("Toggle Toolbar");
        connect(toggleToolbarAction, &QAction::triggered, this, [this]() {
            windowControlManager->toggleToolbar();
        });
        
        setMenuBar(menuBar);
        
        // Status bar
        statusBar()->showMessage("Ready");
    }
    
    ~ExampleMainWindow()
    {
        if (windowControlManager) {
            windowControlManager->setAutoHideEnabled(false);
            delete windowControlManager;
        }
    }

private slots:
    void onToolbarVisibilityChanged(bool visible)
    {
        QString status = visible ? "Toolbar shown" : "Toolbar hidden";
        statusLabel->setText(QString("Status: %1").arg(status));
        statusBar()->showMessage(status, 2000);
        qDebug() << "Toolbar visibility changed:" << visible;
    }
    
    void onAutoHideTriggered()
    {
        statusLabel->setText("Status: Toolbar auto-hidden after inactivity");
        statusBar()->showMessage("Toolbar auto-hidden", 2000);
        qDebug() << "Auto-hide triggered";
    }
    
    void onEdgeHoverDetected()
    {
        statusLabel->setText("Status: Mouse at top edge - showing toolbar");
        statusBar()->showMessage("Edge hover detected", 2000);
        qDebug() << "Edge hover detected";
    }

protected:
    void changeEvent(QEvent *event) override
    {
        if (event->type() == QEvent::WindowStateChange) {
            if (windowState() & Qt::WindowMaximized) {
                statusLabel->setText("Status: Window maximized - auto-hide enabled");
                statusBar()->showMessage("Window maximized - auto-hide enabled", 3000);
            } else if (windowState() & Qt::WindowFullScreen) {
                statusLabel->setText("Status: Window fullscreen - auto-hide enabled");
                statusBar()->showMessage("Window fullscreen - auto-hide enabled", 3000);
            } else {
                statusLabel->setText("Status: Window normal - auto-hide disabled");
                statusBar()->showMessage("Window normal - auto-hide disabled", 3000);
            }
        }
        QMainWindow::changeEvent(event);
    }

private:
    WindowControlManager *windowControlManager;
    QLabel *statusLabel;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    ExampleMainWindow window;
    window.show();
    
    return app.exec();
}

#include "windowcontrolmanager_example.moc"
