/* Anyxbox fixture: a MinGW program with default C runtime startup that uses __thread data, so the exe carries a PE TLS
 * directory (template, index, and the CRT's own callbacks). Prints "tls=42 zero=0". */
#include <stdio.h>
static __thread int counter = 41;
static __thread int zeroed;
int main(void) {
  counter++;
  printf("tls=%d zero=%d\n", counter, zeroed);
  return counter == 42 && zeroed == 0 ? 0 : 3;
}
