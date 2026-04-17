#include "recorder/smf_writer.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "MidiFile.h"

namespace ccomidi {

namespace {

using uchar = unsigned char;

bool is_channel_voice_status(std::uint8_t status) {
  const std::uint8_t kind = status & 0xF0;
  return kind >= 0x80 && kind <= 0xE0;
}

bool is_single_data_byte_status(std::uint8_t status) {
  const std::uint8_t kind = status & 0xF0;
  return kind == 0xC0 || kind == 0xD0;
}

int ticks_for_sample(std::uint64_t sampleTime, double sampleRate, double bpm,
                     std::uint16_t ppq) {
  const double seconds = static_cast<double>(sampleTime) / sampleRate;
  const double ticks = seconds * (bpm / 60.0) * static_cast<double>(ppq);
  return static_cast<int>(ticks + 0.5);
}

} // namespace

bool write_smf1(const std::string &path,
                const RecorderCore::Snapshot &snapshot,
                const SmfWriteOptions &options) {
  const std::uint16_t ppq = options.ppq > 0 ? options.ppq : 480;
  const double sampleRate =
      snapshot.sampleRate > 0.0 ? snapshot.sampleRate : 44100.0;
  double initBpm = options.fallbackBpm > 0.0 ? options.fallbackBpm : 120.0;
  if (!snapshot.tempo.empty() && snapshot.tempo[0].bpm > 0.0)
    initBpm = snapshot.tempo[0].bpm;

  try {
    smf::MidiFile midifile;
    // MidiFile default-constructs with one track; we need exactly two.
    midifile.addTracks(1);
    midifile.setTicksPerQuarterNote(ppq);

    constexpr int kConductorTrack = 0;
    constexpr int kMusicTrack = 1;

    midifile.addTrackName(kConductorTrack, 0, "Conductor");
    midifile.addTimeSignature(kConductorTrack, 0, 4, 4);

    // Seed the music track so MidiFile::write never calls back() on an empty
    // event list (which is UB and crashes the host when nothing was recorded).
    midifile.addTrackName(kMusicTrack, 0, "Music");

    const bool firstTempoAtZero =
        !snapshot.tempo.empty() && snapshot.tempo[0].sampleTime == 0;
    if (!firstTempoAtZero)
      midifile.addTempo(kConductorTrack, 0, initBpm);

    double currentBpm = initBpm;
    int lastMusicTick = 0;
    std::unordered_set<std::uint16_t> heldNotes;
    std::size_t mi = 0;
    std::size_t ti = 0;
    while (mi < snapshot.midi.size() || ti < snapshot.tempo.size()) {
      bool takeTempo;
      if (ti >= snapshot.tempo.size())
        takeTempo = false;
      else if (mi >= snapshot.midi.size())
        takeTempo = true;
      else
        takeTempo =
            snapshot.tempo[ti].sampleTime <= snapshot.midi[mi].sampleTime;

      if (takeTempo) {
        const TempoRecord &t = snapshot.tempo[ti];
        const double bpm = t.bpm > 0.0 ? t.bpm : currentBpm;
        const int tick = ticks_for_sample(t.sampleTime, sampleRate, bpm, ppq);
        midifile.addTempo(kConductorTrack, tick, bpm);
        currentBpm = bpm;
        ++ti;
      } else {
        const MidiRecord &m = snapshot.midi[mi];
        ++mi;
        if (!is_channel_voice_status(m.status))
          continue;
        const int tick =
            ticks_for_sample(m.sampleTime, sampleRate, currentBpm, ppq);
        std::vector<uchar> bytes;
        bytes.push_back(static_cast<uchar>(m.status));
        bytes.push_back(static_cast<uchar>(m.data1));
        if (!is_single_data_byte_status(m.status))
          bytes.push_back(static_cast<uchar>(m.data2));
        midifile.addEvent(kMusicTrack, tick, bytes);
        if (tick > lastMusicTick)
          lastMusicTick = tick;

        const std::uint8_t kind = m.status & 0xF0;
        const std::uint8_t channel = m.status & 0x0F;
        const std::uint16_t key =
            static_cast<std::uint16_t>((channel << 8) | m.data1);
        if (kind == 0x90 && m.data2 > 0)
          heldNotes.insert(key);
        else if (kind == 0x80 || (kind == 0x90 && m.data2 == 0))
          heldNotes.erase(key);
      }
    }

    if (!heldNotes.empty()) {
      const int releaseTick = lastMusicTick + 1;
      for (std::uint16_t key : heldNotes) {
        const std::uint8_t channel = static_cast<std::uint8_t>((key >> 8) & 0x0F);
        const std::uint8_t note = static_cast<std::uint8_t>(key & 0x7F);
        std::vector<uchar> bytes;
        bytes.push_back(static_cast<uchar>(0x80 | channel));
        bytes.push_back(static_cast<uchar>(note));
        bytes.push_back(0);
        midifile.addEvent(kMusicTrack, releaseTick, bytes);
      }
    }

    midifile.sortTracks();
    return midifile.write(path);
  } catch (...) {
    return false;
  }
}

} // namespace ccomidi
