/*
 * Copyright (C) 2022 Sergey Zubarev, info@js-labs.org
 *
 * This file is a part of ZV framework.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "histogram.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int long_cmp(const void * pv1, const void * pv2)
{
    long v1 = *((const long*) pv1);
    long v2 = *((const long*) pv2);
    if (v1 < v2)
        return -1;
    if (v2 < v1)
        return 1;
    return 0;
}


void print_histogram_ex(size_t nvalues, const long * values, int nbuckets)
{
    size_t buffer_size = sizeof(long) * (nvalues + nbuckets);
    long * buffer = malloc(buffer_size);
    if (buffer == NULL)
        return;

    memcpy(buffer, values, sizeof(long) * nvalues);

    long * buckets = (buffer + nvalues);
    for (int idx=0; idx<nbuckets; idx++)
        buckets[idx] = 0;

    qsort(buffer, nvalues, sizeof(long), long_cmp);
    const int min_value = buffer[0];

    int max_value = buffer[nvalues - 1];
    int max_value_2 = buffer[nvalues - 2];
    int vv = (max_value_2 - buffer[0]) / 5;
    if ((max_value - max_value_2) > vv)
    {
        nvalues--;
        max_value = max_value_2;
    }

    const long bucket_width = ((max_value - min_value) / nbuckets);
    for (int idx=0; idx<nvalues; idx++)
    {
        const long value = buffer[idx];
        int bckt = ((value - min_value) / bucket_width);
        if (bckt >= nbuckets)
            bckt = (nbuckets - 1);
        buckets[bckt]++;
    }

    long max_bucket_value = buckets[0];
    for (int idx=1; idx<nbuckets; idx++)
    {
        const long bucket_value = buckets[idx];
        if (bucket_value > max_bucket_value)
            max_bucket_value = bucket_value;
    }

    printf("%u\n", min_value);
    printf("+");
    for (int idx=0; idx<50; idx++)
        printf("-");
    printf("\n");

    const double bar_size = ((double) max_bucket_value) / 50;

    for (int idx=0; idx<nbuckets; idx++)
    {
        printf("|");
        const int cnt = (int) ((buckets[idx] + (bar_size/2)) / bar_size);
        for (int jdx=0; jdx<cnt; jdx++)
            printf("@");
        printf("\n");
    }

    printf("+");
    for (int idx=0; idx<50; idx++)
        printf("-");
    printf("\n");
    printf("%u\n", max_value);

    /*
    printf("bar_size=%g\n", bar_size);
    for (int idx=0; idx<nbuckets; idx++)
        printf("%u ", buckets[idx]);
    printf("\n");
    */

    free(buffer);
}
