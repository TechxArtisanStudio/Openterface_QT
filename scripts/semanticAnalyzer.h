#ifndef SEMANTIC_ANALYZER_H
#define SEMANTIC_ANALYZER_H

#include "AST.h"
#include "target/MouseManager.h"
#include <memory>
#include <QPoint>
#include <QString>

class SemanticAnalyzer {
public:
    SemanticAnalyzer(MouseManager* mouseManager);
    void analyze(const ASTNode* node);

private:
    MouseManager* mouseManager;
    
    void analyzeClickStatement(const ClickStatementNode* node);
    QPoint parseCoordinates(const std::vector<std::string>& options);
    int parseMouseButton(const std::vector<std::string>& options);
    void resetParameters();
};

#endif // SEMANTIC_ANALYZER_H
