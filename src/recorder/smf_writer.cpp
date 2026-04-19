#include "recorder/smf_writer.h"

#include <cstdint>
#include <cstdio>
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
    constexpr int kConductorTrack = 0;

    int channelTrack[16];
    for (int i = 0; i < 16; ++i)
      channelTrack[i] = -1;
    int nextTrack = 1;
    for (const MidiRecord &m : snapshot.midi) {
      if (!is_channel_voice_status(m.status))
        continue;
      const std::uint8_t channel = m.status & 0x0F;
      if (channelTrack[channel] < 0)
        channelTrack[channel] = nextTrack++;
    }
    // Always keep at least one music track so MidiFile::write never calls
    // back() on an empty event list (UB) when nothing was recorded.
    const int totalTracks = nextTrack > 1 ? nextTrack : 2;

    smf::MidiFile midifile;
    // MidiFile default-constructs with one track.
    midifile.addTracks(totalTracks - 1);
    midifile.setTicksPerQuarterNote(ppq);

    midifile.addTrackName(kConductorTrack, 0, "Conductor");
    midifile.addTimeSignature(kConductorTrack, 0, 4, 4);

    if (snapshot.hasLoop) {
      //  if the snapshot has a loop, emit addMarker("[") and addMarker("]") on the conductor track. 
      const double loopBpm =
          !snapshot.tempo.empty() && snapshot.tempo[0].bpm > 0.0
              ? snapshot.tempo[0].bpm
              : initBpm;
      const int startTick = ticks_for_sample(snapshot.loopStartSample,
                                             sampleRate, loopBpm, ppq);
      const int endTick =
          ticks_for_sample(snapshot.loopEndSample, sampleRate, loopBpm, ppq);
      midifile.addMarker(kConductorTrack, startTick, "[");
      midifile.addMarker(kConductorTrack, endTick, "]");
    }

    if (nextTrack == 1) {
      midifile.addTrackName(1, 0, "Music");
    } else {
      for (int ch = 0; ch < 16; ++ch) {
        if (channelTrack[ch] < 0)
          continue;
        char name[16];
        std::snprintf(name, sizeof(name), "Ch %d", ch + 1);
        midifile.addTrackName(channelTrack[ch], 0, name);
      }
    }

    const bool firstTempoAtZero =
        !snapshot.tempo.empty() && snapshot.tempo[0].sampleTime == 0;
    if (!firstTempoAtZero)
      midifile.addTempo(kConductorTrack, 0, initBpm);

    double currentBpm = initBpm;
    int lastTickPerChannel[16] = {0};
    std::unordered_set<std::uint16_t> heldNotes;
    // Per (channel, controller) last-emitted CC value, -1 = unset. Used to drop
    // consecutive CCs with the same value. Exempts controllers 0x1D/0x1E, which
    // form GBA prefix/value command pairs whose repeats are semantically
    // distinct (see test_same_tick_cc_arrival_order_preserved).
    int lastCcValue[16][128];
    for (auto &row : lastCcValue)
      for (auto &v : row)
        v = -1;
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
        const std::uint8_t channel = m.status & 0x0F;
        const int track = channelTrack[channel];
        if (track < 0)
          continue;
        if ((m.status & 0xF0) == 0xB0 && m.data1 != 0x1D && m.data1 != 0x1E) {
          if (lastCcValue[channel][m.data1] == m.data2)
            continue;
          lastCcValue[channel][m.data1] = m.data2;
        }
        const int tick =
            ticks_for_sample(m.sampleTime, sampleRate, currentBpm, ppq);
        std::vector<uchar> bytes;
        bytes.push_back(static_cast<uchar>(m.status));
        bytes.push_back(static_cast<uchar>(m.data1));
        if (!is_single_data_byte_status(m.status))
          bytes.push_back(static_cast<uchar>(m.data2));
        midifile.addEvent(track, tick, bytes);
        if (tick > lastTickPerChannel[channel])
          lastTickPerChannel[channel] = tick;

        const std::uint8_t kind = m.status & 0xF0;
        const std::uint16_t key =
            static_cast<std::uint16_t>((channel << 8) | m.data1);
        if (kind == 0x90 && m.data2 > 0)
          heldNotes.insert(key);
        else if (kind == 0x80 || (kind == 0x90 && m.data2 == 0))
          heldNotes.erase(key);
      }
    }

    for (std::uint16_t key : heldNotes) {
      const std::uint8_t channel = static_cast<std::uint8_t>((key >> 8) & 0x0F);
      const std::uint8_t note = static_cast<std::uint8_t>(key & 0x7F);
      const int track = channelTrack[channel];
      if (track < 0)
        continue;
      const int releaseTick = lastTickPerChannel[channel] + 1;
      std::vector<uchar> bytes;
      bytes.push_back(static_cast<uchar>(0x80 | channel));
      bytes.push_back(static_cast<uchar>(note));
      bytes.push_back(0);
      midifile.addEvent(track, releaseTick, bytes);
    }

    // Intentionally skip sortTracks() on music tracks: its same-tick comparator
    // reorders CCs by controller number, which swaps GBA 0x1E prefix commands
    // behind their 0x1D value. Music tracks are appended in tick-ascending
    // order per track, so MidiFile::write can compute delta times directly.
    //
    // The conductor track is another matter: we append Name/TimeSig/markers
    // at arbitrary ticks and the tempo fallback at tick 0 AFTER the markers,
    // which would produce negative deltas (then VLQ-clamped to 2^28-1,
    // overflowing mid2agb's int32 ConvertTimes). Sort just the conductor.
    midifile.sortTrackNoteOnsBeforeOffs(kConductorTrack);
    return midifile.write(path);
  } catch (...) {
    return false;
  }
}

} // namespace ccomidi
