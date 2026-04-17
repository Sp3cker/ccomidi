#include "recorder/recorder_core.h"

namespace ccomidi {

void RecorderCore::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  midi_.clear();
  tempo_.clear();
  samplePosition_ = 0;
  lastTempoBpm_ = 0.0;
}

void RecorderCore::set_sample_rate(double sampleRate) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (sampleRate > 0.0)
    sampleRate_ = sampleRate;
}

double RecorderCore::sample_rate() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sampleRate_;
}

void RecorderCore::push_event_in_block(std::uint32_t sampleInBlock,
                                       std::uint8_t status, std::uint8_t data1,
                                       std::uint8_t data2) {
  std::lock_guard<std::mutex> lock(mutex_);
  MidiRecord record = {};
  record.sampleTime = samplePosition_ + sampleInBlock;
  record.status = status;
  record.data1 = data1;
  record.data2 = data2;
  midi_.push_back(record);
}

void RecorderCore::set_tempo_in_block(std::uint32_t sampleInBlock, double bpm) {
  if (bpm <= 0.0)
    return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (bpm == lastTempoBpm_)
    return;
  TempoRecord record = {};
  record.sampleTime = samplePosition_ + sampleInBlock;
  record.bpm = bpm;
  tempo_.push_back(record);
  lastTempoBpm_ = bpm;
}

void RecorderCore::advance_block(std::uint32_t frames) {
  std::lock_guard<std::mutex> lock(mutex_);
  samplePosition_ += frames;
}

std::size_t RecorderCore::midi_event_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return midi_.size();
}

std::size_t RecorderCore::tempo_event_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return tempo_.size();
}

std::uint64_t RecorderCore::current_sample_position() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return samplePosition_;
}

double RecorderCore::duration_seconds() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (sampleRate_ <= 0.0)
    return 0.0;
  return static_cast<double>(samplePosition_) / sampleRate_;
}

double RecorderCore::last_tempo_bpm() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lastTempoBpm_;
}

RecorderCore::Snapshot RecorderCore::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Snapshot snap;
  snap.midi = midi_;
  snap.tempo = tempo_;
  snap.sampleRate = sampleRate_;
  snap.samplePosition = samplePosition_;
  return snap;
}

} // namespace ccomidi
