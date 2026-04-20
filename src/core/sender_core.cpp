#include "core/sender_core.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ccomidi {

namespace {

using MessagePair = std::pair<std::uint8_t, std::uint8_t>;

constexpr std::uint8_t kStatusCcBase = 0xB0;

void append_message(SenderCore::EncodedCommand *encoded,
                    std::uint8_t controller, std::uint8_t value) {
  if (!encoded || encoded->count >= encoded->messages.size())
    return;

  encoded->messages[encoded->count++] = MessagePair{controller, value};
  encoded->valid = true;
}

bool encoded_equal(const SenderCore::EncodedCommand &lhs,
                   const SenderCore::EncodedCommand &rhs) {
  if (lhs.valid != rhs.valid || lhs.count != rhs.count)
    return false;

  for (std::size_t i = 0; i < lhs.count; ++i) {
    if (lhs.messages[i] != rhs.messages[i])
      return false;
  }

  return true;
}

} // namespace

SenderCore::SenderCore() { reset(); }

void SenderCore::reset() {
  outputChannelValue_ = 0.0;
  programValue_ = 0.0;
  programEnabledValue_ = 1.0;
  rows_ = {};
  reset_runtime_state();
}

void SenderCore::reset_runtime_state() {
  lastEmittedChannel_ = output_channel();
  lastEmittedProgram_ = program();
  lastEmittedProgramValid_ = false;
  lastTransportPlaying_ = false;
  for (RowState &row : rows_) {
    row.lastEmittedValid = false;
    row.lastEmitted = {};
  }
}

void SenderCore::set_output_channel(double value) {
  outputChannelValue_ = value;
}

void SenderCore::set_program(double value) { programValue_ = value; }

void SenderCore::set_program_enabled(double value) {
  programEnabledValue_ = value;
}

void SenderCore::set_row_enabled(std::size_t row, double value) {
  if (row >= kMaxCommandRows)
    return;
  rows_[row].enabledValue = value;
}

void SenderCore::set_row_type(std::size_t row, double value) {
  if (row >= kMaxCommandRows)
    return;
  rows_[row].typeValue = static_cast<double>(
      sanitize_row_type(row, floor_to_command_type(value)));
}

void SenderCore::set_row_value(std::size_t row, std::size_t field,
                               double value) {
  if (row >= kMaxCommandRows || field >= kMaxCommandFields)
    return;
  rows_[row].fieldValues[field] = value;
}

double SenderCore::output_channel_value() const { return outputChannelValue_; }

double SenderCore::program_value() const { return programValue_; }

double SenderCore::program_enabled_value() const {
  return programEnabledValue_;
}

double SenderCore::row_enabled_value(std::size_t row) const {
  if (row >= kMaxCommandRows)
    return 0.0;
  return rows_[row].enabledValue;
}

double SenderCore::row_type_value(std::size_t row) const {
  if (row >= kMaxCommandRows)
    return 0.0;
  return static_cast<double>(row_type(row));
}

double SenderCore::row_value_raw(std::size_t row, std::size_t field) const {
  if (row >= kMaxCommandRows || field >= kMaxCommandFields)
    return 0.0;
  return rows_[row].fieldValues[field];
}

std::uint8_t SenderCore::output_channel() const {
  return floor_to_u8(outputChannelValue_, 0, 15);
}

std::uint8_t SenderCore::program() const {
  return floor_to_u8(programValue_, 0, 127);
}

bool SenderCore::program_enabled() const {
  return floor_to_u8(programEnabledValue_, 0, 1) != 0;
}

bool SenderCore::row_enabled(std::size_t row) const {
  if (row >= kMaxCommandRows)
    return false;
  return floor_to_u8(rows_[row].enabledValue, 0, 1) != 0;
}

CommandType SenderCore::row_type(std::size_t row) const {
  if (row >= kMaxCommandRows)
    return CommandType::None;
  return sanitize_row_type(row, floor_to_command_type(rows_[row].typeValue));
}

std::uint8_t SenderCore::row_value(std::size_t row, std::size_t field) const {
  if (row >= kMaxCommandRows || field >= kMaxCommandFields)
    return 0;
  return floor_to_u8(rows_[row].fieldValues[field], 0, 127);
}

std::uint8_t SenderCore::floor_to_u8(double value, std::uint8_t minValue,
                                     std::uint8_t maxValue) {
  const double floored = std::floor(value);
  if (floored <= static_cast<double>(minValue))
    return minValue;
  if (floored >= static_cast<double>(maxValue))
    return maxValue;
  return static_cast<std::uint8_t>(floored);
}

CommandType SenderCore::floor_to_command_type(double value) {
  const auto raw =
      floor_to_u8(value, static_cast<std::uint8_t>(CommandType::None),
                  static_cast<std::uint8_t>(CommandType::MemAcc10));
  return static_cast<CommandType>(raw);
}

CommandType SenderCore::sanitize_row_type(std::size_t row, CommandType type) {
  if (is_fixed_command_row(row))
    return fixed_command_type_for_row(row);
  if (is_fixed_command_type(type))
    return CommandType::None;
  return type;
}

