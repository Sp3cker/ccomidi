#include <clap/clap.h>
#include <clap/events.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/plugin-features.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>

#include <vector>

#include "core/command_spec.h"
#include "core/sender_core.h"
#include "gui/ccomidi_editor.h"
#include "plugin/ccomidi_plugin_shared.h"
#include "plugin/param_ids.h"

namespace ccomidi {

constexpr std::uint32_t kStateVersion = 5;

Plugin *from_plugin(const clap_plugin_t *plugin) {
  return plugin ? static_cast<Plugin *>(plugin->plugin_data) : nullptr;
}
/* Name of CC */
const char *command_type_name(CommandType type) {
  return kCommandSpecs[static_cast<uint8_t>(type)].displayName;
}

const char *fixed_row_name(std::size_t row) {
  return command_type_name(fixed_command_type_for_row(row));
}

bool row_has_dynamic_type(std::size_t row) {
  return !is_fixed_command_row(row);
}

CommandType sanitize_param_row_type(std::size_t row, CommandType type) {
  if (is_fixed_command_row(row))
    return fixed_command_type_for_row(row);
  if (is_fixed_command_type(type))
    return CommandType::None;
  return type;
}

bool should_rescan_param_info(const ParamAddress &address) {
  return address.kind == ParamKind::OutputChannel ||
         (address.kind == ParamKind::RowType &&
          row_has_dynamic_type(address.row));
};

const char *command_field_name(CommandType type, std::uint32_t field) {
  // const CommandSpec &spec = command_spec(type);
  // if (field > spec.fieldCount) {
  //   throw std::out_of_range(
  //       "command_field_name: CC command does not have field number");
  // };
  switch (type) {
  case CommandType::MemAcc0C:
  case CommandType::MemAcc10:
    switch (field) {
    case 0:
      return "Op";
    case 1:
      return "Arg1";
    case 2:
      return "Arg2";
    case 3:
      return "Data";
    default:
      return "Value";
    }
  case CommandType::Xcmd0D:
    switch (field) {
    case 0:
      return "B0";
    case 1:
      return "B1";
    case 2:
      return "B2";
    case 3:
      return "B3";
    default:
      return "Value";
    }
  case CommandType::None:
    break;
  default:
    if (field == 0)
      return command_type_name(type);
    break;
  }

  (void)command_spec(type);  // reserved for future multi-param naming
  // When not a multi-param cc AND
  switch (field) {
  case 0:
    return "Value A";
  case 1:
    return "Value B";
  case 2:
    return "Value C";
  case 3:
    return "Value D";
  default:
    return "Value";
  }
}

void fill_ui_snapshot(Plugin *plugin, UiSnapshot *snapshot) {
  if (!plugin || !snapshot)
    return;

  std::lock_guard<std::mutex> lock(plugin->stateMutex);
  snapshot->outputChannel = plugin->core.output_channel_value();
  snapshot->program = plugin->core.program_value();
  snapshot->programEnabled = plugin->core.program_enabled_value();
  for (std::size_t row = 0; row < kMaxCommandRows; ++row) {
    snapshot->rows[row].enabled = plugin->core.row_enabled_value(row);
    snapshot->rows[row].type = plugin->core.row_type_value(row);
    for (std::size_t field = 0; field < kMaxCommandFields; ++field)
      snapshot->rows[row].values[field] =
          plugin->core.row_value_raw(row, field);
  }
}

void schedule_param_info_rescan(Plugin *plugin) {
  if (!plugin || !plugin->host)
    return;

  plugin->pendingParamInfoRescan = true;
  if (plugin->host->request_callback)
    plugin->host->request_callback(plugin->host);
}

void set_param_value(Plugin *plugin, clap_id paramId, double value) {
  if (!plugin)
    return;

  ParamAddress address = {};
  if (!decode_param_id(paramId, &address))
    return;

  switch (address.kind) {
  case ParamKind::OutputChannel:
    plugin->core.set_output_channel(value);
    break;
  case ParamKind::Program:
    plugin->core.set_program(value);
    break;
  case ParamKind::ProgramEnabled:
    plugin->core.set_program_enabled(value);
    break;
  case ParamKind::RowEnabled:
    plugin->core.set_row_enabled(address.row, value);
    break;
  case ParamKind::RowType:
    plugin->core.set_row_type(address.row, value);
    break;
  case ParamKind::RowValue0:
    plugin->core.set_row_value(address.row, 0, value);
    break;
  case ParamKind::RowValue1:
    plugin->core.set_row_value(address.row, 1, value);
    break;
  case ParamKind::RowValue2:
    plugin->core.set_row_value(address.row, 2, value);
    break;
  case ParamKind::RowValue3:
    plugin->core.set_row_value(address.row, 3, value);
    break;
  }
}

void apply_ui_param_change(Plugin *plugin, clap_id paramId, double value) {
  if (!plugin)
    return;

  ParamAddress address = {};
  if (!decode_param_id(paramId, &address))
    return;

  {
    std::lock_guard<std::mutex> lock(plugin->stateMutex);
    std::array<bool, kMaxCommandRows> rowChanged = {};
    bool channelChanged = false;
    bool programChanged = false;
    plugin->core.apply_parameter_change(AutomationEvent{0, address, value},
                                        &channelChanged, &rowChanged,
                                        &programChanged);
    plugin->pendingUiChannelChange =
        plugin->pendingUiChannelChange || channelChanged;
    plugin->pendingUiProgramChange =
        plugin->pendingUiProgramChange || programChanged;
    for (std::size_t row = 0; row < kMaxCommandRows; ++row)
      plugin->pendingUiRowChanged[row] =
          plugin->pendingUiRowChanged[row] || rowChanged[row];
    plugin->pendingUiParamEvents.emplace_back(paramId, value);
    if (should_rescan_param_info(address))
      schedule_param_info_rescan(plugin);
  }

  request_host_param_sync(plugin);
}

void emit_pending_ui_param_events(Plugin *plugin,
                                  const clap_output_events_t *out) {
  if (!plugin || !out)
    return;

  std::vector<std::pair<clap_id, double>> events;
  {
    std::lock_guard<std::mutex> lock(plugin->stateMutex);
    events.swap(plugin->pendingUiParamEvents);
  }

  for (const auto &[paramId, value] : events) {
    clap_event_param_gesture_t begin = {};
    begin.header.size = sizeof(begin);
    begin.header.time = 0;
    begin.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    begin.header.type = CLAP_EVENT_PARAM_GESTURE_BEGIN;
    begin.header.flags = 0;
    begin.param_id = paramId;
    out->try_push(out, &begin.header);

    clap_event_param_value_t valueEvent = {};
    valueEvent.header.size = sizeof(valueEvent);
    valueEvent.header.time = 0;
    valueEvent.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    valueEvent.header.type = CLAP_EVENT_PARAM_VALUE;
    valueEvent.header.flags = 0;
    valueEvent.param_id = paramId;
    valueEvent.cookie = nullptr;
    valueEvent.note_id = -1;
    valueEvent.port_index = -1;
    valueEvent.channel = -1;
    valueEvent.key = -1;
    valueEvent.value = value;
    out->try_push(out, &valueEvent.header);

    clap_event_param_gesture_t end = begin;
    end.header.type = CLAP_EVENT_PARAM_GESTURE_END;
    out->try_push(out, &end.header);
  }
}

double get_param_value(const Plugin *plugin, clap_id paramId, bool *ok) {
  if (ok)
    *ok = false;
  if (!plugin)
    return 0.0;

  ParamAddress address = {};
  if (!decode_param_id(paramId, &address))
    return 0.0;

  if (ok)
    *ok = true;

  switch (address.kind) {
  case ParamKind::OutputChannel:
    return plugin->core.output_channel_value();
  case ParamKind::Program:
    return plugin->core.program_value();
  case ParamKind::ProgramEnabled:
    return plugin->core.program_enabled_value();
  case ParamKind::RowEnabled:
    return plugin->core.row_enabled_value(address.row);
  case ParamKind::RowType:
    return plugin->core.row_type_value(address.row);
  case ParamKind::RowValue0:
    return plugin->core.row_value_raw(address.row, 0);
  case ParamKind::RowValue1:
    return plugin->core.row_value_raw(address.row, 1);
  case ParamKind::RowValue2:
    return plugin->core.row_value_raw(address.row, 2);
  case ParamKind::RowValue3:
    return plugin->core.row_value_raw(address.row, 3);
  }

  if (ok)
    *ok = false;
  return 0.0;
}

clap_param_info_flags param_flags_for_id(clap_id id) {
  if (id == kParamOutputChannel)
    return CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS |
           CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;

  if (id == kParamProgram)
    return CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS |
           CLAP_PARAM_IS_STEPPED;

  if (id == kParamProgramEnabled)
    return CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS |
           CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;

  ParamAddress address = {};
  if (!decode_param_id(id, &address))
    return 0;

  switch (address.kind) {
  case ParamKind::RowEnabled:
    return CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS |
           CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
  case ParamKind::RowType:
    if (is_fixed_command_row(address.row))
      return CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM | CLAP_PARAM_IS_HIDDEN;
    return CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS |
           CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
  case ParamKind::RowValue0:
  case ParamKind::RowValue1:
  case ParamKind::RowValue2:
  case ParamKind::RowValue3: {
    clap_param_info_flags flags =
        CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS;
    if (is_fixed_command_row(address.row)) {
      const std::uint32_t field =
          static_cast<std::uint32_t>(address.kind) -
          static_cast<std::uint32_t>(ParamKind::RowValue0);
      const CommandType type = fixed_command_type_for_row(address.row);
      if (field >= command_spec(type).fieldCount)
        flags |= CLAP_PARAM_IS_HIDDEN;
    }
    return flags;
  }
  case ParamKind::OutputChannel:
  case ParamKind::Program:
  case ParamKind::ProgramEnabled:
    return 0;
  }
  return CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS;
}

void describe_param(const Plugin *plugin, clap_id id, clap_param_info_t *info) {
  std::memset(info, 0, sizeof(*info));
  info->id = id;
  info->cookie = nullptr;
  info->flags = param_flags_for_id(id);

  if (id == kParamOutputChannel) {
    std::snprintf(info->name, sizeof(info->name), "Output Channel");
    std::snprintf(info->module, sizeof(info->module), "Global");
    info->min_value = 0.0;
    info->max_value = 15.0;
    info->default_value = 0.0;
    return;
  }

  if (id == kParamProgram) {
    std::snprintf(info->name, sizeof(info->name), "Program");
    std::snprintf(info->module, sizeof(info->module), "Global");
    info->min_value = 0.0;
    info->max_value = 127.0;
    info->default_value = 0.0;
    return;
  }

  if (id == kParamProgramEnabled) {
    std::snprintf(info->name, sizeof(info->name), "Emit Program Change");
    std::snprintf(info->module, sizeof(info->module), "Global");
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = 1.0;
    return;
  }

  ParamAddress address = {};
  if (!decode_param_id(id, &address))
    return;

  if (is_fixed_command_row(address.row))
    std::snprintf(info->module, sizeof(info->module), "Fixed/%s",
                  fixed_row_name(address.row));
  else
    std::snprintf(
        info->module, sizeof(info->module), "Rows/%02u",
        static_cast<unsigned>(address.row - kFixedCommandRowCount + 1));
  switch (address.kind) {
  case ParamKind::RowEnabled:
    if (is_fixed_command_row(address.row))
      std::snprintf(info->name, sizeof(info->name), "%s Enable",
                    fixed_row_name(address.row));
    else
      std::snprintf(
          info->name, sizeof(info->name), "Row %02u Enable",
          static_cast<unsigned>(address.row - kFixedCommandRowCount + 1));
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = 0.0;
    break;
  case ParamKind::RowType:
    if (is_fixed_command_row(address.row)) {
      const double fixedType =
          static_cast<double>(fixed_command_type_for_row(address.row));
      std::snprintf(info->name, sizeof(info->name), "%s Command",
                    fixed_row_name(address.row));
      info->min_value = fixedType;
      info->max_value = fixedType;
      info->default_value = fixedType;
    } else {
      std::snprintf(
          info->name, sizeof(info->name), "Row %02u Command",
          static_cast<unsigned>(address.row - kFixedCommandRowCount + 1));
      info->min_value = static_cast<double>(CommandType::None);
      info->max_value = static_cast<double>(CommandType::Xcmd0D);
      info->default_value = static_cast<double>(CommandType::None);
    }
    break;
  case ParamKind::RowValue0:
  case ParamKind::RowValue1:
  case ParamKind::RowValue2:
  case ParamKind::RowValue3: {
    const std::uint32_t field =
        static_cast<std::uint32_t>(address.kind) -
        static_cast<std::uint32_t>(ParamKind::RowValue0);
    CommandType rowCommand = CommandType::None;
    if (plugin)
      rowCommand = plugin->core.row_type(address.row);
    if (is_fixed_command_row(address.row))
      std::snprintf(info->name, sizeof(info->name), "%s %s",
                    fixed_row_name(address.row),
                    command_field_name(rowCommand, field));
    else
      std::snprintf(
          info->name, sizeof(info->name), "Row %02u %s",
          static_cast<unsigned>(address.row - kFixedCommandRowCount + 1),
          command_field_name(rowCommand, field));
    info->min_value = 0.0;
    info->max_value = 127.0;
    info->default_value = 0.0;
    break;
  }
  case ParamKind::OutputChannel:
  case ParamKind::Program:
  case ParamKind::ProgramEnabled:
    break;
  }
}

std::uint8_t quantize_channel(double value) {
  if (value <= 0.0)
    return 0;
  if (value >= 15.0)
    return 15;
  return static_cast<std::uint8_t>(std::floor(value));
}

bool is_forwarded_midi_status(std::uint8_t status) {
  switch (status & 0xF0) {
  case 0x80:
  case 0x90:
  case 0xA0:
  case 0xE0:
    return true;
  default:
    return false;
  }
}

void push_midi_event(std::uint32_t time, std::uint8_t status,
                     std::uint8_t data1, std::uint8_t data2,
                     const clap_output_events_t *outEvents) {
  if (!outEvents)
    return;

  clap_event_midi_t event = {};
  event.header.size = sizeof(event);
  event.header.time = time;
  event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  event.header.type = CLAP_EVENT_MIDI;
  event.header.flags = 0;
  event.port_index = 0;
  event.data[0] = status;
  event.data[1] = data1;
  event.data[2] = data2;
  outEvents->try_push(outEvents, &event.header);
}

void reload_voicegroup_if_changed(Plugin *plugin) {
  if (!plugin)
    return;
  const long long diskMtime = voicegroup_bridge_state_mtime();
  if (diskMtime == plugin->voiceLoad.mtimeNs)
    return;
  std::lock_guard<std::mutex> lock(plugin->stateMutex);
  plugin->voiceLoad = voicegroup_bridge_load_state();
}

void request_host_param_sync(Plugin *plugin) {
  if (!plugin || !plugin->host)
    return;

  const auto *hostParams = static_cast<const clap_host_params_t *>(
      plugin->host->get_extension(plugin->host, CLAP_EXT_PARAMS));
  if (hostParams && hostParams->rescan)
    hostParams->rescan(plugin->host, CLAP_PARAM_RESCAN_VALUES);
  if (hostParams && hostParams->request_flush)
    hostParams->request_flush(plugin->host);
  else if (plugin->host->request_process)
    plugin->host->request_process(plugin->host);
}

void destroy_editor(Plugin *plugin) {
  if (!plugin || !plugin->editor)
    return;

  EditorState *editor = plugin->editor;
  plugin->editor = nullptr;

  editor_prepare_destroy(editor);
  editor_destroy(editor);
}

bool plugin_init(const clap_plugin_t *plugin) {
  Plugin *self = from_plugin(plugin);
  if (!self)
    return false;

  std::lock_guard<std::mutex> lock(self->stateMutex);
  self->core.reset();
  return true;
}

void plugin_destroy(const clap_plugin_t *plugin) {
  Plugin *self = from_plugin(plugin);
  destroy_editor(self);
  delete self;
}

bool plugin_activate(const clap_plugin_t *plugin, double sampleRate,
                     std::uint32_t minFrames, std::uint32_t maxFrames) {
  (void)sampleRate;
  (void)minFrames;
  (void)maxFrames;

  Plugin *self = from_plugin(plugin);
  if (!self)
    return false;

  self->active = true;
  std::lock_guard<std::mutex> lock(self->stateMutex);
  self->core.reset_runtime_state();
  return true;
}

void plugin_deactivate(const clap_plugin_t *plugin) {
  Plugin *self = from_plugin(plugin);
  if (!self)
    return;
  self->active = false;
}

bool plugin_start_processing(const clap_plugin_t *plugin) {
  Plugin *self = from_plugin(plugin);
  if (!self)
    return false;
  std::lock_guard<std::mutex> lock(self->stateMutex);
  self->core.reset_runtime_state();
  return true;
}

void plugin_stop_processing(const clap_plugin_t *plugin) { (void)plugin; }

void plugin_reset(const clap_plugin_t *plugin) {
  Plugin *self = from_plugin(plugin);
  if (!self)
    return;
  std::lock_guard<std::mutex> lock(self->stateMutex);
  self->core.reset_runtime_state();
}

void push_planned_events(const PlannedEvents &planned, std::uint32_t timeOffset,
                         const clap_output_events_t *outEvents) {
  if (!outEvents)
    return;

  for (std::size_t i = 0; i < planned.count; ++i) {
    push_midi_event(planned.events[i].time + timeOffset,
                    planned.events[i].status, planned.events[i].data1,
                    planned.events[i].data2, outEvents);
  }
}

void push_midi_events(const std::vector<MidiEvent> &events,
                      std::uint32_t timeOffset,
                      const clap_output_events_t *outEvents) {
  if (!outEvents)
    return;

  for (const MidiEvent &event : events)
    push_midi_event(event.time + timeOffset, event.status, event.data1,
                    event.data2, outEvents);
}

void push_merged_events(const PlannedEvents &planned,
                        const std::vector<MidiEvent> &forwarded,
                        std::uint32_t timeOffset,
                        const clap_output_events_t *outEvents) {
  std::size_t plannedIndex = 0;
  std::size_t forwardedIndex = 0;

  while (plannedIndex < planned.count || forwardedIndex < forwarded.size()) {
    const bool usePlanned =
        forwardedIndex >= forwarded.size() ||
        (plannedIndex < planned.count &&
         planned.events[plannedIndex].time <= forwarded[forwardedIndex].time);

    if (usePlanned) {
      push_midi_event(planned.events[plannedIndex].time + timeOffset,
                      planned.events[plannedIndex].status,
                      planned.events[plannedIndex].data1,
                      planned.events[plannedIndex].data2, outEvents);
      ++plannedIndex;
    } else {
      push_midi_event(forwarded[forwardedIndex].time + timeOffset,
                      forwarded[forwardedIndex].status,
                      forwarded[forwardedIndex].data1,
                      forwarded[forwardedIndex].data2, outEvents);
      ++forwardedIndex;
    }
  }
}

clap_process_status plugin_process(const clap_plugin_t *plugin,
                                   const clap_process_t *process) {
  Plugin *self = from_plugin(plugin);
  if (!self || !process)
    return CLAP_PROCESS_ERROR;

  const bool initialPlaying =
      process->transport &&
      ((process->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0);
  bool wasPlaying = false;
  bool pendingUiChannelChange = false;
  bool pendingUiProgramChange = false;
  std::array<bool, kMaxCommandRows> pendingUiRowChanged = {};
  PlannedEvents pendingUiEvents = {};
  {
    std::lock_guard<std::mutex> lock(self->stateMutex);
    wasPlaying = self->core.runtime_was_playing();
    pendingUiChannelChange = self->pendingUiChannelChange;
    pendingUiProgramChange = self->pendingUiProgramChange;
    pendingUiRowChanged = self->pendingUiRowChanged;
    self->pendingUiChannelChange = false;
    self->pendingUiProgramChange = false;
    self->pendingUiRowChanged.fill(false);
    self->core.emit_preapplied_changes(
        initialPlaying, pendingUiChannelChange, pendingUiRowChanged, 0,
        &pendingUiEvents, pendingUiProgramChange);
    (void)wasPlaying;
  }

  push_planned_events(pendingUiEvents, 0, process->out_events);
  emit_pending_ui_param_events(self, process->out_events);

  std::uint8_t activeOutputChannel = 0;
  {
    std::lock_guard<std::mutex> lock(self->stateMutex);
    activeOutputChannel = self->core.output_channel();
  }

  bool currentPlaying = initialPlaying;
  std::uint32_t sliceStart = 0;
  std::vector<AutomationEvent> sliceAutomation;
  std::vector<MidiEvent> sliceForwardedMidi;
  sliceAutomation.reserve(
      process->in_events ? process->in_events->size(process->in_events) : 0);
  sliceForwardedMidi.reserve(
      process->in_events ? process->in_events->size(process->in_events) : 0);

  auto flush_slice = [&](std::uint32_t endTime, bool nextPlaying) {
    PlannedEvents planned;
    {
      std::lock_guard<std::mutex> lock(self->stateMutex);
      self->core.process_block(TransportState{currentPlaying},
                               sliceAutomation.empty() ? nullptr
                                                       : sliceAutomation.data(),
                               sliceAutomation.size(), &planned);
    }
    push_merged_events(planned, sliceForwardedMidi, sliceStart,
                       process->out_events);
    sliceAutomation.clear();
    sliceForwardedMidi.clear();
    sliceStart = endTime;
    currentPlaying = nextPlaying;
  };

  const std::uint32_t eventCount =
      process->in_events ? process->in_events->size(process->in_events) : 0;
  std::uint32_t index = 0;
  while (index < eventCount) {
    const clap_event_header_t *header =
        process->in_events->get(process->in_events, index);
    if (!header) {
      ++index;
      continue;
    }

    const std::uint32_t eventTime = header->time;
    bool hasTransportUpdate = false;
    bool updatedPlaying = currentPlaying;
    std::vector<AutomationEvent> paramEventsAtTime;
    std::vector<MidiEvent> midiEventsAtTime;

    while (index < eventCount) {
      const clap_event_header_t *groupHeader =
          process->in_events->get(process->in_events, index);
      if (!groupHeader || groupHeader->time != eventTime)
        break;

      if (groupHeader->space_id == CLAP_CORE_EVENT_SPACE_ID) {
        if (groupHeader->type == CLAP_EVENT_TRANSPORT) {
          const auto *transportEvent =
              reinterpret_cast<const clap_event_transport_t *>(groupHeader);
          hasTransportUpdate = true;
          updatedPlaying =
              (transportEvent->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        } else if (groupHeader->type == CLAP_EVENT_PARAM_VALUE) {
          const auto *paramEvent =
              reinterpret_cast<const clap_event_param_value_t *>(groupHeader);
          ParamAddress address = {};
          if (decode_param_id(paramEvent->param_id, &address)) {
            if (address.kind == ParamKind::OutputChannel)
              activeOutputChannel = quantize_channel(paramEvent->value);
            paramEventsAtTime.push_back(
                AutomationEvent{0, address, paramEvent->value});
            if (should_rescan_param_info(address))
              schedule_param_info_rescan(self);
          }
        } else if (groupHeader->type == CLAP_EVENT_MIDI) {
          const auto *midiEvent =
              reinterpret_cast<const clap_event_midi_t *>(groupHeader);
          if (is_forwarded_midi_status(midiEvent->data[0])) {
            const std::uint8_t status = static_cast<std::uint8_t>(
                (midiEvent->data[0] & 0xF0) | activeOutputChannel);
            midiEventsAtTime.push_back(
                MidiEvent{0, status, midiEvent->data[1], midiEvent->data[2]});
          }
        } else if (groupHeader->type == CLAP_EVENT_NOTE_ON ||
                   groupHeader->type == CLAP_EVENT_NOTE_OFF ||
                   groupHeader->type == CLAP_EVENT_NOTE_CHOKE) {
          const auto *noteEvent =
              reinterpret_cast<const clap_event_note_t *>(groupHeader);
          const std::uint8_t key = static_cast<std::uint8_t>(
              std::clamp<int>(static_cast<int>(noteEvent->key), 0, 127));
          std::uint8_t velocity = static_cast<std::uint8_t>(std::clamp<int>(
              static_cast<int>(std::lround(noteEvent->velocity * 127.0)), 0,
              127));
          std::uint8_t status = static_cast<std::uint8_t>(
              (groupHeader->type == CLAP_EVENT_NOTE_ON ? 0x90 : 0x80) |
              activeOutputChannel);
          if (groupHeader->type == CLAP_EVENT_NOTE_ON && velocity == 0)
            velocity = 1;
          if (groupHeader->type == CLAP_EVENT_NOTE_CHOKE)
            velocity = 0;
          midiEventsAtTime.push_back(MidiEvent{0, status, key, velocity});
        }
      }

      ++index;
    }

    if (hasTransportUpdate) {
      flush_slice(eventTime, updatedPlaying);
    }

    for (AutomationEvent &event : paramEventsAtTime)
      event.time = eventTime - sliceStart;
    sliceAutomation.insert(sliceAutomation.end(), paramEventsAtTime.begin(),
                           paramEventsAtTime.end());
    for (MidiEvent &event : midiEventsAtTime)
      event.time = eventTime - sliceStart;
    sliceForwardedMidi.insert(sliceForwardedMidi.end(),
                              midiEventsAtTime.begin(), midiEventsAtTime.end());
  }

  flush_slice(process->frames_count, currentPlaying);

  // Zero the declared stereo audio output so the host receives defined
  // silence. See audio_ports_count for why the bus exists.
  if (process->audio_outputs && process->audio_outputs_count > 0) {
    const clap_audio_buffer_t &out = process->audio_outputs[0];
    for (std::uint32_t ch = 0; ch < out.channel_count; ++ch) {
      if (out.data32 && out.data32[ch])
        std::memset(out.data32[ch], 0, sizeof(float) * process->frames_count);
      if (out.data64 && out.data64[ch])
        std::memset(out.data64[ch], 0, sizeof(double) * process->frames_count);
    }
  }
  return CLAP_PROCESS_CONTINUE;
}

const void *plugin_get_extension(const clap_plugin_t *plugin, const char *id);
void plugin_on_main_thread(const clap_plugin_t *plugin);

const char *s_features[] = {
    CLAP_PLUGIN_FEATURE_NOTE_EFFECT,
    CLAP_PLUGIN_FEATURE_UTILITY,
    nullptr,
};

const clap_plugin_descriptor_t s_descriptor = {
    .clap_version = CLAP_VERSION,
    .id = "com.sallegrezza.ccomidi",
    .name = "ccomidi",
    .vendor = "sallegrezza",
    .url = "",
    .manual_url = "",
    .support_url = "",
    .version = "0.1.0",
    .description =
        "Playback-triggered MIDI CC sender for m4a-compatible commands",
    .features = s_features,
};

const clap_plugin_t s_pluginPrototype = {
    .desc = &s_descriptor,
    .plugin_data = nullptr,
    .init = plugin_init,
    .destroy = plugin_destroy,
    .activate = plugin_activate,
    .deactivate = plugin_deactivate,
    .start_processing = plugin_start_processing,
    .stop_processing = plugin_stop_processing,
    .reset = plugin_reset,
    .process = plugin_process,
    .get_extension = plugin_get_extension,
    .on_main_thread = plugin_on_main_thread,
};

bool gui_is_api_supported(const clap_plugin_t *plugin, const char *api,
                          bool isFloating) {
  (void)plugin;
  bool supported = false;
  if (isFloating)
    supported = true;
#if defined(_WIN32)
  else
    supported = api && std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
#elif defined(__APPLE__)
  else
    supported = api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
#else
  else
    supported = api && std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
#endif
  return supported;
}

bool gui_get_preferred_api(const clap_plugin_t *plugin, const char **api,
                           bool *isFloating) {
  (void)plugin;
  if (!api || !isFloating)
    return false;

  *isFloating = false;
#if defined(_WIN32)
  *api = CLAP_WINDOW_API_WIN32;
#elif defined(__APPLE__)
  *api = CLAP_WINDOW_API_COCOA;
#else
  *api = CLAP_WINDOW_API_X11;
#endif
  return true;
}

bool gui_create(const clap_plugin_t *plugin, const char *api, bool isFloating) {
  (void)api;
  (void)isFloating;
  Plugin *self = from_plugin(plugin);
  if (!self || self->editor)
    return false;

  self->editor = editor_create(self);
  return self->editor != nullptr;
}

void gui_destroy(const clap_plugin_t *plugin) {
  Plugin *self = from_plugin(plugin);
  destroy_editor(self);
}

bool gui_set_scale(const clap_plugin_t *plugin, double scale) {
  (void)plugin;
  (void)scale;
  return false;
}

bool gui_get_size(const clap_plugin_t *plugin, std::uint32_t *width,
                  std::uint32_t *height) {
  Plugin *self = from_plugin(plugin);
  if (!self || !self->editor)
    return false;
  editor_get_size(self->editor, width, height);
  return true;
}

bool gui_can_resize(const clap_plugin_t *plugin) {
  Plugin *self = from_plugin(plugin);
  return self && self->editor && editor_can_resize(self->editor);
}

bool gui_get_resize_hints(const clap_plugin_t *plugin,
                          clap_gui_resize_hints_t *hints) {
  (void)plugin;
  if (!hints)
    return false;
  hints->can_resize_horizontally = true;
  hints->can_resize_vertically = true;
  hints->preserve_aspect_ratio = false;
  hints->aspect_ratio_width = 0;
  hints->aspect_ratio_height = 0;
  return true;
}

bool gui_adjust_size(const clap_plugin_t *plugin, std::uint32_t *width,
                     std::uint32_t *height) {
  (void)plugin;
  (void)width;
  (void)height;
  return true;
}

bool gui_set_size(const clap_plugin_t *plugin, std::uint32_t width,
                  std::uint32_t height) {
  Plugin *self = from_plugin(plugin);
  return self && self->editor && editor_set_size(self->editor, width, height);
}

bool gui_set_parent(const clap_plugin_t *plugin, const clap_window_t *window) {
  Plugin *self = from_plugin(plugin);
  if (!self || !self->editor || !window)
    return false;

  std::uintptr_t nativeParent = 0;
#if defined(_WIN32)
  nativeParent = reinterpret_cast<std::uintptr_t>(window->win32);
#elif defined(__APPLE__)
  nativeParent = reinterpret_cast<std::uintptr_t>(window->cocoa);
#else
  nativeParent = static_cast<std::uintptr_t>(window->x11);
#endif
  return editor_set_parent(self->editor, nativeParent);
}

bool gui_set_transient(const clap_plugin_t *plugin,
                       const clap_window_t *window) {
  (void)plugin;
  (void)window;
  return true;
}

void gui_suggest_title(const clap_plugin_t *plugin, const char *title) {
  (void)plugin;
  (void)title;
}

bool gui_show(const clap_plugin_t *plugin) {
  Plugin *self = from_plugin(plugin);
  if (!self || !self->editor)
    return false;
  const bool shown = editor_show(self->editor);
  if (shown)
    editor_start_internal_timer(self->editor);
  return shown;
}

bool gui_hide(const clap_plugin_t *plugin) {
  Plugin *self = from_plugin(plugin);
  if (!self || !self->editor)
    return false;
  return editor_hide(self->editor);
}

const clap_plugin_gui_t s_gui = {
    .is_api_supported = gui_is_api_supported,
    .get_preferred_api = gui_get_preferred_api,
    .create = gui_create,
    .destroy = gui_destroy,
    .set_scale = gui_set_scale,
    .get_size = gui_get_size,
    .can_resize = gui_can_resize,
    .get_resize_hints = gui_get_resize_hints,
    .adjust_size = gui_adjust_size,
    .set_size = gui_set_size,
    .set_parent = gui_set_parent,
    .set_transient = gui_set_transient,
    .suggest_title = gui_suggest_title,
    .show = gui_show,
    .hide = gui_hide,
};

std::uint32_t audio_ports_count(const clap_plugin_t *plugin, bool isInput) {
  (void)plugin;
  // Ableton (and several other VST3 hosts) reject plugins without an audio
  // output bus. We're a MIDI effect — we don't produce audio — but we
  // declare a stereo output and write silence into it so the host will load
  // us. The MIDI output bus still carries the transformed events downstream.
  return isInput ? 0u : 1u;
}

bool audio_ports_get(const clap_plugin_t *plugin, std::uint32_t index,
                     bool isInput, clap_audio_port_info_t *info) {
  (void)plugin;
  if (isInput || index != 0 || !info)
    return false;
  std::memset(info, 0, sizeof(*info));
  info->id = 0;
  std::snprintf(info->name, sizeof(info->name), "%s", "Audio Output");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}

const clap_plugin_audio_ports_t s_audioPorts = {
    .count = audio_ports_count,
    .get = audio_ports_get,
};

bool note_ports_get(const clap_plugin_t *plugin, std::uint32_t index,
                    bool isInput, clap_note_port_info_t *info) {
  (void)plugin;
  if (index != 0 || !info)
    return false;

  std::memset(info, 0, sizeof(*info));
  info->id = 0;
  info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
  info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
  std::snprintf(info->name, sizeof(info->name), "%s",
                isInput ? "MIDI Input" : "MIDI Output");
  return true;
}
std::uint32_t note_ports_count(const clap_plugin_t *plugin, bool isInput) {
  (void)plugin;
  (void)isInput;
  return 1u;
}
const clap_plugin_note_ports_t s_notePorts = {
    .count = note_ports_count,
    .get = note_ports_get,
};

std::uint32_t params_count(const clap_plugin_t *plugin) {
  (void)plugin;
  return total_param_count();
}

// DAW-visible display order. clap_ids are kept stable so saved sessions stay
// compatible; this table only re-orders how the host enumerates them, pulling
// Program (PC number) and every row's Value0 ("amount") knob to the top and
// pushing the stepped Enabled/Type slots to the bottom.
// Layout: 16 rows × 6 slots + 3 globals = 99 entries.
static_assert(kMaxCommandRows == 16,
              "param display table assumes 16 command rows");
static_assert(total_param_count() == 99,
              "param display table assumes 99 total params");

constexpr clap_id kParamDisplayOrder[99] = {
    0,  97, 98,                                                  // globals
    3,  9,  15, 21, 27, 33, 39, 45, 51, 57, 63, 69, 75, 81, 87,  93,  // Value0
    4,  10, 16, 22, 28, 34, 40, 46, 52, 58, 64, 70, 76, 82, 88,  94,  // Value1
    5,  11, 17, 23, 29, 35, 41, 47, 53, 59, 65, 71, 77, 83, 89,  95,  // Value2
    6,  12, 18, 24, 30, 36, 42, 48, 54, 60, 66, 72, 78, 84, 90,  96,  // Value3
    1,  7,  13, 19, 25, 31, 37, 43, 49, 55, 61, 67, 73, 79, 85,  91,  // Enabled
    2,  8,  14, 20, 26, 32, 38, 44, 50, 56, 62, 68, 74, 80, 86,  92,  // Type
};

bool params_get_info(const clap_plugin_t *plugin, std::uint32_t paramIndex,
                     clap_param_info_t *info) {
  if (!info || paramIndex >= total_param_count())
    return false;

  Plugin *self = from_plugin(plugin);
  std::lock_guard<std::mutex> lock(self->stateMutex);
  describe_param(self, kParamDisplayOrder[paramIndex], info);
  return true;
}

bool params_get_value(const clap_plugin_t *plugin, clap_id paramId,
                      double *value) {
  Plugin *self = from_plugin(plugin);
  if (!self || !value)
    return false;

  std::lock_guard<std::mutex> lock(self->stateMutex);
  bool ok = false;
  *value = get_param_value(self, paramId, &ok);
  return ok;
}

bool params_value_to_text(const clap_plugin_t *plugin, clap_id paramId,
                          double value, char *display, std::uint32_t size) {
  (void)plugin;
  if (!display || size == 0)
    return false;

  if (paramId == kParamOutputChannel) {
    std::snprintf(display, size, "%u",
                  static_cast<unsigned>(quantize_channel(value) + 1));
    return true;
  }

  if (paramId == kParamProgram) {
    int program = static_cast<int>(std::floor(value));
    program = std::clamp(program, 0, 127);
    std::snprintf(display, size, "%d", program);
    return true;
  }

  if (paramId == kParamProgramEnabled) {
    std::snprintf(display, size, "%s", std::floor(value) >= 1.0 ? "On" : "Off");
    return true;
  }

  ParamAddress address = {};
  if (!decode_param_id(paramId, &address))
    return false;

  switch (address.kind) {
  case ParamKind::RowEnabled:
    std::snprintf(display, size, "%s", std::floor(value) >= 1.0 ? "On" : "Off");
    return true;
  case ParamKind::RowType:
    std::snprintf(display, size, "%s",
                  command_type_name(static_cast<CommandType>(std::clamp<int>(
                      static_cast<int>(std::floor(value)),
                      static_cast<int>(CommandType::None),
                      static_cast<int>(CommandType::Xcmd0D)))));
    return true;
  default:
    std::snprintf(display, size, "%.3f", value);
    return true;
  }
}

bool params_text_to_value(const clap_plugin_t *plugin, clap_id paramId,
                          const char *display, double *value) {
  (void)plugin;
  if (!display || !value)
    return false;

  if (paramId == kParamOutputChannel) {
    char *end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display)
      return false;
    *value = std::clamp(parsed - 1.0, 0.0, 15.0);
    return true;
  }

  if (paramId == kParamProgram) {
    char *end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display)
      return false;
    *value = std::clamp(parsed, 0.0, 127.0);
    return true;
  }

  if (paramId == kParamProgramEnabled) {
    if (std::strcmp(display, "On") == 0) {
      *value = 1.0;
      return true;
    }
    if (std::strcmp(display, "Off") == 0) {
      *value = 0.0;
      return true;
    }
    char *end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display)
      return false;
    *value = parsed >= 1.0 ? 1.0 : 0.0;
    return true;
  }

