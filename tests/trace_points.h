#if !defined(UIO_TESTS_TRACE_POINTS_H)
#define UIO_TESTS_TRACE_POINTS_H

#include "zv/os_thread.h"
#include "zv/os_time.h"

struct trace_point_s
{
    struct timeval tv;
    tid_t thread_id;
};

void trace_points_sum_latency(const struct trace_point_s * trace_points, size_t count);
void trace_points_sum_threads(const struct trace_point_s * trace_points, size_t count);

#endif /* UIO_TESTS_TRACE_POINTS_H */
