module;

#include <cassert>
#include <stop_token>

export module mo_yanxi.react_flow:async;

import :manager;
import :node_interface;
import :modifier;
import :endpoint;
import mo_yanxi.type_register;
import mo_yanxi.meta_programming;
import std;

namespace mo_yanxi::react_flow{

	export struct async_context{
		std::stop_token node_stop_token{};
		std::stop_token manager_stop_token{};
		progressed_async_node_base* task{};

		//TODO add manager stop token?

		bool stop_requested() const noexcept{
			return node_stop_token.stop_requested() || manager_stop_token.stop_requested();
		}
	};

	template <typename T, typename... Args>
	struct async_node_task;

	export
	template <typename Ret, typename... Args>
		requires (spec_of_descriptor<Ret> && (spec_of_descriptor<Args> && ...))
	struct async_node : modifier_base<async_node<Ret, Args...>, Ret, Args...>{
	private:
		using base = modifier_base<async_node, Ret, Args...>;
		friend base;

		using decay_argument_type = std::tuple<typename descriptor_trait<Args>::output_type...>;
		friend async_node_task<Ret, Args...>;

	public:
		void set_manager(manager& manager) override{
			manager_ = &manager;
		}

	private:
		manager* manager_{};

		//TODO successor_that_receives progress
		node_holder<provider_general<progress_check>> progress_provider_{};

		async_type async_type_{async_type::def};
		std::size_t dispatched_count_{};
		std::stop_source stop_source_{std::nostopstate};

	public:

		[[nodiscard]] async_node() = default;

		[[nodiscard]] explicit async_node(propagate_type data_propagate_type, async_type async_type)
			: base(data_propagate_type), async_type_(async_type){
		}

#pragma region Async_Settings

		[[nodiscard]] std::size_t get_dispatched() const noexcept{
			return dispatched_count_;
		}

		request_pass_handle<typename base::return_output_type> request_raw(bool allow_expired) override{
			if constexpr (descriptor_trait<Ret>::cached){
				const auto state = this->get_data_state();
				if(state == data_state::expired && !allow_expired){
					return make_request_handle_unexpected<typename base::return_output_type>(data_state::expired);
				}else{
					return react_flow::make_request_handle_expected_from_data_storage(this->get_cache(), state == data_state::expired);
				}
			}
			if(this->get_dispatched() > 0){
				return make_request_handle_unexpected<typename base::return_output_type>(data_state::awaiting);
			}
			return make_request_handle_unexpected<typename base::return_output_type>(data_state::failed);
		}

		[[nodiscard]] async_type get_async_type() const noexcept{
			return async_type_;
		}

		void set_async_type(const async_type async_type) noexcept{
			async_type_ = async_type;
		}

		bool add_progress_receiver(node& node){
			return progress_provider_->connect_successor(node);
		}

		bool erase_progress_receiver(node& node){
			return progress_provider_->disconnect_successor(node);
		}

		[[nodiscard]] std::stop_token get_stop_token() const noexcept{
			assert(stop_source_.stop_possible());
			return stop_source_.get_token();
		}

		bool async_cancel() noexcept{
			if(dispatched_count_ == 0) return false;
			if(!stop_source_.stop_possible()) return false;
			stop_source_.request_stop();
			stop_source_ = std::stop_source{std::nostopstate};
			return true;
		}

		[[nodiscard]] data_state get_data_state() const noexcept override{
			if constexpr(descriptor_trait<Ret>::cached){
				return this->get_cache_data_state();
			}else{
				return get_dispatched() ? data_state::awaiting : data_state::failed;
			}
		}
#pragma endregion

	protected:
		void apply_arguments(typename base::argument_pass_type& args){
			assert(manager_);
			if(stop_source_.stop_possible()){
				if(async_type_ == async_type::async_latest){
					if(async_cancel()){
						stop_source_ = {};
					}
				}
			} else if(!stop_source_.stop_requested()){
				stop_source_ = {};
			}

			++dispatched_count_;


			manager_->push_task(std::make_unique<async_node_task<Ret, Args...>>(*this, [&]<std::size_t ...Idx>(std::index_sequence<Idx...>){
				return decay_argument_type{std::get<Idx>(args).get() ...};
			}(std::index_sequence_for<Args...>{})));

		}

		base::return_pass_type apply(const async_context& ctx, decay_argument_type&& args){
			return [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>){
				return this->operator()(ctx, std::move(std::get<Idx>(args))...);
			}(std::index_sequence_for<Args...>());
		}

		virtual base::return_pass_type operator()(const async_context& ctx, typename descriptor_trait<Args>::output_type&&... args) = 0;

	};

	template <typename T, typename... Args>
	struct async_node_task final : progressed_async_node_base{
	private:
		using type = async_node<T, Args...>;
		node_pointer modifier_{};
		std::stop_token stop_token_{};

		type::decay_argument_type arguments_{};
		type::return_pass_type result_{};

		type& get() const noexcept{
			return static_cast<type&>(*modifier_);
		}

	public:
		[[nodiscard]] explicit async_node_task(type& modifier, type::decay_argument_type&& args) :
			progressed_async_node_base{!modifier.progress_provider_->get_outputs().empty()},
			modifier_(std::addressof(modifier)), stop_token_(modifier.get_stop_token()),
			arguments_{std::move(args)}{
		}

