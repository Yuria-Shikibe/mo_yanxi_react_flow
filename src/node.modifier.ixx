module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.react_flow:modifier;

import :manager;
import :node_interface;

import mo_yanxi.react_flow.data_storage;
import mo_yanxi.meta_programming;
import mo_yanxi.concepts;
import std;

namespace mo_yanxi::react_flow{

	export
	template <typename Ret, typename... Args>
		requires (spec_of_descriptor<Ret> && (spec_of_descriptor<Args> && ...))
	struct modifier : type_aware_node<typename descriptor_trait<Ret>::output_type>{
	private:
		static constexpr std::size_t arg_count = sizeof...(Args);
		static_assert(arg_count > 0);

		static constexpr std::array<data_type_index, arg_count> in_type_indices{
				unstable_type_identity_of<typename descriptor_trait<Args>::input_type>()...
			};

	protected:
		using return_descriptor = Ret;
		using return_transformed_type = typename descriptor_trait<Ret>::output_type;

		using return_pass_type = typename descriptor_trait<Ret>::input_pass_type;

		using arg_type = std::tuple<Args...>;
		using arg_trans_type = std::tuple<typename descriptor_trait<Args>::output_type ...>;
		using arg_pass_type = std::tuple<typename descriptor_trait<Args>::output_pass_type ...>;

#ifdef _MSC_VER //workaround
		static constexpr std::size_t _true_trigger_index = mo_yanxi::tuple_index_v<trigger_type, arg_trans_type>;
		static constexpr bool has_trigger = _true_trigger_index != std::tuple_size_v<arg_trans_type>;
		static constexpr std::size_t trigger_index = has_trigger ? _true_trigger_index : 0;
#else
		static constexpr std::size_t trigger_index = mo_yanxi::tuple_index_v<trigger_type, arg_trans_type>;
		static constexpr bool has_trigger = trigger_index != std::tuple_size_v<arg_trans_type>;
#endif

		static constexpr std::array<bool, arg_count> quiet_map{descriptor_trait<Args>::no_push ...};

	private:
		std::array<raw_node_ptr, arg_count> parents_{};
		std::vector<successor_entry> successors_{};

		ADAPTED_NO_UNIQUE_ADDRESS arg_type arguments_{};
		ADAPTED_NO_UNIQUE_ADDRESS return_descriptor ret_descriptor_{};
		ADAPTED_NO_UNIQUE_ADDRESS dirty_bits<descriptor_trait<Args>::cached ...> dirty_flags_{};

		optional_val<data_state, descriptor_trait<Ret>::cached> data_state_{};
	public:

		[[nodiscard]] modifier() = default;

		[[nodiscard]] explicit modifier(propagate_type data_propagate_type)
			: type_aware_node<return_transformed_type>(data_propagate_type){
		}

#pragma region Connection_Region
	public:
		[[nodiscard]] bool is_isolated() const noexcept override{
			return std::ranges::none_of(parents_, std::identity{}) && successors_.empty();
		}

		[[nodiscard]] data_state get_data_state() const noexcept override{
			if constexpr (descriptor_trait<Ret>::cached){
				return *data_state_;
			}else{
				data_state states{};

				for(const raw_node_ptr& p : parents_){
					update_state_enum(states, p->get_data_state());
				}

				return states;
			}
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
			return node::try_insert(successors_, slot, post);
		}

