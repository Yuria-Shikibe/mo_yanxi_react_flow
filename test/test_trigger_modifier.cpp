#include <gtest/gtest.h>
import mo_yanxi.react_flow;
import std;

using namespace mo_yanxi::react_flow;

TEST(TriggerTypeTest, TransientDisabled) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();

    std::atomic<int> count = 0;
    // make_transformer without async_type creates a synchronous transformer (modifier_transient)
    auto& node = mgr.add_node(make_transformer(propagate_behavior::eager, [&](int v){
        count++;
        return v;
    }));

    node.set_trigger_type(trigger_type::disabled);

    p.connect_successor(node);

    p.update_value(1);

    // Synchronous execution, should be immediate if enabled
    EXPECT_EQ(count, 0);
}

TEST(TriggerTypeTest, TransientOnPulse) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();

    std::atomic<int> count = 0;
    auto& node = mgr.add_node(make_transformer(propagate_behavior::eager, [&](int v){
        count++;
        return v;
    }));

    node.set_trigger_type(trigger_type::on_pulse);

    p.connect_successor(node);

    p.update_value(1);

    EXPECT_EQ(count, 1);
    EXPECT_EQ(node.get_trigger_type(), trigger_type::disabled);

    p.update_value(2);
    EXPECT_EQ(count, 1);
}

TEST(TriggerTypeTest, TransientUpdateTrigger) {
    manager mgr;
    auto& p_data = mgr.add_node<provider_cached<int>>();
    auto& p_trigger = mgr.add_node<provider_cached<trigger_type>>();

    std::atomic<int> count = 0;
    auto& node = mgr.add_node(make_transformer(propagate_behavior::eager, [&](int v, trigger_type t){
        count++;
        return v;
    }));

    node.set_trigger_type(trigger_type::disabled);

    p_data.connect_successor(node);
    p_trigger.connect_successor(node);

    // Initial values
    p_trigger.update_value(trigger_type::disabled);
    p_data.update_value(1);

    EXPECT_EQ(count, 0);

    // Update trigger to active
    p_trigger.update_value(trigger_type::active);

    EXPECT_EQ(node.get_trigger_type(), trigger_type::active);

    // It might execute because p_trigger update triggers on_push.
    EXPECT_EQ(count, 1);

    p_data.update_value(2);
    EXPECT_EQ(count, 2);
}

struct my_cached_modifier : modifier_argument_cached<int, int>{
    using modifier_argument_cached::modifier_argument_cached;
    std::atomic<int>* count_ptr;

    int operator()(const int& arg) override{
        (*count_ptr)++;
        return arg;
    }
};

TEST(TriggerTypeTest, CachedDisabled) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();

    std::atomic<int> count = 0;
    auto& node = mgr.add_node<my_cached_modifier>(propagate_behavior::eager);
    node.count_ptr = &count;

    node.set_trigger_type(trigger_type::disabled);

    p.connect_successor(node);

    p.update_value(1);

    EXPECT_EQ(count, 0);
}

TEST(TriggerTypeTest, CachedOnPulse) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();

    std::atomic<int> count = 0;
    auto& node = mgr.add_node<my_cached_modifier>(propagate_behavior::eager);
    node.count_ptr = &count;

    node.set_trigger_type(trigger_type::on_pulse);

    p.connect_successor(node);

    p.update_value(1);

    EXPECT_EQ(count, 1);
    EXPECT_EQ(node.get_trigger_type(), trigger_type::disabled);

    p.update_value(2);
    EXPECT_EQ(count, 1);
}

struct my_cached_modifier_trigger : modifier_argument_cached<int, int, trigger_type>{
    using modifier_argument_cached::modifier_argument_cached;
    std::atomic<int>* count_ptr;

    int operator()(const int& arg, const trigger_type& t) override{
        (*count_ptr)++;
        return arg;
    }
};

TEST(TriggerTypeTest, CachedUpdateTrigger) {
    manager mgr;
    auto& p_data = mgr.add_node<provider_cached<int>>();
    auto& p_trigger = mgr.add_node<provider_cached<trigger_type>>();

    std::atomic<int> count = 0;
    auto& node = mgr.add_node<my_cached_modifier_trigger>(propagate_behavior::eager);
    node.count_ptr = &count;

    node.set_trigger_type(trigger_type::disabled);

    p_data.connect_successor(node);
    p_trigger.connect_successor(node);

    // Initial values
    p_trigger.update_value(trigger_type::disabled);
    p_data.update_value(1);

    EXPECT_EQ(count, 0);

    // Update trigger to active
    p_trigger.update_value(trigger_type::active);

    EXPECT_EQ(node.get_trigger_type(), trigger_type::active);

    // It might execute because p_trigger update triggers on_push.
    EXPECT_EQ(count, 1);

    p_data.update_value(2);
    EXPECT_EQ(count, 2);
}
