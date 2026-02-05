#include <gtest/gtest.h>
import mo_yanxi.react_flow;
import std;

using namespace mo_yanxi::react_flow;

TEST(CachingTest, ProviderCached) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();
    
    p.update_value(123);
    
    auto handle = p.request_raw(false);
    ASSERT_TRUE(handle.has_value());
    EXPECT_EQ(handle.value().fetch().value(), 123);
}

TEST(CachingTest, ModifierTransientVsCachedArguments) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();
    
    int upstream_compute_count = 0;
    // Upstream node that counts computations
    auto& upstream = mgr.add_node(make_transformer(propagate_behavior::lazy, [&](int v){
        upstream_compute_count++;
        return v;
    }));
    
    p.connect_successors(upstream);
    
    // Case 1: Transient downstream
    int transient_compute_count = 0;
    auto& downstream_transient = mgr.add_node(make_transformer(propagate_behavior::lazy, [&](int v){
        transient_compute_count++;
        return v;
    }));
    
    upstream.connect_successors(downstream_transient);
    
    p.update_value(1);
    
    // Request downstream
    downstream_transient.request(true);
    EXPECT_EQ(upstream_compute_count, 1);
    EXPECT_EQ(transient_compute_count, 1);
    
    // Request again - Transient node requests upstream again
    downstream_transient.request(true);
    EXPECT_EQ(upstream_compute_count, 2); // Increased!
    EXPECT_EQ(transient_compute_count, 2);
    
    
    // Case 2: Argument Cached downstream
    upstream.disconnect_successors(downstream_transient);
    upstream_compute_count = 0; // Reset
    
    int cached_compute_count = 0;
    
    struct my_cached_node : modifier_argument_cached<int, int> {
        using modifier_argument_cached::modifier_argument_cached;
        int* counter;
        int operator()(const int& arg) override {
            (*counter)++;
            return arg;
        }
    };
    
    auto& downstream_cached = mgr.add_node<my_cached_node>(propagate_behavior::lazy);
    downstream_cached.counter = &cached_compute_count;
    
    upstream.connect_successors(downstream_cached);
    
    // Need to trigger update to mark dirty? 
    // update_value was called before connection.
    p.update_value(2); 
    // Upstream gets notified. Upstream notifies downstream.
    
    // Request downstream
    downstream_cached.request(true);
    EXPECT_EQ(upstream_compute_count, 1);
    EXPECT_EQ(cached_compute_count, 1);
    
    // Request again - Cached node uses cached arguments, DOES NOT request upstream
    downstream_cached.request(true);
    EXPECT_EQ(upstream_compute_count, 1); // Stays same!
    EXPECT_EQ(cached_compute_count, 2); // Re-computes (since result is not cached, only args)
}

TEST(CachingTest, TerminalCached) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();
    
    int compute_count = 0;
    auto& trans = mgr.add_node(make_transformer(propagate_behavior::lazy, [&](int v){
        compute_count++;
        return v;
    }));
    
    auto& term = mgr.add_node<terminal_cached<int>>(propagate_behavior::lazy);
    
    p.connect_successors(trans);
    trans.connect_successors(term);
    
    p.update_value(10);
    
    EXPECT_EQ(compute_count, 0);
    
    // First request
    EXPECT_EQ(term.request_cache(), 10);
    EXPECT_EQ(compute_count, 1);
    
    // Second request - should return cached value without requesting upstream
    EXPECT_EQ(term.request_cache(), 10);
    EXPECT_EQ(compute_count, 1); // Stays same!
}
