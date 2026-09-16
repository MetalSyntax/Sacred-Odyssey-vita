#ifndef EMBEDDED_SHADERS_H
#define EMBEDDED_SHADERS_H

#include <stddef.h>

const char *get_embedded_shader(const char *name, size_t *out_len);
void ensure_embedded_shaders_installed(void);

#endif // EMBEDDED_SHADERS_H
