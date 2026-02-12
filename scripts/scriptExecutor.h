#ifndef SCRIPTEXECUTOR_H
#define SCRIPTEXECUTOR_H

#include <QObject>
#include <QPoint>
#include <QRegularExpression>
#include "AST.h"
#include "KeyboardMouse.h"
#include "target/MouseManager.h"
#include "regex/RegularExpression.h"

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

public slots:
    bool executeCommand(const ASTNode* node);

private:
    MouseManager* mouseManager = nullptr;
    KeyboardMouse* keyboardMouse = nullptr;
    RegularExpression& regex = RegularExpression::instance();
};

#endif // SCRIPTEXECUTOR_H