import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;
import modifier_example;

import std;

void simple_io_async_example(){
	using namespace mo_yanxi::react_flow;

	bool done_flag{};

	manager manager;

	auto& num_input = manager.add_node<provider_cached<std::string>>();
	auto& path_input = manager.add_node<provider_cached<std::string>>();

	auto& stoi_trans = manager.add_node<string_to_arth<int>>();
	auto& int_trans = manager.add_node(make_transformer([](stoa_result<int> val){
		return val.value_or(0) * 2;
	}));

	auto node = make_async_transformer([](const async_context& ctx, std::string&& filepath) -> std::string {
		std::ifstream file(filepath);

			if(!std::filesystem::exists(filepath)){
				return {};
			}

			auto total = std::filesystem::file_size(filepath);

			if (!file.is_open()) {
				return {};
			}


			std::string result;
			result.reserve(total);

			std::string line;
			while (std::getline(file, line)) {
				if(ctx.stop_requested()){
					return {};
				}
				result += line;
				result += "\n";
				ctx.task->set_progress(result.size(), total);
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}

			ctx.task->set_progress_done();
			return result;
	});

	auto& path_reader = manager.add_node(std::move(node));

	auto& formatter = manager.add_node(make_transformer<descriptor<std::string, {true}, std::string_view>, descriptor<int, {.quiet = true}>>([](std::string_view str, int val){
		return std::format("{} -- {}", str, val);
	}));

	auto& final_printer = manager.add_node(make_listener([&](const std::string& str){
		std::println("Result: {}", str);
		done_flag = true;
	}));

	auto& progress_listener = manager.add_node(make_listener([](const progress_check& prog){
		if(prog.changed){
			std::println(std::cout, "Reading Progress: {:.1f}%", prog.get_f32_progress() * 100);
			std::cout.flush();
		}
	}));

	connect_chain({&num_input, &stoi_trans, &int_trans, &formatter});
	connect_chain({&path_input, &path_reader, &formatter});
	connect_chain({&formatter, &final_printer});
	path_reader.add_progress_receiver(progress_listener);

	/*
	 * num_input -> stoi_trans -> int_trans ->|
	 *                                        |
	 * path_input -> path_reader -----------> formatter -> final_printer
	 *               |
	 *               (builtin progress provider) -> progress_listener
	 */


	std::println(std::cout, "Input a number and a file path");
	std::cout.flush();

	std::string input;
	std::cin >> input;
	num_input.update_value(std::move(input));

	std::cin >> input;
	path_input.update_value(std::move(input));

	while(!done_flag){
		manager.update();
	}
}

void benchmark_diamond_dag() {
    using namespace mo_yanxi::react_flow;
    constexpr std::uint32_t iterations = 1'000'000;

    // --- 1. React Flow 设置 ---
    manager mgr{manager_no_async}; // 禁用异步以单纯测试计算开销

    auto& p_a = mgr.add_node<provider_cached<double>>();
    auto& p_b = mgr.add_node<provider_cached<double>>();
    auto& p_c = mgr.add_node<provider_cached<double>>();

    auto& t_x = mgr.add_node(make_transformer([](double a, double b) {
        return a + b;
    }));
    auto& t_y = mgr.add_node(make_transformer([](double b, double c) {
        return b * c;
    }));
    auto& t_z = mgr.add_node(make_transformer([](double x, double y) {
        return std::sqrt(x * x + y * y);
    }));

    // 监听最终结果
    volatile double dummy_result = 0.0;
    auto& listener = mgr.add_node(make_listener([&](double z) {
        dummy_result = z;
    }));

    // 连接节点 (注意：transformer 的参数顺序对应 slot 的索引)
    p_a.connect_successor(0, t_x);
    p_b.connect_successor(1, t_x);

    p_b.connect_successor(0, t_y);
    p_c.connect_successor(1, t_y);

    t_x.connect_successor(0, t_z);
    t_y.connect_successor(1, t_z);

    t_z.connect_successor(0, listener);

    // 初始化基础数据
    p_a.update_value(10.0);
    p_c.update_value(5.0);

    // --- 2. Benchmark: Node Flow ---
    auto start_node = std::chrono::high_resolution_clock::now();
    for (std::uint32_t i = 0; i < iterations; ++i) {
        p_b.update_value(static_cast<double>(i)); // 触发整条链路的 update
    }
    auto end_node = std::chrono::high_resolution_clock::now();

    // --- 3. Benchmark: 直接硬编码 ---
    auto start_direct = std::chrono::high_resolution_clock::now();
    double a = 10.0;
    double c = 5.0;
    for (std::uint32_t i = 0; i < iterations; ++i) {
        double b = static_cast<double>(i);
        double x = a + b;
        double y = b * c;
        dummy_result = std::sqrt(x * x + y * y);
    }
    auto end_direct = std::chrono::high_resolution_clock::now();

    // --- 输出结果 ---
    auto duration_node = std::chrono::duration_cast<std::chrono::milliseconds>(end_node - start_node).count();
    auto duration_direct = std::chrono::duration_cast<std::chrono::milliseconds>(end_direct - start_direct).count();

    std::println("=== Diamond DAG Benchmark ({} iterations) ===", iterations);
    std::println("React Flow Duration: {} ms", duration_node);
    std::println("Direct Code Duration: {} ms", duration_direct);
    std::println("Overhead Ratio: {:.2f}x", static_cast<double>(duration_node) / static_cast<double>(std::max<long long>(duration_direct, 1)));
}

