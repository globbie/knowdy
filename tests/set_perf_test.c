
#include <knd_set_idx.h>
#include <knd_utils.h>

#include <time.h>

const char * num_to_knd_set_idx_key(unsigned n)
{
    static char buf[100] = {0};

    char *result = buf;
    do
        *result++ = obj_id_seq[n % KND_RADIX_BASE];
    while ((n /= KND_RADIX_BASE) != 0);
    *result = '\0';

    return buf;
}

struct kndSetIdx *gen_set_idx(unsigned step, unsigned num_elements)
{
    int err;

    struct kndSetIdx *result;
    err = knd_set_idx_new(&result);
    if (err) exit(1);

    for (unsigned int n = 0; num_elements != 0; num_elements--, n += step) {
        const char *key = num_to_knd_set_idx_key(n);
        err = knd_set_idx_add(result, key, strlen(key));
        if (err) exit(1);
    }

    return result;
}

int main() {
    clock_t from;

    printf("initializing data sets...\n");
    from = clock();
    struct kndSetIdx *input[] = {
        gen_set_idx(3,  1000000),
        gen_set_idx(5,  500000),
        gen_set_idx(7,  5000000),
        gen_set_idx(9,  5000000),
//        gen_set_idx(11, 10000000)
    };
    printf("initializing data sets: ok, %fsec\n", ((double)(clock() - from)) / CLOCKS_PER_SEC);

    printf("performing intersection...\n");
    from = clock();
    struct kndSetIdx *intersection;
    knd_set_idx_new_result_of_intersect(&intersection, &input[0], sizeof input / sizeof input[0]);
    printf("performing intersection: ok, %fsec\n", ((double)(clock() - from)) / CLOCKS_PER_SEC);

    return 0;
}

