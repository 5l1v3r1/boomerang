#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "Class.h"


Class::Class(const QString &name, Prog *_prog)
    : Module(name, _prog)
    , m_type(CompoundType::get())
{
}