  ParamAddress address = {};
  if (!decode_param_id(paramId, &address))
    return false;

  switch (address.kind) {
  case ParamKind::RowEnabled:
    if (std::strcmp(display, "On") == 0) {
      *value = 1.0;
      return true;
    }
    if (std::strcmp(display, "Off") == 0) {
      *value = 0.0;
      return true;
    }
    {
      char *end = nullptr;
      const double parsed = std::strtod(display, &end);
      if (end == display)
        return false;
      *value = parsed >= 1.0 ? 1.0 : 0.0;
      return true;
    }
  case ParamKind::RowType:
    for (int candidate = static_cast<int>(CommandType::None);
         candidate <= static_cast<int>(CommandType::Xcmd0D); ++candidate) {
      if (std::strcmp(display, command_type_name(
                                   static_cast<CommandType>(candidate))) == 0) {
        *value = static_cast<double>(sanitize_param_row_type(
            address.row, static_cast<CommandType>(candidate)));
        return true;
      }
    }
    {
      char *end = nullptr;
      const double parsed = std::strtod(display, &end);
      if (end == display)
        return false;
      *value = static_cast<double>(sanitize_param_row_type(
          address.row, static_cast<CommandType>(std::clamp<int>(
                           static_cast<int>(std::floor(parsed)),
                           static_cast<int>(CommandType::None),
                           static_cast<int>(CommandType::Xcmd0D)))));
      return true;
    }
  default:
    char *end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display)
      return false;
    *value = std::clamp(parsed, 0.0, 127.0);
    return true;
  }
}

