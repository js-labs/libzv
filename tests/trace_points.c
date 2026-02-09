#include "trace_points.h"
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

static int td_cmp(const void * arg1, const void * arg2)
{
    const uint64_t * td1 = (const uint64_t*) arg1;
    const uint64_t * td2 = (const uint64_t*) arg2;
    if (*td1 < *td2)
        return -1;
    else if (*td2 < *td1)
        return 1;
    else
        return 0;
}


void trace_points_sum_latency(const struct trace_point_s * trace_points, size_t count)
{
    size_t count_m1 = (count - 1);
    uint64_t * td = malloc(sizeof(uint64_t) * count_m1);
    const struct trace_point_s * tp = trace_points;

    uint64_t tv0 = (tp->tv.tv_sec * 1000000) + tp->tv.tv_usec;
    tp++;
    for (size_t idx=0; idx<count_m1; idx++, tp++)
    {
        const uint64_t tv1 = (tp->tv.tv_sec * 1000000) + tp->tv.tv_usec;
        td[idx] = (tv1 - tv0);
        tv0 = tv1;
    }

    qsort(td, count_m1, sizeof(td[0]), td_cmp);

    printf("50%% <= %" PRIu64 " usec\n", td[count_m1/2]);
    printf("80%% <= %" PRIu64 " usec\n", td[count_m1*8/10]);
    printf("90%% <= %" PRIu64 " usec\n", td[count_m1*9/10]);

    free(td);
}


void trace_points_sum_threads(const struct trace_point_s * trace_points, size_t count)
{
    struct list_item_s
    {
        struct list_item_s * next;
        pid_t thread_id;
        size_t count;
    };

    struct list_item_s * head = NULL;
    const struct trace_point_s * tp = trace_points;

    for (size_t idx=0; idx<count; idx++, tp++)
    {
        struct list_item_s * item = head;
        for (; item!=NULL; item=item->next)
        {
            if (item->thread_id == tp->thread_id)
            {
                item->count++;
                break;
            }
        }

        if (item == NULL)
        {
            item = malloc(sizeof(struct list_item_s));
            item->next = head;
            item->thread_id = tp->thread_id;
            item->count = 1;
            head = item;
        }
    }

    for (struct list_item_s * item=head; item!=NULL;)
    {
        printf("Thread %d : %zd times\n", item->thread_id, item->count);
        struct list_item_s * next = item->next;
        free(item);
        item = next;
    }
}
