/* File: BinaryFileFactory.cpp
 * Desc: This file contains the implementation of the factory function
 * BinaryFile::getInstanceFor(), and also BinaryFile::Load()
 *
 * This function determines the type of a binary and loads the appropriate
 * loader class dynamically.
*/

#include "BinaryFile.h"
#include "boomerang.h"
#include "IBinaryImage.h"

#include <QDir>
#include <QPluginLoader>
#include <QCoreApplication>
#include <QString>
#include <QDebug>
#include <cstdio>

#define LMMH(x)                                                                                                        \
    ((unsigned)((Byte *)(&x))[0] + ((unsigned)((Byte *)(&x))[1] << 8) + ((unsigned)((Byte *)(&x))[2] << 16) +          \
     ((unsigned)((Byte *)(&x))[3] << 24))

using namespace std;
QString BinaryFileFactory::m_base_path = "";

QObject *BinaryFileFactory::Load(const QString &sName) {
    IBinaryImage *Image = Boomerang::get()->getImage();
    Image->reset();
    QObject *pBF = getInstanceFor(sName);
    LoaderInterface *ldr_iface = qobject_cast<LoaderInterface *>(pBF);
    if (ldr_iface == nullptr) {
        fprintf(stderr, "unrecognised binary file format.\n");
        return nullptr;
    }
    if (ldr_iface->RealLoad(sName) == 0) {
        fprintf(stderr, "Loading '%s' failed\n", qPrintable(sName));
        delete pBF;
        return nullptr;
    }
    Image->calculateTextLimits();
    return pBF;
}

#define TESTMAGIC2(buf, off, a, b) (buf[off] == a && buf[off + 1] == b)
#define TESTMAGIC4(buf, off, a, b, c, d) (buf[off] == a && buf[off + 1] == b && buf[off + 2] == c && buf[off + 3] == d)

static QString selectPluginForFile(const QString &sName) {
    QString libName;
    unsigned char buf[64];
    QFile f(sName);
    if(!f.open(QFile::ReadOnly)) {
        fprintf(stderr, "Unable to open binary file: %s\n", qPrintable(sName));
        return nullptr;
    }
    f.read((char *)buf,sizeof(buf));
    if (TESTMAGIC4(buf, 0, '\177', 'E', 'L', 'F')) {
        /* ELF Binary */
        libName = "ElfBinaryFile";
    } else if (TESTMAGIC2(buf, 0, 'M', 'Z')) { /* DOS-based file */
        int peoff = LMMH(buf[0x3C]);
        if (peoff != 0 && f.seek(peoff) ) {
            f.read((char *)buf,4);
            if (TESTMAGIC4(buf, 0, 'P', 'E', 0, 0)) {
                /* Win32 Binary */
                libName = "Win32BinaryFile";
            } else if (TESTMAGIC2(buf, 0, 'N', 'E')) {
                /* Win16 / Old OS/2 Binary */
            } else if (TESTMAGIC2(buf, 0, 'L', 'E')) {
                /* Win32 VxD (Linear Executable) or DOS4GW app */
                libName = "DOS4GWBinaryFile";
            } else if (TESTMAGIC2(buf, 0, 'L', 'X')) {
                /* New OS/2 Binary */
            }
        }
        /* Assume MS-DOS Real-mode binary. */
        if (libName.size() == 0)
            libName = "ExeBinaryFile";
    } else if (TESTMAGIC4(buf, 0x3C, 'a', 'p', 'p', 'l') || TESTMAGIC4(buf, 0x3C, 'p', 'a', 'n', 'l')) {
        /* PRC Palm-pilot binary */
        libName = "PalmBinaryFile";
    } else if ((buf[0] == 0xfe && buf[1] == 0xed && buf[2] == 0xfa && buf[3] == 0xce) ||
               (buf[0] == 0xce && buf[1] == 0xfa && buf[2] == 0xed && buf[3] == 0xfe) ||
               (buf[0] == 0xca && buf[1] == 0xfe && buf[2] == 0xba && buf[3] == 0xbe)) {
        /* Mach-O Mac OS-X binary */
        libName = "MachOBinaryFile";
    } else if (buf[0] == 0x02 && buf[2] == 0x01 && (buf[1] == 0x10 || buf[1] == 0x0B) &&
               (buf[3] == 0x07 || buf[3] == 0x08 || buf[4] == 0x0B)) {
        /* HP Som binary (last as it's not really particularly good magic) */
        libName = "HpSomBinaryFile";
    } else if (buf[0] == 0x4c && buf[1] == 0x01) {
        libName = "IntelCoffFile";
    } else {
        fprintf(stderr, "Unrecognised binary file\n");
        return "";
    }
    return libName;
}
/**
 * Perform simple magic on the file by the given name in order to determine the appropriate type, and then return an
 * instance of the appropriate subclass.
 */
QObject *BinaryFileFactory::getInstanceFor(const QString &sName) {
    QDir pluginsDir(qApp->applicationDirPath());
    pluginsDir.cd("lib");
    if (!qApp->libraryPaths().contains(pluginsDir.absolutePath())) {
        qApp->addLibraryPath(pluginsDir.absolutePath());
    }
    QString libName = selectPluginForFile(sName);
    if (libName.isEmpty())
        return nullptr;

    QPluginLoader plugin_loader(libName);
    if (!plugin_loader.load()) {
        qCritical() << plugin_loader.errorString();
        return nullptr;
    }
    return plugin_loader.instance();
}

void BinaryFileFactory::UnLoad() {

}
