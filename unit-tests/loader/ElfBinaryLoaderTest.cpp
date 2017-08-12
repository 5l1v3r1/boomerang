#include "ElfBinaryLoaderTest.h"

#include "boomerang/core/Boomerang.h"
#include "boomerang/util/Log.h"
#include "boomerang/core/BinaryFileFactory.h"
#include "boomerang/db/IBinaryImage.h"
#include "boomerang/db/IBinarySection.h"

#include <QLibrary>


#define HELLO_CLANG4           (BOOMERANG_TEST_BASE "/tests/inputs/elf/hello-clang4-dynamic")
#define HELLO_CLANG4_STATIC    (BOOMERANG_TEST_BASE "/tests/inputs/elf/hello-clang4-static")
#define HELLO_PENTIUM          (BOOMERANG_TEST_BASE "/tests/inputs/pentium/hello")


/// path to the ELF loader plugin
#ifdef _WIN32
#  define ELF_LOADER    (BOOMERANG_TEST_BASE "/lib/libboomerang-ElfLoader.dll")
#else
#  define ELF_LOADER    (BOOMERANG_TEST_BASE "/lib/libboomerang-ElfLoader.so")
#endif


void ElfBinaryLoaderTest::initTestCase()
{
    Boomerang::get()->getSettings()->setDataDirectory(BOOMERANG_TEST_BASE "/lib/boomerang/");
}


void ElfBinaryLoaderTest::testElfLoadClang()
{
    BinaryFileFactory bff;
    IFileLoader       *loader = bff.loadFile(HELLO_CLANG4);

    // test the loader
    QVERIFY(loader != nullptr);
    QCOMPARE(loader->getFormat(), LoadFmt::ELF);
    QCOMPARE(loader->getMachine(), Machine::PENTIUM);
    QCOMPARE(loader->hasDebugInfo(), false);
    QCOMPARE(loader->getEntryPoint(),     Address(0x080482F0));
    QCOMPARE(loader->getMainEntryPoint(), Address(0x080483F0));

    // test the loaded image
    IBinaryImage *image = Boomerang::get()->getImage();
    QVERIFY(image != nullptr);

    QCOMPARE(image->getNumSections(), (size_t)29);
    QCOMPARE(image->getSectionInfo(0)->getName(),  QString(".interp"));
    QCOMPARE(image->getSectionInfo(10)->getName(), QString(".plt"));
    QCOMPARE(image->getSectionInfo(28)->getName(), QString(".shstrtab"));
    QCOMPARE(image->getLimitTextLow(),  Address(0x08000001));
    QCOMPARE(image->getLimitTextHigh(), Address(0x0804A020));
}


void ElfBinaryLoaderTest::testElfLoadClangStatic()
{
    BinaryFileFactory bff;
    IFileLoader       *loader = bff.loadFile(HELLO_CLANG4_STATIC);

    // test the loader
    QVERIFY(loader != nullptr);
    QCOMPARE(loader->getFormat(), LoadFmt::ELF);
    QCOMPARE(loader->getMachine(), Machine::PENTIUM);
    QCOMPARE(loader->hasDebugInfo(), false);
    QCOMPARE(loader->getEntryPoint(),     Address(0x0804884F));
    QCOMPARE(loader->getMainEntryPoint(), Address(0x080489A0));

    // test the loaded image
    IBinaryImage *image = Boomerang::get()->getImage();
    QVERIFY(image != nullptr);

    QCOMPARE(image->getNumSections(), (size_t)29);
    QCOMPARE(image->getSectionInfo(0)->getName(), QString(".note.ABI-tag"));
    QCOMPARE(image->getSectionInfo(13)->getName(), QString(".eh_frame"));
    QCOMPARE(image->getSectionInfo(28)->getName(), QString(".shstrtab"));
    QCOMPARE(image->getLimitTextLow(),  Address(0x08000001));
    QCOMPARE(image->getLimitTextHigh(), Address(0x080ECDA4));
}


void ElfBinaryLoaderTest::testPentiumLoad()
{
    // Load Pentium hello world
    BinaryFileFactory bff;
    IFileLoader       *loader = bff.loadFile(HELLO_PENTIUM);

    QVERIFY(loader != nullptr);
    QCOMPARE(loader->getFormat(), LoadFmt::ELF);
    QCOMPARE(loader->getMachine(), Machine::PENTIUM);

    IBinaryImage *image = Boomerang::get()->getImage();
    QVERIFY(image != nullptr);

    QCOMPARE(image->getNumSections(), (size_t)33);
    QCOMPARE(image->getSectionInfo(1)->getName(), QString(".note.ABI-tag"));
    QCOMPARE(image->getSectionInfo(32)->getName(), QString(".strtab"));
}


typedef unsigned (*ElfHashFcn)(const char *);

void ElfBinaryLoaderTest::testElfHash()
{
    QLibrary z;

    z.setFileName(ELF_LOADER);
    bool opened = z.load();
    QVERIFY(opened);

    // Use the handle to find the "elf_hash" function
    ElfHashFcn hashFcn = (ElfHashFcn)z.resolve("elf_hash");
    QVERIFY(hashFcn);

    // Call the function with the string "main
    unsigned int hashValue = hashFcn("main");
    QCOMPARE(hashValue, 0x737FEU);
}

QTEST_MAIN(ElfBinaryLoaderTest)

