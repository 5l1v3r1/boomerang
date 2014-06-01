#pragma once
#include "exp.h"
/* before anything */
#define YY_USE_CLASS
#include <cstdio>

#include "config.h"
#ifdef HAVE_LIBGC
#include "gc.h"
#else
#define NO_GARBAGE_COLLECTOR
#endif
#include <cassert>
#include <sstream>
#include "types.h"
#include "rtl.h"
#include "table.h"
#include "insnameelem.h"
#include "util.h" // E.g. str()
#include "statement.h"

class SSLScanner;

union yy_SSLParser_stype {
    Exp *exp;
    char *str;
    int num;
    double dbl;
    Instruction *regtransfer;
    Type *typ;

    Table *tab;
    InsNameElem *insel;
    std::list<std::string> *parmlist;
    std::list<std::string> *strlist;
    std::deque<Exp *> *exprlist;
    std::deque<std::string> *namelist;
    std::list<Exp *> *explist;
    RTL *rtlist;
};
#define YY_SSLParser_DEBUG 1
/* use no goto to be clean in C++ */
//#define YY_SSLParser_USE_GOTO 0

#ifndef YY_SSLParser_PURE
#endif

/* prefix */
#ifndef YY_SSLParser_DEBUG

/* YY_SSLParser_DEBUG */
#endif
#ifndef YY_SSLParser_LSP_NEEDED

//#line 70 "/usr/local/lib/bison.h"
/* YY_SSLParser_LSP_NEEDED*/
#endif
/* DEFAULT STYPE*/
/* DEFAULT MISCELANEOUS */
#ifndef YY_SSLParser_DEBUG_FLAG
#define YY_SSLParser_DEBUG_FLAG yydebug
#endif

/* TOKEN C */
class SSLParser {
  public:
    enum YY_SSLParser_ENUM_TOKEN {
        YY_SSLParser_NULL_TOKEN = 0

        /* #line 185 "/usr/local/lib/bison.h" */
        ,
        COND_OP = 258,
        BIT_OP = 259,
        ARITH_OP = 260,
        LOG_OP = 261,
        NAME = 262,
        ASSIGNTYPE = 263,
        REG_ID = 264,
        REG_NUM = 265,
        COND_TNAME = 266,
        DECOR = 267,
        FARITH_OP = 268,
        FPUSH = 269,
        FPOP = 270,
        TEMP = 271,
        SHARES = 272,
        CONV_FUNC = 273,
        TRUNC_FUNC = 274,
        TRANSCEND = 275,
        FABS_FUNC = 276,
        BIG = 277,
        LITTLE = 278,
        NAME_CALL = 279,
        NAME_LOOKUP = 280,
        ENDIANNESS = 281,
        COVERS = 282,
        INDEX = 283,
        NOT = 284,
        LNOT = 285,
        FNEG = 286,
        THEN = 287,
        LOOKUP_RDC = 288,
        BOGUS = 289,
        ASSIGN = 290,
        TO = 291,
        COLON = 292,
        S_E = 293,
        AT = 294,
        ADDR = 295,
        REG_IDX = 296,
        EQUATE = 297,
        MEM_IDX = 298,
        TOK_INTEGER = 299,
        TOK_FLOAT = 300,
        FAST = 301,
        OPERAND = 302,
        FETCHEXEC = 303,
        CAST_OP = 304,
        FLAGMACRO = 305,
        SUCCESSOR = 306,
        NUM = 307,
        FLOATNUM = 308,
        FCHS = 309
        /* enum token */
    }; /* end of enum declaration */
  public:
    int yyparse(RTLInstDict &Dict);
    virtual void yyerror(const char *msg);
    virtual int yylex();
    yy_SSLParser_stype yylval;
    int yynerrs;
    int yychar;
#if YY_SSLParser_DEBUG != 0
  public:
    int YY_SSLParser_DEBUG_FLAG; /*  nonzero means print parse trace    */
#endif
  public:
    SSLParser(const std::string &sslFile, bool trace);

  public:
    SSLParser(std::istream &in, bool trace);
    virtual ~SSLParser();
    OPER strToOper(const char *s);               /* Convert string to an operator */
    static Instruction *parseExp(const char *str); /* Parse an expression or assignment from a string */
    /* The code for expanding tables and saving to the dictionary */
    void expandTables(InsNameElem *iname, std::list<std::string> *params, RTL *o_rtlist, RTLInstDict &Dict);
    Exp *makeSuccessor(Exp *e); /* Get successor (of register expression) */

    /*
    * The scanner.
    */
    SSLScanner *theScanner;

  protected:
    /*
    * The file from which the SSL spec is read.
    */
    std::string sslFile;

    /*
    * Result for parsing an assignment.
    */
    Instruction *the_asgn;

    /*
    * Maps SSL constants to their values.
    */
    std::map<std::string, int> ConstTable;

    /*
    * maps index names to instruction name-elements
    */
    std::map<std::string, InsNameElem *> indexrefmap;

    /*
    * Maps table names to Table's.
    */
    std::map<std::string, Table *> TableDict;

    /*
    * True when FLOAT keyword seen; false when INTEGER keyword seen
    * (in @REGISTER section)
    */
    bool bFloat;
};
/* END */
