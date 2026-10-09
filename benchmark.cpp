#include <linux/perf_event.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef ENABLE_TCMALLOC
#include <gperftools/malloc_extension.h>
#endif


static long long runs;
static long long alloc_dealloc_every;
static constexpr std::array<std::uint16_t, 9> block_sizes = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048};

class PerfCounters {
public:
  using Clock = std::chrono::steady_clock;

  PerfCounters() {
    m_cycles = open(PERF_COUNT_HW_CPU_CYCLES, -1);
    m_instructions = open(PERF_COUNT_HW_INSTRUCTIONS, m_cycles);
  }
  ~PerfCounters() {
    close(m_instructions);
    close(m_cycles);
  }

  void start() {
    t0 = Clock::now();
    ioctl(m_cycles, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
    ioctl(m_cycles, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
  }
  void stop() {
    ioctl(m_cycles, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
    t1 = Clock::now();
  }
  struct Result {
    uint64_t cycles;
    uint64_t instructions;
    double time_diff_ns;
  };
  Result read() const {
    struct {
      uint64_t nr;
      uint64_t values[2];
    } buf{};
    if (::read(m_cycles, &buf, sizeof(buf)) != sizeof(buf))
      throw std::runtime_error("read perf counters failed");
    return {
      buf.values[0], buf.values[1],
      std::chrono::duration<double, std::nano>(t1 - t0).count()
    };
  }

private:
  static int open(uint64_t config, int groupFd) {
    perf_event_attr pe{};
    pe.size = sizeof(pe);
    pe.type = PERF_TYPE_HARDWARE;
    pe.config = config;
    pe.disabled = (groupFd == -1);
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    pe.read_format = PERF_FORMAT_GROUP;
    int fd = syscall(SYS_perf_event_open, &pe, 0, -1, groupFd, 0);
    if (fd == -1)
      throw std::runtime_error("perf_event_open failed");
    return fd;
  }

  Clock::time_point t0;
  Clock::time_point t1;
  int m_cycles;
  int m_instructions;
};

template <class T>
inline __attribute__((always_inline)) void doNotOptimize(T& value) {
    asm volatile("" : "+r,m"(value) : : "memory");
}

struct Result {
  PerfCounters::Result result;
  std::uint8_t thread_amount;
};

static struct {
  std::mutex lock_results;
  std::vector<Result> results;
} data;

static void thread_callback(std::uint8_t thread_amount) {
  std::vector<void *> memory_blocks;
  doNotOptimize(memory_blocks);
  memory_blocks.reserve(alloc_dealloc_every);

  PerfCounters pc;
  pc.start();
  long long current_run = 1;
  for (;;) {
    if (current_run++ == runs)
      break;

    if (current_run % alloc_dealloc_every == 0) {
      for (auto ptr : memory_blocks) {
        doNotOptimize(ptr);
        auto* u8_ptr = reinterpret_cast<uint8_t*>(ptr);
        delete[] u8_ptr;
      }
      memory_blocks.clear();
    } else {
      memory_blocks.emplace_back(new std::uint8_t[block_sizes[current_run % block_sizes.size()]]);
    }
  }
  pc.stop();
  // EoB-enchmarking

  {
    std::lock_guard lock(data.lock_results);
    Result res = { .result = std::move(pc.read()),
                   .thread_amount = thread_amount };
    data.results.emplace_back(std::move(res));
  }
}

int main(int argc, char **argv) {
  runs = (argc > 1 ? atoll(argv[1]) : 1000000);
  alloc_dealloc_every = (argc > 2 ? atoll(argv[2]) : 1000);

  if (alloc_dealloc_every == 0 || runs / alloc_dealloc_every == 0)
    return 1;

  {
    PerfCounters();
  }

  const std::array<std::uint8_t, 5> thread_amount_to_test = {1, 4, 8, 16, 32};
  for (auto thread_amount : thread_amount_to_test) {
    std::vector<std::jthread> threads;
    threads.reserve(thread_amount);
    for (auto i = 0u; i < thread_amount; ++i) {
      threads.emplace_back([thread_amount]() {
        thread_callback(thread_amount);
      });
    }
  }

#ifdef ENABLE_TCMALLOC
  size_t allocated = 0, heap_size = 0, total_physical_bytes = 0;
  size_t max_total_thread_cache_bytes = 0, min_per_thread_cache_bytes = 0;
  size_t current_total_thread_cache_bytes = 0, central_cache_free_bytes = 0;
  size_t transfer_cache_free_bytes = 0, thread_cache_free_bytes = 0;
  size_t pageheap_free_bytes = 0, pageheap_unmapped_bytes = 0;

  const auto & me = MallocExtension::instance();
  me->GetNumericProperty("generic.current_allocated_bytes", &allocated);
  me->GetNumericProperty("generic.heap_size", &heap_size);
  me->GetNumericProperty("generic.total_physical_bytes", &total_physical_bytes);
  me->GetNumericProperty("tcmalloc.max_total_thread_cache_bytes", &max_total_thread_cache_bytes);
  me->GetNumericProperty("tcmalloc.min_per_thread_cache_bytes", &min_per_thread_cache_bytes);
  me->GetNumericProperty("tcmalloc.current_total_thread_cache_bytes", &current_total_thread_cache_bytes);
  me->GetNumericProperty("tcmalloc.central_cache_free_bytes", &central_cache_free_bytes);
  me->GetNumericProperty("tcmalloc.transfer_cache_free_bytes", &transfer_cache_free_bytes);
  me->GetNumericProperty("tcmalloc.thread_cache_free_bytes", &thread_cache_free_bytes);
  me->GetNumericProperty("tcmalloc.pageheap_free_bytes", &pageheap_free_bytes);
  me->GetNumericProperty("tcmalloc.pageheap_unmapped_bytes", &pageheap_unmapped_bytes);

  std::printf("# [TCMalloc Stats]\n"
    "# generic.current_allocated_bytes..........: %zu\n"
    "# generic.heap_size........................: %zu\n"
    "# generic.total_physical_bytes.............: %zu\n"
    "# tcmalloc.max_total_thread_cache_bytes....: %zu\n"
    "# tcmalloc.min_per_thread_cache_bytes......: %zu\n"
    "# tcmalloc.current_total_thread_cache_bytes: %zu\n"
    "# tcmalloc.central_cache_free_bytes........: %zu\n"
    "# tcmalloc.transfer_cache_free_bytes.......: %zu\n"
    "# tcmalloc.thread_cache_free_bytes.........: %zu\n"
    "# tcmalloc.pageheap_free_bytes.............: %zu\n"
    "# tcmalloc.pageheap_unmapped_bytes.........: %zu\n",
    allocated, heap_size, total_physical_bytes,
    max_total_thread_cache_bytes, min_per_thread_cache_bytes,
    current_total_thread_cache_bytes,
    central_cache_free_bytes,
    transfer_cache_free_bytes,
    thread_cache_free_bytes,
    pageheap_free_bytes, pageheap_unmapped_bytes);
#endif

  auto& res = data.results;
  std::printf("# Got %zu performance results\n", res.size());
  std::printf("# thread_num, instructions,       cycles,      time_ns\n");
  for (auto& r : res) {
    std::printf("%12u  %12lu  %12lu  %12.0lf\n",
                r.thread_amount, r.result.instructions,
                r.result.cycles, r.result.time_diff_ns);
  }
}
