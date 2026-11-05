#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

class __FlashStringHelper;

class String {
public:
    String(const char* s = "") : s_(s) {}
    const char* c_str() const { return s_; }
    unsigned int length() const { return static_cast<unsigned int>(std::strlen(s_)); }

private:
    const char* s_;
};

class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t* buffer, size_t size) {
        size_t n = 0;
        while (size--) n += write(*buffer++);
        return n;
    }
    size_t print(const char* s) { return write(reinterpret_cast<const uint8_t*>(s), std::strlen(s)); }
};
