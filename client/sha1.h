#ifndef A3_SHA1_H
#define A3_SHA1_H
#include <cstdint>
#include <string>
#include <vector>
class SHA1 {
    uint32_t h[5];
    uint64_t bits;
    unsigned char buf[64];
    size_t used;
    void transform(const unsigned char *p);
    public:
    SHA1();
    void update(const unsigned char *p, size_t n);
    std::string finalHex();
    static std::string hex(const unsigned char *p, size_t n);
}
;
std::string sha1_file(const std::string &path);
std::string sha1_buffer(const unsigned char *p, size_t n);
#endif
