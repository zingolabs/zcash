#include <stdint.h>

#include <boost/thread.hpp>
#include <cstdio>

#include "boost/signals2/signal.hpp"

static bool fHaveGenesis = false;
static boost::mutex cs_GenesisWait;
static boost::condition_variable condvar_GenesisWait;

static void TestFunction(bool f, int i) {
    printf("TestFunction called!\n");
    printf("%d\n", gettid());
    printf("The passed int was: %d\n", i);
}
static void BlockNotifyGenesisWait(
    bool,
    const int *pBlockIndex)  // int* pblockindex is actually a CBlockIndex*
{
    if (pBlockIndex != NULL) {
        {
            printf("TestFunction called!\n");
            printf("%d\n", gettid());
            printf("The passed pBlockIndex was: %p\n", pBlockIndex);
            printf("The passed pBlockIndex value was: %d\n", *pBlockIndex);
            boost::unique_lock<boost::mutex> lock_GenesisWait(cs_GenesisWait);
            fHaveGenesis = true;
        }
        condvar_GenesisWait.notify_all();
    }
}
boost::signals2::signal<void(bool, const int *)> NotifyBlockTip;
boost::signals2::signal<void(bool, int)> TestSignalParams;

void thread_task() {
    for (int i = 0; i < 5; ++i) BlockNotifyGenesisWait(false, &i);
}
int main() {
    printf("In the main function of main.cpp\n");
    printf("%d\n", gettid());
    // boost::thread_group threadGroup;
    // TestSignalParams.connect(TestFunction);
    // boost::thread* t = threadGroup.create_thread(&thread_task);
    // t->join();

    boost::thread_group threadGroup;
    NotifyBlockTip.connect(BlockNotifyGenesisWait);
    boost::thread *t = threadGroup.create_thread(&thread_task);
    t->join();
}
