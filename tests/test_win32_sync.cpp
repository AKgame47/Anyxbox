#include "syslayer/win32_sync.h"
#include "syslayer/win32_memory.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
using namespace anyxbox;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
int main() {
  HandleTable t;
  auto ev = std::make_shared<Event>(false, false);
  uint32_t h = t.add(ev);
  CHECK(h != 0 && h != 0xFFFFFFFF && h % 4 == 0);
  CHECK(WaitForSingleObject(t, h, 10) == WAIT_TIMEOUT);
  std::thread th([&] { std::this_thread::sleep_for(std::chrono::milliseconds(30)); ev->set(); });
  CHECK(WaitForSingleObject(t, h, INFINITE_MS) == WAIT_OBJECT_0);
  th.join();
  CHECK(WaitForSingleObject(t, h, 5) == WAIT_TIMEOUT);       // auto-reset consumed
  auto mev = std::make_shared<Event>(true, true);
  uint32_t mh = t.add(mev);
  CHECK(WaitForSingleObject(t, mh, 0) == WAIT_OBJECT_0 && WaitForSingleObject(t, mh, 0) == WAIT_OBJECT_0);
  mev->reset(); CHECK(WaitForSingleObject(t, mh, 0) == WAIT_TIMEOUT);
  CHECK(WaitForSingleObject(t, 0x9999, 0) == WAIT_FAILED);
  CHECK(t.close(h) && !t.close(h) && WaitForSingleObject(t, h, 0) == WAIT_FAILED);
  // semaphore: 4 workers, 3 permits released, exactly 3 get through
  auto sem = std::make_shared<Semaphore>(0, 3);
  uint32_t sh = t.add(sem);
  std::atomic<int> got{0};
  std::vector<std::thread> ws;
  for (int i = 0; i < 4; i++) ws.emplace_back([&] { if (WaitForSingleObject(t, sh, 300) == WAIT_OBJECT_0) got++; });
  int32_t prev = -1;
  CHECK(sem->release(3, &prev) && prev == 0);
  for (auto& w : ws) w.join();
  CHECK(got == 3);
  { auto s2 = std::make_shared<Semaphore>(2, 3); CHECK(!s2->release(2, nullptr)); CHECK(s2->release(1, &prev) && prev == 2); }
  // virtual memory
  VirtualMemory vm;
  void* p = vm.alloc(5000);
  CHECK(p && ((uintptr_t)p % 4096) == 0 && vm.region_size(p) == 8192);
  CHECK(vm.region_size((char*)p + 8191) == 8192 && vm.region_size((char*)p + 8192) == 0);
  CHECK(((unsigned char*)p)[4999] == 0);
  CHECK(!vm.free((char*)p + 16) && vm.free(p) && !vm.free(p));
  CHECK(vm.alloc(0) == nullptr && vm.bytes_committed() == 0);
  std::puts("ok"); return 0;
}
