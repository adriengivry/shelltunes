#ifndef SEARCH_H
#define SEARCH_H

#include "types.h"

void free_search_results(AppState *st);

// Returns count of results, 0 for no results, -1 on error.
int run_search(AppState *st, const char *raw_query);

#endif /* SEARCH_H */
