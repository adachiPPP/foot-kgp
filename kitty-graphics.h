#pragma once

#include <stddef.h>
#include "terminal.h"

void kitty_graphics(struct terminal *term, const char *data, size_t len);
