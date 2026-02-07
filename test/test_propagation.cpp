#include <gtest/gtest.h>
import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;
import std;

using namespace mo_yanxi::react_flow;

TEST(PropagationTest, EagerPropagation) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();
    
    int received_value = 0;
    // listener defaults to eager
    auto& t = mgr.add_node(make_listener([&](int v){
        received_value = v;
    }));

    p.connect_successor(t);
    
    p.update_value(42);
    EXPECT_EQ(received_value, 42);
    
    p.update_value(100);
    EXPECT_EQ(received_value, 100);
}

TEST(PropagationTest, LazyPropagation) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();
    
    int computation_count = 0;
    // transformer with lazy behavior
    auto& trans = mgr.add_node(make_transformer(propagate_behavior::lazy, [&](int v){
        computation_count++;
        return v * 2;
    }));

    // terminal_cached with lazy behavior
    auto& term = mgr.add_node<terminal_cached<int>>(propagate_behavior::lazy);
    
    p.connect_successor(trans);
    trans.connect_successor(term);
    
    p.update_value(10);
    
    // Nothing should happen yet
    EXPECT_EQ(computation_count, 0);
    EXPECT_EQ(term.get_data_state(), data_state::expired);
    
    // Requesting data triggers the chain
    EXPECT_EQ(term.request_cache(), 20);
    EXPECT_EQ(computation_count, 1);
    EXPECT_EQ(term.get_data_state(), data_state::fresh);

    // Update again
    p.update_value(20);
    EXPECT_EQ(computation_count, 1); // Still 1
    EXPECT_EQ(term.get_data_state(), data_state::expired);

    EXPECT_EQ(term.request_cache(), 40);
    EXPECT_EQ(computation_count, 2);
}

TEST(PropagationTest, PulsePropagation) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>(propagate_behavior::pulse);
    
    int received_value = 0;

    auto& t = mgr.add_node(make_listener([&](int v){
        received_value = v;
    }));

    p.connect_successor(t);
    
    p.update_value(42);
    // Not updated yet
    EXPECT_EQ(received_value, 0);
    
    // Trigger pulse
    mgr.update();
    EXPECT_EQ(received_value, 42);

    p.update_value(55);
    EXPECT_EQ(received_value, 42); // Still old value
    
    mgr.update();
    EXPECT_EQ(received_value, 55);
}
