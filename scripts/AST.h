#ifndef AST_H
#define AST_H

#include <memory>
#include <string>
#include <vector>

enum class ASTNodeType {
    Expression,
    Statement,
    ClickStatement,
    StatementList,
    // Add more node types as needed
};

class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual ASTNodeType getType() const = 0;
    virtual const std::vector<std::unique_ptr<ASTNode>>& getChildren() const { return children; }
protected:
    std::vector<std::unique_ptr<ASTNode>> children;
};

class ExpressionNode : public ASTNode {
public:
    ASTNodeType getType() const override { return ASTNodeType::Expression; }
    // Add expression-specific members
};

class StatementNode : public ASTNode {
public:
    ASTNodeType getType() const override { return ASTNodeType::Statement; }
    // Add statement-specific members
};

class ClickStatementNode : public ASTNode {
public:
    ClickStatementNode(const std::vector<std::string>& options) : options(options) {}
    ASTNodeType getType() const override { return ASTNodeType::ClickStatement; }
    const std::vector<std::string>& getOptions() const { return options; }
private:
    std::vector<std::string> options;
};

class StatementListNode : public ASTNode {
public:
    ASTNodeType getType() const override { return ASTNodeType::StatementList; }
    void addStatement(std::unique_ptr<ASTNode> statement) {
        if (statement) {
            children.push_back(std::move(statement));
        }
    }
};

// Add more specific node types as needed

#endif // AST_H 