void params_flush(const clap_plugin_t *plugin, const clap_input_events_t *in,
                  const clap_output_events_t *out) {
  Plugin *self = from_plugin(plugin);
  if (!self || !in)
    return;

  bool channelChanged = false;
  bool programChanged = false;
  std::array<bool, kMaxCommandRows> rowChanged = {};
  PlannedEvents planned = {};
  {
    std::lock_guard<std::mutex> lock(self->stateMutex);
    const std::uint32_t count = in->size(in);
    for (std::uint32_t i = 0; i < count; ++i) {
      const clap_event_header_t *header = in->get(in, i);
      if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
          header->type != CLAP_EVENT_PARAM_VALUE)
        continue;

      const auto *event =
          reinterpret_cast<const clap_event_param_value_t *>(header);
      ParamAddress address = {};
      if (!decode_param_id(event->param_id, &address))
        continue;

      bool localChannelChanged = false;
      bool localProgramChanged = false;
      self->core.apply_parameter_change(
          AutomationEvent{0, address, event->value}, &localChannelChanged,
          &rowChanged, &localProgramChanged);
      channelChanged = channelChanged || localChannelChanged;
      programChanged = programChanged || localProgramChanged;
      if (should_rescan_param_info(address))
        schedule_param_info_rescan(self);
    }

    self->core.emit_preapplied_changes(false, channelChanged, rowChanged, 0,
                                       &planned, programChanged);
  }

  push_planned_events(planned, 0, out);
  emit_pending_ui_param_events(self, out);
}

