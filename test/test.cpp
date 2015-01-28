#include <iostream>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <random>
#include <omp.h>
using namespace std;

#ifndef N_PARALLEL
#define N_PARALLEL 4
#endif

static inline uint64_t rdtsc(void)
{
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)lo) | (((uint64_t)hi) << 32);
}

std::vector<int> gen_vector(int n)
{
	vector<int> v(n);
	for (int i = 1; i < n; i++)
		v[i] = i;

	std::random_device rd;
	std::shuffle(v.begin() + 1, v.end(), mt19937(rd()));

	vector<int> vv(n);
	for (int i = 1; i < n; i++)
		vv[v[i]] = i;

	v[0] = vv[1];
	v[vv[n - 1]] = 0;
	for (int i = 1; i < n - 1; i++)
		v[vv[i]] = vv[i + 1];

	return v;
}

int main()
{
    const int n = 1 << 24;
	const int n_parallel = N_PARALLEL;

	std::vector<std::vector<int>> a(n_parallel);
	for (int i = 0; i < n_parallel; i++)
		a[i] = gen_vector(n);

    int sum_local[n_parallel] = { 0 };
    auto cycles = rdtsc();

    #pragma omp parallel num_threads(n_parallel)
    {
        int ithr = omp_get_thread_num();

        int ix = 0;
        for (int i = 0; i < n; i++)
            ix = a[ithr][ix];
        sum_local[ithr] += ix;
    }
    
    int sum = 0;
    for (int i = 0; i < n_parallel; i++)
        sum += sum_local[i];

    cycles = rdtsc() - cycles;
    cout << "cycles per access: " << cycles / n_parallel / n << endl;
    if (sum != 0)
    {
        cout << "sum != 0" << endl;
        return 1;
    }
    return 0;
}