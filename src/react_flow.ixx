module;

#include <version>

export module mo_yanxi.react_flow;

export import :node_interface;
export import :nodes;
export import :async;
export import :modifier;

export import :manager;

import std;
//TODO support multi async consumer and better scheduler?

namespace mo_yanxi::react_flow{
	export
	template <std::ranges::input_range Rng = std::initializer_list<node*>>
	void connect_chain(const Rng& chain){
		if constexpr(std::ranges::range<std::ranges::range_reference_t<Rng>>){
			std::ranges::for_each(chain, connect_chain<std::ranges::range_reference_t<Rng>>);
		} else{
#ifdef __cpp_lib_ranges_zip
			for(auto&& [l, r] : chain | std::views::adjacent<2>){
#else
				for(auto it = chain.begin(), next_it = std::next(it); next_it != chain.end(); ++it, ++next_it){
					auto&& l = *it;
					auto&& r = *next_it;
#endif

				if constexpr(std::same_as<decltype(l), node&>){
					l.connect_successor(r);
				} else if(std::same_as<decltype(*l), node&>){
					(*l).connect_successor(*r);
				}
			}
		}
	}

	export
	template <typename Fn>
	[[nodiscard]] auto make_async_transformer(propagate_type data_propagate_type, async_type async_type, Fn&& fn){
		return async_transformer_unambiguous<Fn>{data_propagate_type, async_type, std::forward<Fn>(fn)};
	}
	export
	template <typename Fn>
	[[nodiscard]] auto make_async_transformer(propagate_type data_propagate_type, Fn&& fn){
		return async_transformer_unambiguous<Fn>{data_propagate_type, async_type::async_latest, std::forward<Fn>(fn)};
	}

}