const clap_plugin_params_t s_params = {
    .count = params_count,
    .get_info = params_get_info,
    .get_value = params_get_value,
    .value_to_text = params_value_to_text,
    .text_to_value = params_text_to_value,
    .flush = params_flush,
};

bool read_row_state(const clap_istream_t *stream, double values[6]) {
  return stream->read(stream, values, sizeof(double) * 6) ==
         static_cast<std::int64_t>(sizeof(double) * 6);
}

void apply_row_state(SenderCore *core, std::size_t row,
                     const double values[6]) {
  if (!core)
    return;
  core->set_row_enabled(row, values[0]);
  core->set_row_type(row, values[1]);
  core->set_row_value(row, 0, values[2]);
  core->set_row_value(row, 1, values[3]);
  core->set_row_value(row, 2, values[4]);
  core->set_row_value(row, 3, values[5]);
}

bool state_save(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
  Plugin *self = from_plugin(plugin);
  if (!self || !stream)
    return false;

  std::lock_guard<std::mutex> lock(self->stateMutex);
  const std::uint32_t version = kStateVersion;
  if (stream->write(stream, &version, sizeof(version)) != sizeof(version))
    return false;

  const double outputChannel = self->core.output_channel_value();
  if (stream->write(stream, &outputChannel, sizeof(outputChannel)) !=
      sizeof(outputChannel))
    return false;

  const double program = self->core.program_value();
  if (stream->write(stream, &program, sizeof(program)) != sizeof(program))
    return false;

  const double programEnabled = self->core.program_enabled_value();
  if (stream->write(stream, &programEnabled, sizeof(programEnabled)) !=
      sizeof(programEnabled))
    return false;

  for (std::size_t row = 0; row < kMaxCommandRows; ++row) {
    const double values[] = {
        self->core.row_enabled_value(row), self->core.row_type_value(row),
        self->core.row_value_raw(row, 0),  self->core.row_value_raw(row, 1),
        self->core.row_value_raw(row, 2),  self->core.row_value_raw(row, 3)};
    if (stream->write(stream, values, sizeof(values)) != sizeof(values))
      return false;
  }

  return true;
}

