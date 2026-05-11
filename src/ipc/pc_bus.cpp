#include "ipc/pc_bus.h"

#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cstring>

namespace ccomidi::ipc {

namespace {
constexpr const char *kShmName = "/ccomidi_pc_bus_v1";
}

PCBus::~PCBus() { close(); }

bool PCBus::open() {
  if (shared_)
    return true;

#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
  const size_t size = sizeof(Shared);

  // O_CREAT|O_RDWR — first opener creates the segment, subsequent openers
  // attach to it. Permissions 0666 because plugins in different host
  // processes (each with their own uid/gid context under sandboxing) may
  // both need to attach.
  fd_ = ::shm_open(kShmName, O_CREAT | O_RDWR, 0666);
  if (fd_ < 0)
    return false;

  // ftruncate is idempotent in the size-already-correct case. First opener
  // grows it from 0; later openers see it already at `size`.
  if (::ftruncate(fd_, static_cast<off_t>(size)) != 0) {
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  void *p = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
  if (p == MAP_FAILED) {
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  shared_ = static_cast<Shared *>(p);

  // First opener: zero-init and stamp magic. Race: two plugins may call
  // open() simultaneously, both find magic == 0, both stamp. That's fine
  // — they stamp the same magic and version, and the slot seq counters
  // start at 0 in either case.
  const std::uint32_t observedMagic =
      shared_->magic.load(std::memory_order_acquire);
  if (observedMagic != kPCBusMagic) {
    // The region was zero-initialized by ftruncate (POSIX guarantees zero
    // fill for newly extended bytes).
    shared_->version.store(kPCBusVersion, std::memory_order_relaxed);
    shared_->magic.store(kPCBusMagic, std::memory_order_release);
  }

  return true;
#else
  return false;
#endif
}

void PCBus::close() {
#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
  if (shared_) {
    ::munmap(shared_, sizeof(Shared));
    shared_ = nullptr;
  }
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
#endif
  // Deliberately do NOT shm_unlink — other plugin instances may still be
  // attached. The segment persists for the user's session, which is fine
  // (stale slots just describe the last PC seen).
}

void PCBus::publish(std::uint8_t channel, const PCBusSlot &slot) {
  if (!shared_ || channel >= kPCBusChannels)
    return;

  ChannelEntry &entry = shared_->entries[channel];
  const std::uint64_t pre =
      entry.seq.load(std::memory_order_relaxed);
  // Bump to odd: write-in-progress marker. Release so readers that see the
  // odd value know to retry.
  entry.seq.store(pre + 1, std::memory_order_release);
  // Plain memcpy of the slot. Readers gated by the seq counter.
  std::memcpy(&entry.slot, &slot, sizeof(PCBusSlot));
  // Bump to even: publish. Release pairs with reader's acquire.
  entry.seq.store(pre + 2, std::memory_order_release);
}

std::uint64_t PCBus::read_seq(std::uint8_t channel) const {
  if (!shared_ || channel >= kPCBusChannels)
    return 0;
  return shared_->entries[channel].seq.load(std::memory_order_acquire);
}

bool PCBus::read_slot(std::uint8_t channel, PCBusSlot *out,
                      std::uint64_t *seq_out) const {
  if (!shared_ || channel >= kPCBusChannels || !out)
    return false;

  const ChannelEntry &entry = shared_->entries[channel];

  // Bounded retry: if a writer keeps racing us we'd spin forever otherwise.
  // 8 attempts is generous — a writer's critical section is two stores and
  // one memcpy.
  for (int attempt = 0; attempt < 8; ++attempt) {
    const std::uint64_t pre =
        entry.seq.load(std::memory_order_acquire);
    if ((pre & 1u) != 0u) {
      // Writer in flight, try again.
      continue;
    }
    PCBusSlot copy;
    std::memcpy(&copy, &entry.slot, sizeof(PCBusSlot));
    const std::uint64_t post =
        entry.seq.load(std::memory_order_acquire);
    if (pre == post) {
      *out = copy;
      if (seq_out)
        *seq_out = pre;
      return true;
    }
  }
  return false;
}

} // namespace ccomidi::ipc