		bool erase_successors_single_edge(std::size_t slot, node& post) noexcept final{
			return node::try_erase(successors_, slot, post);
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
		request_pass_handle<return_transformed_type> request_raw(bool allow_expired) override{
			if constexpr (descriptor_trait<Ret>::cached){
				auto state = get_data_state();
				if(state == data_state::fresh || (state == data_state::expired && allow_expired)){
					return react_flow::make_request_handle_expected_from_data_storage(ret_descriptor_.get(), state == data_state::expired);
				}
			}

			arg_pass_type arguments{};

			data_state_ = data_state::fresh;

			auto all_successful = [&, this]<std::size_t ... Idx>(std::index_sequence<Idx...>){
				return ([&, this]<std::size_t I>(){
					using D = std::tuple_element_t<I, arg_type>;
					using InputTy = typename descriptor_trait<D>::input_type;

					if constexpr(descriptor_trait<D>::cached){
						if(dirty_flags_.get(I) && (!allow_expired || !descriptor_trait<D>::allow_expired)){
							if(!parents_[I]) return false;
							node& n = *parents_[I];


							if(auto rst = node_type_cast<InputTy>(n).request_raw(false)){
								std::get<I>(arguments) = std::get<I>(arguments_) << std::move(rst).value();
								dirty_flags_.template set<I>(false);
								return true;
							}

							this->update_data_state(data_state::expired);
							std::get<I>(arguments) = std::get<I>(arguments_).get();
							return allow_expired;
						}

						std::get<I>(arguments) = std::get<I>(arguments_).get();
						return true;
					}

					if(!parents_[I]) return false;
					node& n = *parents_[I];

					auto rst = node_type_cast<InputTy>(n).request_raw(allow_expired && descriptor_trait<D>::allow_expired);
					this->update_data_state(rst.state());
					if(rst){
						std::get<I>(arguments) = std::get<I>(arguments_) << std::move(rst).value();
						return true;
					}

					return false;
				}.template operator()<Idx>() & ...);
			}(std::index_sequence_for<Args...>{});

			if(all_successful || !allow_expired){
				return_pass_type rst = ret_descriptor_ << this->apply(arguments);
				return react_flow::make_request_handle_expected_from_data_storage(std::move(rst), all_successful);
			}else{
				return make_request_handle_unexpected<return_transformed_type>(data_state::failed);
			}

		}

	protected:
		bool check_trigger_type(){
			if constexpr (!has_trigger){
				return true;
			}else{
				if constexpr (descriptor_trait<std::tuple_element<trigger_index, arg_type>>::cached){
					switch(trigger_type& trigger = *std::get<trigger_index>(arguments_)){
					case trigger_type::active: return true;
					case trigger_type::disabled: return false;
					case trigger_type::on_pulse:
						trigger = trigger_type::disabled;
						return true;
					default: std::unreachable();
					}
				}else{
					if(const raw_node_ptr p = parents_[trigger_index]){
						return node_type_cast<trigger_type>(*p).request_raw(true).value_or(trigger_type::active).get() != trigger_type::disabled;
					}
					return true;
				}
			}
		}


		void mark_updated(std::size_t from_index) noexcept override{
			if(dirty_flags_.try_set(from_index)){
				data_state_ = data_state::expired;
				node::mark_updated(from_index);
			}
		}

		void on_pulse_received(manager& m) override{
			if(this->data_pending_state_ != data_pending_state::waiting_pulse) return;
			this->data_pending_state_ = data_pending_state::done;

			this->update(trigger_type::active, nullptr);
		}

		FORCE_INLINE bool has_cache_at(std::size_t index) const noexcept{
			return dirty_flags_.has_bit(index);
		}

		void on_push(std::size_t target_index, push_data_obj&& in_data) override{

			auto update_cache = [&]{
				[&, this]<std::size_t ... Idx>(std::index_sequence<Idx...>){
					([&, this]<std::size_t I>(){
						using Ty = std::tuple_element_t<I, arg_type>;

						if constexpr(!descriptor_trait<Ty>::cached) return false;
						else if(I == target_index){
							using InputTy = typename descriptor_trait<Ty>::input_type;
							std::get<I>(arguments_).set(push_data_cast<InputTy>(in_data));
							dirty_flags_.template set<I>(false);
							return true;
						}
						return false;
					}.template operator()<Idx>() || ...);
				}(std::index_sequence_for<Args...>{});
			};

			if(quiet_map[target_index]){
				if (has_cache_at(target_index)){
					update_cache();
				}
				mark_updated(-1);
			}


			trigger_type trigger{};
			if constexpr (has_trigger && requires{requires trigger_index < std::tuple_size_v<arg_type>; /*workaround for msvc*/}){
				if(target_index == trigger_index){
					auto rst = push_data_cast<trigger_type>(in_data).get();
					using DescriptorTy = std::tuple_element_t<trigger_index, arg_type>;
					if(descriptor_trait<DescriptorTy>::cached && descriptor_trait<DescriptorTy>::identity){
						std::get<trigger_index>(arguments_).set(rst == trigger_type::on_pulse ? trigger_type::disabled : rst);
						dirty_flags_.template set<trigger_index>();
					}
					if(rst == trigger_type::disabled)return;
					trigger = rst;
				}else{
					if(!check_trigger_type()){
						return;
					}
				}
			}

			switch (this->get_propagate_type()) {
			case propagate_type::eager:
				this->update(trigger, [&]<std::size_t I>(arg_pass_type& arguments){
					if(I == target_index){
						using Ty = std::tuple_element_t<I, arg_type>;
						using InputTy = typename descriptor_trait<Ty>::input_type;
						std::get<I>(arguments) = std::get<I>(arguments_) << std::move(push_data_cast<InputTy>(in_data));
						if constexpr (descriptor_trait<Ty>::cached)dirty_flags_.template set<I>(false);
						return true;
					}
					return false;
				});
				break;
			case propagate_type::lazy:
				update_cache();
				mark_updated(-1);
				break;
			case propagate_type::pulse:
				update_cache();
				this->data_pending_state_ = data_pending_state::waiting_pulse;
				break;
			default:
				std::unreachable();
			}
		}

		void update_children(push_data_storage<return_transformed_type>& val) const{
			if(successors_.empty()) return;

			react_flow::push_to_successors(successors_, std::move(val));

		}

		return_pass_type apply(arg_pass_type& arguments){
			return [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>){
				return this->operator()(react_flow::pass_data(std::get<Idx>(arguments))...);
			}(std::index_sequence_for<Args...>());
		}

		virtual return_pass_type operator()(typename descriptor_trait<Args>::operator_pass_type... args) = 0;

	private:
		FORCE_INLINE void update(trigger_type trigger, auto checker){
			arg_pass_type arguments{};

			if constexpr (has_trigger){
				std::get<trigger_index>(arguments) = trigger;
			}

			data_state_ = data_state::fresh;

			auto all_successful = [&, this]<std::size_t ... Idx>(std::index_sequence<Idx...>){
				return ([&, this]<std::size_t I>(){
					using D = std::tuple_element_t<I, arg_type>;
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
						if(dirty_flags_.get(I) && !descriptor_trait<D>::allow_expired){
							if(!parents_[I]) return false;
							node& n = *parents_[I];

							if(auto rst = node_type_cast<InputTy>(n).request_raw(false)){
								std::get<I>(arguments) = std::get<I>(arguments_) << std::move(rst).value();
								dirty_flags_.template set<I>(false);
								return true;
							}

							this->update_data_state(data_state::expired);
						}

						//fallback, use cache even it is expired
						// TODO check if expire is allow by tag?
						std::get<I>(arguments) = std::get<I>(arguments_).get();
						return true;
					}

					if(!parents_[I]) return false;
					node& n = *parents_[I];

					auto rst = node_type_cast<InputTy>(n).request_raw(descriptor_trait<D>::allow_expired);
					this->update_data_state(rst.state());
					if(rst){
						std::get<I>(arguments) = std::get<I>(arguments_) << std::move(rst).value();
						return true;
					}

					return false;
				}.template operator()<Idx>() & ...);
			}(std::index_sequence_for<Args...>{});


			push_data_storage<return_transformed_type> rst = ret_descriptor_ << this->apply(arguments);
			this->update_children(rst);
		}