bool state_load(const clap_plugin_t *plugin, const clap_istream_t *stream) {
  Plugin *self = from_plugin(plugin);
  if (!self || !stream)
    return false;

  std::uint32_t version = 0;
  if (stream->read(stream, &version, sizeof(version)) != sizeof(version))
    return false;
  if (version != 2 && version != 3 && version != 4 && version != kStateVersion)
    return false;

  double outputChannel = 0.0;
  if (stream->read(stream, &outputChannel, sizeof(outputChannel)) !=
      sizeof(outputChannel))
    return false;

  double program = 0.0;
  if (version == 4 || version == kStateVersion) {
    if (stream->read(stream, &program, sizeof(program)) != sizeof(program))
      return false;
  }

  double programEnabled = 1.0;
  if (version == kStateVersion) {
    if (stream->read(stream, &programEnabled, sizeof(programEnabled)) !=
        sizeof(programEnabled))
      return false;
  }

  std::lock_guard<std::mutex> lock(self->stateMutex);
  self->core.reset();
  self->core.set_output_channel(outputChannel);
  self->core.set_program(program);
  self->core.set_program_enabled(programEnabled);

  if (version == kStateVersion || version == 4 || version == 3) {
    for (std::size_t row = 0; row < kMaxCommandRows; ++row) {
      double values[6] = {};
      if (!read_row_state(stream, values))
        return false;
      apply_row_state(&self->core, row, values);
    }
  } else {
    const std::size_t selectedChannel = quantize_channel(outputChannel);
    for (std::size_t channel = 0; channel < kMidiChannelCount; ++channel) {
      for (std::size_t row = 0; row < kMaxCommandRows; ++row) {
        double values[6] = {};
        if (!read_row_state(stream, values))
          return false;
        if (channel == selectedChannel)
          apply_row_state(&self->core, row, values);
      }
    }
  }

  if (self->active)
    self->core.reset_runtime_state();
  schedule_param_info_rescan(self);

  return true;
}

