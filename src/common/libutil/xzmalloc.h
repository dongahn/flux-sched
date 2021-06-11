#ifndef _UTIL_XZMALLOC_H
#define _UTIL_XZMALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stdarg.h>

/* Memory allocation functions that call oom() on allocation error.
 */
void *xzmalloc (size_t size);
char *xstrdup (const char *s);

#ifdef __cplusplus
}
#endif

#endif /* !_UTIL_XZMALLOC_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

