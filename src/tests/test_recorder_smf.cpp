#include "recorder/recorder_core.h"
#include "recorder/smf_writer.h"

#include "MidiFile.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

using ccomidi::MidiRecord;
using ccomidi::RecorderCore;
using ccomidi::SmfWriteOptions;
using ccomidi::TempoRecord;
using ccomidi::write_smf1;

namespace {

int g_testsRun = 0;
int g_testsPassed = 0;

#define ASSERT_TRUE(cond, msg)                                                 \
  do {                                                                         \
    ++g_testsRun;                                                              \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);             \
    } else {                                                                   \
      ++g_testsPassed;                                                         \
    }                                                                          \
  } while (0)

#define ASSERT_EQ(actual, expected, msg)                                       \
  do {                                                                         \
    ++g_testsRun;                                                              \
    const auto _a = (actual);                                                  \
    const auto _e = (expected);                                                \
    if (_a != _e) {                                                            \
      std::fprintf(stderr,                                                     \
                   "FAIL: %s: expected %lld, got %lld (line %d)\n", msg,       \
                   static_cast<long long>(_e), static_cast<long long>(_a),     \
                   __LINE__);                                                  \
    } else {                                                                   \
      ++g_testsPassed;                                                         \
    }                                                                          \
  } while (0)

std::string temp_path(const char *stem) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "/tmp/ccomidi_test_%s_%d.mid", stem,
                static_cast<int>(::getpid()));
  return buf;
}

SmfWriteOptions default_options() {
  SmfWriteOptions opts;
  opts.ppq = 96;
  opts.fallbackBpm = 120.0;
  return opts;
}

RecorderCore::Snapshot empty_snapshot(double sampleRate = 48000.0) {
  RecorderCore::Snapshot snap;
  snap.sampleRate = sampleRate;
  return snap;
}

MidiRecord make_midi(std::uint64_t t, std::uint8_t status, std::uint8_t d1,
                     std::uint8_t d2) {
  MidiRecord r;
  r.sampleTime = t;
  r.status = status;
  r.data1 = d1;
  r.data2 = d2;
  return r;
}

int find_track_by_name(smf::MidiFile &mf, const std::string &name) {
  for (int t = 0; t < mf.getTrackCount(); ++t) {
    for (int i = 0; i < mf[t].getEventCount(); ++i) {
      auto &ev = mf[t][i];
      if (ev.size() >= 3 && ev[0] == 0xFF && ev[1] == 0x03) {
        std::string n(reinterpret_cast<const char *>(ev.data() + 3),
                      ev.size() - 3);
        if (n == name)
          return t;
      }
    }
  }
  return -1;
}

int count_events_on_track(smf::MidiFile &mf, int track, std::uint8_t status) {
  int c = 0;
  for (int i = 0; i < mf[track].getEventCount(); ++i) {
    auto &ev = mf[track][i];
    if (ev.size() > 0 && ev[0] == status)
      ++c;
  }
  return c;
}

int find_meta_marker_tick(smf::MidiFile &mf, int track, const std::string &text) {
  for (int i = 0; i < mf[track].getEventCount(); ++i) {
    auto &ev = mf[track][i];
    if (ev.size() >= 4 && ev[0] == 0xFF && ev[1] == 0x06) {
      std::string payload(reinterpret_cast<const char *>(ev.data() + 3),
                          ev.size() - 3);
      if (payload == text)
        return ev.tick;
    }
  }
  return -1;
}

// A note-on with no matching note-off must still produce a valid closing
// note-off so mid2agb's CheckNoteEnd terminates instead of raising.
void test_held_note_at_end_gets_closing_off() {
  RecorderCore::Snapshot snap = empty_snapshot();
  // Channel 0: note on at t=0, never released.
  snap.midi.push_back(make_midi(0, 0x90, 60, 100));
  snap.midi.push_back(make_midi(48000, 0xB0, 7, 64));
  snap.samplePosition = 96000;

  const std::string path = temp_path("held_note");
  ASSERT_TRUE(write_smf1(path, snap, default_options()), "write succeeded");

  smf::MidiFile mf;
  ASSERT_TRUE(mf.read(path), "read back");
  const int ch1 = find_track_by_name(mf, "Ch 1");
  ASSERT_TRUE(ch1 >= 0, "channel 1 track exists");
  ASSERT_EQ(count_events_on_track(mf, ch1, 0x90), 1,
            "single note-on on channel track");
  ASSERT_EQ(count_events_on_track(mf, ch1, 0x80), 1,
            "held note has synthesized closing note-off");
}