		void update_data_state(data_state state) noexcept{
			if constexpr (descriptor_trait<Ret>::cached){
				react_flow::update_state_enum(*data_state_, state);
			}
		}

	};

	export
	template <typename Ret, typename Fn, typename... Args>
		requires (std::is_invocable_r_v<typename descriptor_trait<Ret>::input_pass_type, Fn, typename descriptor_trait<Args>::operator_pass_type ...> && spec_of_descriptor<Ret> && (spec_of_descriptor<Args> && ...))
	struct transformer_v2 final : modifier<Ret, Args...>{
	private:
		ADAPTED_NO_UNIQUE_ADDRESS Fn fn;

	protected:
		modifier<Ret, Args...>::return_pass_type operator()(descriptor_trait<Args>::operator_pass_type ...args) override final {
			return std::invoke(fn, args...);
		}

	public:
		explicit transformer_v2(propagate_type data_propagate_type)
			: modifier<Ret, Args...>(data_propagate_type){
		}

		transformer_v2() = default;

		transformer_v2(propagate_type data_propagate_type, const Fn& fn)
			: modifier<Ret, Args...>(data_propagate_type),
			fn(fn){
		}

		explicit transformer_v2(const Fn& fn)
			: fn(fn){
		}

		explicit transformer_v2(Fn&& fn)
			: fn(std::move(fn)){
		}
	};

	template <typename T>
	using make_descriptor_t = std::conditional_t<spec_of_descriptor<T>, T, descriptor<T>>;


	template <typename T>
	struct extract_type : std::type_identity<T>{

	};

	template <typename T>
	struct extract_type<push_data_storage<T>> : std::type_identity<T>{

	};

	template <typename T>
	using extract_value_t = extract_type<T>::type;


