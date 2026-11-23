#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "pe_builder.h"
#include "runtime/run_pe.h"
using namespace pebuild;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
static void emit(std::vector<uint8_t>& c, std::initializer_list<uint8_t> l){ c.insert(c.end(), l.begin(), l.end()); }
static void emit32(std::vector<uint8_t>& c, uint32_t v){ for(int i=0;i<4;i++) c.push_back((v>>(8*i))&0xff); }
// disp32 where `tail` more bytes follow the displacement inside the same instruction
static void rip(std::vector<uint8_t>& c, uint32_t target, uint32_t tail = 0){ emit32(c, target - (0x1000 + (uint32_t)c.size() + 4 + tail)); }

// Runs in a child process; true if the program crashed (signal or abnormal exit), false if it returned normally.
static bool crashes(const std::vector<uint8_t>& img) {
  pid_t p = fork();
  if (p == 0) {
    int nul = open("/dev/null", O_WRONLY); dup2(nul, 2); dup2(nul, 1);
    auto r = anyxbox::run_pe(img.data(), img.size(), true);
    _exit(r.ran ? 0 : 3);
  }
  int st = 0; waitpid(p, &st, 0);
  return !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}
int main() {
  // A: writes then reads its own .data section: allowed
  { std::vector<uint8_t> c;
    emit(c, {0xC7,0x05}); rip(c, 0x2000, 4); emit32(c, 0x77);   // mov dword [data],0x77
    emit(c, {0x8B,0x05}); rip(c, 0x2000);                        // mov eax,[data]
    emit(c, {0xC3});
    auto img = make2(c, {});
    auto r = anyxbox::run_pe(img.data(), img.size(), true);
    CHECK(r.ran && r.exit_code == 0x77);
    CHECK(!crashes(img) == true); }
  // B: writes into its own .text (read+exec): must fault
  { std::vector<uint8_t> c;
    emit(c, {0xC7,0x05}); rip(c, 0x1100, 4); emit32(c, 0);
    emit(c, {0xC3});
    auto img = make2(c, {});
    CHECK(crashes(img)); }
  // C: jumps into .data (read+write, not executable): must fault
  { std::vector<uint8_t> c;
    emit(c, {0x48,0x8D,0x05}); rip(c, 0x2000);                   // lea rax,[data]
    emit(c, {0xFF,0xE0});                                         // jmp rax
    auto img = make2(c, {});
    CHECK(crashes(img)); }
  // D: control: same code as B but with the single read+write+exec section does not fault
  { std::vector<uint8_t> c;
    emit(c, {0xC7,0x05}); rip(c, 0x1100, 4); emit32(c, 0);
    emit(c, {0xC3});
    auto img = make(c, {});
    CHECK(!crashes(img)); }
  // E: a section not aligned to a page is refused cleanly
  { auto img = make2({0xC3}, {});
    size_t s2 = 0x84+20+240+40; w32(img, s2+12, 0x2100); w32(img, s2+8, 0x100);
    auto r = anyxbox::run_pe(img.data(), img.size(), true);
    CHECK(!r.ran && r.error.find("section layout") != std::string::npos); }
  std::puts("ok"); return 0;
}
