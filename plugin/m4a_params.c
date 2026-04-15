#include "m4a_params.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m4a_engine.h"

enum {
    PARAM_PROGRAM_BASE = 0,
    PARAM_COUNT = MAX_TRACKS,
};

static uint8_t clamp_u8_param(double value, uint8_t minValue, uint8_t maxValue)
{
    int ivalue = (int)(value + (value >= 0.0 ? 0.5 : -0.5));
    if (ivalue < minValue) ivalue = minValue;
    if (ivalue > maxValue) ivalue = maxValue;
    return (uint8_t)ivalue;
}

static bool is_program_param(clap_id param_id, int *trackIndex)
{
    if (param_id < PARAM_PROGRAM_BASE || param_id >= PARAM_PROGRAM_BASE + MAX_TRACKS)
        return false;
    if (trackIndex)
        *trackIndex = (int)(param_id - PARAM_PROGRAM_BASE);
    return true;
}

void m4a_params_init(M4APluginData *data)
{
    for (int i = 0; i < MAX_TRACKS; ++i)
        atomic_init(&data->programParams[i], (uint8_t)i);
}

void m4a_params_set_program(M4APluginData *data, int trackIndex, uint8_t program)
{
    if (trackIndex < 0 || trackIndex >= MAX_TRACKS)
        return;

    atomic_store(&data->programParams[trackIndex], program);
}

static void apply_param_value(M4APluginData *data, clap_id param_id, double value)
{
    int trackIndex;
    if (!is_program_param(param_id, &trackIndex))
        return;

    /* Host params live as doubles, but the m4a engine stores the raw 0..127
     * program number per track. Mirror first, then push into the engine if
     * audio is active. */
    uint8_t program = clamp_u8_param(value, 0, 127);
    m4a_params_set_program(data, trackIndex, program);
    if (data->activated)
        m4a_engine_program_change(&data->engine, trackIndex, program);
}

void m4a_params_sync_to_engine(M4APluginData *data)
{
    if (!data->engine.voiceGroup)
        return;

    /* m4a_engine_init() zeroes every track, so whenever we rebuild or reload
     * engine state we need to replay the stored program selection back into
     * each track's currentProgram/currentVoice pair. */
    for (int trackIndex = 0; trackIndex < MAX_TRACKS; ++trackIndex) {
        uint8_t program = atomic_load(&data->programParams[trackIndex]);
        m4a_engine_program_change(&data->engine, trackIndex, program);
    }
}

void m4a_params_process_event(M4APluginData *data, const clap_event_param_value_t *ev)
{
    apply_param_value(data, ev->param_id, ev->value);
}

bool m4a_params_state_save(M4APluginData *data, const clap_ostream_t *stream)
{
    /* Persist the param mirror explicitly. currentProgram also exists inside
     * the engine, but that copy gets rebuilt on activate/reset. */
    for (int i = 0; i < MAX_TRACKS; ++i) {
        uint8_t program = atomic_load(&data->programParams[i]);
        if (stream->write(stream, &program, 1) != 1)
            return false;
    }

    return true;
}

void m4a_params_state_load(M4APluginData *data, const clap_istream_t *stream)
{
    /* Program bytes are optional so old states still load cleanly; missing
     * entries naturally fall back to program 0. */
    for (int i = 0; i < MAX_TRACKS; ++i) {
        uint8_t program = 0;
        stream->read(stream, &program, 1);
        m4a_params_set_program(data, i, program);
    }
}

static uint32_t params_count(const clap_plugin_t *plugin)
{
    (void)plugin;
    return PARAM_COUNT;
}

static bool params_get_info(const clap_plugin_t *plugin, uint32_t param_index,
                            clap_param_info_t *info)
{
    (void)plugin;
    if (!info || param_index >= PARAM_COUNT)
        return false;

    memset(info, 0, sizeof(*info));
    info->id = param_index;
    info->cookie = NULL;
    snprintf(info->name, sizeof(info->name), "Chn %u", (unsigned)(param_index + 1));
    snprintf(info->module, sizeof(info->module), "Programs");
    info->min_value = 0.0;
    info->max_value = 127.0;
    info->default_value = (double)param_index;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
    return true;
}

static bool params_get_value(const clap_plugin_t *plugin, clap_id param_id, double *value)
{
    M4APluginData *data = (M4APluginData *)plugin->plugin_data;
    if (!value)
        return false;

    int trackIndex;
    if (!is_program_param(param_id, &trackIndex))
        return false;
    *value = atomic_load(&data->programParams[trackIndex]);
    return true;
}

static bool params_value_to_text(const clap_plugin_t *plugin, clap_id param_id, double value,
                                 char *display, uint32_t size)
{
    (void)plugin;
    if (!display || size == 0)
        return false;

    if (!is_program_param(param_id, NULL))
        return false;
    snprintf(display, size, "%u", clamp_u8_param(value, 0, 127));
    return true;
}

static bool params_text_to_value(const clap_plugin_t *plugin, clap_id param_id,
                                 const char *display, double *value)
{
    (void)plugin;
    if (!display || !value)
        return false;

    char *end = NULL;
    double parsed = strtod(display, &end);
    if (end == display)
        return false;
    if (!is_program_param(param_id, NULL))
        return false;
    *value = clamp_u8_param(parsed, 0, 127);
    return true;
}

static void params_flush(const clap_plugin_t *plugin, const clap_input_events_t *in,
                         const clap_output_events_t *out)
{
    M4APluginData *data = (M4APluginData *)plugin->plugin_data;
    (void)out;

    /* We only consume host-driven param changes here. There is no GUI-side
     * param editing path yet, so we don't need to emit outbound param events. */
    if (in) {
        uint32_t eventCount = in->size(in);
        for (uint32_t i = 0; i < eventCount; ++i) {
            const clap_event_header_t *hdr = in->get(in, i);
            if (!hdr || hdr->space_id != CLAP_CORE_EVENT_SPACE_ID)
                continue;
            if (hdr->type == CLAP_EVENT_PARAM_VALUE)
                m4a_params_process_event(data, (const clap_event_param_value_t *)hdr);
        }
    }
}

static const clap_plugin_params_t s_params = {
    .count = params_count,
    .get_info = params_get_info,
    .get_value = params_get_value,
    .value_to_text = params_value_to_text,
    .text_to_value = params_text_to_value,
    .flush = params_flush,
};

const clap_plugin_params_t *m4a_params_extension(void)
{
    return &s_params;
}
