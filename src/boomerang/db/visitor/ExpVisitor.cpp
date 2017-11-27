#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "ExpVisitor.h"


bool ExpVisitor::visit(const std::shared_ptr<Unary>&, bool& visitChildren)
{
    visitChildren = true;
    return true;
}


bool ExpVisitor::visit(const std::shared_ptr<Binary>&, bool& visitChildren)
{
    visitChildren = true;
    return true;
}


bool ExpVisitor::visit(const std::shared_ptr<Ternary>&, bool& visitChildren)
{
    visitChildren = true;
    return true;
}


bool ExpVisitor::visit(const std::shared_ptr<TypedExp>&, bool& visitChildren)
{
    visitChildren = true;
    return true;
}


bool ExpVisitor::visit(const std::shared_ptr<FlagDef>&, bool& visitChildren)
{
    visitChildren = true;
    return true;
}


bool ExpVisitor::visit(const std::shared_ptr<RefExp>&, bool& visitChildren)
{
    visitChildren = true;
    return true;
}


bool ExpVisitor::visit(const std::shared_ptr<Location>&, bool& visitChildren)
{
    visitChildren = true;
    return true;
}


bool ExpVisitor::visit(const std::shared_ptr<Const>&)
{
    return true;
}


bool ExpVisitor::visit(const std::shared_ptr<Terminal>&)
{
    return true;
}
