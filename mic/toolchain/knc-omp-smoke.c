/* OpenMP link probe: needs a target libgomp/libiomp5 to link. Never executed by the probes. */
#include <omp.h>

int main(void)
{
    int n = 0;
#pragma omp parallel reduction(+:n)
    n += 1;
    return n == omp_get_max_threads() ? 0 : 1;
}
