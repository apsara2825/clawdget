/* clawdget: spinner.h - "thinking..." progress indicator while waiting for LLM */
#ifndef PC_SPINNER_H
#define PC_SPINNER_H

/* start the spinner on stderr ("thinking... Ns").
 * no-op when stderr is not a tty or when disabled. */
void pc_thinking_start(void);

/* force-disable/enable the spinner (gateway mode disables it) */
void pc_thinking_set_enabled(int enabled);

/* stop the spinner, erasing its line. safe to call repeatedly. */
void pc_thinking_stop(void);

#endif
