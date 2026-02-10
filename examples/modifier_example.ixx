//
// Created by Matrix on 2026/2/9.
//

export module modifier_example;

import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;
import std;

export
void modifier_test(){
	using namespace mo_yanxi::react_flow;

	manager manager;

	auto& num_input = manager.add_node<provider_general<std::string>>();

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
		if(input){
			std::println(std::cout, "!Push: input * 2 = {}", *input);
		}else{
			std::println(std::cout, "!Push: nullopt");
		}
		std::cout.flush();
	}));

	connect_chain({&num_input, &stoi, &mul2, &listener});

	auto fetch = [&]{
		if(auto v = listener.request(false).value()){
			std::println("!Fetch: {}", *v);
		}else{
			std::println("!Fetch: nullopt");
		}

	};


	num_input.update_value("123");
	num_input.update_value("651207");

	fetch();

	num_input.update_value("abc");
	num_input.update_value("1111111111111111111111111");

	fetch();

	stoi.set_propagate_type(propagate_type::lazy);
	num_input.update_value("555");

	fetch();

/*
Expected:
!Push: input * 2 = 246
!Push: input * 2 = 1302414
!Fetch: 1302414
!Push: nullopt
!Push: nullopt
!Fetch: nullopt
!Fetch: 1110
*/
}
