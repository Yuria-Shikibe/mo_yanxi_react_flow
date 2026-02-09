module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.react_flow:nodes;

import :manager;
import :node_interface;

import mo_yanxi.react_flow.data_storage;
import mo_yanxi.meta_programming;
import std;

namespace mo_yanxi::react_flow{
	export
	template <typename T>
	struct provider_cached : provider_general<T>{
		using value_type = T;
		static_assert(std::is_object_v<value_type>);

	private:
		T data_{};

	public:
		[[nodiscard]] provider_cached() = default;

		[[nodiscard]] explicit provider_cached(propagate_type propagate_type)
			: provider_general<T>(propagate_type){
		}

		using provider_general<T>::update_value;

		template <bool check_equal = false, std::invocable<T&> Proj, typename Ty>
			requires (std::assignable_from<std::invoke_result_t<Proj, T&>, Ty&&>)
		void update_value(Proj proj, Ty&& value){
			auto& val = std::invoke(std::move(proj), data_);
			if constexpr(check_equal){
				if(val == value){
					return;
				}
			}

			val = std::forward<Ty>(value);
			on_update();
		}

		[[nodiscard]] data_state get_data_state() const noexcept override{
			return data_state::fresh;
		}

		request_pass_handle<T> request_raw(bool allow_expired) override{
			//source is always fresh
			return react_flow::make_request_handle_expected_ref(data_, false);
		}

	protected:
		void on_push(std::size_t, push_data_obj&& in_data) override{
			auto& storage = push_data_cast<T>(in_data);
			data_ = storage.get();
			on_update();
		}

		void on_pulse_received(manager& m) override{
			if(this->data_pending_state_ != data_pending_state::waiting_pulse) return;
			this->data_pending_state_ = data_pending_state::done;
			react_flow::push_to_successors(this->successors, push_data_storage{data_});
		}

	private:
		void on_update(){
			switch(this->data_propagate_type_){
			case propagate_type::eager : this->data_pending_state_ = data_pending_state::done;
				react_flow::push_to_successors(this->successors, push_data_storage{data_});
				break;
			case propagate_type::lazy : this->data_pending_state_ = data_pending_state::done;
				std::ranges::for_each(this->successors, &successor_entry::mark_updated);
				break;
			case propagate_type::pulse : this->data_pending_state_ = data_pending_state::waiting_pulse;
				break;
			default : std::unreachable();
			}
		}
	};

	//TODO provider transient?
#pragma region Legacy
	export
	template <typename O, typename I>
	struct relay : type_aware_node<O>{
	private:
		static constexpr data_type_index in_type_indices = unstable_type_identity_of<I>();

	protected:
		O cache{};
		raw_node_ptr parent{};
		std::vector<successor_entry> successors{};

	public:
		[[nodiscard]] std::span<const data_type_index> get_in_socket_type_index() const noexcept final{
			return std::span{&in_type_indices, 1};
		}

		[[nodiscard]] std::span<const raw_node_ptr> get_inputs() const noexcept final{
			return {&parent, 1};
		}

		[[nodiscard]] std::span<const successor_entry> get_outputs() const noexcept final{
			return successors;
		}

		request_pass_handle<O> request_raw(bool allow_expired) final{
			if(this->is_data_expired() && this->parent){
				if(auto rst =
					react_flow::node_type_cast<I>(*this->parent).request_raw(allow_expired); rst && rst.value()){
					this->data_pending_state_ = data_pending_state::done;
					this->cache = this->operator()(rst.value());
				}
			}

			if(!this->is_data_expired() || allow_expired){
				return react_flow::make_request_handle_expected_ref(this->cache, this->is_data_expired());
			}

			return react_flow::make_request_handle_unexpected<O>(data_state::expired);
		}

	protected:
		void mark_updated(std::size_t from_index) noexcept final{
			type_aware_node<O>::mark_updated(from_index);
		}

		virtual O operator()(const I& input){
			if constexpr(std::convertible_to<const I&, O>){
				return input;
			} else{
				throw invalid_node_error{"not implemented!"};
			}
		}

		virtual O operator()(I&& input){
			return this->operator()(input);
		}

		virtual O operator()(push_data_storage<I>& input){
			return this->operator()(input.get());
		}

		void on_push(std::size_t from_index, push_data_obj&& in_data) final{
			assert(from_index == 0);
			auto& storage = push_data_cast<I>(in_data);
			this->cache = this->operator()(storage.get());

			switch(this->get_propagate_type()){
			case propagate_type::eager :{
				this->data_pending_state_ = data_pending_state::done;
				push_data_storage<O> data(this->cache);
				this->push_update(data, this->get_out_socket_type_index());
				break;
			}
			case propagate_type::lazy :{
				this->data_pending_state_ = data_pending_state::done;
				std::ranges::for_each(this->get_outputs(), &successor_entry::mark_updated);
				break;
			}
			case propagate_type::pulse :{
				this->data_pending_state_ = data_pending_state::waiting_pulse;
				break;
			}
			default : break;
			}
		}


		void on_pulse_received(manager& m) final{
			if(this->data_pending_state_ != data_pending_state::waiting_pulse) return;
			this->data_pending_state_ = data_pending_state::done;
			push_data_storage<O> data(cache);
			this->push_update(data, this->get_out_socket_type_index());
		}

		void disconnect_self_from_context() noexcept final{
			for(const auto& successor : successors){
				successor.entity->erase_predecessor_single_edge(successor.index, *this);
			}
			successors.clear();
			if(parent){
				parent->erase_successors_single_edge(0, *this);
				parent = nullptr;
			}
		}

	private:
		bool connect_successors_impl(std::size_t slot, node& post) final{
			if(auto& ptr = post.get_inputs()[slot]){
				post.erase_predecessor_single_edge(slot, *ptr);
			}
			return node::try_insert(successors, slot, post);
		}

		bool erase_successors_single_edge(std::size_t slot, node& post) noexcept final{
			return node::try_erase(successors, slot, post);
		}

		void connect_predecessor_impl(std::size_t slot, node& prev) final{
			assert(slot == 0);
			if(parent){
				const auto rng = parent->get_outputs();
				const auto idx = std::ranges::distance(rng.begin(),
					std::ranges::find(rng, this, &successor_entry::get));
				parent->erase_successors_single_edge(idx, *this);
			}
			parent = std::addressof(prev);
		}

		void erase_predecessor_single_edge(std::size_t slot, node& prev) noexcept final{
			assert(slot == 0);
			if(parent == &prev) parent = {};
		}

	public:
		using type_aware_node<O>::type_aware_node;

		[[nodiscard]] data_state get_data_state() const noexcept final{
			return this->is_data_expired() ? data_state::expired : data_state::fresh;
		}

		[[nodiscard]] bool is_isolated() const noexcept final{
			return parent == nullptr && successors.empty();
		}
	};

	export
	template <typename S>
	struct cache final : relay<S, S>{
	protected:
		S operator()(const S& input) final {
			return input;
		}

		S operator()(S&& input) final {
			return std::move(input);
		}

		S operator()(push_data_storage<S>& input) final {
			return input.get();
		}
	};

	template <typename Fn, typename... Args>
	consteval auto test_invoke_result(){
		return std::type_identity<std::invoke_result_t<Fn, Args...>>{};
	}
#pragma endregion

}