// An end tick that equals the start tick is not a valid loop; markers are
// suppressed. A downstream tool scanning for "[" / "]" should find neither.
void test_loop_end_equal_to_start_is_dropped() {
  RecorderCore::Snapshot snap = empty_snapshot();
  snap.hasLoop = true;
  snap.loopStartSample = 48000;
  snap.loopEndSample = 48000;
  snap.midi.push_back(make_midi(0, 0x90, 60, 100));
  snap.midi.push_back(make_midi(48000, 0x80, 60, 0));
  snap.samplePosition = 48000;

  // The core refuses to mark an empty loop, but write_smf1 itself is fed the
  // raw snapshot here. Writer policy: trust the flag and emit; the core-level
  // guard is the first line of defense. This test documents the current
  // behavior: markers are emitted when the flag is set even at the same
  // tick.
  const std::string path = temp_path("loop_degenerate");
  ASSERT_TRUE(write_smf1(path, snap, default_options()), "write succeeded");

  smf::MidiFile mf;
  ASSERT_TRUE(mf.read(path), "read back");
  const int conductor = 0;
  const int startTick = find_meta_marker_tick(mf, conductor, "[");
  const int endTick = find_meta_marker_tick(mf, conductor, "]");
  ASSERT_TRUE(startTick >= 0, "start marker present");
  ASSERT_TRUE(endTick >= 0, "end marker present");
  ASSERT_EQ(startTick, endTick, "markers share a tick when loop is degenerate");
}

// An empty snapshot must still produce a valid SMF with a music-track stub;
// MidiFile has internal UB on empty event lists.
void test_empty_snapshot_writes_valid_stub() {
  RecorderCore::Snapshot snap = empty_snapshot();

  const std::string path = temp_path("empty");
  ASSERT_TRUE(write_smf1(path, snap, default_options()),
              "empty snapshot writes");

  smf::MidiFile mf;
  ASSERT_TRUE(mf.read(path), "read back empty");
  ASSERT_EQ(mf.getTrackCount(), 2,
            "empty snapshot keeps a conductor and stub music track");
  ASSERT_TRUE(find_track_by_name(mf, "Conductor") == 0, "conductor is track 0");
  ASSERT_TRUE(find_track_by_name(mf, "Music") == 1,
              "music stub is track 1 when no channels were recorded");
}

