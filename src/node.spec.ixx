module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.react_flow:endpoint;

import :manager;
import :node_interface;
import :successory_list;

import mo_yanxi.react_flow.util;
import mo_yanxi.meta_programming;
import std;

namespace mo_yanxi::react_flow{


	export
	template <typename T>
	struct provider_general : type_aware_node<T>{
		static constexpr data_type_index node_data_type_index = unstable_type_identity_of<T>();
		friend manager;

		[[nodiscard]] provider_general() = default;

	protected:
		//this class should always be eager, make it protected
		[[nodiscard]] explicit provider_general( propagate_type propagate_type)
			: type_aware_node<T>{propagate_type}{
		}

	public:
		//TODO should these two function virtual?


		void update_value(T&& value){
			this->on_push(0, data_carrier{std::move(value)});
		}

		void update_value(const T& value){
			this->on_push(0, data_carrier{value});
		}

		void update_value(data_carrier<T>& data){
			this->on_push(0, data);
		}

		void update_value(data_carrier<T>&& data){
			this->on_push(0, data);
		}

		[[nodiscard]] std::span<const data_type_index> get_in_socket_type_index() const noexcept final{
			return {};
		}


		void disconnect_self_from_context() noexcept final{
			for(const auto& successor : successors){
				successor.entity->erase_predecessor_single_edge(successor.index, *this);
			}
			successors.clear();
		}

		[[nodiscard]] std::span<const successor_entry> get_outputs() const noexcept final{
			return successors;
		}

		request_pass_handle<T> request_raw(bool allow_expired) override{
			return make_request_handle_unexpected<T>(data_state::failed);
		}

		bool erase_successors_single_edge(std::size_t slot, node& post) noexcept final{
			return try_erase(successors, slot, post);
		}

	protected:
		bool connect_successors_impl(const std::size_t slot, node& post) final{
			if(auto&& ptr = post.get_inputs()[slot]){
				ptr->erase_successors_single_edge(slot, post);
				post.erase_predecessor_single_edge(slot, *ptr);
			}
			return try_insert(successors, slot, post);
		}

		successor_list successors{};

		void on_push(std::size_t target_index, data_carrier_obj&& in_data) override{
			assert(target_index == 0);
			react_flow::push_to_successors(successors, data_carrier_cast<T>(std::move(in_data)));
		}
	};

	export
	template <typename T>
	struct terminal_cached;

	export
	template <typename T>
	struct terminal : type_aware_node<T>{
		friend node;

	private:
		static constexpr data_type_index node_data_type_index = unstable_type_identity_of<T>();
		tracked_parent_ptr parent{};

	protected:
		[[nodiscard]] explicit terminal(propagate_type data_propagate_type)
			: type_aware_node<T>(data_propagate_type){
		}

	public:
		[[nodiscard]] terminal() = default;

		bool pull_and_push(bool allow_expired) override {
			if(!parent) return false;

			if(auto rst = this->terminal<T>::request_raw(allow_expired)){
				this->data_pending_state_ = data_pending_state::done;
				auto carrier = std::move(rst).value();
				this->on_update(carrier);
				return true;
			}
			return false;
		}

		[[nodiscard]] std::span<const tracked_parent_ptr> get_inputs() const noexcept final{
			return {&parent, 1};
		}

		[[nodiscard]] std::span<const data_type_index> get_in_socket_type_index() const noexcept final{
			return std::span{&node_data_type_index, 1};
		}

		[[nodiscard]] data_state get_data_state() const noexcept override{
			if(!parent)return data_state::failed;
			return this->parent->get_data_state();
		}

		void check_expired_and_update(bool allow_expired){
			if(!this->is_data_expired()) return;

			if(auto rst = this->request_raw(allow_expired)){
				this->on_update(rst.value());
			}
		}

		request_pass_handle<T> request_raw(const bool allow_expired) override{
			assert(parent != nullptr);
			return node_type_cast<T>(*parent).request_raw(allow_expired);
		}

