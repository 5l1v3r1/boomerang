#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "Type.h"


#include "boomerang/core/Boomerang.h"
#include "boomerang/db/CFG.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/db/Signature.h"
#include "boomerang/db/exp/Const.h"
#include "boomerang/db/exp/Binary.h"
#include "boomerang/db/exp/Location.h"
#include "boomerang/db/exp/RefExp.h"
#include "boomerang/db/exp/Terminal.h"
#include "boomerang/type/DataIntervalMap.h"
#include "boomerang/type/type/CompoundType.h"
#include "boomerang/type/type/PointerType.h"
#include "boomerang/type/type/ArrayType.h"
#include "boomerang/type/type/UnionType.h"
#include "boomerang/type/type/VoidType.h"
#include "boomerang/type/type/IntegerType.h"
#include "boomerang/type/type/SizeType.h"
#include "boomerang/type/type/CharType.h"
#include "boomerang/type/type/BooleanType.h"
#include "boomerang/type/type/FloatType.h"
#include "boomerang/util/Log.h"
#include "boomerang/util/Types.h"
#include "boomerang/util/Util.h"

#include <cassert>
#include <cstring>


/// For NamedType
static QMap<QString, SharedType> namedTypes;


Type::Type(TypeID _id)
    : id(_id)
{
}


Type::~Type()
{
}


bool Type::isCString() const
{
    if (!resolvesToPointer()) {
        return false;
    }

    SharedType p = as<PointerType>()->getPointsTo();

    if (p->resolvesToChar()) {
        return true;
    }

    if (!p->resolvesToArray()) {
        return false;
    }

    p = p->as<ArrayType>()->getBaseType();
    return p->resolvesToChar();
}


SharedType Type::parseType(const char *)
{
    assert(!"Not implemented");
    return nullptr;
}


bool Type::operator!=(const Type& other) const
{
    return !(*this == other);
}


QString Type::prints()
{
    return getCtype(false); // For debugging
}


void Type::dump()
{
    LOG_MSG("%1", getCtype(false)); // For debugging
}


void Type::addNamedType(const QString& name, SharedType type)
{
    if (namedTypes.find(name) != namedTypes.end()) {
        if (!(*type == *namedTypes[name])) {
            LOG_WARN("Redefinition of type %1", name);
            LOG_WARN(" type     = %1", type->prints());
            LOG_WARN(" previous = %1", namedTypes[name]->prints());
            namedTypes[name] = type; // WARN: was *type==*namedTypes[name], verify !
        }
    }
    else {
        // check if it is:
        // typedef int a;
        // typedef a b;
        // we then need to define b as int
        // we create clones to keep the GC happy
        if (namedTypes.find(type->getCtype()) != namedTypes.end()) {
            namedTypes[name] = namedTypes[type->getCtype()]->clone();
        }
        else {
            namedTypes[name] = type->clone();
        }
    }
}


SharedType Type::getNamedType(const QString& name)
{
    auto iter = namedTypes.find(name);

    return (iter != namedTypes.end()) ? *iter : nullptr;
}


void Type::dumpNames()
{
    for (auto it = namedTypes.begin(); it != namedTypes.end(); ++it) {
        LOG_VERBOSE("%1 -> %2", it.key(), it.value()->getCtype());
    }
}


SharedType Type::getTempType(const QString& name)
{
    SharedType ty;
    QChar      ctype = ' ';

    if (name.size() > 3) {
        ctype = name[3];
    }

    switch (ctype.toLatin1())
    {
    // They are all int32, except for a few specials
    case 'f':
        ty = FloatType::get(32);
        break;

    case 'd':
        ty = FloatType::get(64);
        break;

    case 'F':
        ty = FloatType::get(80);
        break;

    case 'D':
        ty = FloatType::get(128);
        break;

    case 'l':
        ty = IntegerType::get(64);
        break;

    case 'h':
        ty = IntegerType::get(16);
        break;

    case 'b':
        ty = IntegerType::get(8);
        break;

    default:
        ty = IntegerType::get(32);
        break;
    }

    return ty;
}


QString Type::getTempName() const
{
    return "tmp"; // what else can we do? (besides panic)
}


void Type::clearNamedTypes()
{
    namedTypes.clear();
}


// Note: don't want to call this->resolve() for this case, since then we (probably) won't have a NamedType and the
// assert will fail
#define RESOLVES_TO_TYPE(x)                                                   \
    bool Type::resolvesTo ## x() const {                                      \
        auto ty = shared_from_this();                                         \
        if (ty->isNamed()) {                                                  \
            ty = std::static_pointer_cast<const NamedType>(ty)->resolvesTo(); \
        }                                                                     \
        return ty && ty->is ## x();                                           \
    }