const clap_plugin_state_t s_state = {
    .save = state_save,
    .load = state_load,
};

const void *plugin_get_extension(const clap_plugin_t *plugin, const char *id) {
  (void)plugin;
  if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
    return &s_audioPorts;
  if (std::strcmp(id, CLAP_EXT_GUI) == 0)
    return &s_gui;
  if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0)
    return &s_notePorts;
  if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
    return &s_params;
  if (std::strcmp(id, CLAP_EXT_STATE) == 0)
    return &s_state;
  return nullptr;
}

void plugin_on_main_thread(const clap_plugin_t *plugin) {
  Plugin *self = from_plugin(plugin);
  if (!self)
    return;
  if (self->pendingParamInfoRescan) {
    const auto *hostParams = static_cast<const clap_host_params_t *>(
        self->host->get_extension(self->host, CLAP_EXT_PARAMS));
    self->pendingParamInfoRescan = false;
    if (hostParams && hostParams->rescan)
      hostParams->rescan(self->host, CLAP_PARAM_RESCAN_INFO);
  }
  (void)self;
}

std::uint32_t factory_get_plugin_count(const clap_plugin_factory_t *factory) {
  (void)factory;
  return 1;
}

const clap_plugin_descriptor_t *
factory_get_plugin_descriptor(const clap_plugin_factory_t *factory,
                              std::uint32_t index) {
  (void)factory;
  return index == 0 ? &s_descriptor : nullptr;
}

