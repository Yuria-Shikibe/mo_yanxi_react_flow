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


int main(){
	modifier_test();

	// simple_io_async_example();
}