		void on_finish(manager& manager) override{
			--get().dispatched_count_;
			set_progress_done();

			get().store_result(std::move(result_));
		}

		node* get_owner_if_node() noexcept override{
			return modifier_.get();
		}

		void on_update_check(manager& manager) override{
			if(const auto prog = get_progress(); prog.changed){
				get().progress_provider_->update_value(prog);
			}
		}

	private:
		void execute(const manager& manager) override{
			result_ = get().apply(async_context{stop_token_, manager.get_manager_stop_token(), this}, std::move(arguments_));
		}
	};

	template <typename T>
	using optional_value_type_t = typename std::optional<T>::value_type;

	template <typename Fn, typename... Args>
	consteval auto test_invoke_result_async(){
		if constexpr(std::invocable<Fn, const async_context&, Args...>){
			return std::type_identity<optional_value_type_t<std::invoke_result_t<Fn, const async_context&, Args...>>>
				{};
		} else{
			// Should not happen for async transformer if strict check
			return std::type_identity<optional_value_type_t<std::invoke_result_t<Fn, Args...>>>{};
		}
	}

	export
	template <typename Ret, typename Fn, typename... Args>
	struct async_transformer_v2 final : async_node<Ret, Args...>{
	private:
		Fn fn;

	public:
		async_transformer_v2() = default;

		[[nodiscard]] explicit async_transformer_v2(propagate_type data_propagate_type, async_type async_type, Fn&& fn)
			: async_node<Ret, Args...>(data_propagate_type, async_type), fn(std::move(fn)){
		}

		[[nodiscard]] explicit async_transformer_v2(propagate_type data_propagate_type, async_type async_type, const Fn& fn)
			: async_node<Ret, Args...>(data_propagate_type, async_type), fn(fn){
		}

	protected:

		descriptor_trait<Ret>::input_pass_type operator()(
			const async_context& ctx,
			descriptor_trait<Args>::output_type&&... args) override{
			if constexpr(std::invocable<Fn, const async_context&, typename descriptor_trait<Args>::output_type&&...>){
				return std::invoke(fn, ctx, std::move(args)...);
			} else{
				return std::invoke(fn, std::move(args)...);
			}
		}
	};

	export
	template <typename... Args, typename Fn>
		requires (sizeof...(Args) > 0)
	[[nodiscard]] auto make_async_transformer(propagate_type data_propagate_type, async_type async_type, Fn&& fn){
		using return_type = decltype(test_invoke_result_async<std::decay_t<Fn>&, typename descriptor_trait<make_descriptor_t<Args>>::output_type&&...>())::type;
		return async_transformer_v2<descriptor<return_type>, std::decay_t<Fn>, make_descriptor_t<Args>...>{
			data_propagate_type, async_type, std::forward<Fn>(fn)
		};
	}

	export
	template <typename... Args, typename Fn>
		requires (sizeof...(Args) > 0)
	[[nodiscard]] auto make_async_transformer(propagate_type data_propagate_type, Fn&& fn){
		return react_flow::make_async_transformer<Args...>(data_propagate_type, async_type::async_latest, std::forward<Fn>(fn));
	}

	export
	template <typename... Args, typename Fn>
		requires (sizeof...(Args) > 0)
	[[nodiscard]] auto make_async_transformer(Fn&& fn){
		return react_flow::make_async_transformer<Args...>(propagate_type::eager, async_type::async_latest, std::forward<Fn>(fn));
	}

	export
	template <typename Fn>
	[[nodiscard]] auto make_async_transformer(propagate_type data_propagate_type, async_type async_type, Fn&& fn){
		using Args = function_traits<std::decay_t<Fn>>::mem_func_args_type;

		static_assert(std::tuple_size_v<Args> > 0);

		if constexpr (std::same_as<std::remove_cvref_t<std::tuple_element_t<0, Args>>, async_context>){
			return [&]<std::size_t ...Idx>(std::index_sequence<Idx...>){
				return react_flow::make_async_transformer<descriptor<std::decay_t<std::tuple_element_t<Idx + 1, Args>>> ...>(data_propagate_type, async_type, std::forward<Fn>(fn));
			}(std::make_index_sequence<std::tuple_size_v<Args> - 1>{});
		}else{
			return [&]<std::size_t ...Idx>(std::index_sequence<Idx...>){
				return react_flow::make_async_transformer<descriptor<std::decay_t<std::tuple_element_t<Idx, Args>>> ...>(data_propagate_type, async_type, std::forward<Fn>(fn));
			}(std::make_index_sequence<std::tuple_size_v<Args>>{});
		}
	}

	export
	template <typename Fn>
	[[nodiscard]] auto make_async_transformer(propagate_type data_propagate_type, Fn&& fn){
		return react_flow::make_async_transformer(data_propagate_type, async_type::async_latest, std::forward<Fn>(fn));
	}

	export
	template <typename Fn>
	[[nodiscard]] auto make_async_transformer(Fn&& fn){
		return react_flow::make_async_transformer(propagate_type::eager, async_type::async_latest, std::forward<Fn>(fn));
	}

}
