#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "Proc.h"


#include "boomerang/core/Project.h"
#include "boomerang/db/Module.h"
#include "boomerang/db/Prog.h"
#include "boomerang/db/statements/CallStatement.h"
#include "boomerang/util/Log.h"


Function::Function(Address entryAddr, const std::shared_ptr<Signature>& sig, Module *module)
    : m_module(module)
    , m_entryAddress(entryAddr)
    , m_signature(sig)
{
    if (module) {
        m_prog = module->getProg();
    }
}


Function::~Function()
{
}


QString Function::getName() const
{
    assert(m_signature);
    return m_signature->getName();
}


void Function::setName(const QString& name)
{
    assert(m_signature);
    m_signature->setName(name);
}


Address Function::getEntryAddress() const
{
    return m_entryAddress;
}


void Function::setEntryAddress(Address entryAddr)
{
    if (m_module) {
        m_module->setLocationMap(m_entryAddress, nullptr);
        m_module->setLocationMap(entryAddr,      this);
    }

    m_entryAddress = entryAddr;
}


Prog *Function::getProg()
{
    return m_prog;
}


const Prog *Function::getProg() const
{
    return m_prog;
}


void Function::setModule(Module *module)
{
    if (module == m_module) {
        return;
    }

    removeFromModule();
    m_module = module;
    module->getFunctionList().push_back(this);
    module->setLocationMap(m_entryAddress, this);
}


void Function::removeFromModule()
{
    assert(m_module);
    m_module->getFunctionList().remove(this);
    m_module->setLocationMap(m_entryAddress, nullptr);
}


void Function::removeParameter(SharedExp e)
{
    const int n = m_signature->findParam(e);

    if (n != -1) {
        m_signature->removeParameter(n);

        for (auto const& elem : m_callers) {
            if (m_prog->getProject()->getSettings()->debugUnused) {
                LOG_MSG("Removing argument %1 in pos %2 from %3", e, n, elem);
            }

            (elem)->removeArgument(n);
        }
    }
}


void Function::renameParameter(const QString& oldName, const QString& newName)
{
    m_signature->renameParam(oldName, newName);
}
