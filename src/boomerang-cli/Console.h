#pragma once

#include <QStringList>
#include <QMap>

enum class CommandStatus
{
    Success      = 0,
    AsyncSuccess = 1,  ///< Command started asynchronously
    ExitProgram  = 2,
    Failure      = -1, ///< Generic error
    ParseError   = -2
};

enum CommandType
{
    CT_unknown   = -1,
    CT_decode    = 1,
    CT_load      = 2,
    CT_save      = 3,
    CT_decompile = 4,
    CT_codegen   = 5,
    CT_move      = 6,
    CT_add       = 7,
    CT_delete    = 8,
    CT_rename    = 9,
    CT_info      = 10,
    CT_printRTL  = 11,
    CT_exit      = 12,
    CT_help      = 13,
    CT_callgraph = 14,
    CT_printCFG  = 15,
    CT_replay    = 16
};


/**
 * Console for interactive mode.
 * Parses arguments and passes them to libboomerang to be executed.
 */
class Console
{
public:
    Console();

    CommandStatus handleCommand(const QString& command);

    /// Execute the commands in \p file line by line
    CommandStatus replayFile(const QString& file);

    bool commandSucceeded(CommandStatus status)
    {
        return status == CommandStatus::Success || status == CommandStatus::AsyncSuccess;
    }
private:
    /**
     * Split \p commandWithArgs into \p command and \p args.
     * \returns Success or ParseError
     */
    CommandStatus splitCommand(const QString& commandWithArgs, QString& command, QStringList& args);

    CommandType commandNameToType(const QString& command);

    CommandStatus processCommand(const QString& command, const QStringList& args);

private:
    CommandStatus handleDecode(const QStringList& args);
    CommandStatus handleDecompile(const QStringList& args);
    CommandStatus handleCodegen(const QStringList& args);
    CommandStatus handleCallgraph(const QStringList& args);
    CommandStatus handlePrintCfg(const QStringList& args);
    CommandStatus handleReplay(const QStringList& args);
    CommandStatus handlePrintRTL(const QStringList& args);
    CommandStatus handleMove(const QStringList& args);
    CommandStatus handleAdd(const QStringList& args);
    CommandStatus handleDelete(const QStringList& args);
    CommandStatus handleRename(const QStringList& args);
    CommandStatus handleInfo(const QStringList& args);

    CommandStatus handleExit(const QStringList& args);
    CommandStatus handleHelp(const QStringList& args);

private:
    QMap<QString, CommandType> m_commandTypes;
};
