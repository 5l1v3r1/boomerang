/**
 * \file types.h
 * \brief Contains some often used basic type definitions
 */
#ifndef __TYPES_H__
#define __TYPES_H__
#include <iosfwd>
#include <stdint.h>

// Machine types
typedef unsigned char       Byte;        /* 8 bits */
typedef unsigned short      SWord;       /* 16 bits */
typedef unsigned int        DWord;       /* 32 bits */
struct ADDRESS { /* pointer. size depends on platform */
//    ADDRESS() {}
//    ADDRESS(uint32_t v) : m_value(v) {}
    typedef intptr_t value_type;
    value_type m_value;
    static ADDRESS g(value_type x) { // construct host/native oblivious address
        ADDRESS z;
        z.m_value =x;
        return z;
    }
    ADDRESS native() const { return ADDRESS::g(m_value&0xFFFFFFFF); }
    static ADDRESS host_ptr(const void *x) {
        ADDRESS z;
        z.m_value =value_type(x);
        return z;
    }
    bool    isZero() const {return m_value==0;}
    bool    operator==(const ADDRESS &other) const { return m_value==other.m_value; }
    bool    operator!=(const ADDRESS &other) const { return m_value!=other.m_value; }
    bool    operator<(const ADDRESS &other) const  { return m_value < other.m_value; }
    bool    operator>(const ADDRESS &other) const  { return m_value > other.m_value; }
    bool    operator>=(const ADDRESS &other) const { return m_value >= other.m_value;}
    bool operator<=(const ADDRESS &other) const { return m_value <= other.m_value;}

    ADDRESS operator+(const ADDRESS &other) const {
        return ADDRESS::g(m_value + other.m_value);
    }
    ADDRESS operator++(int){
        ADDRESS res = *this;
        m_value++;
        return res;
    }
    ADDRESS operator+=(const ADDRESS &other) {
        m_value += other.m_value;
        return *this;
    }
    ADDRESS operator+=(intptr_t other) {
        m_value += other;
        return *this;
    }
    ADDRESS &operator=(intptr_t v) {
        m_value = v;
        return *this;
    }
    ADDRESS operator+(intptr_t val) const {
        return ADDRESS::g(m_value + val);
    }
    ADDRESS operator-(const ADDRESS &other) const {
        return ADDRESS::g(m_value - other.m_value);
    }
    ADDRESS operator-=(intptr_t v) {
        m_value -= v;
        return *this;
    }
    ADDRESS operator-(intptr_t other) const {
        return ADDRESS::g(m_value - other);
    }
        friend std::ostream& operator<< (std::ostream& stream, const ADDRESS& addr);
    //operator intptr_t() const {return int(m_value);}
};
template<class T,class U>
bool IN_RANGE(const T &val,const U &range_start,const U &range_end) {
    return ((val>=range_start)&&(val<range_end));
}

#define STD_SIZE    32                    // Standard size
// Note: there is a known name collision with NO_ADDRESS in WinSock.h
#ifdef NO_ADDRESS
#undef NO_ADDRESS
#endif
#define NO_ADDRESS (ADDRESS::g(-1))     // For invalid ADDRESSes

typedef uint64_t QWord;        // 64 bits

#endif    // #ifndef __TYPES_H__
