module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.react_flow:modifier;

import :manager;
import :node_interface;
import :successory_list;

import mo_yanxi.react_flow.util;
import mo_yanxi.meta_programming;
import mo_yanxi.concepts;
import std;

namespace mo_yanxi::react_flow{
	template <typename T>
	struct param_load_result{
		T param;
		data_state state;
		bool success;
	};


	template <typename Impl, typename Ret, typename... Args>
		requires (spec_of_descriptor<Ret> && (spec_of_descriptor<Args> && ...))
	struct modifier_base : type_aware_node<typename descriptor_trait<Ret>::output_type>{
	protected:
		friend node;
		static constexpr std::size_t argument_count = sizeof...(Args);
		static_assert(argument_count > 0);

	private:
		static constexpr std::array<data_type_index, argument_count> in_type_indices{
				unstable_type_identity_of<typename descriptor_trait<Args>::input_type>()...
			};

	protected:
		using return_descriptor = Ret;
		using return_output_type = typename descriptor_trait<Ret>::output_type;
		using return_pass_type = typename descriptor_trait<Ret>::input_pass_type;

		using input_descriptors = std::tuple<Args...>;
		using input_converted_args_type = std::tuple<typename descriptor_trait<Args>::output_type...>;
		using argument_pass_type = std::tuple<typename descriptor_trait<Args>::output_pass_type...>;

#ifdef _MSC_VER //workaround
		static constexpr std::size_t _true_trigger_index = mo_yanxi::tuple_index_v<trigger_type,
			input_converted_args_type>;
		static constexpr bool has_trigger = _true_trigger_index != std::tuple_size_v<input_converted_args_type>;
		static constexpr std::size_t trigger_index = has_trigger ? _true_trigger_index : 0;
#else
		static constexpr std::size_t trigger_index = mo_yanxi::tuple_index_v<trigger_type, input_converted_args_type>;
		static constexpr bool has_trigger = trigger_index != std::tuple_size_v<input_converted_args_type>;
#endif

		static constexpr std::array<bool, argument_count> quiet_map{descriptor_trait<Args>::no_push...};

	private:
		std::array<raw_node_ptr, argument_count> parents_{};
		successor_list successors_{};

	protected:
		ADAPTED_NO_UNIQUE_ADDRESS input_descriptors arguments_{};
		ADAPTED_NO_UNIQUE_ADDRESS return_descriptor ret_descriptor_{};
		ADAPTED_NO_UNIQUE_ADDRESS expire_flags<descriptor_trait<Args>::cached...> expired_flags_{};
		ADAPTED_NO_UNIQUE_ADDRESS optional_val<data_state, descriptor_trait<Ret>::cached> data_state_{};

	public:
		using type_aware_node<return_output_type>::type_aware_node;

#pragma region Connection_Region

	public:
		modifier_base(const modifier_base& other) = delete;
		modifier_base(modifier_base&& other) noexcept = default;
		modifier_base& operator=(const modifier_base& other) = delete;
		modifier_base& operator=(modifier_base&& other) noexcept = default;

		[[nodiscard]] bool is_isolated() const noexcept final{
			return std::ranges::none_of(parents_, std::identity{}) && successors_.empty();
		}

		[[nodiscard]] std::span<const data_type_index> get_in_socket_type_index() const noexcept final{
			return std::span{in_type_indices};
		}

		void disconnect_self_from_context() noexcept final{
			for(std::size_t i = 0; i < parents_.size(); ++i){
				if(raw_node_ptr& ptr = parents_[i]){
					ptr->erase_successors_single_edge(i, *this);
					ptr = nullptr;
				}
			}
			for(const auto& successor : successors_){
				successor.entity->erase_predecessor_single_edge(successor.index, *this);
			}
			successors_.clear();
		}

		[[nodiscard]] std::span<const raw_node_ptr> get_inputs() const noexcept final{
			return parents_;
		}

		[[nodiscard]] std::span<const successor_entry> get_outputs() const noexcept final{
			return successors_;
		}

	private:
		bool connect_successors_impl(std::size_t slot, node& post) final{
			if(auto& ptr = post.get_inputs()[slot]){
				post.erase_predecessor_single_edge(slot, *ptr);
			}
			return try_insert(successors_, slot, post);
		}

		bool erase_successors_single_edge(std::size_t slot, node& post) noexcept final{
			return try_erase(successors_, slot, post);
		}

