#
# This file is part of the Boomerang Decompiler.
#
# See the file "LICENSE.TERMS" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL
# WARRANTIES.
#


# This script will perform configuration of all system specific settings
include(CheckIncludeFile)
include(CheckTypeSize)
include(CheckLibraryExists)
include(TestBigEndian)

CHECK_INCLUDE_FILE(byteswap.h HAVE_BYTESWAP_H)
CHECK_INCLUDE_FILE(dlfcn.h HAVE_DLFCN_H)
CHECK_INCLUDE_FILE(fcntl.h HAVE_FCNTL_H)
CHECK_INCLUDE_FILE(inttypes.h HAVE_INTTYPES_H)
CHECK_INCLUDE_FILE(unistd.h HAVE_UNISTD_H)
CHECK_INCLUDE_FILE(malloc.h HAVE_MALLOC_H)
CHECK_INCLUDE_FILE(memory.h HAVE_MEMORY_H)
CHECK_INCLUDE_FILE(stdint.h HAVE_STDINT_H)
CHECK_INCLUDE_FILE(stdlib.h HAVE_STDLIB_H)
CHECK_INCLUDE_FILE(strings.h HAVE_STRINGS_H)
CHECK_INCLUDE_FILE(string.h HAVE_STRING_H)
CHECK_INCLUDE_FILE(sys/stat.h HAVE_SYS_STAT_H)
CHECK_INCLUDE_FILE(sys/time.h HAVE_SYS_TIME_H)
CHECK_INCLUDE_FILE(sys/types.h HAVE_SYS_TYPES_H)
CHECK_INCLUDE_FILE(unistd.h HAVE_UNISTD_H)


CHECK_TYPE_SIZE(char SIZEOF_CHAR)
CHECK_TYPE_SIZE(double SIZEOF_DOUBLE)
CHECK_TYPE_SIZE(float SIZEOF_FLOAT)
CHECK_TYPE_SIZE(int SIZEOF_INT)
CHECK_TYPE_SIZE("int *" SIZEOF_INT_P)
CHECK_TYPE_SIZE(long SIZEOF_LONG)
CHECK_TYPE_SIZE("long double" SIZEOF_LONG_DOUBLE)
CHECK_TYPE_SIZE("long long" SIZEOF_LONG_LONG)
CHECK_TYPE_SIZE(short SIZEOF_SHORT)

TEST_BIG_ENDIAN(WORDS_BIGENDIAN)

# Check for big/little endian
if (WORDS_BIGENDIAN)
    add_definitions(-DBOOMERANG_BIG_ENDIAN=1)
else ()
    add_definitions(-DBOOMERANG_BIG_ENDIAN=0)
endif ()

# Check 32/64 bit system
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_definitions(-DBOOMERANG_BITNESS=64)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(WARNING "Compiling Boomerang as a 32 bit binary is not officially supported."
            "Please consider compiling Boomerang as a 64 bit binary.")
    add_definitions(-DBOOMERANG_BITNESS=32)
else ()
    message(FATAL_ERROR "Unknown platform with sizeof(void*) == ${CMAKE_SIZEOF_VOID_P}")
endif ()


add_definitions(-DDEBUG=0)

# Define this to 1 if you want to use Dominance Numbers for analysis
add_definitions(-DUSE_DOMINANCE_NUMS=0)
add_definitions(-DBCCTR_LONG=0)
add_definitions(-DYYMAXDEPTH=10000)
add_definitions(-DSYMS_IN_BACK_END=0)
add_definitions(-DDEBUG_SPLIT_FOR_BRANCH=0)
add_definitions(-DNEW=0)
add_definitions(-DCHECK_REAL_PHI_LOOPS=0)
add_definitions(-DPRINT_BBINDEX=0)    # Non zero to print <index>: before <statement number>
add_definitions(-DPRINT_BACK_EDGES=0) # Non zero to generate green back edges
add_definitions(-DDEBUG_SIMP=0)       # Set to 1 to print every change
add_definitions(-DDEBUG_PARAMS=1)     #
add_definitions(-DRECURSION_WIP=0)
add_definitions(-DPRINT_UNION=0)      # Set to 1 to debug unions to stderr
add_definitions(-DV9_ONLY=0)
add_definitions(-DBRANCH_DS_ERROR=0)  # If set, a branch to the delay slot of a delayed
                                      # CTI instruction is flagged as an error

# Boomerang configuration options
option(BOOMERANG_BUILD_TESTS "Build the testing tree. Requires Qt5Test." OFF)
option(BOOMERANG_BUILD_GUI   "Build the GUI interface. Requires Qt5Widgets." ON)
option(BOOMERANG_BUILD_CLI   "Build the command line interface." ON)
