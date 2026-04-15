#ifndef M4A_PARAMS_H
#define M4A_PARAMS_H

#include <stdint.h>
#include <stdbool.h>
#include <clap/ext/params.h>
#include "m4a_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

void m4a_params_init(M4APluginData *data);
void m4a_params_set_program(M4APluginData *data, int trackIndex, uint8_t program);
void m4a_params_sync_to_engine(M4APluginData *data);
void m4a_params_process_event(M4APluginData *data, const clap_event_param_value_t *ev);
bool m4a_params_state_save(M4APluginData *data, const clap_ostream_t *stream);
void m4a_params_state_load(M4APluginData *data, const clap_istream_t *stream);
const clap_plugin_params_t *m4a_params_extension(void);

#ifdef __cplusplus
}
#endif

#endif /* M4A_PARAMS_H */
