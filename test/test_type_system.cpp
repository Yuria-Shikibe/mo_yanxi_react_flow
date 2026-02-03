#include <gtest/gtest.h>
import mo_yanxi.react_flow.common;
import mo_yanxi.react_flow;
import std;

using namespace mo_yanxi::react_flow;

TEST(TypeSystemTest, CompatibleTypes) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();
    auto& t = mgr.add_node(make_listener([&](int v){}));
    
    EXPECT_NO_THROW(p.connect_successors(t));
}

TEST(TypeSystemTest, IncompatibleTypes) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();
    auto& t = mgr.add_node(make_listener([&](float v){}));
    
    // Should throw invalid_node because type IDs are different
    EXPECT_THROW(p.connect_successors(t), invalid_node);
}

TEST(TypeSystemTest, ImplicitConversionFails) {
    // The system uses strict type identity, so int -> long should fail
    manager mgr;
    auto& p = mgr.add_node<provider_cached<int>>();
    auto& t = mgr.add_node(make_listener([](long){}));
    
    EXPECT_THROW(p.connect_successors(t), invalid_node);
}
