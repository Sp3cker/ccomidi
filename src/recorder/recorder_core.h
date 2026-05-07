#ifndef CCOMIDI_RECORDER_CORE_H
#define CCOMIDI_RECORDER_CORE_H

#include <cstdint>
#include <mutex>
#include <vector>

namespace ccomidi {

struct MidiRecord {
  std::uint64_t sampleTime = 0;
  std::uint8_t status = 0;
  std::uint8_t data1 = 0;
  std::uint8_t data2 = 0;
};

struct TempoRecord {
  std::uint64_t sampleTime = 0;
  double bpm = 0.0;
};

class RecorderCore {
public:
  struct Snapshot {
    std::vector<MidiRecord> midi;
    std::vector<TempoRecord> tempo;
    double sampleRate = 44100.0;
    std::uint64_t samplePosition = 0;
    // converts the transport seconds into the recorder's sample-time frame under the same mutex as everything else.
    bool hasLoop = false;
    std::uint64_t loopStartSample = 0;
    std::uint64_t loopEndSample = 0;
  };

  void reset();
  void set_sample_rate(double sampleRate);
  double sample_rate() const;
  void reserve(std::size_t midiCapacity, std::size_t tempoCapacity);

  void push_event_in_block(std::uint32_t sampleInBlock, std::uint8_t status,
                           std::uint8_t data1, std::uint8_t data2);
  void set_tempo_in_block(std::uint32_t sampleInBlock, double bpm);
  void update_loop_from_transport(bool active, double loopStartSeconds,
                                  double loopEndSeconds,
                                  double songPosSeconds);
  void advance_block(std::uint32_t frames);

  std::size_t midi_event_count() const;
  std::size_t tempo_event_count() const;
  std::uint64_t current_sample_position() const;
  double duration_seconds() const;
  double last_tempo_bpm() const;

  Snapshot snapshot() const;

private:
  mutable std::mutex mutex_;
  std::vector<MidiRecord> midi_;
  std::vector<TempoRecord> tempo_;
  std::uint64_t samplePosition_ = 0;
  double sampleRate_ = 44100.0;
  double lastTempoBpm_ = 0.0;
  bool hasLoop_ = false;
  std::uint64_t loopStartSample_ = 0;
  std::uint64_t loopEndSample_ = 0;
};

} // namespace ccomidi

#endif
