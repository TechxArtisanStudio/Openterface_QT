/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/ 

#ifndef AST_H
#define AST_H

#include <memory>
#include <string>
#include <vector>
#include <QString>

enum class ASTNodeType {
    Expression,
    Statement,
    CommandStatement,

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

class CommandStatementNode : public ASTNode {
public:
    CommandStatementNode(const std::vector<std::string>& options) : options(options) {}
    ASTNodeType getType() const override { return ASTNodeType::CommandStatement; }
    const std::vector<std::string>& getOptions() const { return options; }
    const QString getCommandName() const { return commandName; }
    void setCommandName(QString name) { commandName = name; }
private:
    std::vector<std::string> options;
    QString commandName;
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