SenderCore::EncodedCommand SenderCore::encode_row(std::size_t row) const {
  EncodedCommand encoded = {};
  if (row >= kMaxCommandRows || !row_enabled(row))
    return encoded;

  const CommandType type = row_type(row);
  const std::uint8_t value0 = row_value(row, 0);
  const std::uint8_t value1 = row_value(row, 1);
  const std::uint8_t value2 = row_value(row, 2);
  const std::uint8_t value3 = row_value(row, 3);

  switch (type) {
  case CommandType::None:
    break;
  case CommandType::Mod:
    append_message(&encoded, 0x01, value0);
    break;
  case CommandType::Volume:
    append_message(&encoded, 0x07, value0);
    break;
  case CommandType::Pan:
    append_message(&encoded, 0x0A, value0);
    break;
  case CommandType::BendRange:
    append_message(&encoded, 0x14, value0);
    break;
  case CommandType::LfoSpeed:
    append_message(&encoded, 0x15, value0);
    break;
  case CommandType::ModType:
    append_message(&encoded, 0x16, value0);
    break;
  case CommandType::Tune:
    append_message(&encoded, 0x18, value0);
    break;
  case CommandType::LfoDelay:
    append_message(&encoded, 0x1A, value0);
    break;
  case CommandType::Priority21:
    append_message(&encoded, 0x21, value0);
    break;
  case CommandType::Priority27:
    append_message(&encoded, 0x27, value0);
    break;
  case CommandType::XcmdIecv:
    append_message(&encoded, 0x1E, 0x08);
    append_message(&encoded, 0x1D, value0);
    break;
  case CommandType::XcmdIecl:
    append_message(&encoded, 0x1E, 0x09);
    append_message(&encoded, 0x1D, value0);
    break;
  case CommandType::MemAcc0C:
    append_message(&encoded, 0x0D, value0);
    append_message(&encoded, 0x0E, value1);
    append_message(&encoded, 0x0F, value2);
    append_message(&encoded, 0x0C, value3);
    break;
  case CommandType::MemAcc10:
    append_message(&encoded, 0x0D, value0);
    append_message(&encoded, 0x0E, value1);
    append_message(&encoded, 0x0F, value2);
    append_message(&encoded, 0x10, value3);
    break;
  }

  return encoded;
}

bool SenderCore::apply_event(const AutomationEvent &event, bool *channelChanged,
                             std::array<bool, kMaxCommandRows> *rowChanged,
                             bool *programChanged) {
  if (channelChanged)
    *channelChanged = false;
  if (programChanged)
    *programChanged = false;
  std::array<bool, kMaxCommandRows> localRowChanged = {};
  if (!rowChanged)
    rowChanged = &localRowChanged;

  switch (event.address.kind) {
  case ParamKind::OutputChannel: {
    const std::uint8_t before = output_channel();
    outputChannelValue_ = event.value;
    const std::uint8_t after = output_channel();
    if (channelChanged)
      *channelChanged = (before != after);
    return before != after;
  }
  case ParamKind::Program: {
    const std::uint8_t before = program();
    programValue_ = event.value;
    const std::uint8_t after = program();
    if (programChanged)
      *programChanged = (before != after);
    return before != after;
  }
  case ParamKind::ProgramEnabled: {
    const bool before = program_enabled();
    programEnabledValue_ = event.value;
    const bool after = program_enabled();
    if (programChanged)
      *programChanged = (before != after);
    return before != after;
  }
  case ParamKind::RowEnabled:
    if (event.address.row >= kMaxCommandRows)
      return false;
    {
      const bool before = row_enabled(event.address.row);
      rows_[event.address.row].enabledValue = event.value;
      const bool after = row_enabled(event.address.row);
      (*rowChanged)[event.address.row] = before != after;
      if (!after)
        rows_[event.address.row].lastEmittedValid = false;
      return before != after;
    }
  case ParamKind::RowType:
    if (event.address.row >= kMaxCommandRows)
      return false;
    {
      const CommandType before = row_type(event.address.row);
      rows_[event.address.row].typeValue = static_cast<double>(
          sanitize_row_type(event.address.row, floor_to_command_type(event.value)));
      const CommandType after = row_type(event.address.row);
      (*rowChanged)[event.address.row] = before != after;
      return before != after;
    }
  case ParamKind::RowValue0:
  case ParamKind::RowValue1:
  case ParamKind::RowValue2:
  case ParamKind::RowValue3:
    if (event.address.row >= kMaxCommandRows)
      return false;
    {
      const std::size_t field = static_cast<std::size_t>(event.address.kind) -
                                static_cast<std::size_t>(ParamKind::RowValue0);
      const std::uint8_t before = row_value(event.address.row, field);
      rows_[event.address.row].fieldValues[field] = event.value;
      const std::uint8_t after = row_value(event.address.row, field);
      (*rowChanged)[event.address.row] = before != after;
      return before != after;
    }
  }

  return false;
}