const clap_plugin_t *factory_create_plugin(const clap_plugin_factory_t *factory,
                                           const clap_host_t *host,
                                           const char *pluginId) {
  (void)factory;
  if (!host || !pluginId || std::strcmp(pluginId, s_descriptor.id) != 0)
    return nullptr;
  if (!clap_version_is_compatible(host->clap_version))
    return nullptr;

  Plugin *self = new (std::nothrow) Plugin();
  if (!self)
    return nullptr;

  self->host = host;
  self->clapPlugin = s_pluginPrototype;
  self->clapPlugin.plugin_data = self;
  self->voiceLoad = voicegroup_bridge_load_state();
  return &self->clapPlugin;
}

const clap_plugin_factory_t s_factory = {
    .get_plugin_count = factory_get_plugin_count,
    .get_plugin_descriptor = factory_get_plugin_descriptor,
    .create_plugin = factory_create_plugin,
};

bool entry_init(const char *pluginPath) {
  (void)pluginPath;
  return true;
}

void entry_deinit(void) {}

const void *entry_get_factory(const char *factoryId) {
  if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
    return &s_factory;
  return nullptr;
}

} // namespace ccomidi

extern "C" const clap_plugin_entry clap_entry = {
    .clap_version = CLAP_VERSION,
    .init = ccomidi::entry_init,
    .deinit = ccomidi::entry_deinit,
    .get_factory = ccomidi::entry_get_factory,
};
