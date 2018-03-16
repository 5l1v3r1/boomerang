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


class BinaryImage;
class IFileLoader;
class ITypeRecovery;
class QString;

/**
 * The Project interface class
 */
class IProject
{
public:
    virtual ~IProject() = default;

public:
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
    virtual void unloadBinaryFile() = 0;

    /**
     * Get the binary image of the loaded binary.
     */
    virtual BinaryImage *getImage()             = 0;
    virtual const BinaryImage *getImage() const = 0;

    /**
     * \returns the type recovery engine
     */
    virtual ITypeRecovery *getTypeRecoveryEngine() const = 0;

    virtual IFileLoader *getBestLoader(const QString& filePath) const = 0;
};
