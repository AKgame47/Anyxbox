#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>
#include "loader/pe_report.h"
#include "syslayer/win32_file.h"
#include "syslayer/win32_thread.h"
using namespace anyxbox;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
int main() {
  HandleTable t;
  // threads
  std::atomic<int> n{0};
  uint32_t th = CreateThread(t, [&] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); n = 7; return 42u; });
  CHECK(th != 0);
  uint32_t code = 0;
  CHECK(GetExitCodeThread(t, th, &code) && code == 259);
  CHECK(WaitForSingleObject(t, th, INFINITE_MS) == WAIT_OBJECT_0);
  CHECK(GetExitCodeThread(t, th, &code) && code == 42 && n == 7);
  CHECK(WaitForSingleObject(t, th, 0) == WAIT_OBJECT_0);          // stays signaled
  uint32_t th2 = CreateThread(t, []() -> uint32_t { throw 1; });
  CHECK(WaitForSingleObject(t, th2, 1000) == WAIT_OBJECT_0);
  CHECK(GetExitCodeThread(t, th2, &code) && code == 0xC0000005);
  CHECK(CreateThread(t, nullptr) == 0);
  // critical section
  CriticalSection cs; int counter = 0;
  std::vector<std::thread> ws;
  for (int i = 0; i < 4; i++) ws.emplace_back([&] { for (int k = 0; k < 1000; k++) { cs.enter(); cs.enter(); counter++; cs.leave(); cs.leave(); } });
  for (auto& w : ws) w.join();
  CHECK(counter == 4000);
  // mapped folder
  std::string root = (std::filesystem::temp_directory_path() / "anyxbox_t3").string();
  std::filesystem::remove_all(root);
  MappedFolder mf(root);
  std::string h;
  CHECK(!mf.resolve("../x", h) && !mf.resolve("a/../../x", h) && !mf.resolve("C:\\x", h) && !mf.resolve("", h) && !mf.resolve("/", h));
  CHECK(mf.resolve("\\saves\\a.sav", h) && h == root + "/saves/a.sav");
  CHECK(mf.CreateFile(t, "saves/none.bin", OPEN_EXISTING_RW_READ) == 0xFFFFFFFF);
  uint32_t f = mf.CreateFile(t, "saves/a.sav", CREATE_WRITE_TRUNCATE);
  CHECK(f != 0xFFFFFFFF);
  uint32_t w = 0; CHECK(WriteFile(t, f, "hello world", 11, &w) && w == 11);
  uint64_t sz = 0; CHECK(GetFileSize(t, f, &sz) && sz == 11);
  uint64_t pos = 0; CHECK(SetFilePointer(t, f, 6, 0, &pos) && pos == 6);
  char buf[16] = {0}; uint32_t got = 0;
  CHECK(ReadFile(t, f, buf, 15, &got) && got == 5 && std::memcmp(buf, "world", 5) == 0);
  CHECK(ReadFile(t, f, buf, 15, &got) && got == 0);               // EOF
  CHECK(!SetFilePointer(t, f, 0, 9, &pos));
  CHECK(!ReadFile(t, th, buf, 1, &got));                          // not a file handle
  CHECK(mf.exists("saves/a.sav") && !mf.exists("../a.sav"));
  uint32_t r = mf.CreateFile(t, "saves/a.sav", OPEN_EXISTING_RW_READ);
  CHECK(ReadFile(t, r, buf, 5, &got) && got == 5 && std::memcmp(buf, "hello", 5) == 0);
  std::filesystem::remove_all(root);
  // report
  LoadedImage bad; bad.error = "bad DOS magic";
  CHECK(pe_report(bad).find("FAILED (bad DOS magic)") != std::string::npos);
  LoadedImage li; li.ok = false; li.image_size = 10; li.unresolved = {"user32.dll!MessageBoxA"};
  li.imports.push_back({"user32.dll", "MessageBoxA", 0, false, 0});
  auto rep = pe_report(li);
  CHECK(rep.find("imports from user32.dll: 1") != std::string::npos && rep.find("missing user32.dll!MessageBoxA") != std::string::npos);
  std::puts("ok"); return 0;
}
