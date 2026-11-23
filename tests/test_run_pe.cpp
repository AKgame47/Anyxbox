#include <cstdio>
#include <filesystem>
#include <fstream>
#include "pe_builder.h"
#include "runtime/run_pe.h"
using namespace pebuild;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
static void emit(std::vector<uint8_t>& c, std::initializer_list<uint8_t> l){ c.insert(c.end(), l.begin(), l.end()); }
static void emit32(std::vector<uint8_t>& c, uint32_t v){ for(int i=0;i<4;i++) c.push_back((v>>(8*i))&0xff); }
// rip-relative reference to rva from the instruction that ends at code.size()+4
static void rip(std::vector<uint8_t>& c, uint32_t target){ emit32(c, target - (0x1000 + (uint32_t)c.size() + 4)); }
int main() {
  // 1. mov eax, 42 ; ret
  { auto img = make({0xB8,42,0,0,0,0xC3}, {});
    auto r = anyxbox::run_pe(img.data(), img.size(), true);
    CHECK(r.loaded && r.ran && r.exit_code == 42 && r.output.empty());
    auto n = anyxbox::run_pe(img.data(), img.size(), false);
    CHECK(!n.ran && n.error.find("trusted") != std::string::npos); }
  // 2. print "Hello from Anyxbox\n" via GetStdHandle/WriteFile, then ExitProcess(7)
  { const char* msg = "Hello from Anyxbox\n"; uint32_t len = 19;
    std::vector<uint8_t> c;
    emit(c, {0x48,0x83,0xEC,0x38});                    // sub rsp,0x38
    emit(c, {0xB9,0xF5,0xFF,0xFF,0xFF});               // mov ecx,-11
    emit(c, {0xFF,0x15}); rip(c, 0x1260);              // call [GetStdHandle]
    emit(c, {0x48,0x89,0xC1});                         // mov rcx,rax
    emit(c, {0x48,0x8D,0x15}); rip(c, 0x1100);         // lea rdx,[msg]
    emit(c, {0x41,0xB8}); emit32(c, len);              // mov r8d,len
    emit(c, {0x4C,0x8D,0x4C,0x24,0x30});               // lea r9,[rsp+0x30]
    emit(c, {0x48,0xC7,0x44,0x24,0x20,0,0,0,0});       // mov qword [rsp+0x20],0
    emit(c, {0xFF,0x15}); rip(c, 0x1268);              // call [WriteFile]
    emit(c, {0xB9,7,0,0,0});                           // mov ecx,7
    emit(c, {0xFF,0x15}); rip(c, 0x1270);              // call [ExitProcess]
    emit(c, {0xCC});
    auto img = make(c, {"GetStdHandle","WriteFile","ExitProcess"});
    std::memcpy(&img[0x200 + 0x100], msg, len);
    auto r = anyxbox::run_pe(img.data(), img.size(), true);
    CHECK(r.loaded && r.ran);
    CHECK(r.output == "Hello from Anyxbox\n");
    CHECK(r.exit_code == 7); }
  // 3. unknown import is reported and nothing runs
  { auto img = make({0xC3}, {"CreateWindowExW"}, "user32.dll");
    auto r = anyxbox::run_pe(img.data(), img.size(), true);
    CHECK(!r.loaded && !r.ran && r.unresolved.size() == 1 && r.unresolved[0] == "user32.dll!CreateWindowExW"); }
  // 4. garbage rejected
  { std::vector<uint8_t> g(100, 1);
    CHECK(!anyxbox::run_pe(g.data(), g.size(), true).ran); }
  // Launch-route file access: same PE denies opens by default, succeeds with explicit read-only root.
  {
    std::string root=(std::filesystem::temp_directory_path()/"anyxbox_launch_assets").string();
    std::filesystem::remove_all(root);std::filesystem::create_directories(root);
    {std::ofstream f(root+"/asset.bin");f<<"data";}
    std::vector<uint8_t> c;
    emit(c,{0x48,0x83,0xec,0x48});
    emit(c,{0x48,0x8d,0x0d});rip(c,0x1100);
    emit(c,{0xba});emit32(c,0x80000000u);
    emit(c,{0x45,0x31,0xc0,0x45,0x31,0xc9});
    emit(c,{0x48,0xc7,0x44,0x24,0x20,3,0,0,0});
    emit(c,{0x48,0xc7,0x44,0x24,0x28,0,0,0,0});
    emit(c,{0x48,0xc7,0x44,0x24,0x30,0,0,0,0});
    emit(c,{0xff,0x15});rip(c,0x1260);
    emit(c,{0x48,0x83,0xf8,0xff,0x0f,0x95,0xc0,0x0f,0xb6,0xc0,0x48,0x83,0xc4,0x48,0xc3});
    auto img=make(c,{"CreateFileA"});std::memcpy(img.data()+0x300,"asset.bin",10);
    auto denied=anyxbox::run_pe(img.data(),img.size(),true);CHECK(denied.ran && denied.exit_code==0);
    anyxbox::RunOptions opts;opts.asset_root=root;
    auto allowed=anyxbox::run_pe(img.data(),img.size(),true,nullptr,opts);CHECK(allowed.ran && allowed.exit_code==1);
    // Reuse the guest import call with a write/create request, denied by the read-only launcher root.
    w32(img,0x200+12,0x40000000u); // edx immediate in the emitted guest code
    w32(img,0x200+27,2); // CREATE_ALWAYS, which would truncate without read-only enforcement
    auto write_denied=anyxbox::run_pe(img.data(),img.size(),true,nullptr,opts);
    CHECK(write_denied.ran && write_denied.exit_code==0);
    CHECK(std::filesystem::file_size(root+"/asset.bin")==4);
    opts.asset_root=root+"/missing";CHECK(!anyxbox::run_pe(img.data(),img.size(),true,nullptr,opts).ran);
    std::filesystem::remove_all(root);
  }
  std::puts("ok"); return 0;
}
