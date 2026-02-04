#include <gtest/gtest.h>
import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;
import std;

using namespace mo_yanxi::react_flow;

TEST(AsyncNodeTest, BasicAsyncExecution) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();

    std::atomic<int> result = 0;
    std::atomic<bool> executed = false;

    // Create async transformer by using async_context
    auto& trans = mgr.add_node(make_transformer(propagate_behavior::eager, async_type::async_latest,
        [&](const async_context& ctx, int v) {
            executed = true;
            return v * 2;
        }
    ));

    auto& t = mgr.add_node(make_listener([&](int v){
        result = v;
    }));

    p.connect_successors(trans);
    trans.connect_successors(t);

    p.update_value(10);

    // Should happen asynchronously
    auto start = std::chrono::steady_clock::now();
    while(result != 20) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(std::chrono::steady_clock::now() - start > std::chrono::seconds(2)) break;
    }

    EXPECT_EQ(result, 20);
    EXPECT_TRUE(executed);
}

TEST(AsyncNodeTest, TriggerDisabled) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();
    std::atomic<int> count = 0;

    // Using internal type access to set trigger.
    // make_transformer returns transformer struct.
    // It inherits async_node_transient.
    auto& trans = mgr.add_node(make_transformer(propagate_behavior::eager, async_type::async_latest,
        [&](const async_context& ctx, int v) {
            return v;
        }
    ));

    // Set to disabled
    trans.set_trigger_type(trigger_type::disabled);

    auto& t = mgr.add_node(make_listener([&](int v){
        count++;
    }));

    p.connect_successors(trans);
    trans.connect_successors(t);

    p.update_value(1);

    // Give time to process
    for(int i=0; i<10; ++i) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(count, 0);
}

TEST(AsyncNodeTest, TriggerOnPulse) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();
    std::atomic<int> count = 0;

    auto& trans = mgr.add_node(make_transformer(propagate_behavior::eager, async_type::async_latest,
        [&](const async_context& ctx, int v) {
            return v;
        }
    ));

    trans.set_trigger_type(trigger_type::on_pulse);

    auto& t = mgr.add_node(make_listener([&](int v){
        count++;
    }));

    p.connect_successors(trans);
    trans.connect_successors(t);

    // First update
    p.update_value(1);

    // Wait for it
    auto start = std::chrono::steady_clock::now();
    while(count < 1) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(std::chrono::steady_clock::now() - start > std::chrono::seconds(1)) break;
    }
    EXPECT_EQ(count, 1);

    // Should have switched to disabled automatically
    EXPECT_EQ(trans.get_trigger_type(), trigger_type::disabled);

    // Second update should be ignored
    p.update_value(2);

    for(int i=0; i<10; ++i) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(count, 1);
}
