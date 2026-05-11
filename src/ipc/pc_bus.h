#ifndef CCOMIDI_IPC_PC_BUS_H
#define CCOMIDI_IPC_PC_BUS_H

#include <atomic>
#include <cstdint>

namespace ccomidi::ipc {

// Sidechannel for Program Change publication between ccomidi instances
// and a poryaaaa recorder. PC may not survive host MIDI routing (Live
// filters, clap-wrapper VST3 translation drops, etc.). The recorder needs
// authoritative PC; the audio path doesn't (CLAP_EVENT_MIDI 0xCn already
// drives the engine when it gets through). So this bus exists purely to
// give the recorder a routing-independent source of truth.
//
// Threading: writer and reader are both audio-thread, RT-safe. Seqlock
// publication — wait-free for a single writer per slot, lock-free for
// readers. No syscalls past the one-time open().

inline constexpr int kPCBusVersion = 1;
inline constexpr int kPCBusChannels = 16;
inline constexpr std::uint32_t kPCBusMagic = 0x43504257u; // 'CPBW'

struct PCBusSlot {
  std::uint8_t program;
  std::uint8_t bank_msb;
  std::uint8_t bank_lsb;
  std::uint8_t _pad0;
  std::uint32_t writer_pid;
  // Absolute host steady sample time at which the PC was emitted —
  // clap_process_t::steady_time + event.time. -1 if the host doesn't expose
  // steady_time, in which case the reader falls back to "stamp at receive
  // time", losing sub-block accuracy.
  std::int64_t host_steady_sample_time;
};
static_assert(sizeof(PCBusSlot) == 16, "PCBusSlot must be 16 bytes");

class PCBus {
public:
  PCBus() = default;
  ~PCBus();

  PCBus(const PCBus &) = delete;
  PCBus &operator=(const PCBus &) = delete;

  // Maps the shared region, creating it if it doesn't exist. Returns true
  // on success. Idempotent.
  bool open();
  void close();
  bool is_open() const { return shared_ != nullptr; }

  // Writer-side. Publish PC for `channel` (0..15). RT-safe — single atomic
  // store sequence, no syscalls.
  void publish(std::uint8_t channel, const PCBusSlot &slot);

  // Reader-side. Returns the published seq counter for `channel`. Read via
  // acquire so subsequent read_slot() sees consistent data.
  std::uint64_t read_seq(std::uint8_t channel) const;

  // Reader-side seqlock read. Copies the slot under a versioned-read
  // pattern; retries while a writer is in-flight. RT-safe.
  bool read_slot(std::uint8_t channel, PCBusSlot *out,
                 std::uint64_t *seq_out) const;

private:
  struct ChannelEntry {
    // Even = quiescent. Odd = writer in flight.
    alignas(64) std::atomic<std::uint64_t> seq;
    PCBusSlot slot;
  };
  static_assert(sizeof(std::atomic<std::uint64_t>) == 8,
                "atomic uint64 must be lock-free 8 bytes");

  struct Shared {
    std::atomic<std::uint32_t> magic;
    std::atomic<std::uint32_t> version;
    ChannelEntry entries[kPCBusChannels];
  };

  Shared *shared_ = nullptr;
  int fd_ = -1;
};

} // namespace ccomidi::ipc

#endif
