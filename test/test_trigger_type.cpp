#include <gtest/gtest.h>
import mo_yanxi.react_flow;
import std;

using namespace mo_yanxi::react_flow;

TEST(TriggerTypeTest, Disabled) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();

    std::atomic<int> count = 0;
    auto& node = mgr.add_node(make_async_transformer(propagate_type::eager, async_type::async_latest, [&](const async_context&, int v){
        count++;
        return v;
    }));

    // Access async_node interface
    auto& async_n = dynamic_cast<async_node<int, int>&>(node);
    async_n.set_trigger_type(trigger_type::disabled);

    p.connect_successor(node);

    p.update_value(1);

    // Give some time
    for(int i=0; i<10; ++i) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Should be ignored
    EXPECT_EQ(count, 0);
}

TEST(TriggerTypeTest, OnPulse) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();

    std::atomic<int> count = 0;
    auto& node = mgr.add_node(make_async_transformer(propagate_type::eager, async_type::async_latest, [&](const async_context&, int v){
        count++;
        return v;
    }));

    node.set_trigger_type(trigger_type::on_pulse);

    p.connect_successor(node);

    p.update_value(1);

    // Should trigger
    auto start = std::chrono::steady_clock::now();
    while(count == 0 && std::chrono::steady_clock::now() - start < std::chrono::seconds(1)) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(count, 1);
    EXPECT_EQ(node.get_trigger_type(), trigger_type::disabled);

    // Update again
    p.update_value(2);

    // Should ignore
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    mgr.update();

    EXPECT_EQ(count, 1);
}