		void connect_predecessor_impl(std::size_t slot, node& prev) final{
			if(parents_[slot]){
				const auto rng = parents_[slot]->get_outputs();
				const auto idx = std::ranges::distance(rng.begin(),
					std::ranges::find(rng, this, &successor_entry::get));
				parents_[slot]->erase_successors_single_edge(idx, *this);
			}
			parents_[slot] = std::addressof(prev);
		}

		void erase_predecessor_single_edge(std::size_t slot, node& prev) noexcept final{
			if(parents_[slot] == &prev){
				parents_[slot] = {};
			}
		}
#pragma endregion

	public:

		[[nodiscard]] data_state get_data_state() const noexcept override{
			if constexpr(descriptor_trait<Ret>::cached){
				return *data_state_;
			} else{
				data_state states{};

				for(const raw_node_ptr& p : parents_){
					update_state_enum(states, p->get_data_state());
				}

				return states;
			}
		}

		[[nodiscard]] push_dispatch_fptr get_push_dispatch_fptr() const noexcept override{
			return static_cast<const Impl*>(this)->get_this_class_push_dispatch_fptr();
		}
	protected:

		auto get_cache() requires(descriptor_trait<Ret>::cached){
			return ret_descriptor_.get();
		}

		bool check_trigger_type(){
			if constexpr(!has_trigger){
				return true;
			} else{
				if constexpr(descriptor_trait<std::tuple_element_t<trigger_index, input_descriptors>>::cached){
					switch(trigger_type& trigger = *std::get<trigger_index>(arguments_)){
					case trigger_type::active : return true;
					case trigger_type::disabled : return false;
					case trigger_type::once : trigger = trigger_type::disabled;
						return true;
					default : std::unreachable();
					}
				} else{
					if(const raw_node_ptr p = parents_[trigger_index]){
						return node_type_cast<trigger_type>(*p).request_raw(true).value_or(trigger_type::active).get()
							!= trigger_type::disabled;
					}
					return true;
				}
			}
		}

		void store_result(return_pass_type&& rst){
			data_carrier<return_output_type> v = ret_descriptor_ << std::move(rst);
			react_flow::push_to_successors(successors_, std::move(v));
		}

		void on_pulse_received(manager& m) override{
			if(this->data_pending_state_ != data_pending_state::waiting_pulse) return;
			this->data_pending_state_ = data_pending_state::done;

			this->update(trigger_type::active, nullptr);
		}

		void on_push(std::size_t target_index, data_carrier_obj&& in_data) override{
			auto update_cache = [&]{
				[&, this]<std::size_t ... Idx>(std::index_sequence<Idx...>){
					([&, this]<std::size_t I>(){
						using Ty = std::tuple_element_t<I, input_descriptors>;

						if constexpr(!descriptor_trait<Ty>::cached) return false;
						else if(I == target_index){
							using InputTy = typename descriptor_trait<Ty>::input_type;
							std::get<I>(arguments_).set(data_carrier_cast<InputTy>(in_data));
							expired_flags_.template set<I>(false);
							return true;
						}
						return false;
					}.template operator()<Idx>() || ...);
				}(std::index_sequence_for<Args...>{});
			};

			if(quiet_map[target_index]){
				if(has_cache_at(target_index)){
					update_cache();
				}
				mark_updated(-1);
				return;
			}

			trigger_type trigger{};
			if constexpr(has_trigger && requires{
				requires trigger_index < std::tuple_size_v<input_descriptors>; /*workaround for msvc*/
			}){
				if(target_index == trigger_index){
					auto rst = data_carrier_cast<trigger_type>(in_data).get();
					using DescriptorTy = std::tuple_element_t<trigger_index, input_descriptors>;
					if constexpr(descriptor_trait<DescriptorTy>::cached && descriptor_trait<DescriptorTy>::identity){
						std::get<trigger_index>(arguments_).set(
							rst == trigger_type::once ? trigger_type::disabled : rst);
						expired_flags_.template set<trigger_index>();
					}
					if(rst == trigger_type::disabled) return;
					trigger = rst;
				} else{
					if(!check_trigger_type()){
						return;
					}
				}
			}else{
				trigger = trigger_type::active;
			}

			switch(this->get_propagate_type()){
			case propagate_type::eager : this->update(trigger, [&]<std::size_t I>(argument_pass_type& arguments){
					if(I == target_index){
						using Ty = std::tuple_element_t<I, input_descriptors>;
						using InputTy = typename descriptor_trait<Ty>::input_type;
						std::get<I>(arguments) = std::get<I>(arguments_) << std::move(data_carrier_cast<InputTy>(in_data));
						if constexpr(descriptor_trait<Ty>::cached) expired_flags_.template set<I>(false);
						return true;
					}
					return false;
				});
				break;
			case propagate_type::lazy : update_cache();
				mark_updated(-1);
				break;
			case propagate_type::pulse : update_cache();
				this->data_pending_state_ = data_pending_state::waiting_pulse;
				break;
			default : std::unreachable();
			}
		}

