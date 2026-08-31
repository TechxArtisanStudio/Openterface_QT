#ifndef SCRIPTEXECUTOR_H
#define SCRIPTEXECUTOR_H

#include <QObject>
#include <QRect>
#include "target/MouseManager.h"
#include "KeyboardMouse.h"

/**
 * ScriptExecutor acts as a signal router between SemanticAnalyzer and the UI.
 * It forwards capture signals from the worker thread to the main thread.
 */
class ScriptExecutor : public QObject {
    Q_OBJECT
public:
    explicit ScriptExecutor(QObject* parent = nullptr);

    void setMouseManager(MouseManager* mm) { mouseManager = mm; }
    void setKeyboardMouse(KeyboardMouse* km) { keyboardMouse = km; }
    MouseManager* getMouseManager() const { return mouseManager; }
    KeyboardMouse* getKeyboardMouse() const { return keyboardMouse; }

signals:
    void captureImg(const QString& path = "");
    void captureAreaImg(const QString& path = "", const QRect& captureArea = QRect());

private:
    MouseManager* mouseManager = nullptr;
    KeyboardMouse* keyboardMouse = nullptr;
};

#endif // SCRIPTEXECUTOR_H