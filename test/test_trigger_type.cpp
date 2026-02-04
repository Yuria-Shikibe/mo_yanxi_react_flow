#include <gtest/gtest.h>
import mo_yanxi.react_flow;
import std;

using namespace mo_yanxi::react_flow;

TEST(TriggerTypeTest, Disabled) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();

    std::atomic<int> count = 0;
    auto& node = mgr.add_node(make_transformer(propagate_behavior::eager, async_type::async_latest, [&](const async_context&, int v){
        count++;
        return v;
    }));

    // Access async_node interface
    auto& async_n = dynamic_cast<async_node_base<int, int>&>(node);
    async_n.set_trigger_type(trigger_type::disabled);

    p.connect_successors(node);

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
    auto& node = mgr.add_node(make_transformer(propagate_behavior::eager, async_type::async_latest, [&](const async_context&, int v){
        count++;
        return v;
    }));

    auto& async_n = dynamic_cast<async_node_base<int, int>&>(node);
    async_n.set_trigger_type(trigger_type::on_pulse);

    p.connect_successors(node);

    p.update_value(1);

    // Should trigger
    auto start = std::chrono::steady_clock::now();
    while(count == 0 && std::chrono::steady_clock::now() - start < std::chrono::seconds(1)) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(count, 1);
    EXPECT_EQ(async_n.get_trigger_type(), trigger_type::disabled);

    // Update again
    p.update_value(2);

    // Should ignore
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    mgr.update();

    EXPECT_EQ(count, 1);
}

TEST(TriggerTypeTest, OnPulseDelayed) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();

    std::atomic<int> count = 0;
    // Async node with Pulse behavior
    auto& node = mgr.add_node(make_transformer(propagate_behavior::pulse, async_type::async_latest, [&](const async_context&, int v){
        count++;
        return v;
    }));

    auto& async_n = dynamic_cast<async_node_base<int, int>&>(node);
    async_n.set_trigger_type(trigger_type::on_pulse);

    p.connect_successors(node);

    p.update_value(1);

    // Should be waiting pulse
    // Wait a bit to ensure it didn't trigger
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_EQ(count, 0);
    EXPECT_EQ(async_n.get_trigger_type(), trigger_type::on_pulse);

    mgr.update(); // Manager update triggers pulse
    // Does it trigger pulse?
    // Usually there is a mechanism to trigger pulse for nodes.
    // I need to check `manager` or `node` logic.
    // But `async_node` inherits `on_pulse_received(manager)`.
    // Who calls `on_pulse_received`?
    // The `manager` probably iterates nodes?
    // Or the provider triggers it?
    // If I look at `provider_cached` implementation of `on_pulse_received`:
    // It pushes to successors.
    // But `async_node` is downstream.
    // If `async_node` is waiting pulse, who notifies it?
    // The manager calls `on_pulse_received` on nodes?
    // Let's assume `mgr.update()` does it or there is a way.
    // In `test_propagation.cpp`:
    // `mgr.update()` triggers pulse for provider.
    // Here `async_node` is the one with pulse behavior.
    // Does manager trigger pulse for ALL nodes?
    // If so, `mgr.update()` should work.

    // Let's try triggering manager update logic assuming it handles pulses.
    // (If not, I might need to explicitly trigger it, but manager usually handles it).

    // To trigger pulse on specific node?
    // `manager.update()` seems to be the way.

    // Let's loop wait.
    auto start = std::chrono::steady_clock::now();
    while(count == 0 && std::chrono::steady_clock::now() - start < std::chrono::seconds(1)) {
        mgr.update(); // This should eventually trigger pulse
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(count, 1);
    EXPECT_EQ(async_n.get_trigger_type(), trigger_type::disabled);
}
