#ifndef CCOMIDI_SENDER_CORE_H
#define CCOMIDI_SENDER_CORE_H

#include "core/command_spec.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace ccomidi {

constexpr std::size_t kMidiChannelCount = 16;
constexpr std::size_t kMaxCommandRows = 16;
constexpr std::size_t kMaxCommandFields = 4;
constexpr std::size_t kMaxCommandMessages = 5;
constexpr std::size_t kFixedCommandRowCount = 4;

constexpr bool is_fixed_command_row(std::size_t row) {
  return row < kFixedCommandRowCount;
}

constexpr CommandType fixed_command_type_for_row(std::size_t row) {
  switch (row) {
  case 0:
    return CommandType::Volume;
  case 1:
    return CommandType::Pan;
  case 2:
    return CommandType::Mod;
  case 3:
    return CommandType::LfoSpeed;
  default:
    return CommandType::None;
  }
}

constexpr bool is_fixed_command_type(CommandType type) {
  return type == CommandType::Volume || type == CommandType::Pan ||
         type == CommandType::Mod || type == CommandType::LfoSpeed;
}

enum class ParamKind : std::uint8_t {
  OutputChannel = 0,
  RowEnabled = 1,
  RowType = 2,
  RowValue0 = 3,
  RowValue1 = 4,
  RowValue2 = 5,
  RowValue3 = 6,
  Program = 7,
  ProgramEnabled = 8,
};

struct ParamAddress {
  ParamKind kind = ParamKind::OutputChannel;
  std::uint8_t row = 0;
};

struct AutomationEvent {
  std::uint32_t time = 0;
  ParamAddress address = {};
  double value = 0.0;
};

struct TransportState {
  bool isPlaying = false;
};

struct MidiEvent {
  std::uint32_t time = 0;
  std::uint8_t status = 0;
  std::uint8_t data1 = 0;
  std::uint8_t data2 = 0;
};

struct PlannedEvents {
  // +1 reserves space for the optional program-change event that precedes
  // a full snapshot so a snapshot that fills every row cannot truncate.
  std::array<MidiEvent, 1 + kMaxCommandRows * kMaxCommandMessages> events = {};
  std::size_t count = 0;

  void clear() { count = 0; }
};

class SenderCore {
public:
  struct EncodedCommand {
    std::array<std::pair<std::uint8_t, std::uint8_t>, kMaxCommandMessages>
        messages = {};
    std::uint8_t count = 0;
    bool valid = false;
  };

  SenderCore();

  void reset();
  void reset_runtime_state();

  void set_output_channel(double value);
  void set_program(double value);
  void set_program_enabled(double value);
  void set_row_enabled(std::size_t row, double value);
  void set_row_type(std::size_t row, double value);
  void set_row_value(std::size_t row, std::size_t field, double value);

  double output_channel_value() const;
  double program_value() const;
  double program_enabled_value() const;
  double row_enabled_value(std::size_t row) const;
  double row_type_value(std::size_t row) const;
  double row_value_raw(std::size_t row, std::size_t field) const;

  std::uint8_t output_channel() const;
  std::uint8_t program() const;
  bool program_enabled() const;
  bool row_enabled(std::size_t row) const;
  CommandType row_type(std::size_t row) const;
  std::uint8_t row_value(std::size_t row, std::size_t field) const;

  void process_block(const TransportState &transport,
                     const AutomationEvent *events, std::size_t eventCount,
                     PlannedEvents *out);
  bool apply_parameter_change(const AutomationEvent &event,
                              bool *channelChanged,
                              std::array<bool, kMaxCommandRows> *rowChanged,
                              bool *programChanged = nullptr);
  void
  emit_preapplied_changes(bool transportIsPlaying, bool channelChanged,
                          const std::array<bool, kMaxCommandRows> &rowChanged,
                          std::uint32_t time, PlannedEvents *out,
                          bool programChanged = false);
  bool runtime_was_playing() const;

private:
  struct RowState {
    double enabledValue = 0.0;
    double typeValue = 0.0;
    std::array<double, kMaxCommandFields> fieldValues = {};

    bool lastEmittedValid = false;
    EncodedCommand lastEmitted = {};
  };

  static std::uint8_t floor_to_u8(double value, std::uint8_t minValue,
                                  std::uint8_t maxValue);
  static CommandType floor_to_command_type(double value);
  static CommandType sanitize_row_type(std::size_t row, CommandType type);
  EncodedCommand encode_row(std::size_t row) const;
  bool apply_event(const AutomationEvent &event, bool *channelChanged,
                   std::array<bool, kMaxCommandRows> *rowChanged,
                   bool *programChanged);
  void emit_snapshot(std::uint32_t time, PlannedEvents *out);
  void emit_changed_rows(std::uint32_t time,
                         const std::array<bool, kMaxCommandRows> &rowChanged,
                         PlannedEvents *out);
  void emit_program_change(std::uint32_t time, PlannedEvents *out);
  void append_encoded(std::uint32_t time, const EncodedCommand &encoded,
                      PlannedEvents *out);

  double outputChannelValue_ = 0.0;
  double programValue_ = 0.0;
  double programEnabledValue_ = 1.0;
  std::uint8_t lastEmittedChannel_ = 0;
  std::uint8_t lastEmittedProgram_ = 0;
  bool lastEmittedProgramValid_ = false;
  bool lastTransportPlaying_ = false;
  std::array<RowState, kMaxCommandRows> rows_ = {};
};

} // namespace ccomidi

#endif
