/* Minimal intrin.h for clang cross-build (inline asm, no stdlib needed) */
#pragma once

static __inline__ unsigned char __inbyte(unsigned short p) {
  unsigned char v;
  __asm__ volatile ("inb %w1,%b0" : "=a"(v) : "Nd"(p)); return v;
}
static __inline__ void __outbyte(unsigned short p, unsigned char d) {
  __asm__ volatile ("outb %b0,%w1" : : "a"(d), "Nd"(p));
}
static __inline__ unsigned int __indword(unsigned short p) {
  unsigned int v;
  __asm__ volatile ("inl %w1,%0" : "=a"(v) : "Nd"(p)); return v;
}
static __inline__ void __outdword(unsigned short p, unsigned int d) {
  __asm__ volatile ("outl %0,%w1" : : "a"(d), "Nd"(p));
}
static __inline__ void __cpuid(int r[4], int fn) {
  __asm__ volatile ("cpuid"
    : "=a"(r[0]),"=b"(r[1]),"=c"(r[2]),"=d"(r[3]) : "a"(fn));
}