		void try_fetch(){
			if(!parent)return;
			if(auto dat = this->nothrow_request(true)){
				this->on_update(*dat);
			}
		}


		void disconnect_self_from_context() noexcept final{
			if(parent){
				parent->erase_successors_single_edge(0, *this);
				parent.reset(nullptr);
			}
		}


		void erase_predecessor_single_edge(std::size_t slot, node& prev) noexcept final{
			assert(slot == 0);
			if(parent.get() == std::addressof(prev)){
				parent.reset(nullptr);
			}
		}

	protected:
		void connect_predecessor_impl(const std::size_t slot, node& prev) final{
			assert(slot == 0);
			if(auto ptr = parent.get()){
				ptr->erase_successors_single_edge(0, *this);
			}
			parent.reset(std::addressof(prev));
		}


		virtual void on_update(data_carrier<T>& data){
		}


		void on_update(data_carrier<T>&& data){
			this->on_update(data);
		}

		void mark_updated(const std::size_t from_index) noexcept final{
			assert(from_index == 0);
			this->data_pending_state_ = data_pending_state::expired;
		}

		friend terminal_cached<T>;

		void on_push(const std::size_t from_index, data_carrier_obj&& in_data) override{
			assert(from_index == 0);
			this->data_pending_state_ = data_pending_state::done;
			auto& storage = data_carrier_cast<T>(in_data);
			this->on_update(storage);
		}

	public:
		[[nodiscard]] push_dispatch_fptr get_push_dispatch_fptr() const noexcept override{
			return this->get_this_class_push_dispatch_fptr();
		}
	};

	export
	template <typename T>
	struct terminal_cached : terminal<T>{
		friend node;

		[[nodiscard]] const T& request_cache(){
			update_cache();
			return cache_;
		}

		[[nodiscard]] data_state get_data_state() const noexcept override{
			if(this->is_data_expired()) return data_state::expired;
			return data_state::fresh;
		}

		[[nodiscard]] explicit terminal_cached(propagate_type data_propagate_type)
			: terminal<T>(data_propagate_type){
		}

		[[nodiscard]] terminal_cached() = default;

		request_pass_handle<T> request_raw(const bool allow_expired) final{
			update_cache();

			if(!this->is_data_expired() || allow_expired){
				return react_flow::make_request_handle_expected_ref(cache_, this->is_data_expired());
			} else{
				return react_flow::make_request_handle_unexpected<T>(data_state::expired);
			}
		}

		bool pull_and_push(bool allow_expired) override {
			if(!this->parent) return false;


			if(auto rst = this->terminal<T>::request_raw(allow_expired)){
				this->data_pending_state_ = data_pending_state::done;


				this->cache_ = std::move(rst).value().get();
				data_carrier<T> carrier{this->cache_};
				this->on_update(carrier);
				return true;
			}
			return false;
		}

	protected:
		void on_pulse_received(manager& m) override{
			if(this->data_pending_state_ != data_pending_state::waiting_pulse) return;
			this->data_pending_state_ = data_pending_state::done;

			data_carrier d{cache_};
			this->on_update(d);
		}

		void on_push(const std::size_t from_index, data_carrier_obj&& in_data) override{
			switch(this->data_propagate_type_){
			case propagate_type::eager : this->data_pending_state_ = data_pending_state::done;
				{
					auto& storage = data_carrier_cast<T>(in_data);
					cache_ = storage.get();


					data_carrier d{cache_};
					this->on_update(d);
				}

				break;
			case propagate_type::lazy : this->data_pending_state_ = data_pending_state::expired;
				break;
			case propagate_type::pulse : this->data_pending_state_ = data_pending_state::waiting_pulse;
				{
					auto& storage = data_carrier_cast<T>(in_data);
					cache_ = storage.get();
				}
				break;
			default : std::unreachable();
			}
		}

	private:
		T cache_{};

		void update_cache(){
			if(this->is_data_expired()){
				if(auto rst = this->terminal<T>::request_raw(false)){
					this->data_pending_state_ = data_pending_state::done;
					cache_ = rst.value().get();
					this->on_update(data_carrier{cache_});
				}
			}
		}


