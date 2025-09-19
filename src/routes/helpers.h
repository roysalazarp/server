#ifndef HELPERS_H
#define HELPERS_H

#include "routes.h"

int is_authenticated(char *err, int *user_id, RequestCtx *rctx);

#endif /* HELPERS_H */