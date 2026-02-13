#ifndef SCRIPTRUNNER_H
#define SCRIPTRUNNER_H

#include <QObject>
#include <memory>
#include <QLoggingCategory>

class ScriptTool;
class ScriptExecutor;
class ASTNode;

Q_DECLARE_LOGGING_CATEGORY(log_script_runner)

class ScriptRunner : public QObject {
    Q_OBJECT
public:
    explicit ScriptRunner(ScriptTool* tool, ScriptExecutor* executor, QObject* parent = nullptr);
    void runTree(std::shared_ptr<ASTNode> tree, QObject* originSender = nullptr);

signals:
    void analysisFinished(QObject* originSender, bool success);

private:
    ScriptTool* m_tool;
    ScriptExecutor* m_executor;
};

#endif // SCRIPTRUNNER_H