#include <clap/clap.h>
#include <clap/events.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/timer-support.h>
#include <clap/plugin-features.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "core/sender_core.h"
#include "gui/ccomidi_editor.h"
#include "plugin/param_ids.h"
#include "plugin/ccomidi_plugin_shared.h"

namespace ccomidi {

constexpr std::uint32_t kStateVersion = 2;

Plugin *from_plugin(const clap_plugin_t *plugin)
{
    return plugin ? static_cast<Plugin *>(plugin->plugin_data) : nullptr;
}

const char *command_type_name(CommandType type)
{
    switch (type) {
    case CommandType::None: return "None";
    case CommandType::Mod: return "Mod";
    case CommandType::Volume: return "Volume";
    case CommandType::Pan: return "Pan";
    case CommandType::BendRange: return "BendRange";
    case CommandType::LfoSpeed: return "LfoSpeed";
    case CommandType::ModType: return "ModType";
    case CommandType::Tune: return "Tune";
    case CommandType::LfoDelay: return "LfoDelay";
    case CommandType::Priority21: return "Priority21";
    case CommandType::Priority27: return "Priority27";
    case CommandType::XcmdIecv: return "XcmdIecv";
    case CommandType::XcmdIecl: return "XcmdIecl";
    case CommandType::MemAcc0C: return "MemAcc0C";
    case CommandType::MemAcc10: return "MemAcc10";
    }

    return "Unknown";
}

void fill_ui_snapshot(Plugin *plugin, UiSnapshot *snapshot)
{
    if (!plugin || !snapshot)
        return;

    std::lock_guard<std::mutex> lock(plugin->stateMutex);
    snapshot->outputChannel = plugin->core.output_channel_value();
    for (std::size_t row = 0; row < kMaxCommandRows; ++row) {
        snapshot->rows[row].enabled = plugin->core.row_enabled_value(row);
        snapshot->rows[row].type = plugin->core.row_type_value(row);
        for (std::size_t field = 0; field < kMaxCommandFields; ++field)
            snapshot->rows[row].values[field] = plugin->core.row_value_raw(row, field);
    }
}

void set_param_value(Plugin *plugin, clap_id paramId, double value)
{
    if (!plugin)
        return;

    ParamAddress address = {};
    if (!decode_param_id(paramId, &address))
        return;

    switch (address.kind) {
    case ParamKind::OutputChannel:
        plugin->core.set_output_channel(value);
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

void apply_ui_param_change(Plugin *plugin, clap_id paramId, double value)
{
    if (!plugin)
        return;

    ParamAddress address = {};
    if (!decode_param_id(paramId, &address))
        return;

    {
        std::lock_guard<std::mutex> lock(plugin->stateMutex);
        std::array<bool, kMaxCommandRows> rowChanged = {};
        bool channelChanged = false;
        plugin->core.apply_parameter_change(AutomationEvent{0, address, value},
                                            &channelChanged,
                                            &rowChanged);
        plugin->pendingUiChannelChange = plugin->pendingUiChannelChange || channelChanged;
        for (std::size_t row = 0; row < kMaxCommandRows; ++row)
            plugin->pendingUiRowChanged[row] = plugin->pendingUiRowChanged[row] || rowChanged[row];
    }

    request_host_param_sync(plugin);
}

double get_param_value(const Plugin *plugin, clap_id paramId, bool *ok)
{
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

clap_param_info_flags param_flags_for_id(clap_id id)
{
    if (id == kParamOutputChannel)
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
        return CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS |
               CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
    case ParamKind::OutputChannel:
        return 0;
    default:
        return CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS;
    }
}

void describe_param(clap_id id, clap_param_info_t *info)
{
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

    ParamAddress address = {};
    if (!decode_param_id(id, &address))
        return;

    std::snprintf(info->module, sizeof(info->module), "Rows/%02u", static_cast<unsigned>(address.row + 1));
    switch (address.kind) {
    case ParamKind::RowEnabled:
        std::snprintf(info->name, sizeof(info->name), "Row %02u Enable", static_cast<unsigned>(address.row + 1));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        break;
    case ParamKind::RowType:
        std::snprintf(info->name, sizeof(info->name), "Row %02u Command", static_cast<unsigned>(address.row + 1));
        info->min_value = static_cast<double>(CommandType::None);
        info->max_value = static_cast<double>(CommandType::MemAcc10);
        info->default_value = static_cast<double>(CommandType::None);
        break;
    case ParamKind::RowValue0:
    case ParamKind::RowValue1:
    case ParamKind::RowValue2:
    case ParamKind::RowValue3: {
        const std::uint32_t field = static_cast<std::uint32_t>(address.kind) -
                                    static_cast<std::uint32_t>(ParamKind::RowValue0);
        std::snprintf(info->name, sizeof(info->name), "Row %02u Value %c",
                      static_cast<unsigned>(address.row + 1),
                      static_cast<char>('A' + field));
        info->min_value = 0.0;
        info->max_value = 127.0;
        info->default_value = 0.0;
        break;
    }
    case ParamKind::OutputChannel:
        break;
    }
}

std::uint8_t quantize_channel(double value)
{
    if (value <= 0.0)
        return 0;
    if (value >= 15.0)
        return 15;
    return static_cast<std::uint8_t>(std::floor(value));
}

void request_host_param_sync(Plugin *plugin)
{
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

void destroy_editor(Plugin *plugin)
{
    if (!plugin || !plugin->editor)
        return;

    EditorState *editor = plugin->editor;
    plugin->editor = nullptr;

    const clap_id timerId = plugin->guiTimerId;
    plugin->guiTimerId = CLAP_INVALID_ID;

    editor_prepare_destroy(editor);

    if (timerId != CLAP_INVALID_ID) {
        const auto *timerSupport = static_cast<const clap_host_timer_support_t *>(
            plugin->host->get_extension(plugin->host, CLAP_EXT_TIMER_SUPPORT));
        if (timerSupport && timerSupport->unregister_timer)
            timerSupport->unregister_timer(plugin->host, timerId);
    }

    editor_destroy(editor);
}

bool plugin_init(const clap_plugin_t *plugin)
{
    Plugin *self = from_plugin(plugin);
    if (!self)
        return false;

    std::lock_guard<std::mutex> lock(self->stateMutex);
    self->core.reset();
    return true;
}

void plugin_destroy(const clap_plugin_t *plugin)
{
    Plugin *self = from_plugin(plugin);
    destroy_editor(self);
    delete self;
}

bool plugin_activate(const clap_plugin_t *plugin, double sampleRate,
                     std::uint32_t minFrames, std::uint32_t maxFrames)
{
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

void plugin_deactivate(const clap_plugin_t *plugin)
{
    Plugin *self = from_plugin(plugin);
    if (!self)
        return;
    self->active = false;
}

bool plugin_start_processing(const clap_plugin_t *plugin)
{
    Plugin *self = from_plugin(plugin);
    if (!self)
        return false;
    std::lock_guard<std::mutex> lock(self->stateMutex);
    self->core.reset_runtime_state();
    return true;
}

void plugin_stop_processing(const clap_plugin_t *plugin)
{
    (void)plugin;
}

void plugin_reset(const clap_plugin_t *plugin)
{
    Plugin *self = from_plugin(plugin);
    if (!self)
        return;
    std::lock_guard<std::mutex> lock(self->stateMutex);
    self->core.reset_runtime_state();
}

void push_planned_events(const PlannedEvents &planned,
                         std::uint32_t timeOffset,
                         const clap_output_events_t *outEvents)
{
    if (!outEvents)
        return;

    for (std::size_t i = 0; i < planned.count; ++i) {
        clap_event_midi_t event = {};
        event.header.size = sizeof(event);
        event.header.time = planned.events[i].time + timeOffset;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.header.flags = 0;
        event.port_index = 0;
        event.data[0] = planned.events[i].status;
        event.data[1] = planned.events[i].data1;
        event.data[2] = planned.events[i].data2;
        if (!outEvents->try_push(outEvents, &event.header))
            return;
    }
}

clap_process_status plugin_process(const clap_plugin_t *plugin, const clap_process_t *process)
{
    Plugin *self = from_plugin(plugin);
    if (!self || !process)
        return CLAP_PROCESS_ERROR;

    const bool initialPlaying = process->transport &&
                                ((process->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0);
    bool wasPlaying = false;
    bool pendingUiChannelChange = false;
    std::array<bool, kMaxCommandRows> pendingUiRowChanged = {};
    PlannedEvents pendingUiEvents = {};
    {
        std::lock_guard<std::mutex> lock(self->stateMutex);
        wasPlaying = self->core.runtime_was_playing();
        pendingUiChannelChange = self->pendingUiChannelChange;
        pendingUiRowChanged = self->pendingUiRowChanged;
        self->pendingUiChannelChange = false;
        self->pendingUiRowChanged.fill(false);
        if (initialPlaying && wasPlaying && pendingUiChannelChange) {
            self->core.emit_preapplied_changes(true, true, pendingUiRowChanged, 0, &pendingUiEvents);
        } else if (initialPlaying && wasPlaying) {
            self->core.emit_preapplied_changes(true, false, pendingUiRowChanged, 0, &pendingUiEvents);
        }
    }

    push_planned_events(pendingUiEvents, 0, process->out_events);

    bool currentPlaying = initialPlaying;
    std::uint32_t sliceStart = 0;
    std::vector<AutomationEvent> sliceAutomation;
    sliceAutomation.reserve(process->in_events ? process->in_events->size(process->in_events) : 0);

    auto flush_slice = [&](std::uint32_t endTime, bool nextPlaying) {
        PlannedEvents planned;
        {
            std::lock_guard<std::mutex> lock(self->stateMutex);
            self->core.process_block(TransportState{currentPlaying},
                                     sliceAutomation.empty() ? nullptr : sliceAutomation.data(),
                                     sliceAutomation.size(),
                                     &planned);
        }
        push_planned_events(planned, sliceStart, process->out_events);
        sliceAutomation.clear();
        sliceStart = endTime;
        currentPlaying = nextPlaying;
    };

    const std::uint32_t eventCount = process->in_events ? process->in_events->size(process->in_events) : 0;
    std::uint32_t index = 0;
    while (index < eventCount) {
        const clap_event_header_t *header = process->in_events->get(process->in_events, index);
        if (!header) {
            ++index;
            continue;
        }

        const std::uint32_t eventTime = header->time;
        bool hasTransportUpdate = false;
        bool updatedPlaying = currentPlaying;
        std::vector<AutomationEvent> paramEventsAtTime;

        while (index < eventCount) {
            const clap_event_header_t *groupHeader = process->in_events->get(process->in_events, index);
            if (!groupHeader || groupHeader->time != eventTime)
                break;

            if (groupHeader->space_id == CLAP_CORE_EVENT_SPACE_ID) {
                if (groupHeader->type == CLAP_EVENT_TRANSPORT) {
                    const auto *transportEvent = reinterpret_cast<const clap_event_transport_t *>(groupHeader);
                    hasTransportUpdate = true;
                    updatedPlaying = (transportEvent->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
                } else if (groupHeader->type == CLAP_EVENT_PARAM_VALUE) {
                    const auto *paramEvent = reinterpret_cast<const clap_event_param_value_t *>(groupHeader);
                    ParamAddress address = {};
                    if (decode_param_id(paramEvent->param_id, &address))
                        paramEventsAtTime.push_back(AutomationEvent{0, address, paramEvent->value});
                }
            }

            ++index;
        }

        if (hasTransportUpdate) {
            flush_slice(eventTime, updatedPlaying);
        }

        for (AutomationEvent &event : paramEventsAtTime)
            event.time = eventTime - sliceStart;
        sliceAutomation.insert(sliceAutomation.end(), paramEventsAtTime.begin(), paramEventsAtTime.end());
    }

    flush_slice(process->frames_count, currentPlaying);
    return CLAP_PROCESS_CONTINUE;
}

const void *plugin_get_extension(const clap_plugin_t *plugin, const char *id);
void plugin_on_main_thread(const clap_plugin_t *plugin);

void timer_on_timer(const clap_plugin_t *plugin, clap_id timerId)
{
    Plugin *self = from_plugin(plugin);
    if (!self || !self->editor)
        return;
    if (self->guiTimerId != CLAP_INVALID_ID && timerId != self->guiTimerId)
        return;
    editor_tick(self->editor);
}

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
    .description = "Playback-triggered MIDI CC sender for m4a-compatible commands",
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

bool gui_is_api_supported(const clap_plugin_t *plugin, const char *api, bool isFloating)
{
    (void)plugin;
    if (isFloating)
        return true;
#if defined(_WIN32)
    return api && std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
#elif defined(__APPLE__)
    return api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
#else
    return api && std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
#endif
}

bool gui_get_preferred_api(const clap_plugin_t *plugin, const char **api, bool *isFloating)
{
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

bool gui_create(const clap_plugin_t *plugin, const char *api, bool isFloating)
{
    (void)api;
    (void)isFloating;
    Plugin *self = from_plugin(plugin);
    if (!self || self->editor)
        return false;

    self->editor = editor_create(self);
    if (!self->editor)
        return false;

    const auto *timerSupport = static_cast<const clap_host_timer_support_t *>(
        self->host->get_extension(self->host, CLAP_EXT_TIMER_SUPPORT));
    if (timerSupport && timerSupport->register_timer)
        timerSupport->register_timer(self->host, 16, &self->guiTimerId);
    else
        self->guiTimerId = CLAP_INVALID_ID;

    return true;
}

void gui_destroy(const clap_plugin_t *plugin)
{
    Plugin *self = from_plugin(plugin);
    destroy_editor(self);
}

bool gui_set_scale(const clap_plugin_t *plugin, double scale)
{
    (void)plugin;
    (void)scale;
    return false;
}

bool gui_get_size(const clap_plugin_t *plugin, std::uint32_t *width, std::uint32_t *height)
{
    Plugin *self = from_plugin(plugin);
    if (!self || !self->editor)
        return false;
    editor_get_size(self->editor, width, height);
    return true;
}

bool gui_can_resize(const clap_plugin_t *plugin)
{
    Plugin *self = from_plugin(plugin);
    return self && self->editor && editor_can_resize(self->editor);
}

bool gui_get_resize_hints(const clap_plugin_t *plugin, clap_gui_resize_hints_t *hints)
{
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

bool gui_adjust_size(const clap_plugin_t *plugin, std::uint32_t *width, std::uint32_t *height)
{
    (void)plugin;
    (void)width;
    (void)height;
    return true;
}

bool gui_set_size(const clap_plugin_t *plugin, std::uint32_t width, std::uint32_t height)
{
    Plugin *self = from_plugin(plugin);
    return self && self->editor && editor_set_size(self->editor, width, height);
}

bool gui_set_parent(const clap_plugin_t *plugin, const clap_window_t *window)
{
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

bool gui_set_transient(const clap_plugin_t *plugin, const clap_window_t *window)
{
    (void)plugin;
    (void)window;
    return true;
}

void gui_suggest_title(const clap_plugin_t *plugin, const char *title)
{
    (void)plugin;
    (void)title;
}

bool gui_show(const clap_plugin_t *plugin)
{
    Plugin *self = from_plugin(plugin);
    if (!self || !self->editor)
        return false;
    const bool shown = editor_show(self->editor);
    if (shown && self->guiTimerId == CLAP_INVALID_ID)
        editor_start_internal_timer(self->editor);
    return shown;
}

bool gui_hide(const clap_plugin_t *plugin)
{
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

const clap_plugin_timer_support_t s_timerSupport = {
    .on_timer = timer_on_timer,
};

std::uint32_t audio_ports_count(const clap_plugin_t *plugin, bool isInput)
{
    (void)plugin;
    (void)isInput;
    return 0;
}

bool audio_ports_get(const clap_plugin_t *plugin, std::uint32_t index, bool isInput,
                     clap_audio_port_info_t *info)
{
    (void)plugin;
    (void)index;
    (void)isInput;
    (void)info;
    return false;
}

const clap_plugin_audio_ports_t s_audioPorts = {
    .count = audio_ports_count,
    .get = audio_ports_get,
};

std::uint32_t note_ports_count(const clap_plugin_t *plugin, bool isInput)
{
    (void)plugin;
    return isInput ? 0u : 1u;
}

bool note_ports_get(const clap_plugin_t *plugin, std::uint32_t index, bool isInput,
                    clap_note_port_info_t *info)
{
    (void)plugin;
    if (isInput || index != 0 || !info)
        return false;

    std::memset(info, 0, sizeof(*info));
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::snprintf(info->name, sizeof(info->name), "MIDI Output");
    return true;
}

const clap_plugin_note_ports_t s_notePorts = {
    .count = note_ports_count,
    .get = note_ports_get,
};

std::uint32_t params_count(const clap_plugin_t *plugin)
{
    (void)plugin;
    return total_param_count();
}

bool params_get_info(const clap_plugin_t *plugin, std::uint32_t paramIndex, clap_param_info_t *info)
{
    (void)plugin;
    if (!info || paramIndex >= total_param_count())
        return false;

    describe_param(static_cast<clap_id>(paramIndex), info);
    return true;
}

bool params_get_value(const clap_plugin_t *plugin, clap_id paramId, double *value)
{
    Plugin *self = from_plugin(plugin);
    if (!self || !value)
        return false;

    std::lock_guard<std::mutex> lock(self->stateMutex);
    bool ok = false;
    *value = get_param_value(self, paramId, &ok);
    return ok;
}

bool params_value_to_text(const clap_plugin_t *plugin, clap_id paramId, double value, char *display,
                          std::uint32_t size)
{
    (void)plugin;
    if (!display || size == 0)
        return false;

    if (paramId == kParamOutputChannel) {
        std::snprintf(display, size, "%u", static_cast<unsigned>(quantize_channel(value) + 1));
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
        std::snprintf(display, size, "%s", command_type_name(static_cast<CommandType>(std::clamp<int>(
                                               static_cast<int>(std::floor(value)),
                                               static_cast<int>(CommandType::None),
                                               static_cast<int>(CommandType::MemAcc10)))));
        return true;
    default:
        std::snprintf(display, size, "%.3f", value);
        return true;
    }
}

bool params_text_to_value(const clap_plugin_t *plugin, clap_id paramId, const char *display, double *value)
{
    (void)plugin;
    if (!display || !value)
        return false;

    char *end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display)
        return false;

    if (paramId == kParamOutputChannel) {
        *value = std::clamp(parsed - 1.0, 0.0, 15.0);
        return true;
    }

    ParamAddress address = {};
    if (!decode_param_id(paramId, &address))
        return false;

    switch (address.kind) {
    case ParamKind::RowEnabled:
        *value = parsed >= 1.0 ? 1.0 : 0.0;
        return true;
    case ParamKind::RowType:
        *value = std::clamp(parsed,
                            static_cast<double>(CommandType::None),
                            static_cast<double>(CommandType::MemAcc10));
        return true;
    default:
        *value = std::clamp(parsed, 0.0, 127.0);
        return true;
    }
}

void params_flush(const clap_plugin_t *plugin, const clap_input_events_t *in,
                  const clap_output_events_t *out)
{
    (void)out;
    Plugin *self = from_plugin(plugin);
    if (!self || !in)
        return;

    std::lock_guard<std::mutex> lock(self->stateMutex);
    const std::uint32_t count = in->size(in);
    for (std::uint32_t i = 0; i < count; ++i) {
        const clap_event_header_t *header = in->get(in, i);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE)
            continue;

        const auto *event = reinterpret_cast<const clap_event_param_value_t *>(header);
        ParamAddress address = {};
        if (!decode_param_id(event->param_id, &address))
            continue;
        self->core.apply_parameter_change(AutomationEvent{0, address, event->value}, nullptr, nullptr);
    }
}

const clap_plugin_params_t s_params = {
    .count = params_count,
    .get_info = params_get_info,
    .get_value = params_get_value,
    .value_to_text = params_value_to_text,
    .text_to_value = params_text_to_value,
    .flush = params_flush,
};

bool state_save(const clap_plugin_t *plugin, const clap_ostream_t *stream)
{
    Plugin *self = from_plugin(plugin);
    if (!self || !stream)
        return false;

    std::lock_guard<std::mutex> lock(self->stateMutex);
    const std::uint32_t version = kStateVersion;
    if (stream->write(stream, &version, sizeof(version)) != sizeof(version))
        return false;

    const double outputChannel = self->core.output_channel_value();
    if (stream->write(stream, &outputChannel, sizeof(outputChannel)) != sizeof(outputChannel))
        return false;

    for (std::size_t channel = 0; channel < kMidiChannelCount; ++channel) {
        for (std::size_t row = 0; row < kMaxCommandRows; ++row) {
            const double enabled = self->core.row_enabled_value(channel, row);
            const double type = self->core.row_type_value(channel, row);
            const double value0 = self->core.row_value_raw(channel, row, 0);
            const double value1 = self->core.row_value_raw(channel, row, 1);
            const double value2 = self->core.row_value_raw(channel, row, 2);
            const double value3 = self->core.row_value_raw(channel, row, 3);
            const double values[] = {enabled, type, value0, value1, value2, value3};
            if (stream->write(stream, values, sizeof(values)) != sizeof(values))
                return false;
        }
    }

    return true;
}

bool state_load(const clap_plugin_t *plugin, const clap_istream_t *stream)
{
    Plugin *self = from_plugin(plugin);
    if (!self || !stream)
        return false;

    std::uint32_t version = 0;
    if (stream->read(stream, &version, sizeof(version)) != sizeof(version))
        return false;
    if (version != kStateVersion)
        return false;

    double outputChannel = 0.0;
    if (stream->read(stream, &outputChannel, sizeof(outputChannel)) != sizeof(outputChannel))
        return false;

    std::lock_guard<std::mutex> lock(self->stateMutex);
    self->core.reset();
    self->core.set_output_channel(outputChannel);

    for (std::size_t channel = 0; channel < kMidiChannelCount; ++channel) {
        self->core.set_output_channel(static_cast<double>(channel));
        for (std::size_t row = 0; row < kMaxCommandRows; ++row) {
            double values[6] = {};
            if (stream->read(stream, values, sizeof(values)) != sizeof(values))
                return false;
            self->core.set_row_enabled(row, values[0]);
            self->core.set_row_type(row, values[1]);
            self->core.set_row_value(row, 0, values[2]);
            self->core.set_row_value(row, 1, values[3]);
            self->core.set_row_value(row, 2, values[4]);
            self->core.set_row_value(row, 3, values[5]);
        }
    }
    self->core.set_output_channel(outputChannel);

    if (self->active)
        self->core.reset_runtime_state();

    return true;
}

const clap_plugin_state_t s_state = {
    .save = state_save,
    .load = state_load,
};

const void *plugin_get_extension(const clap_plugin_t *plugin, const char *id)
{
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
    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0)
        return &s_timerSupport;
    return nullptr;
}

void plugin_on_main_thread(const clap_plugin_t *plugin)
{
    Plugin *self = from_plugin(plugin);
    if (!self || !self->editor)
        return;
    if (self->guiTimerId == CLAP_INVALID_ID && !editor_was_closed(self->editor))
        editor_tick(self->editor);
}

std::uint32_t factory_get_plugin_count(const clap_plugin_factory_t *factory)
{
    (void)factory;
    return 1;
}

const clap_plugin_descriptor_t *factory_get_plugin_descriptor(const clap_plugin_factory_t *factory,
                                                              std::uint32_t index)
{
    (void)factory;
    return index == 0 ? &s_descriptor : nullptr;
}

const clap_plugin_t *factory_create_plugin(const clap_plugin_factory_t *factory, const clap_host_t *host,
                                           const char *pluginId)
{
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
    return &self->clapPlugin;
}

const clap_plugin_factory_t s_factory = {
    .get_plugin_count = factory_get_plugin_count,
    .get_plugin_descriptor = factory_get_plugin_descriptor,
    .create_plugin = factory_create_plugin,
};

bool entry_init(const char *pluginPath)
{
    (void)pluginPath;
    return true;
}

void entry_deinit(void)
{
}

const void *entry_get_factory(const char *factoryId)
{
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