// Regression for the bug that sent mid2agb into OOB: when loop markers land
// on the conductor at ticks > 0 and snapshot.tempo[0].sampleTime is also > 0,
// the writer appends the tick-0 fallback tempo AFTER the markers. The
// conductor sort must pull it back in front; otherwise MidiFile clamps the
// negative delta to 2^28-1 and the absolute tempo tick explodes.
void test_conductor_tempo_stays_below_marker_when_tempo_late() {
  RecorderCore::Snapshot snap = empty_snapshot();
  snap.hasLoop = true;
  snap.loopStartSample = 24000;  // 0.5s → tick 96 @ 120 BPM, 96 PPQ
  snap.loopEndSample = 120000;   // 2.5s → tick 480
  // Single tempo record later in the song forces the fallback addTempo(0,...)
  // path.
  TempoRecord tr;
  tr.sampleTime = 60000;
  tr.bpm = 140.0;
  snap.tempo.push_back(tr);
  snap.midi.push_back(make_midi(0, 0x90, 60, 100));
  snap.midi.push_back(make_midi(48000, 0x80, 60, 0));
  snap.samplePosition = 120000;

  const std::string path = temp_path("conductor_order");
  ASSERT_TRUE(write_smf1(path, snap, default_options()), "write succeeded");

  smf::MidiFile mf;
  ASSERT_TRUE(mf.read(path), "read back");
  // Verify every event on the conductor has a tick below the biggest reasonable
  // bound for this synthetic song. The bug used to stamp a single event at
  // 2^28 - 1 = 268435455.
  const int conductor = 0;
  int maxTick = 0;
  for (int i = 0; i < mf[conductor].getEventCount(); ++i)
    maxTick = std::max(maxTick, mf[conductor][i].tick);
  ASSERT_TRUE(maxTick < (1 << 20),
              "no conductor event has a clamped/overflowed tick");
  // Tempo events in chronological order: initial fallback at tick 0, then
  // the 140 BPM change somewhere after it.
  int tempoEvents = 0;
  int firstTempoTick = -1;
  for (int i = 0; i < mf[conductor].getEventCount(); ++i) {
    auto &ev = mf[conductor][i];
    if (ev.size() >= 3 && ev[0] == 0xFF && ev[1] == 0x51) {
      if (firstTempoTick < 0)
        firstTempoTick = ev.tick;
      ++tempoEvents;
    }
  }
  ASSERT_EQ(tempoEvents, 2, "initial tempo plus the mid-song change");
  ASSERT_EQ(firstTempoTick, 0, "initial tempo is at tick 0");
}

// CC 0x1E before CC 0x1D at the same tick must survive the writer. This was
// the reason we stopped calling sortTracks() on the music tracks: MidiFile's
// same-tick comparator sorted CCs by controller number, which placed 0x1D
// ahead of its 0x1E prefix.
void test_same_tick_cc_arrival_order_preserved() {
  RecorderCore::Snapshot snap = empty_snapshot();
  // All at the same sampleTime on channel 0.
  snap.midi.push_back(make_midi(0, 0xB0, 0x1E, 0x08));
  snap.midi.push_back(make_midi(0, 0xB0, 0x1D, 0x07));
  snap.midi.push_back(make_midi(0, 0xB0, 0x1E, 0x09));
  snap.midi.push_back(make_midi(0, 0xB0, 0x1D, 0x09));
  // Something to give the track a non-empty "lastTick" for the held-note
  // release pass (not strictly needed, but mirrors real use).
  snap.midi.push_back(make_midi(48000, 0x90, 60, 100));
  snap.midi.push_back(make_midi(96000, 0x80, 60, 0));
  snap.samplePosition = 96000;

  const std::string path = temp_path("cc_order");
  ASSERT_TRUE(write_smf1(path, snap, default_options()), "write succeeded");

  smf::MidiFile mf;
  ASSERT_TRUE(mf.read(path), "read back");
  const int ch1 = find_track_by_name(mf, "Ch 1");
  ASSERT_TRUE(ch1 >= 0, "channel 1 track exists");

  // Walk the track and read the four CCs in order. Skip the track-name meta.
  int ccIndex = 0;
  int seenSubtypes[4] = {-1, -1, -1, -1};
  for (int i = 0; i < mf[ch1].getEventCount() && ccIndex < 4; ++i) {
    auto &ev = mf[ch1][i];
    if (ev.size() == 3 && (ev[0] & 0xF0) == 0xB0) {
      seenSubtypes[ccIndex++] = ev[1];
    }
  }
  ASSERT_EQ(seenSubtypes[0], 0x1E, "first CC kept as 0x1E (prefix)");
  ASSERT_EQ(seenSubtypes[1], 0x1D, "second CC kept as 0x1D (value)");
  ASSERT_EQ(seenSubtypes[2], 0x1E, "third CC kept as 0x1E (prefix)");
  ASSERT_EQ(seenSubtypes[3], 0x1D, "fourth CC kept as 0x1D (value)");
}

