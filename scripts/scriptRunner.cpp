#include "scriptRunner.h"
#include "scriptExecutor.h"
#include "scripts/semanticAnalyzer.h"
#include "ui/advance/scripttool.h"
#include <QThread>
#include <QMetaObject>
#include <QDebug>

ScriptRunner::ScriptRunner(ScriptTool* tool, ScriptExecutor* executor, QObject* parent)
    : QObject(parent), m_tool(tool), m_executor(executor)
{
}

void ScriptRunner::runTree(std::shared_ptr<ASTNode> tree, QObject* originSender)
{
    if (!tree) {
        emit analysisFinished(originSender, false);
        return;
    }

    // Verify that executor and its managers are properly initialized
    if (!m_executor) {
        qWarning() << "Error: ScriptExecutor is not initialized";
        emit analysisFinished(originSender, false);
        return;
    }
    
    MouseManager* mouseManager = m_executor->getMouseManager();
    KeyboardMouse* keyboardMouse = m_executor->getKeyboardMouse();
    
    if (!mouseManager || !keyboardMouse) {
        qWarning() << "Error: MouseManager or KeyboardMouse not initialized in ScriptExecutor";
        emit analysisFinished(originSender, false);
        return;
    }

    QThread* workerThread = new QThread;
    SemanticAnalyzer* workerAnalyzer = new SemanticAnalyzer(mouseManager, keyboardMouse);
    workerAnalyzer->moveToThread(workerThread);

    // Route capture signals to executor so UI can respond
    if (m_executor) {
        connect(workerAnalyzer, &SemanticAnalyzer::captureImg, m_executor, &ScriptExecutor::captureImg, Qt::QueuedConnection);
        connect(workerAnalyzer, &SemanticAnalyzer::captureAreaImg, m_executor, &ScriptExecutor::captureAreaImg, Qt::QueuedConnection);
        connect(workerAnalyzer, &SemanticAnalyzer::commandData, m_executor, &ScriptExecutor::executeCommand, Qt::QueuedConnection);
    }

    // Connect command increase to the script tool UI
    if (m_tool) {
        connect(workerAnalyzer, &SemanticAnalyzer::commandIncrease, m_tool, &ScriptTool::handleCommandIncrement, Qt::QueuedConnection);
    }

    // When analysis finishes, emit up and stop thread
    connect(workerAnalyzer, &SemanticAnalyzer::analysisFinished, this, [this, originSender, workerThread](bool success){
        emit analysisFinished(originSender, success);
        workerThread->quit();
    });

    connect(workerThread, &QThread::finished, workerAnalyzer, &QObject::deleteLater);
    connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);

    workerThread->start();

    std::shared_ptr<ASTNode> treeRef = std::move(tree);
    QMetaObject::invokeMethod(workerAnalyzer, [workerAnalyzer, treeRef]() mutable {
        workerAnalyzer->analyzeTree(std::move(treeRef));
    }, Qt::QueuedConnection);
}
