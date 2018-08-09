#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#pragma once


#include "boomerang/util/OStream.h"

#include <QFile>
#include <QStringList>

#include <map>


class Module;


class CodeWriter
{
    struct WriteDest
    {
        WriteDest(const QString& outFileName);
        WriteDest(const WriteDest&) = delete;
        WriteDest(WriteDest&&) = default;

        ~WriteDest();

        WriteDest& operator=(const WriteDest&) = delete;
        WriteDest& operator=(WriteDest&&) = default;

        QFile m_outFile;
        OStream m_os;
    };

    typedef std::map<const Module *, WriteDest> WriteDestMap;

public:
    bool writeCode(const Module *module, const QStringList& lines);

private:
    WriteDestMap m_dests;
};
