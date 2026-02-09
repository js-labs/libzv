#if !defined(HISTOGRAM_H)
#define HISTOGRAM_H

#include <stddef.h>

void print_histogram_ex(size_t nvalues, const long * values, int nbuckets);

static inline void print_histogram(size_t nvalues, const long * values)
{
    print_histogram_ex(nvalues, values, 20);
}

#endif /* !defined(HISTOGRAM_H) */