		FORCE_INLINE bool has_cache_at(std::size_t index) const noexcept{
			return expired_flags_.has_bit(index);
		}

		void mark_updated(std::size_t from_index) noexcept override{
			if(expired_flags_.try_set(from_index)){
				data_state_ = data_state::expired;
				node::mark_updated(from_index);
			}
		}

		template <bool fast_fail = false>
		FORCE_INLINE param_load_result<argument_pass_type> load_arguments(trigger_type trigger, bool allow_expired, auto checker){
			argument_pass_type arguments{};

			if constexpr(has_trigger){
				std::get<trigger_index>(arguments) = trigger;
			}

			data_state state = data_state::fresh;

			bool success = [&, this]<std::size_t ... Idx>(std::index_sequence<Idx...>){
				return (([&, this]<std::size_t I> FORCE_INLINE (){
					using D = std::tuple_element_t<I, input_descriptors>;
					using InputTy = typename descriptor_trait<D>::input_type;
					if constexpr(has_trigger && I == trigger_index){
						return true;
					}

					if constexpr(!std::is_null_pointer_v<decltype(checker)>){
						if(checker.template operator()<I>(arguments)){
							return true;
						}
					}

					if constexpr(descriptor_trait<D>::cached){
						if(expired_flags_.get(I) && (!descriptor_trait<D>::allow_expired || !allow_expired)){
							if(!parents_[I]) return false;
							node& n = *parents_[I];

							if(auto rst = node_type_cast<InputTy>(n).request_raw(false)){
								std::get<I>(arguments) = std::get<I>(arguments_) << std::move(rst).value();
								expired_flags_.template set<I>(false);
								return true;
							}

							update_state_enum(state, data_state::expired);
							if(!allow_expired)return false;
						}

						//fallback, use cache even it is expired
						// TODO check if expire is allow by tag?
						std::get<I>(arguments) = std::get<I>(arguments_).get();
						return true;
					}

					if(!parents_[I]) return false;
					node& n = *parents_[I];

					auto rst = node_type_cast<InputTy>(n).request_raw(descriptor_trait<D>::allow_expired && allow_expired);
					react_flow::update_state_enum(state, rst.state());

					if(rst){
						std::get<I>(arguments) = std::get<I>(arguments_) << std::move(rst).value();
						return true;
					}

					return false;
				}.template operator()<Idx>() || !fast_fail) && ...);
			}(std::index_sequence_for<Args...>{});

			data_state_ = state;

			return {arguments, state, success};
		}

	private:
		FORCE_INLINE void update(trigger_type trigger, auto checker){
			auto [arguments, state, success] = this->load_arguments(trigger, true, std::move(checker));

			if(successors_.empty() || !success) return;

			static_cast<Impl*>(this)->apply_arguments(arguments);
		}

	};

	export
	template <typename Ret, typename... Args>
		requires (spec_of_descriptor<Ret> && (spec_of_descriptor<Args> && ...))
	struct modifier : modifier_base<modifier<Ret, Args...>, Ret, Args...>{
	private:
		using base = modifier_base<modifier, Ret, Args...>;
		friend base;

	public:

		[[nodiscard]] modifier() = default;

		[[nodiscard]] explicit modifier(propagate_type data_propagate_type)
			: base(data_propagate_type){
		}

		[[nodiscard]] request_pass_handle<typename base::return_output_type> request_raw(bool allow_expired) override{
			if constexpr(descriptor_trait<Ret>::cached){
				auto state = this->get_data_state();
				if(state == data_state::fresh || (state == data_state::expired && allow_expired)){
					return react_flow::make_request_handle_expected_from_data_storage(this->get_cache(), state == data_state::expired);
				}
			}

			auto [arguments, state, success] = this->template load_arguments<true>(trigger_type::active, allow_expired, nullptr);

			if(success){
				return react_flow::make_request_handle_expected_from_data_storage(this->ret_descriptor_ << this->apply(arguments), state == data_state::expired);
			} else{
				return make_request_handle_unexpected<typename base::return_output_type>(data_state::failed);
			}
		}