void SenderCore::append_encoded(std::uint32_t time,
                                const EncodedCommand &encoded,
                                PlannedEvents *out) {
  if (!out || !encoded.valid)
    return;

  for (std::size_t i = 0; i < encoded.count; ++i) {
    if (out->count >= out->events.size())
      return;

    MidiEvent &event = out->events[out->count++];
    event.time = time;
    event.status = static_cast<std::uint8_t>(kStatusCcBase | output_channel());
    event.data1 = encoded.messages[i].first;
    event.data2 = encoded.messages[i].second;
  }
}

void SenderCore::emit_program_change(std::uint32_t time, PlannedEvents *out) {
  if (!out || !program_enabled() || out->count >= out->events.size())
    return;

  MidiEvent &event = out->events[out->count++];
  event.time = time;
  event.status = static_cast<std::uint8_t>(0xC0 | output_channel());
  event.data1 = program();
  event.data2 = 0;
  lastEmittedProgram_ = program();
  lastEmittedProgramValid_ = true;
}

void SenderCore::emit_snapshot(std::uint32_t time, PlannedEvents *out) {
  lastEmittedChannel_ = output_channel();
  emit_program_change(time, out);

  for (std::size_t row = 0; row < rows_.size(); ++row) {
    EncodedCommand encoded = encode_row(row);
    rows_[row].lastEmitted = encoded;
    rows_[row].lastEmittedValid = encoded.valid;
    append_encoded(time, encoded, out);
  }
}

void SenderCore::emit_changed_rows(
    std::uint32_t time, const std::array<bool, kMaxCommandRows> &rowChanged,
    PlannedEvents *out) {
  for (std::size_t row = 0; row < rows_.size(); ++row) {
    if (!rowChanged[row])
      continue;

    EncodedCommand encoded = encode_row(row);
    RowState &state = rows_[row];

    if (!encoded.valid) {
      state.lastEmittedValid = false;
      state.lastEmitted = {};
      continue;
    }

    if (state.lastEmittedValid && encoded_equal(state.lastEmitted, encoded))
      continue;

    append_encoded(time, encoded, out);
    state.lastEmitted = encoded;
    state.lastEmittedValid = true;
  }
}

void SenderCore::process_block(const TransportState &transport,
                               const AutomationEvent *events,
                               std::size_t eventCount, PlannedEvents *out) {
  if (!out)
    return;

  out->clear();

  std::size_t eventIndex = 0;
  bool startEdgeHandled = false;

  while (eventIndex < eventCount ||
         (!startEdgeHandled && transport.isPlaying && !lastTransportPlaying_)) {
    const std::uint32_t currentTime =
        (!startEdgeHandled && transport.isPlaying && !lastTransportPlaying_ &&
         eventIndex < eventCount)
            ? std::min<std::uint32_t>(0, events[eventIndex].time)
        : (!startEdgeHandled && transport.isPlaying && !lastTransportPlaying_)
            ? 0
            : events[eventIndex].time;

    bool channelChanged = false;
    bool programChanged = false;
    std::array<bool, kMaxCommandRows> rowChanged = {};

    while (eventIndex < eventCount && events[eventIndex].time == currentTime) {
      bool localChannelChanged = false;
      bool localProgramChanged = false;
      apply_event(events[eventIndex], &localChannelChanged, &rowChanged,
                  &localProgramChanged);
      channelChanged = channelChanged || localChannelChanged;
      programChanged = programChanged || localProgramChanged;
      ++eventIndex;
    }

    if (!startEdgeHandled && transport.isPlaying && !lastTransportPlaying_ &&
        currentTime == 0) {
      emit_snapshot(0, out);
      startEdgeHandled = true;
      continue;
    }

    if (transport.isPlaying) {
      if (channelChanged) {
        emit_snapshot(currentTime, out);
      } else {
        if (programChanged)
          emit_program_change(currentTime, out);
        emit_changed_rows(currentTime, rowChanged, out);
      }
    }
  }

  if (!startEdgeHandled && transport.isPlaying && !lastTransportPlaying_)
    emit_snapshot(0, out);

  lastTransportPlaying_ = transport.isPlaying;
}

bool SenderCore::apply_parameter_change(
    const AutomationEvent &event, bool *channelChanged,
    std::array<bool, kMaxCommandRows> *rowChanged, bool *programChanged) {
  return apply_event(event, channelChanged, rowChanged, programChanged);
}

void SenderCore::emit_preapplied_changes(
    bool transportIsPlaying, bool channelChanged,
    const std::array<bool, kMaxCommandRows> &rowChanged, std::uint32_t time,
    PlannedEvents *out, bool programChanged) {
  (void)transportIsPlaying;
  if (!out)
    return;

  out->clear();
  if (channelChanged) {
    emit_snapshot(time, out);
  } else {
    if (programChanged)
      emit_program_change(time, out);
    emit_changed_rows(time, rowChanged, out);
  }
}

bool SenderCore::runtime_was_playing() const { return lastTransportPlaying_; }

} // namespace ccomidi