RESOLVES_TO_TYPE(Void)
RESOLVES_TO_TYPE(Func)
RESOLVES_TO_TYPE(Boolean)
RESOLVES_TO_TYPE(Char)
RESOLVES_TO_TYPE(Integer)
RESOLVES_TO_TYPE(Float)
RESOLVES_TO_TYPE(Pointer)
RESOLVES_TO_TYPE(Array)
RESOLVES_TO_TYPE(Compound)
RESOLVES_TO_TYPE(Union)
RESOLVES_TO_TYPE(Size)


void Type::starPrint(QTextStream& os)
{
    os << "*" << this << "*";
}


QString Type::toString() const
{
    QString     res;
    QTextStream tgt(&res);

    tgt << *this;
    return res;
}


// A crude shortcut representation of a type
QTextStream& operator<<(QTextStream& os, const Type& t)
{
    switch (t.getId())
    {
    case eInteger:
        {
            int sg = t.as<IntegerType>()->getSignedness();
            // 'j' for either i or u, don't know which
            os << (sg == 0 ? 'j' : sg > 0 ? 'i' : 'u');
            os << t.as<IntegerType>()->getSize();
            break;
        }

    case eFloat:
        os << 'f';
        os << t.as<FloatType>()->getSize();
        break;

    case ePointer:
        os << t.as<PointerType>()->getPointsTo() << '*';
        break;

    case eSize:
        os << t.getSize();
        break;

    case eChar:
        os << 'c';
        break;

    case eVoid:
        os << 'v';
        break;

    case eBoolean:
        os << 'b';
        break;

    case eCompound:
        os << "struct";
        break;

    case eUnion:
        os << "union";
        break;

    // case eUnion:    os << t.getCtype(); break;
    case eFunc:
        os << "func";
        break;

    case eArray:
        os << '[' << t.as<ArrayType>()->getBaseType();

        if (!t.as<ArrayType>()->isUnbounded()) {
            os << ", " << t.as<ArrayType>()->getLength();
        }

        os << ']';
        break;

    case eNamed:
        os << t.as<NamedType>()->getName();
        break;
    }

    return os;
}


QTextStream& operator<<(QTextStream& os, const SharedConstType& t)
{
    if (t == nullptr) {
        return os << '0';
    }

    return os << *t;
}


SharedType Type::newIntegerLikeType(int size, int signedness)
{
    if (size == 1) {
        return BooleanType::get();
    }

    if ((size == 8) && (signedness >= 0)) {
        return CharType::get();
    }

    return IntegerType::get(size, signedness);
}


SharedType Type::createUnion(SharedType other, bool& ch, bool bHighestPtr /* = false */) const
{
    // `this' should not be a UnionType
    assert(!resolvesToUnion());

    // Put all the hard union logic in one place
    if (other->resolvesToUnion()) {
        return other->meetWith(((Type *)this)->shared_from_this(), ch, bHighestPtr)->clone();
    }

    // Check for anytype meet compound with anytype as first element
    if (other->resolvesToCompound()) {
        auto       otherComp = other->as<CompoundType>();
        SharedType firstType = otherComp->getType((unsigned)0);

        if (firstType->isCompatibleWith(*this)) {
            // struct meet first element = struct
            return other->clone();
        }
    }

    // Check for anytype meet array of anytype
    if (other->resolvesToArray()) {
        auto       otherArr = other->as<ArrayType>();
        SharedType elemTy   = otherArr->getBaseType();

        if (elemTy->isCompatibleWith(*this)) {
            // x meet array[x] == array
            ch = true; // since 'this' type is not an array, but the returned type is
            return other->clone();
        }
    }

    auto u = std::make_shared<UnionType>();
    u->addType(this->clone());
    u->addType(other->clone());
    ch = true;
    return u;
}


bool Type::isCompatibleWith(const Type& other, bool all /* = false */) const
{
    // Note: to prevent infinite recursion, CompoundType, ArrayType, and UnionType
    // implement this function as a delegation to isCompatible()
    if (other.resolvesToCompound() || other.resolvesToArray() || other.resolvesToUnion()) {
        return other.isCompatible(*this, all);
    }

    return isCompatible(other, all);
}


bool Type::isSubTypeOrEqual(SharedType other)
{
    if (resolvesToVoid()) {
        return true;
    }

    if (*this == *other) {
        return true;
    }

    if (this->resolvesToCompound() && other->resolvesToCompound()) {
        return this->as<CompoundType>()->isSubStructOf(other);
    }

    // Not really sure here
    return false;
}


SharedType Type::dereference()
{
    if (resolvesToPointer()) {
        return as<PointerType>()->getPointsTo();
    }

    if (resolvesToUnion()) {
        return as<UnionType>()->dereferenceUnion();
    }

    return VoidType::get(); // Can't dereference this type. Note: should probably be bottom
}
