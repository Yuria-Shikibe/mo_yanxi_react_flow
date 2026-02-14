//
// Created by Matrix on 2026/2/9.
//

export module modifier_example;

import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;
import mo_yanxi.utility;
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

export
void move_only_test(){
	using namespace mo_yanxi::react_flow;

	manager manager;
	auto& num_input = manager.add_node<provider_general<int>>();

	auto& node = manager.add_node(make_transformer([](int input){
		return std::make_unique<int>(input);
	}));

	auto& listener1 = manager.add_node(make_listener([](data_pass_t<std::unique_ptr<int>> input){
		auto i = input.extract();
		if(i){
			std::println("1 Move: {}", *i);
		}else{
			std::println("1 Move: {}", nullptr);
		}
	}));

	auto& listener2 = manager.add_node(make_listener([](data_pass_t<std::unique_ptr<int>> input){
		auto i = input.extract();
		if(i){
			std::println("2 Move: {}", *i);
		}else{
			std::println("2 Move: {}", nullptr);
		}
	}));

	connect_chain({&num_input, &node, &listener1});
	connect_chain({&node, &listener2});
	num_input.update_value(114514);
}



export
void view_test(){
	using namespace mo_yanxi::react_flow;

	struct not_trivial{
		int value;

		not_trivial() = default;

		not_trivial(int value)
			: value(value){
		}

		not_trivial(const not_trivial& other)
			: value(other.value){
		}

		not_trivial& operator=(const not_trivial& other){
			if(this == &other) return *this;
			value = other.value;
			return *this;
		}
	};

	manager manager;
	auto& num_input = manager.add_node<provider_general<int>>();

	auto& node = manager.add_node(make_transformer<int>(std::in_place_type<descriptor<not_trivial, {true}, const not_trivial*>>,[](int input){
		return not_trivial{input};
	}));

	auto& listener1 = manager.add_node(make_listener([](data_pass_t<const not_trivial*> input){
		std::println("Input: {:p}->{}", (void*)input, input->value);
	}));

	auto& listener2 = manager.add_node(make_listener([](data_pass_t<const not_trivial*> input){
		std::println("Input: {:p}->{}", (void*)input, input->value);
	}));

	connect_chain({&num_input, &node, &listener1});
	connect_chain({&node, &listener2});
	num_input.update_value(114514);
}


