#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stddef.h>

#include "types.h"

// Process a single key press.
// Sets *quit to true when the user confirmed they want to exit.
void handle_input(AppState *st, int ch, char *status, size_t status_sz, bool *quit);

#endif /* INPUT_H */
