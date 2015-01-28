#include <iostream>
#include <cstdint>
#include <chrono>
#include <vector>
#include <random>
#include <boost/coroutine/all.hpp>
using namespace boost;
using namespace std;

vector<boost::coroutines::symmetric_coroutine<void>::call_type> coro;

static int nthreads;

struct CoroInfo
{
    CoroInfo* next;
    boost::coroutines::symmetric_coroutine<void>::yield_type* yield;
    boost::coroutines::symmetric_coroutine<void>::call_type* coro;
    int index;
};

vector<CoroInfo> coroInfos;
CoroInfo* current;

void coroYield();

void coroInit(int nthr, void (*fn)(void *), void *data)
{
    cout << "coroInit " << nthr << endl;
    nthreads = nthr;
    coroInfos.resize(nthr);
    for (int i = 0; i < nthr; i++)
    {
        coro.push_back(
            boost::coroutines::symmetric_coroutine<void>::call_type(
                [fn, data, i](boost::coroutines::symmetric_coroutine<void>::yield_type& yield)
                {
                    coroInfos[i].yield = &yield;
                    cout << "save yield " << i << endl;
                    cout.flush();
                    cout << "before fn " << i << endl;
                    fn(data);
                    cout << "after fn " << i << endl;
                    coroYield();
                }));
        
    }
    
    for (int i = 0; i < nthr; i++)
    {
        coroInfos[i].coro = &coro[(i + 1) % nthr];
        coroInfos[i].next = &coroInfos[(i + 1) % nthr];
        coroInfos[i].index = i;
    }

    current = &coroInfos[nthr - 1];    
}

void coroStart()
{
    cout << "coroStart" << endl;
    coro[0]();
}

void coroYield()
{
    current = current->next;
    (*current->yield)(*current->coro);
}

extern "C"
{
void GOMP_parallel_start_patched(void (*fn)(void *), void *data, unsigned num_threads)
{
    cout << "GOMP_parallel_start_patched " << num_threads << endl;
    coroInit(num_threads, fn, data);
    coroStart();
}

void GOMP_parallel_end_patched()
{
    cout << "GOMP_parallel_end_patched" << endl;
}

int omp_get_thread_num_patched()
{
    cout << "omp_get_thread_num_patched" << endl;
    return current->next->index;
}
}