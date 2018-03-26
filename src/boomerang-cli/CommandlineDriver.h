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


#include "boomerang-cli/Console.h"
#include "boomerang/core/Project.h"

#include <QObject>
#include <QTimer>
#include <QThread>


class DecompilationThread : public QThread
{
    Q_OBJECT

public:
    DecompilationThread(IProject *project);

    void run() override;

    void setPathToBinary(const QString value)
    {
        m_pathToBinary = value;
    }

    int resCode() { return m_result; }

private:
    /**
     * Loads the executable file and decodes it.
     * \param fname The name of the file to load.
     * \param pname How the Prog will be named.
     */
    bool loadAndDecode(const QString& fname, const QString& pname);

    /**
     * The program will be subsequently be loaded, decoded, decompiled and written to a source file.
     * After decompilation the elapsed time is printed to LOG_STREAM().
     *
     * \param fname The name of the file to load.
     * \param pname The name that will be given to the Proc.
     *
     * \return Zero on success, nonzero on faillure.
     */
    int decompile(const QString& fname, const QString& pname);

private:
    QString m_pathToBinary;
    int m_result = 0;
    IProject *m_project;
};


class CommandlineDriver : public QObject
{
    Q_OBJECT

public:
    explicit CommandlineDriver(QObject *parent = nullptr);

public:
    int applyCommandline(const QStringList& args);
    int decompile();

    /**
     * Displays a command line and processes the commands entered.
     *
     * \retval 0 stdin was closed.
     * \retval 2 The user typed exit or quit.
     */
    int interactiveMain();

public slots:
    void onCompilationTimeout();

private:
    Project m_project;
    Console m_console;
    DecompilationThread m_thread;
    QTimer m_kill_timer;
    int minsToStopAfter = 0;
};