void benchmark_lazy_pipeline() {
    using namespace mo_yanxi::react_flow;
    constexpr std::uint32_t iterations = 100'000;
    constexpr std::uint32_t reads_per_iteration = 10; // 模拟高频写入、低频读取

    manager mgr{manager_no_async};

    // --- 1. React Flow 设置 (Lazy 模式) ---
    auto& source = mgr.add_node<provider_cached<std::uint32_t>>(propagate_type::lazy);

    // 构造一个多级变换管道
    auto& stage1 = mgr.add_node(make_transformer<std::uint32_t>(propagate_type::lazy, [](std::uint32_t v) {
        return v + 100;
    }));
    auto& stage2 = mgr.add_node(make_transformer<std::uint32_t>(propagate_type::lazy, [](std::uint32_t v) {
        return v * 3;
    }));
    auto& stage3 = mgr.add_node(make_transformer<std::uint32_t>(propagate_type::lazy, [](std::uint32_t v) {
        return v ^ 0xAAAA; // 简单的位运算模拟逻辑
    }));

    // 使用 terminal_cached 作为终点来拉取数据
    auto& sink = mgr.add_node<terminal_cached<std::uint32_t>>(propagate_type::lazy);

    connect_chain({&source, &stage1, &stage2, &stage3, &sink});

    // --- 2. Benchmark: Node Flow ---
    volatile std::uint32_t dummy_result = 0;
    auto start_node = std::chrono::high_resolution_clock::now();
    for (std::uint32_t i = 0; i < iterations; ++i) {
        source.update_value(i); // 仅仅是将后续节点标记为 expired

        // 模拟低频读取：每更新一定次数才真正发起 fetch
        if (i % reads_per_iteration == 0) {
            auto rst = sink.request_raw(false);
            if (rst) {
                dummy_result = rst.value().get();
            }
        }
    }
    auto end_node = std::chrono::high_resolution_clock::now();

    // --- 3. Benchmark: 直接硬编码 ---
    auto start_direct = std::chrono::high_resolution_clock::now();
    for (std::uint32_t i = 0; i < iterations; ++i) {
        // 在硬编码中，我们可以通过标志位或直接控制流来模拟 Lazy
        if (i % reads_per_iteration == 0) {
            std::uint32_t v1 = i + 100;
            std::uint32_t v2 = v1 * 3;
            dummy_result = v2 ^ 0xAAAA;
        }
    }
    auto end_direct = std::chrono::high_resolution_clock::now();

    // --- 输出结果 ---
    auto duration_node = std::chrono::duration_cast<std::chrono::microseconds>(end_node - start_node).count();
    auto duration_direct = std::chrono::duration_cast<std::chrono::microseconds>(end_direct - start_direct).count();

    std::println("=== Lazy Pipeline Benchmark ({} iterations, read every {}) ===", iterations, reads_per_iteration);
    std::println("React Flow Duration: {} us", duration_node);
    std::println("Direct Code Duration: {} us", duration_direct);
    std::println("Overhead Ratio: {:.2f}x", static_cast<double>(duration_node) / static_cast<double>(std::max<long long>(duration_direct, 1)));
}

int main(){
	// move_only_test();
	// view_test();
	benchmark_diamond_dag();
	// benchmark_lazy_pipeline();
}