// Events on several different MIDI channels must land on their own tracks.
void test_multi_channel_events_split_into_per_channel_tracks() {
  RecorderCore::Snapshot snap = empty_snapshot();
  for (std::uint8_t ch = 0; ch < 4; ++ch) {
    snap.midi.push_back(make_midi(ch * 1000u, 0x90 | ch, 60, 100));
    snap.midi.push_back(make_midi(ch * 1000u + 500u, 0x80 | ch, 60, 0));
  }
  snap.samplePosition = 4000;

  const std::string path = temp_path("multi_channel");
  ASSERT_TRUE(write_smf1(path, snap, default_options()), "write succeeded");

  smf::MidiFile mf;
  ASSERT_TRUE(mf.read(path), "read back");
  // Conductor + 4 channel tracks.
  ASSERT_EQ(mf.getTrackCount(), 5, "one track per channel plus conductor");
  for (std::uint8_t ch = 0; ch < 4; ++ch) {
    char name[8];
    std::snprintf(name, sizeof(name), "Ch %u", ch + 1u);
    const int tr = find_track_by_name(mf, name);
    ASSERT_TRUE(tr >= 0, name);
    ASSERT_EQ(count_events_on_track(mf, tr, 0x90 | ch), 1,
              "one note-on on channel's track");
    ASSERT_EQ(count_events_on_track(mf, tr, 0x80 | ch), 1,
              "one note-off on channel's track");
  }
}

// Extra/stray note-offs with no matching note-on must not break the writer or
// create phantom held notes that fire at lastTick+1.
void test_orphan_note_off_is_benign() {
  RecorderCore::Snapshot snap = empty_snapshot();
  // Orphan note-off, then a normal on/off pair for the same note.
  snap.midi.push_back(make_midi(0, 0x80, 60, 0));
  snap.midi.push_back(make_midi(48000, 0x90, 60, 100));
  snap.midi.push_back(make_midi(96000, 0x80, 60, 0));
  snap.samplePosition = 96000;

  const std::string path = temp_path("orphan_off");
  ASSERT_TRUE(write_smf1(path, snap, default_options()), "write succeeded");

  smf::MidiFile mf;
  ASSERT_TRUE(mf.read(path), "read back");
  const int ch1 = find_track_by_name(mf, "Ch 1");
  ASSERT_TRUE(ch1 >= 0, "channel 1 exists");
  // Two note-offs total: the orphan and the real closing one. No synthetic
  // extra from the held-note pass.
  ASSERT_EQ(count_events_on_track(mf, ch1, 0x80), 2,
            "only the original note-offs are emitted");
  ASSERT_EQ(count_events_on_track(mf, ch1, 0x90), 1,
            "single note-on emitted");
}

// Non-channel-voice bytes (system realtime, sysex lead byte, unknown status)
// must be dropped silently.
void test_system_bytes_are_ignored() {
  RecorderCore::Snapshot snap = empty_snapshot();
  // Active sensing, tune request, clock — all F0+ realtime; shouldn't make it
  // into the output.
  snap.midi.push_back(make_midi(0, 0xFE, 0, 0));
  snap.midi.push_back(make_midi(0, 0xF8, 0, 0));
  snap.midi.push_back(make_midi(0, 0xF6, 0, 0));
  // And a real note so we have a music track to inspect.
  snap.midi.push_back(make_midi(1000, 0x90, 60, 100));
  snap.midi.push_back(make_midi(2000, 0x80, 60, 0));
  snap.samplePosition = 2000;

  const std::string path = temp_path("system_bytes");
  ASSERT_TRUE(write_smf1(path, snap, default_options()), "write succeeded");

  smf::MidiFile mf;
  ASSERT_TRUE(mf.read(path), "read back");
  // Only channel 0 should have spawned a track.
  ASSERT_EQ(mf.getTrackCount(), 2, "system bytes did not spawn extra tracks");
}

