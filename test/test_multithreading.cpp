#include <gtest/gtest.h>
import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;
import std;

using namespace mo_yanxi::react_flow;

TEST(MultithreadingTest, AsyncUpdate) {
    manager mgr;
    auto p = mgr.add_node<provider_cached<int>>();

    std::atomic<int> received_value = 0;
    auto t = mgr.add_node(make_listener([&](int v){
        received_value.store(v);
    }));

    p->connect_successors(*t);

    std::jthread worker([&]{
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        mgr.push_posted_act([&]{
            p->update_value(100);
        });
    });

    // Main thread loop to process updates
    auto start = std::chrono::steady_clock::now();
    while(received_value.load() != 100) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(2)) {
            break;
        }
    }

    EXPECT_EQ(received_value.load(), 100);
}

TEST(MultithreadingTest, MultipleThreads) {
    manager mgr;
    auto p = mgr.add_node<provider_cached<int>>();

    std::atomic<int> sum = 0;
    auto t = mgr.add_node(make_listener([&](int v){
        sum.fetch_add(v);
    }));

    p->connect_successors(*t);

    const int num_threads = 10;
    std::vector<std::jthread> threads;
    threads.reserve(num_threads);
    for(int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]{
            mgr.push_posted_act([&]{
                p->update_value(1);
            });
        });
    }

    auto start = std::chrono::steady_clock::now();
    while(sum.load() < num_threads) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(2)) {
            break;
        }
    }

    EXPECT_EQ(sum.load(), num_threads);
}