	public:
		[[nodiscard]] push_dispatch_fptr get_push_dispatch_fptr() const noexcept override{
			return this->get_this_class_push_dispatch_fptr();
		}
	};

export
template <typename T, typename O = T, std::invocable<const T&> F = std::identity>
	requires std::convertible_to<std::invoke_result_t<F, const T&>, O>
struct provider_cached : provider_general<O> {
    using value_type = T;
    using output_type = O;
    using converter_type = F;

    static_assert(std::is_object_v<value_type>);

private:
    T data_{};
    ADAPTED_NO_UNIQUE_ADDRESS F converter_{};

public:
    [[nodiscard]] provider_cached() = default;

    [[nodiscard]] explicit provider_cached(propagate_type propagate_type, F converter = F{})
        : provider_general<O>(propagate_type), converter_(std::move(converter)) {
    }


	void update_value_quiet(T&& value) {
    	data_ = std::move(value);
    }

	void update_value_quiet(const T& value) {
    	data_ = value;
    }


    void update_value(T&& value) {
        data_ = std::move(value);
        on_update();
    }

    void update_value(const T& value) {
        data_ = value;
        on_update();
    }

    const T& get_raw_cache() const noexcept {
        return data_;
    }

    O get_output_cache() const {
        return std::invoke_r<O>(converter_, data_);
    }

    bool pull_and_push([[maybe_unused]] bool allow_expired) override {
        this->data_pending_state_ = data_pending_state::done;
        react_flow::push_to_successors(this->successors, data_carrier<O>{get_output_cache()});
        return true;
    }


    template <bool check_equal = false, std::invocable<T&> Proj, typename Ty>
        requires (std::assignable_from<std::invoke_result_t<Proj, T&>, Ty&&>)
    void update_value(Proj proj, Ty&& value) {
        auto& val = std::invoke(std::move(proj), data_);
        if constexpr(check_equal){
            if(val == value){
                return;
            }
        }

        val = std::forward<Ty>(value);
        on_update();
    }

    [[nodiscard]] data_state get_data_state() const noexcept override {
        return data_state::fresh;
    }

    request_pass_handle<O> request_raw(bool allow_expired) override {


        return react_flow::make_request_handle_expected<O>(get_output_cache(), false);
    }

protected:

    void on_push(std::size_t, data_carrier_obj&& in_data) override {
        auto& storage = data_carrier_cast<T>(in_data);
        data_ = storage.get();
        on_update();
    }

    void on_pulse_received(manager& m) override {
        if(this->data_pending_state_ != data_pending_state::waiting_pulse) return;
        this->data_pending_state_ = data_pending_state::done;
        react_flow::push_to_successors(this->successors, data_carrier<O>{get_output_cache()});
    }

private:
    void on_update() {
        switch(this->data_propagate_type_) {
        case propagate_type::eager :
            this->data_pending_state_ = data_pending_state::done;
            react_flow::push_to_successors(this->successors, data_carrier<O>{get_output_cache()});
            break;
        case propagate_type::lazy :
            this->data_pending_state_ = data_pending_state::done;
            node::mark_updated(-1);
            break;
        case propagate_type::pulse :
            this->data_pending_state_ = data_pending_state::waiting_pulse;
            break;
        default : std::unreachable();
        }
    }
};


template <auto mfptr>
struct member_ptr_invoker{
	using MptrTrait = mptr_info<decltype(mfptr)>;
	static typename MptrTrait::value_type operator()(typename MptrTrait::class_access_type* val) noexcept(std::is_nothrow_invocable_v<decltype(mfptr), typename MptrTrait::class_type*>){
		return std::invoke(mfptr, val);
	}
};

export
template <auto mfptr>
using provider_member = provider_cached<
	typename mptr_info<decltype(mfptr)>::class_access_type*,
	typename mptr_info<decltype(mfptr)>::value_type,
	member_ptr_invoker<mfptr>>;
}
