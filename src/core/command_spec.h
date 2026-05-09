#ifndef CCOMIDI_COMMAND_SPEC_H
#define CCOMIDI_COMMAND_SPEC_H
#include <array>
#include <cstddef>
#include <cstdint>

struct FieldSpec {
  const char *name;
  std::uint8_t minValue;
  std::uint8_t maxValue;
  std::uint8_t defaultValue;
};
struct CommandSpec {
  const char *displayName;
  uint8_t
      fieldCount; // How many values this param takes. 1 for vol, 4 for Memacc
  std::array<FieldSpec, 15> fields;
};
namespace ccomidi {

// clang-format off
inline constexpr std::array<CommandSpec, 23> kCommandSpecs = {{
  // None = 0
  {"None",        0, {{}}},
  // Mod = 1
  {"Mod",         1, {{{"Mod Amt",   0, 127,  0}}}},
  // Volume = 2
  {"Volume",      1, {{{"Level",     0, 127,  0}}}},
  // Pan = 3
  {"Pan",         1, {{{"Position",  0, 127, 64}}}},
  // BendRange = 4
  {"Bend Range",  1, {{{"Semitones", 0, 127,  2}}}},
  // LfoSpeed = 5
  {"LFO Speed",   1, {{{"Speed",     0, 127,  0}}}},
  // ModType = 6
  {"Mod Type",    1, {{{"Type",      0, 127,  0}}}},
  // Tune = 7
  {"Tune",        1, {{{"Tune",      0, 127,  0}}}},
  // LfoDelay = 8
  {"LFO Delay",   1, {{{"Delay",     0, 127,  0}}}},
  // Priority21 = 9
  {"Priority 21", 1, {{{"Value",     0, 127,  0}}}},
  // Priority27 = 10
  {"Priority 27", 1, {{{"Value",     0, 127,  0}}}},
  // XcmdIecv = 11
  {"XCMD IECV",   1, {{{"Value",     0, 127,  0}}}},
  // XcmdIecl = 12
  {"XCMD IECL",   1, {{{"Value",     0, 127,  0}}}},
  // MemAcc0C = 13
  {"Mem Acc 0C",  4, {{{"Op", 0, 127, 0}, {"Arg1", 0, 127, 0}, {"Arg2", 0, 127, 0}, {"Data", 0, 127, 0}}}},
  // MemAcc10 = 14
  {"Mem Acc 10",  4, {{{"Op", 0, 127, 0}, {"Arg1", 0, 127, 0}, {"Arg2", 0, 127, 0}, {"Data", 0, 127, 0}}}},
  // XcmdType = 15 (selector 0x02)
  {"xTYPE",       1, {{{"Type",      0, 127,  0}}}},
  // XcmdAtta = 16 (selector 0x04)
  // ADSR XCMDs carry the GBA ToneData byte directly. DirectSound uses the
  // full 0..255 (musical max 254 — 0xFF reserved). CGB voices (square /
  // wave / noise) take attack/decay/release as 3-bit and sustain as 4-bit;
  // the engine masks them down internally, so the dial range can stay wide
  // here without distorting CGB voices' actual behavior.
  {"xATTA",       1, {{{"Attack",    0, 254,  0}}}},
  // XcmdDeca = 17 (selector 0x05)
  {"xDECA",       1, {{{"Decay",     0, 254,  0}}}},
  // XcmdSust = 18 (selector 0x06)
  {"xSUST",       1, {{{"Sustain",   0, 254,  0}}}},
  // XcmdRele = 19 (selector 0x07)
  {"xRELE",       1, {{{"Release",   0, 254,  0}}}},
  // XcmdLeng = 20 (selector 0x0A)
  {"xLENG",       1, {{{"Length",    0, 127,  0}}}},
  // XcmdSwee = 21 (selector 0x0B)
  {"xSWEE",       1, {{{"Sweep",     0, 127,  0}}}},
  // Xcmd0D = 22 (selector 0x0D, 4 LE bytes — store + notify)
  {"XCMD 0D",     4, {{{"B0", 0, 127, 0}, {"B1", 0, 127, 0}, {"B2", 0, 127, 0}, {"B3", 0, 127, 0}}}},
}};
// clang-format on
// xWAVE (selector 0x01) is intentionally absent: v2 stores no real address
// over MIDI CC, so the driver is notify-only. Send it from a different path
// if/when an encoding escape or address-table resolver is added.
enum class CommandType : std::uint8_t {
  None = 0,
  Mod = 1,
  Volume = 2,
  Pan = 3,
  BendRange = 4,
  LfoSpeed = 5,
  ModType = 6,
  Tune = 7,
  LfoDelay = 8,
  Priority21 = 9,
  Priority27 = 10,
  XcmdIecv = 11,
  XcmdIecl = 12,
  MemAcc0C = 13,
  MemAcc10 = 14,
  XcmdType = 15,
  XcmdAtta = 16,
  XcmdDeca = 17,
  XcmdSust = 18,
  XcmdRele = 19,
  XcmdLeng = 20,
  XcmdSwee = 21,
  Xcmd0D = 22,
};
const CommandSpec &command_spec(CommandType type);

inline const CommandSpec &command_spec(CommandType type) {
  return kCommandSpecs[static_cast<std::size_t>(type)];
}

} // namespace ccomidi
#endif
