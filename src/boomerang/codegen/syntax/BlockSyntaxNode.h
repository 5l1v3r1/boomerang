#pragma once

#include "boomerang/codegen/syntax/SyntaxNode.h"

class BlockSyntaxNode : public SyntaxNode
{
public:
    BlockSyntaxNode();
    virtual ~BlockSyntaxNode();

    virtual bool isBlock() const override;

    virtual void ignoreGoto() override;

    size_t getNumStatements() const;

    SyntaxNode *getStatement(size_t n);

    void prependStatement(SyntaxNode *n);

    void addStatement(SyntaxNode *n);

    void setStatement(size_t i, SyntaxNode *n);

    virtual size_t getNumOutEdges() const override;
    virtual SyntaxNode *getOutEdge(SyntaxNode *root, size_t n) override;

    virtual bool endsWithGoto() const override;

    virtual bool startsWith(SyntaxNode *node) const override;

    virtual SyntaxNode *getEnclosingLoop(SyntaxNode *base, SyntaxNode *cur = nullptr) override;

    virtual SyntaxNode *clone() override;
    virtual SyntaxNode *replace(SyntaxNode *from, SyntaxNode *to) override;

    virtual SyntaxNode *findNodeFor(BasicBlock *bb) override;
    virtual void printAST(SyntaxNode *root, QTextStream& os) override;
    virtual int evaluate(SyntaxNode *root) override;
    virtual void addSuccessors(SyntaxNode *root, std::vector<SyntaxNode *>& successors) override;

private:
    std::vector<SyntaxNode *> statements;
};