	protected:
		void apply_arguments(typename base::argument_pass_type& args){
			this->store_result(this->apply(args));
		}

		base::return_pass_type apply(base::argument_pass_type& arguments){
			return [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>){
				return this->operator()(react_flow::pass_data(std::get<Idx>(arguments))...);
			}(std::index_sequence_for<Args...>());
		}

		virtual base::return_pass_type operator()(typename descriptor_trait<Args>::operator_pass_type... args) = 0;

	};

	export
	template <typename Ret, typename Fn, typename... Args>
		requires (std::is_invocable_r_v<typename descriptor_trait<Ret>::input_pass_type, Fn, typename descriptor_trait<
			Args>::operator_pass_type...> && spec_of_descriptor<Ret> && (spec_of_descriptor<Args> && ...))
	struct transformer final : modifier_base<transformer<Ret, Fn, Args...>, Ret, Args...>{
	private:
		using base = modifier_base<transformer, Ret, Args...>;
		friend base;

		ADAPTED_NO_UNIQUE_ADDRESS Fn fn;

	public:
		[[nodiscard]] transformer() = default;

		[[nodiscard]] explicit transformer(propagate_type data_propagate_type)
			: base(data_propagate_type){
		}

		[[nodiscard]] transformer(propagate_type data_propagate_type, Fn&& fn)
			: base(data_propagate_type),
			fn(std::move(fn)){
		}

		[[nodiscard]] transformer(propagate_type data_propagate_type, const Fn& fn)
			: base(data_propagate_type),
			fn(fn){
		}

		[[nodiscard]] explicit transformer(const Fn& fn)
			: fn(fn){
		}

		[[nodiscard]] explicit transformer(Fn&& fn)
			: fn(std::move(fn)){
		}

		[[nodiscard]] request_pass_handle<typename base::return_output_type> request_raw(bool allow_expired) override{
			if constexpr(descriptor_trait<Ret>::cached){
				auto state = this->get_data_state();
				if(state == data_state::fresh || (state == data_state::expired && allow_expired)){
					return react_flow::make_request_handle_expected_from_data_storage(this->get_cache(),
						state == data_state::expired);
				}
			}

			auto [arguments, state, success] = this->template load_arguments<true>(trigger_type::active, allow_expired,
				nullptr);

			if(success){
				return react_flow::make_request_handle_expected_from_data_storage(
					this->ret_descriptor_ << this->apply(arguments), state == data_state::expired);
			} else{
				return make_request_handle_unexpected<typename base::return_output_type>(data_state::failed);
			}
		}

	private:
		void apply_arguments(typename base::argument_pass_type& args){
			this->store_result(this->apply(args));
		}

		typename base::return_pass_type apply(base::argument_pass_type& arguments){
			return [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>){
				return std::invoke(fn, react_flow::pass_data(std::get<Idx>(arguments))...);
			}(std::index_sequence_for<Args...>());
		}
	};

	template <typename T>
	using make_descriptor_t = std::conditional_t<spec_of_descriptor<T>, T, descriptor<T>>;

	template <typename T>
	struct extract_type : std::type_identity<T>{
	};

	template <typename T>
	struct extract_type<data_carrier<T>> : std::type_identity<T>{
	};

	template <typename T>
	using extract_value_t = extract_type<T>::type;


