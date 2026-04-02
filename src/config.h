#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

#include "types.h"

bool        init_config_dirs(AppState *st);
void        init_default_config(AppState *st);
void        load_config(AppState *st);
void        save_config(AppState *st);
const char *get_ytdlp_cmd(AppState *st);

#endif /* CONFIG_H */
