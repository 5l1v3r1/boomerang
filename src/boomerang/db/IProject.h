#pragma once

class IBinaryImage;
class QByteArray;
class QString;
struct ITypeRecovery;

/**
 * \brief The Project interface class
 */
class IProject
{
public:
    IProject() {}
    virtual ~IProject() {}

    /**
     * Import a binary file from \p filePath.
     * Loads the binary file and decodes it.
     * If a binary file is already loaded, it is unloaded first (all unsaved data is lost).
     * \returns whether loading was successful.
     */
    virtual bool loadBinaryFile(const QString& filePath) = 0;

    /**
     * Loads a saved file from \p filePath.
     * If a binary file is already loaded, it is unloaded first (all unsaved data is lost).
     * \returns whether loading was successful.
     */
    virtual bool loadSaveFile(const QString& filePath) = 0;

    /**
     * Saves data to the save file at \p filePath.
     * If the file already exists, it is overwritten.
     * \returns whether saving was successful.
     */
    virtual bool writeSaveFile(const QString& filePath) = 0;

    /**
     * Checks if the project contains a loaded binary.
     */
    virtual bool isBinaryLoaded() const = 0;

    /**
     * Unloads the loaded binary file.
     * If there is no loaded binary, nothing happens.
     */
    virtual void unload() = 0;

    virtual QByteArray& getFiledata()             = 0;
    virtual const QByteArray& getFiledata() const = 0;
    virtual IBinaryImage *getOrCreateImage()      = 0;
};
