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


#include "boomerang/ssl/type/Type.h"

#include <unordered_set>


// The union type represents the union of any number of any other types
struct BOOMERANG_API UnionElement
{
    SharedType type;
    QString name;

    bool operator==(const UnionElement &other) const { return *type == *other.type; }
};


struct BOOMERANG_API hashUnionElem
{
    size_t operator()(const UnionElement &e) const;
};


class BOOMERANG_API UnionType : public Type
{
public:
    typedef std::unordered_set<UnionElement, hashUnionElem> UnionEntrySet;
    typedef UnionEntrySet::iterator ilUnionElement;

public:
    /// Create a new empty union type.
    UnionType();

    /// Create a new union type with unnamed members.
    UnionType(const std::initializer_list<SharedType> &members);

    UnionType(const UnionType &other) = default;
    UnionType(UnionType &&other)      = default;

    virtual ~UnionType() override;

    UnionType &operator=(const UnionType &other) = default;
    UnionType &operator=(UnionType &&other) = default;

public:
    static std::shared_ptr<UnionType> get() { return std::make_shared<UnionType>(); }
    static std::shared_ptr<UnionType> get(const std::initializer_list<SharedType> &members)
    {
        return std::make_shared<UnionType>(members);
    }

    /**
     * Add a new type to this union
     * \param type the type of the new member
     * \param name the name of the new member
     */
    void addType(SharedType type, const QString &name = "");

    size_t getNumTypes() const { return li.size(); }

    // Return true if this type is already in the union. Note: linear search, but number of types is
    // usually small
    bool findType(SharedType ty); // Return true if ty is already in the union

    ilUnionElement begin() { return li.begin(); }
    ilUnionElement end() { return li.end(); }
    // Type        *getType(const char *name);
    // const        char *getName(int n) { assert(n < getNumTypes()); return names[n].c_str(); }

    virtual SharedType clone() const override;

    virtual bool operator==(const Type &other) const override;

    // virtual bool        operator-=(const Type& other) const;
    virtual bool operator<(const Type &other) const override;

    virtual size_t getSize() const override;

    virtual QString getCtype(bool final = false) const override;

    /// \copydoc Type::meetWith
    virtual SharedType meetWith(SharedType other, bool &changed, bool useHighestPtr) const override;

    virtual bool isCompatibleWith(const Type &other, bool all) const override
    {
        return isCompatible(other, all);
    }
    virtual bool isCompatible(const Type &other, bool all) const override;

private:
    // Note: list, not vector, as it is occasionally desirable to insert elements without affecting
    // iterators (e.g. meetWith(another Union))
    UnionEntrySet li;
};
