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


#include <benchmark/benchmark.h>
#include <vector>
#include <cmath>
#include <cstdint>

import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;

// ============================================================================
// 1. 核心复杂计算函数 (模拟重负载业务逻辑)
// ============================================================================

// 模拟重负载的归一化/非线性变换
std::vector<double> heavy_normalize(const std::vector<double>& input) {
    std::vector<double> res(input.size());
    for (std::uint32_t i = 0; i < input.size(); ++i) {
        res[i] = std::sin(input[i]) * 100.0 + std::cos(input[i] * 0.5);
    }
    return res;
}

// 模拟指数移动平均 (EMA) 时序依赖计算
std::vector<double> heavy_ema(const std::vector<double>& input) {
    std::vector<double> res(input.size());
    if (input.empty()) return res;

    double alpha = 0.2;
    res[0] = input[0];
    for (std::uint32_t i = 1; i < input.size(); ++i) {
        res[i] = alpha * input[i] + (1.0 - alpha) * res[i - 1];
    }
    return res;
}

// 模拟非线性激活/平滑变换 (如近似 MACD)
std::vector<double> heavy_macd(const std::vector<double>& input) {
    std::vector<double> res(input.size());
    for (std::uint32_t i = 0; i < input.size(); ++i) {
        res[i] = std::tanh(input[i] * 0.01) * 50.0;
    }
    return res;
}

// 最终的数据聚合评分
double aggregate_score(const std::vector<double>& input) {
    double score = 0.0;
    for (std::uint32_t i = 0; i < input.size(); ++i) {
        score += input[i];
    }
    return score;
}

// ============================================================================
// 2. 原生直接调用 Benchmark
// ============================================================================

static void BM_Direct_Pipeline(benchmark::State& state) {
    std::uint32_t data_size = static_cast<std::uint32_t>(state.range(0));
    std::vector<double> initial_data(data_size, 1.5);

    for (auto _ : state) {
        // 直接嵌套调用，编译器可能会进行极致的内存与循环优化
        std::vector<double> norm_data = heavy_normalize(initial_data);
        std::vector<double> ema_data = heavy_ema(norm_data);
        std::vector<double> macd_data = heavy_macd(ema_data);
        double final_result = aggregate_score(macd_data);

        benchmark::DoNotOptimize(final_result);
        benchmark::ClobberMemory();
    }
}
// 测试从 1000 到 100000 规模的数据点
BENCHMARK(BM_Direct_Pipeline)->Range(1000, 100000);

// ============================================================================
// 3. React Flow 节点调用 Benchmark
// ============================================================================

static void BM_ReactFlow_Pipeline(benchmark::State& state) {
    using namespace mo_yanxi::react_flow;

    std::uint32_t data_size = static_cast<std::uint32_t>(state.range(0));
    std::vector<double> initial_data(data_size, 1.5);

    // 初始化流图管理器
    manager mgr;

    // 创建数据提供节点
    auto& provider = mgr.add_node<provider_cached<std::vector<double>>>();

    // 创建中间转换节点，使用 eager 模式实现数据到达后立即推送
    auto& node_norm = mgr.add_node(make_transformer(heavy_normalize));
    auto& node_ema  = mgr.add_node(make_transformer(heavy_ema));
    auto& node_macd = mgr.add_node(make_transformer(heavy_macd));

    // 创建终端监听节点，提取结果供 Benchmark 统计
    double final_result = 0.0;
    auto& node_score = mgr.add_node(make_listener([&](double s) {
        final_result = s;
    }));

    // 构建数据流图：Provider -> Norm -> EMA -> MACD -> Score
    provider.connect_successor(node_norm);
    node_norm.connect_successor(node_ema);
    node_ema.connect_successor(node_macd);

    // Aggregate Score 是把 vector 变 double，但我们这里为了连贯，专门再加一个聚合 Transformer
    auto& node_agg = mgr.add_node(make_transformer(aggregate_score));
    node_macd.connect_successor(node_agg);
    node_agg.connect_successor(node_score);

    for (auto _ : state) {
        // 触发数据更新，eager 模式下会同步阻塞直到整个图遍历完成
        provider.update_value(initial_data);

        // 确保 manager 清理所有挂起的状态或异步任务（如果有）
        mgr.update();

        benchmark::DoNotOptimize(final_result);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_ReactFlow_Pipeline)->Range(1000, 100000);

// 定义测试数据量范围：从 1024 到 1024*1024
// BENCHMARK(BM_Node)->Range(1024, 64 * 1024);
// BENCHMARK(BM_Raw)->Range(1024, 64 * 1024);

BENCHMARK_MAIN();