	template <typename Fn>
	FORCE_INLINE auto adapt_fn_(Fn&& fn){
		using CurrentTy = function_traits<std::decay_t<Fn>>::mem_func_args_type;
		using Expected = unary_apply_to_tuple_t<data_pass_t, unary_apply_to_tuple_t<extract_value_t,
			unary_apply_to_tuple_t<std::decay_t, CurrentTy>>>;
		if constexpr(std::same_as<CurrentTy, Expected>){
			return std::forward<Fn>(fn);
		} else{
			constexpr static auto adaptor = []<typename Dst, typename Src> FORCE_INLINE (Src& input) static -> Dst{
				if constexpr(std::is_lvalue_reference_v<Dst> && !std::is_const_v<std::remove_reference_t<Dst>>){
					static_assert(false, "non const lvalue reference is not allowed");
				}

				if constexpr(std::convertible_to<Src&, Dst>){
					return input;
				} else if constexpr(spec_of<Src, data_carrier>){
					if constexpr(std::is_reference_v<Dst> && std::is_const_v<std::remove_reference_t<Dst>>){
						//is const reference, return const view
						return input.get_ref_view();
					} else{
						//crop it to value or rvalue ref
						return input.get();
					}
				} else{
					static_assert(std::same_as<std::decay_t<Dst>, Src>, "type mismatch ant not convertible");

					if constexpr(std::is_rvalue_reference_v<Dst>){
						return std::move(input);
					} else{
						return Dst{input}; //decay copy
					}
				}
			};
			return [&]<std::size_t ... Idx>(std::index_sequence<Idx...>){
				return [f = std::forward<Fn>(fn)] FORCE_INLINE (std::tuple_element_t<Idx, Expected>... args){
					return std::invoke(f, adaptor.template operator()<std::tuple_element_t<Idx, CurrentTy>>(args)...);
				};
			}(std::make_index_sequence<std::tuple_size_v<CurrentTy>>{});
		}
	}

	export
	template <typename... Args, typename Fn>
		requires (sizeof...(Args) > 0)
	[[nodiscard]] FORCE_INLINE auto make_transformer(propagate_type data_propagate_type, Fn&& fn){
		auto adapted = react_flow::adapt_fn_(std::forward<Fn>(fn));
		using return_type = std::invoke_result_t<decltype(adapted), typename descriptor_trait<make_descriptor_t<
			Args>>::operator_pass_type...>;
		return transformer<descriptor<return_type>, decltype(adapted), make_descriptor_t<Args>...>{
				data_propagate_type, std::move(adapted)
			};
	}

	export
	template <typename... Args, typename Fn>
		requires (sizeof...(Args) > 0)
	[[nodiscard]] FORCE_INLINE auto make_transformer(Fn&& fn){
		return react_flow::make_transformer<Args...>(propagate_type::eager, std::forward<Fn&&>(fn));
	}

	export
	template <typename... Args, typename Ret, typename Fn>
	[[nodiscard]] FORCE_INLINE auto make_transformer(propagate_type data_propagate_type, std::in_place_type_t<Ret>,
		Fn&& fn){
		auto adapted = react_flow::adapt_fn_(std::forward<Fn>(fn));

		if constexpr(spec_of_descriptor<Ret>){
			return transformer<Ret, std::decay_t<Fn>, make_descriptor_t<Args>...>{
					data_propagate_type, std::move(adapted)
				};
		} else{
			return transformer<descriptor<Ret>, std::decay_t<Fn>, make_descriptor_t<Args>...>{
					data_propagate_type, std::move(adapted)
				};
		}
	}

	export
	template <typename... Args, typename Ret, typename Fn>
	[[nodiscard]] FORCE_INLINE auto make_transformer(std::in_place_type_t<Ret>, Fn&& fn){
		return react_flow::make_transformer<Args...>(propagate_type::eager, std::in_place_type_t<Ret>{},
			std::forward<Fn&&>(fn));
	}

	template <typename Ret, typename RawFn, typename Tup>
	struct transformer_unambiguous_helper;

	template <typename Ret, typename RawFn, typename... Args>
	struct transformer_unambiguous_helper<Ret, RawFn, std::tuple<Args...>>{
		using type = transformer<descriptor<std::decay_t<Ret>>, RawFn, descriptor<extract_value_t<std::decay_t<Args>>>
			...>;
	};

	export
	template <typename Fn>
	using transformer_unambiguous = typename transformer_unambiguous_helper<
		typename function_traits<std::remove_cvref_t<Fn>>::return_type,
		std::remove_cvref_t<Fn>,
		typename function_traits<std::remove_cvref_t<Fn>>::mem_func_args_type
	>::type;

	export
	template <typename Fn>
	[[nodiscard]] FORCE_INLINE auto make_transformer(propagate_type data_propagate_type, Fn&& fn){
		auto adapted = react_flow::adapt_fn_(std::forward<Fn>(fn));
		return transformer_unambiguous<decltype(adapted)>(data_propagate_type,
			react_flow::adapt_fn_(std::move(adapted)));
	}

	export
	template <typename Fn>
	[[nodiscard]] FORCE_INLINE auto make_transformer(Fn&& fn){
		return react_flow::make_transformer(propagate_type::eager, std::forward<Fn&&>(fn));
	}
}
