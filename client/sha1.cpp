#include "sha1.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
static uint32_t rol(uint32_t x,int n) {
    return (x<<n)|(x>>(32-n));
}
SHA1::SHA1():bits(0),used(0) {
    h[0]=0x67452301u;
    h[1]=0xefcdab89u;
    h[2]=0x98badcfeu;
    h[3]=0x10325476u;
    h[4]=0xc3d2e1f0u;
}
void SHA1::transform(const unsigned char *p) {
    uint32_t w[80];
    for(int i=0;i<16;i++)w[i]=(uint32_t(p[4*i])<<24)|(uint32_t(p[4*i+1])<<16)|(uint32_t(p[4*i+2])<<8)|p[4*i+3];
    for(int i=16;i<80;i++)w[i]=rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
    for(int i=0;i<80;i++) {
        uint32_t f,k;
        if(i<20) {
            f=(b&c)|((~b)&d);
            k=0x5a827999u;
        } else if(i<40) {
            f=b^c^d;
            k=0x6ed9eba1u;
        } else if(i<60) {
            f=(b&c)|(b&d)|(c&d);
            k=0x8f1bbcdcu;
        } else {
            f=b^c^d;
            k=0xca62c1d6u;
        }
        uint32_t t=rol(a,5)+f+e+k+w[i];
        e=d;
        d=c;
        c=rol(b,30);
        b=a;
        a=t;
    }
    h[0]+=a;
    h[1]+=b;
    h[2]+=c;
    h[3]+=d;
    h[4]+=e;
}
void SHA1::update(const unsigned char *p,size_t n) {
    bits += uint64_t(n)*8;
    while(n) {
        size_t take=64-used;
        if(take>n)take=n;
        memcpy(buf+used,p,take);
        used+=take;
        p+=take;
        n-=take;
        if(used==64) {
            transform(buf);
            used=0;
        }
    }
}
std::string SHA1::hex(const unsigned char *p,size_t n) {
    static const char*d="0123456789abcdef";
    std::string s;
    s.reserve(n*2);
    for(size_t i=0;i<n;i++) {
        s.push_back(d[p[i]>>4]);
        s.push_back(d[p[i]&15]);
    }
    return s;
}
std::string SHA1::finalHex() {
    uint64_t originalBits=bits;
    unsigned char pad=0x80;
    update(&pad,1);
    unsigned char z=0;
    while(used!=56)update(&z,1);
    unsigned char len[8];
    for(int i=0;i<8;i++)len[7-i]=(originalBits>>(8*i))&255;
    memcpy(buf+56,len,8);
    transform(buf);
    used=0;
    unsigned char out[20];
    for(int i=0;i<5;i++) {
        out[4*i]=h[i]>>24;
        out[4*i+1]=h[i]>>16;
        out[4*i+2]=h[i]>>8;
        out[4*i+3]=h[i];
    }
    return hex(out,20);
}
std::string sha1_buffer(const unsigned char*p,size_t n) {
    SHA1 s;
    s.update(p,n);
    return s.finalHex();
}
std::string sha1_file(const std::string&path) {
    int fd=open(path.c_str(),O_RDONLY);
    if(fd<0)return "";
    SHA1 s;
    unsigned char b[65536];
    for(;;) {
        ssize_t n=read(fd,b,sizeof(b));
        if(n<0) {
            close(fd);
            return "";
        }
        if(n==0)break;
        s.update(b,n);
    }
    close(fd);
    return s.finalHex();
}