	template <typename Fn>
	FORCE_INLINE auto adapt_fn_(Fn&& fn) {
		using CurrentTy = function_traits<std::decay_t<Fn>>::mem_func_args_type;
		using Expected = unary_apply_to_tuple_t<data_pass_t, unary_apply_to_tuple_t<extract_value_t,
			unary_apply_to_tuple_t<std::decay_t, CurrentTy>>>;
		if constexpr (std::same_as<CurrentTy, Expected>){
			return std::forward<Fn>(fn);
		}else{
			constexpr static auto adaptor = []<typename Dst, typename Src> FORCE_INLINE (Src& input) static -> Dst {
				if constexpr(std::is_lvalue_reference_v<Dst> && !std::is_const_v<std::remove_reference_t<Dst>>){
					static_assert(false, "non const lvalue reference is not allowed");
				}

				if constexpr (std::convertible_to<Src&, Dst>){
					return input;
				}else if constexpr (spec_of<Src, push_data_storage>){
					if constexpr(std::is_reference_v<Dst> && std::is_const_v<std::remove_reference_t<Dst>>){
						//is const reference, return const view
						return input.get_ref_view();
					}else{
						//crop it to value or rvalue ref
						return input.get();
					}
				}else{
					static_assert(std::same_as<std::decay_t<Dst>, Src>, "type mismatch ant not convertible");

					if constexpr (std::is_rvalue_reference_v<Dst>){
						return std::move(input);
					}else{
						return Dst{input}; //decay copy
					}
				}

			};
			return [&]<std::size_t ...Idx>(std::index_sequence<Idx...>){
				return [f = std::forward<Fn>(fn)](std::tuple_element_t<Idx, Expected> ...args){
					return std::invoke(f, adaptor.template operator()<std::tuple_element_t<Idx, CurrentTy>>(args)...);
				};
			}(std::make_index_sequence<std::tuple_size_v<CurrentTy>>{});
		}
	}

	export
	template <typename ...Args, typename Fn>
		requires (sizeof...(Args) > 0)
	[[nodiscard]] auto make_transformer_v2(propagate_type data_propagate_type, Fn&& fn){
		auto adapted = react_flow::adapt_fn_(std::forward<Fn>(fn));
		using return_type = std::invoke_result_t<decltype(adapted), typename descriptor_trait<make_descriptor_t<Args>>::operator_pass_type ...>;
		return transformer_v2<descriptor<return_type>, decltype(adapted), make_descriptor_t<Args>...>{data_propagate_type, std::move(adapted)};
	}

	export
	template <typename ...Args, typename Fn>
		requires (sizeof...(Args) > 0)
	[[nodiscard]] auto make_transformer_v2(Fn&& fn){
		return react_flow::make_transformer_v2<Args...>(propagate_type::eager, std::forward<Fn&&>(fn));
	}

	export
	template <typename ...Args, typename Ret, typename Fn>
	[[nodiscard]] auto make_transformer_v2(propagate_type data_propagate_type, std::in_place_type_t<Ret>, Fn&& fn){
		auto adapted = react_flow::adapt_fn_(std::forward<Fn>(fn));

		if constexpr (spec_of_descriptor<Ret>){
			return transformer_v2<Ret, std::decay_t<Fn>, make_descriptor_t<Args>...>{data_propagate_type, std::move(adapted)};
		}else{
			return transformer_v2<descriptor<Ret>, std::decay_t<Fn>, make_descriptor_t<Args>...>{data_propagate_type, std::move(adapted)};
		}
	}

	export
	template <typename ...Args, typename Ret, typename Fn>
	[[nodiscard]] auto make_transformer_v2(std::in_place_type_t<Ret>, Fn&& fn){
		return react_flow::make_transformer_v2<Args...>(propagate_type::eager, std::in_place_type_t<Ret>{}, std::forward<Fn&&>(fn));
	}

	template <typename Ret, typename RawFn, typename Tup>
	struct transformer_v2_unambiguous_helper;

	template <typename Ret, typename RawFn, typename... Args>
	struct transformer_v2_unambiguous_helper<Ret, RawFn, std::tuple<Args...>>{
		using type = transformer_v2<descriptor<std::decay_t<Ret>>, RawFn, descriptor<extract_value_t<std::decay_t<Args>>>...>;
	};

	export
	template <typename Fn>
	using transformer_v2_unambiguous = typename transformer_v2_unambiguous_helper<
		typename function_traits<std::remove_cvref_t<Fn>>::return_type,
		std::remove_cvref_t<Fn>,
		typename function_traits<std::remove_cvref_t<Fn>>::mem_func_args_type
	>::type;

	export
	template <typename Fn>
	[[nodiscard]] auto make_transformer_v2(propagate_type data_propagate_type, Fn&& fn){
		auto adapted = react_flow::adapt_fn_(std::forward<Fn>(fn));
		return transformer_v2_unambiguous<decltype(adapted)>(data_propagate_type, react_flow::adapt_fn_(std::move(adapted)));
	}

	export
	template <typename Fn>
	[[nodiscard]] auto make_transformer_v2(Fn&& fn){
		return react_flow::make_transformer_v2(propagate_type::eager, std::forward<Fn&&>(fn));
	}
}