/* clawdget: spinner.h - "thinking..." progress indicator while waiting for LLM */
#ifndef PC_SPINNER_H
#define PC_SPINNER_H

/* start the spinner on stderr ("thinking... Ns").
 * no-op when stderr is not a tty. */
void pc_thinking_start(void);

/* stop the spinner, erasing its line. safe to call repeatedly. */
void pc_thinking_stop(void);

#endif
