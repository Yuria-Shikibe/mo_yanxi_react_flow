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
    EXPECT_EQ(handle.value().get(), 123);
}

TEST(CachingTest, TerminalCached) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();
    
    int compute_count = 0;
    auto& trans = mgr.add_node(make_transformer(propagate_type::lazy, [&](int v){
        compute_count++;
        return v;
    }));
    
    auto& term = mgr.add_node<terminal_cached<int>>(propagate_type::lazy);
    
    p.connect_successor(trans);
    trans.connect_successor(term);
    
    p.update_value(10);
    
    EXPECT_EQ(compute_count, 0);
    
    // First request
    EXPECT_EQ(term.request_cache(), 10);
    EXPECT_EQ(compute_count, 1);
    
    // Second request - should return cached value without requesting upstream
    EXPECT_EQ(term.request_cache(), 10);
    EXPECT_EQ(compute_count, 1); // Stays same!
}