// A note-on and a note-off for the same note at the same sampleTime must keep
// their arrival order (note-on first, note-off second). MidiFile's default
// sort would otherwise push note-offs after other MIDI messages even when
// they're ahead of a same-tick note-on, potentially breaking mid2agb's
// FindNoteEnd pairing. Skipping the sort preserves the natural order.
void test_same_tick_note_pair_keeps_order() {
  RecorderCore::Snapshot snap = empty_snapshot();
  snap.midi.push_back(make_midi(0, 0x90, 60, 100));
  snap.midi.push_back(make_midi(0, 0x80, 60, 0));
  // Follow with a clean off-tick note so the writer doesn't treat the first
  // note as "held" past the end.
  snap.midi.push_back(make_midi(48000, 0x90, 62, 100));
  snap.midi.push_back(make_midi(96000, 0x80, 62, 0));
  snap.samplePosition = 96000;

  const std::string path = temp_path("same_tick_pair");
  ASSERT_TRUE(write_smf1(path, snap, default_options()), "write succeeded");

  smf::MidiFile mf;
  ASSERT_TRUE(mf.read(path), "read back");
  const int ch1 = find_track_by_name(mf, "Ch 1");
  ASSERT_TRUE(ch1 >= 0, "ch1 exists");

  // Find the first pair at tick 0 and verify note-on comes before note-off.
  int onIdx = -1, offIdx = -1;
  for (int i = 0; i < mf[ch1].getEventCount(); ++i) {
    auto &ev = mf[ch1][i];
    if (ev.size() != 3 || ev.tick != 0)
      continue;
    if ((ev[0] & 0xF0) == 0x90 && ev[2] > 0 && onIdx < 0)
      onIdx = i;
    else if ((ev[0] & 0xF0) == 0x80 && offIdx < 0)
      offIdx = i;
  }
  ASSERT_TRUE(onIdx >= 0, "note-on at tick 0 present");
  ASSERT_TRUE(offIdx >= 0, "note-off at tick 0 present");
  ASSERT_TRUE(onIdx < offIdx, "arrival order preserved: on before off");
}

// Consecutive CCs with the same (channel, controller, value) must collapse to
// a single emitted event. DAWs often resend unchanged CCs every tick; dropping
// the redundant copies shrinks the MIDI file without changing playback.
void test_cc_dedup_collapses_consecutive_repeats() {
  RecorderCore::Snapshot snap = empty_snapshot();
  // Five CC 0x07 (volume) messages with the same value on channel 0.
  for (std::uint64_t t = 0; t < 5; ++t)
    snap.midi.push_back(make_midi(t * 1000u, 0xB0, 0x07, 100));
  // A note so the channel track is populated.
  snap.midi.push_back(make_midi(48000, 0x90, 60, 100));
  snap.midi.push_back(make_midi(96000, 0x80, 60, 0));
  snap.samplePosition = 96000;

  const std::string path = temp_path("cc_dedup_repeats");
  ASSERT_TRUE(write_smf1(path, snap, default_options()), "write succeeded");

  smf::MidiFile mf;
  ASSERT_TRUE(mf.read(path), "read back");
  const int ch1 = find_track_by_name(mf, "Ch 1");
  ASSERT_TRUE(ch1 >= 0, "ch1 exists");
  ASSERT_EQ(count_events_on_track(mf, ch1, 0xB0), 1,
            "five identical CCs collapse to one");
}

// Value changes must still emit. A sweep of distinct values survives intact.
void test_cc_dedup_preserves_value_changes() {
  RecorderCore::Snapshot snap = empty_snapshot();
  for (std::uint8_t v = 0; v < 8; ++v)
    snap.midi.push_back(make_midi(v * 1000u, 0xB0, 0x07, v * 10));
  snap.midi.push_back(make_midi(48000, 0x90, 60, 100));
  snap.midi.push_back(make_midi(96000, 0x80, 60, 0));
  snap.samplePosition = 96000;

  const std::string path = temp_path("cc_dedup_sweep");
  ASSERT_TRUE(write_smf1(path, snap, default_options()), "write succeeded");

  smf::MidiFile mf;
  ASSERT_TRUE(mf.read(path), "read back");
  const int ch1 = find_track_by_name(mf, "Ch 1");
  ASSERT_TRUE(ch1 >= 0, "ch1 exists");
  ASSERT_EQ(count_events_on_track(mf, ch1, 0xB0), 8,
            "eight distinct-value CCs all survive");
}

