#include <benchmark/benchmark.h>

import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;
import std;

using namespace mo_yanxi::react_flow;

constexpr size_t data_size = 4;

// 辅助函数：预先生成随机数字字符串
std::vector<std::string> generate_random_strings(size_t count) {
    std::vector<std::string> data;
    data.reserve(count);

    std::mt19937 gen(45745723);
    std::uniform_int_distribution<> dist(0, 1000000); // 生成 0 到 1000000 之间的随机数

    for (size_t i = 0; i < count; ++i) {
        data.emplace_back(std::to_string(dist(gen)));
    }
    return data;
}

static void BM_Node(benchmark::State& state) {
    // 1. 预先生成随机数据
    // state.range(0) 由底部的 ->Range() 控制
    const std::vector<std::string> inputs = generate_random_strings(data_size);

	manager manager;

	auto& num_input = manager.add_node<provider_general<std::string>>();

    // 注意：原代码中未使用的 'node' 变量已移除，仅保留连接到链中的节点
	auto& stoi = manager.add_node(make_transformer<descriptor<std::string, {true}, std::string_view>>([](std::string_view input) -> std::optional<int> {
		int v;
		if(const auto [ptr, ec] = std::from_chars(input.data(), input.data() + input.size(), v); ec == std::errc{}){
			return v;
		}

		return std::nullopt;
	}));

	auto& mul2 = manager.add_node(make_transformer([](std::optional<int>&& input) -> std::optional<int> {
		return input.transform([](int val){return val * 2;});
	}));

	auto& listener = manager.add_node(make_listener([](std::optional<int> input){
		benchmark::DoNotOptimize(input);
	}));

	connect_chain({&num_input, &stoi, &mul2, &listener});

    std::size_t i = 0;
	for (auto _ : state) {
        // 2. 循环使用预生成的随机数据
		num_input.update_value(inputs[i++ % data_size]);
	}
}

static void BM_Raw(benchmark::State& state) {
    // 1. 预先生成随机数据
    const std::vector<std::string> inputs = generate_random_strings(data_size);

	auto trans = [](std::string input) -> std::optional<int> {
		int v;
		if(const auto [ptr, ec] = std::from_chars(input.data(), input.data() + input.size(), v); ec == std::errc{}){
			return v;
		}

		return std::nullopt;
	};

    std::size_t i = 0;
	for (auto _ : state) {
        // 2. 循环使用预生成的随机数据
		auto v = trans(inputs[i++ % data_size]);
		benchmark::DoNotOptimize(v);
	}
}

// 定义测试数据量范围：从 1024 到 1024*1024
BENCHMARK(BM_Node)->Range(1024, 64 * 1024);
BENCHMARK(BM_Raw)->Range(1024, 64 * 1024);

BENCHMARK_MAIN();