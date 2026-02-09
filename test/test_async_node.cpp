
#include <gtest/gtest.h>
import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;
import std;

using namespace mo_yanxi::react_flow;

TEST(AsyncNodeTest, AsyncCancelTest) {
    manager mgr;
    auto& source = mgr.add_node<provider_cached<int>>();

    std::atomic<bool> task_started = false;
    std::atomic<bool> task_finished = false;
    std::atomic<bool> task_cancelled = false;

    auto& processor = mgr.add_node(make_async_transformer(
        propagate_type::eager,
        async_type::def,
        [&](const async_context& ctx, int v) -> int {
            task_started = true;
            // Simulate long running task
            for (int i = 0; i < 20; ++i) {
                if (ctx.node_stop_token.stop_requested()) {
                    task_cancelled = true;
                    return -1;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            task_finished = true;
            return v * 2;
        }
    ));

    std::atomic<int> result = 0;
    auto& listener = mgr.add_node(make_listener([&](int v){
        result = v;
    }));

    source.connect_successor(processor);
    processor.connect_successor(listener);

    // Start task
    std::thread t([&]{
        mgr.push_posted_act([&]{
            source.update_value(10);
        });
    });
    t.join();

    // Wait until started
    while (!task_started) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Cancel
    processor.async_cancel();

    // Run updates for a while
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(1500)) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(task_started);
    EXPECT_TRUE(task_cancelled);
    EXPECT_FALSE(task_finished);
}

TEST(AsyncNodeTest, AsyncFloodLatestTest) {
    manager mgr;
    auto& source = mgr.add_node<provider_cached<int>>();

    std::atomic<int> completed_count = 0;
    std::atomic<int> last_value = 0;

    auto& processor = mgr.add_node(make_async_transformer(
        propagate_type::eager,
        async_type::async_latest,
        [&](const async_context& ctx, int v) -> int {
            // Simulate work
            for (int i = 0; i < 10; ++i) {
                 if (ctx.node_stop_token.stop_requested()) {
                    return -1; // Cancelled
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            completed_count++;
            return v;
        }
    ));

    auto& listener = mgr.add_node(make_listener([&](int v){
        if (v != -1) last_value = v;
    }));

    source.connect_successor(processor);
    processor.connect_successor(listener);

    // Flood updates
    std::thread t([&]{
        for (int i = 1; i <= 10; ++i) {
            mgr.push_posted_act([&, i]{
                source.update_value(i);
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Send faster than processing
        }
    });
    t.join();

    auto start = std::chrono::steady_clock::now();
    while (last_value != 10) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) break;
    }

    EXPECT_EQ(last_value, 10);
    EXPECT_LT(completed_count, 10); // Some should be cancelled/dropped
}

TEST(AsyncNodeTest, AsyncFloodAllTest) {
    manager mgr;
    auto& source = mgr.add_node<provider_cached<int>>();

    std::atomic<int> completed_count = 0;

    auto& processor = mgr.add_node(make_async_transformer(
        propagate_type::eager,
        async_type::async_all,
        [&](const async_context& ctx, int v) -> int {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            completed_count++;
            return v;
        }
    ));

    auto& listener = mgr.add_node(make_listener([&](int v){}));

    source.connect_successor(processor);
    processor.connect_successor(listener);

    int total_updates = 10;
    std::thread t([&]{
        for (int i = 1; i <= total_updates; ++i) {
            mgr.push_posted_act([&, i]{
                source.update_value(i);
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    t.join();

    auto start = std::chrono::steady_clock::now();
    while (completed_count < total_updates) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) break;
    }

    EXPECT_EQ(completed_count, total_updates);
}

TEST(AsyncNodeTest, AsyncDisconnectTest) {
    manager mgr;
    auto& source = mgr.add_node<provider_cached<int>>();

    std::atomic<bool> task_running = false;

    auto& processor = mgr.add_node(make_async_transformer(
        propagate_type::eager,
        async_type::def,
        [&](const async_context& ctx, int v) -> int {
            task_running = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return v;
        }
    ));

    auto& listener = mgr.add_node(make_listener([&](int v){}));

    source.connect_successor(processor);
    processor.connect_successor(listener);

    mgr.push_posted_act([&]{
        source.update_value(1);
    });

    // Wait for start
    while (!task_running) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Disconnect while running
    mgr.push_posted_act([&]{
        source.disconnect_successor(processor);
        processor.disconnect_successor(listener);
        // Optionally erase node
        mgr.erase_node(processor);
    });

    // Continue updating to see if crash
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(1)) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    SUCCEED();
}

TEST(AsyncNodeTest, AsyncProgressTest) {
    manager mgr;
    auto& source = mgr.add_node<provider_cached<int>>();

    auto& processor = mgr.add_node(make_async_transformer(
        propagate_type::eager,
        async_type::def,
        [&](const async_context& ctx, int v) -> int {
            for (int i = 1; i <= 10; ++i) {
                ctx.task->set_progress(i, 10);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            return v;
        }
    ));

    std::atomic<float> last_progress = 0.0f;
    std::atomic<bool> progress_received = false;

    auto& progress_listener = mgr.add_node(make_listener([&](progress_check p){
        if (p.changed) {
            last_progress = p.get_f32_progress();
            progress_received = true;
        }
    }));

    source.connect_successor(processor); // ADDED THIS
    processor.add_progress_receiver(progress_listener);

    mgr.push_posted_act([&]{
        source.update_value(1);
    });

    auto start = std::chrono::steady_clock::now();
    while (last_progress < 1.0f) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) break;
    }

    EXPECT_TRUE(progress_received);
    EXPECT_GE(last_progress, 1.0f);
}
