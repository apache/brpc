// Minimal test to verify no hang after calling tcmalloc's
// MallocExtension_ReleaseFreeMemory C API.
//
// Build (Linux):
//   g++ -O2 -Wall tcmalloc_release_test.cc -o tcmalloc_release_test -ltcmalloc -ldl \
//   || g++ -O2 -Wall tcmalloc_release_test.cc -o tcmalloc_release_test -ltcmalloc_minimal -ldl
// Run:
//   ./tcmalloc_release_test

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <csignal>
#include <unistd.h>

extern "C" void MallocExtension_ReleaseFreeMemory(void) __attribute__((weak));

static void on_alarm(int) {
  std::fprintf(stderr, "Timed out (possible hang after ReleaseFreeMemory)\n");
  std::_Exit(124);
}

static void churn_allocations(std::size_t bytes_total) {
  const std::size_t chunk = 1 << 20; // 1 MiB
  std::vector<void*> ptrs;
  ptrs.reserve(bytes_total / chunk);
  for (std::size_t i = 0; i < bytes_total; i += chunk) {
    void* p = std::malloc(chunk);
    if (!p) break;
    std::memset(p, 0xA5, chunk);
    ptrs.push_back(p);
  }
  for (void* p : ptrs) std::free(p);
}

int main() {
  // Install a timeout so a hang becomes visible fast.
  std::signal(SIGALRM, on_alarm);
  alarm(10);

  std::printf("Phase 1 alloc/free...\n");
  churn_allocations(256ULL << 20); // 256 MiB

  std::printf("Release free memory to system via C API...\n");
  if (MallocExtension_ReleaseFreeMemory) {
    MallocExtension_ReleaseFreeMemory();
  } else {
    std::fprintf(stderr, "MallocExtension_ReleaseFreeMemory symbol not found\n");
  }

  std::printf("Phase 2 alloc/free (should not hang)...\n");
  churn_allocations(256ULL << 20); // 256 MiB

  std::printf("OK\n");
  return 0;
}


