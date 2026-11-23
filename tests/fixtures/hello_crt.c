/* Anyxbox fixture: a normal MinGW program with the default C runtime startup. It prints a line, calls malloc/free and
 * returns 0. Its purpose is to show which imports a real C runtime startup needs (see import_gap in the CI log). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char** argv) {
  (void)argv;
  char* p = (char*)malloc(32);
  if (!p) return 2;
  strcpy(p, "hello from a MinGW program");
  printf("%s, argc=%d\n", p, argc);
  free(p);
  return 0;
}