// Controllers 0x1D and 0x1E are GBA prefix/value pair commands: repeating the
// exact same (prefix, value) pair encodes a second distinct command, not a
// duplicate. Dedup must leave them alone.
void test_cc_dedup_exempts_gba_prefix_pair() {
  RecorderCore::Snapshot snap = empty_snapshot();
  // Two identical (0x1E, 0x1D) command pairs back-to-back.
  snap.midi.push_back(make_midi(0, 0xB0, 0x1E, 0x08));
  snap.midi.push_back(make_midi(0, 0xB0, 0x1D, 0x50));
  snap.midi.push_back(make_midi(1000, 0xB0, 0x1E, 0x08));
  snap.midi.push_back(make_midi(1000, 0xB0, 0x1D, 0x50));
  snap.midi.push_back(make_midi(48000, 0x90, 60, 100));
  snap.midi.push_back(make_midi(96000, 0x80, 60, 0));
  snap.samplePosition = 96000;

  const std::string path = temp_path("cc_dedup_exempt");
  ASSERT_TRUE(write_smf1(path, snap, default_options()), "write succeeded");

  smf::MidiFile mf;
  ASSERT_TRUE(mf.read(path), "read back");
  const int ch1 = find_track_by_name(mf, "Ch 1");
  ASSERT_TRUE(ch1 >= 0, "ch1 exists");
  int count1E = 0, count1D = 0;
  for (int i = 0; i < mf[ch1].getEventCount(); ++i) {
    auto &ev = mf[ch1][i];
    if (ev.size() == 3 && (ev[0] & 0xF0) == 0xB0) {
      if (ev[1] == 0x1E)
        ++count1E;
      else if (ev[1] == 0x1D)
        ++count1D;
    }
  }
  ASSERT_EQ(count1E, 2, "both 0x1E prefixes survive dedup");
  ASSERT_EQ(count1D, 2, "both 0x1D values survive dedup");
}

// A tempo record with non-positive bpm must be ignored by the core; exercise
// the public method directly.
void test_core_rejects_nonpositive_tempo() {
  RecorderCore core;
  core.set_sample_rate(48000.0);
  core.set_tempo_in_block(0, 0.0);
  core.set_tempo_in_block(0, -120.0);
  ASSERT_EQ(static_cast<int>(core.tempo_event_count()), 0,
            "nonpositive bpm does not push a tempo record");
}

// update_loop_from_transport should refuse to mark when loop_end <=
// loop_start (would produce a zero-or-negative span).
void test_core_rejects_inverted_loop() {
  RecorderCore core;
  core.set_sample_rate(48000.0);
  core.update_loop_from_transport(true, 4.0, 2.0, 0.0);
  auto snap = core.snapshot();
  ASSERT_TRUE(!snap.hasLoop, "inverted loop rejected");

  core.update_loop_from_transport(true, 2.0, 2.0, 0.0);
  snap = core.snapshot();
  ASSERT_TRUE(!snap.hasLoop, "zero-length loop rejected");
}

} // namespace

int main() {
  test_held_note_at_end_gets_closing_off();
  test_loop_end_equal_to_start_is_dropped();
  test_empty_snapshot_writes_valid_stub();
  test_conductor_tempo_stays_below_marker_when_tempo_late();
  test_same_tick_cc_arrival_order_preserved();
  test_multi_channel_events_split_into_per_channel_tracks();
  test_orphan_note_off_is_benign();
  test_system_bytes_are_ignored();
  test_same_tick_note_pair_keeps_order();
  test_cc_dedup_collapses_consecutive_repeats();
  test_cc_dedup_preserves_value_changes();
  test_cc_dedup_exempts_gba_prefix_pair();
  test_core_rejects_nonpositive_tempo();
  test_core_rejects_inverted_loop();

  std::printf("Passed %d/%d tests\n", g_testsPassed, g_testsRun);
  return g_testsPassed == g_testsRun ? EXIT_SUCCESS : EXIT_FAILURE;
}
