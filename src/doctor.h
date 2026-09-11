/* clawdget: doctor.h - connectivity diagnostics for the configured API */
#ifndef PC_DOCTOR_H
#define PC_DOCTOR_H

#include "config.h"

/* run connectivity diagnostics against cfg->api_base; human-readable
 * output on stderr. returns 0 if the API is reachable, 1 otherwise. */
int doctor_run(const config_t *cfg);

#endif
