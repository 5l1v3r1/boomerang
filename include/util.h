/***************************************************************************/ /**
  * \file       util.h
  * OVERVIEW:   Provides the definition for the miscellaneous bits and pieces
  *                 implemented in the util.so library
  ******************************************************************************/
#ifndef __UTIL_H__
#define __UTIL_H__

#include <QString>
#include <string>

// was a workaround
#define STR(x) (char *)(x.str().c_str())

void escapeXMLChars(std::string &s);
QString escapeStr(const QString &str);
int lockFileWrite(const char *fname);
void unlockFile(int n);
#ifdef __MINGW32__
extern "C" { extern char *strdup(const char *__s); }
#endif
#endif
