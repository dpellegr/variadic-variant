#include <stdexcept>
#include <typeinfo>

// implementation details, the user should not import this:
namespace variant_impl {

template<int N, typename... Ts>
struct storage_ops;

template<typename X, typename... Ts>
struct position;

template<typename... Ts>
struct type_info;

template<int N, typename T, typename... Ts>
struct storage_ops<N, T&, Ts...> {
	static void del(int n, void *data) {}
	
	template<typename visitor>
	static typename visitor::result_type apply(int n, void *data, visitor& v) {}
};

template<int N, typename T, typename... Ts>
struct storage_ops<N, T, Ts...> {
  static const int IN = N;
  typedef T IT;
  typedef storage_ops<N + 1,Ts...> next;
  
	static void del(int n, void *data) {
		if(n == N) reinterpret_cast<T*>(data)->~T();
		else storage_ops<N + 1, Ts...>::del(n, data);
	}
	template<typename visitor>
	static typename visitor::result_type apply(int n, void *data, visitor& v) {
		if(n == N) return v(*reinterpret_cast<T*>(data));
		else return storage_ops<N + 1, Ts...>::apply(n, data, v);
	}
};

template<int N>
struct storage_ops<N> {
  static const int IN = -1;
  
	static void del(int n, void *data) {
		throw std::runtime_error(
			"Internal error: variant tag is invalid."
		);
	}
	template<typename visitor>
	static typename visitor::result_type apply(int n, void *data, visitor& v) {
		throw std::runtime_error(
			"Internal error: variant tag is invalid."
		);
	}
};

template<typename X>
struct position<X> {
	static const int pos = -1;
};

template<typename X, typename... Ts>
struct position<X, X, Ts...> {
	static const int pos = 0;
};

template<typename X, typename T, typename... Ts>
struct position<X, T, Ts...> {
	static const int pos = position<X, Ts...>::pos != -1 ? position<X, Ts...>::pos + 1 : -1;
};

template<typename T, typename... Ts>
struct type_info<T&, Ts...> {
	static const bool no_reference_types = false;
	static const bool no_duplicates = position<T, Ts...>::pos == -1 && type_info<Ts...>::no_duplicates;
	static const size_t size = type_info<Ts...>::size > sizeof(T&) ? type_info<Ts...>::size : sizeof(T&);
};

template<typename T, typename... Ts>
struct type_info<T, Ts...> {
	static const bool no_reference_types = type_info<Ts...>::no_reference_types;
	static const bool no_duplicates = position<T, Ts...>::pos == -1 && type_info<Ts...>::no_duplicates;
	static const size_t size = type_info<Ts...>::size > sizeof(T) ? type_info<Ts...>::size : sizeof(T&);
};

template<>
struct type_info<> {
	static const bool no_reference_types = true;
	static const bool no_duplicates = true;
	static const size_t size = 0;
};

} // namespace variant_impl

template<typename... Types>
class variant {
	static_assert(variant_impl::type_info<Types...>::no_reference_types, "Reference types are not permitted in variant.");
	static_assert(variant_impl::type_info<Types...>::no_duplicates, "variant type arguments contain duplicate types.");
	
	int tag;
	char storage[variant_impl::type_info<Types...>::size];
	
	variant() = delete;
	
	template<typename X>
	void init(const X& x) {
		tag = variant_impl::position<X, Types...>::pos;
		new(storage) X(x);
	}
	
//  template <Types> struct types {};

public:
	template<typename X>
	variant(const X& v) {
		static_assert(
			variant_impl::position<X, Types...>::pos != -1,
			"Type not in variant."
		);
		init(v);
	}
	~variant() {
		variant_impl::storage_ops<0, Types...>::del(tag, storage);
	}
	template<typename X>
	void operator=(const X& v) {
		static_assert(
			variant_impl::position<X, Types...>::pos != -1,
			"Type not in variant."
		);
		this->~variant();
		init(v);
	}
	template<typename X>
	X& get() {
		static_assert(
			variant_impl::position<X, Types...>::pos != -1,
			"Type not in variant."
		);
		if(tag == variant_impl::position<X, Types...>::pos) {
			return *reinterpret_cast<X*>(storage);
		} else {
			throw std::runtime_error(
				std::string("variant does not contain value of type ") + typeid(X).name()
			);
		}
	}
	template<typename X>
	const X& get() const {
		static_assert(
			variant_impl::position<X, Types...>::pos != -1,
			"Type not in variant."
		);
		if(tag == variant_impl::position<X, Types...>::pos) {
			return *reinterpret_cast<const X*>(storage);
		} else {
			throw std::runtime_error(
				std::string("variant does not contain value of type ") + typeid(X).name()
			);
		}
	}
	template<typename visitor>
	typename visitor::result_type visit(visitor& v) {
		return variant_impl::storage_ops<0, Types...>::apply(tag, storage, v);
	}
	
	int which() const {return tag;}
	void* data() {return storage;}
	
};

template<typename Result>
struct visitor {
	typedef Result result_type;
};


///////////////////////

namespace variant_impl {

//recursive double storage class to extract the correct storages objecs
template< int N1, int N2, typename S1, typename S2>
struct double_storage {
  template<typename visitor>
	static typename visitor::result_type apply(visitor& v, int n1, void *data1, int n2, void *data2) {
    if (n1 != S1::IN ) return double_storage< S1::next::IN, S2::IN, typename S1::next, S2>::apply(v, n1, data1, n2, data2);
    if (n2 != S2::IN ) return double_storage< S1::IN, S2::next::IN, S1, typename S2::next>::apply(v, n1, data1, n2, data2);
    return v(*reinterpret_cast< typename S1::IT*>(data1),*reinterpret_cast<typename S2::IT*>(data2)); //real apply
	}
};

//the two following templates specialization are used to terminate the recursion
template<int N1, typename S1, typename S2>
struct double_storage<N1,-1,S1,S2> {
  template<typename visitor>
	static typename visitor::result_type apply(visitor& v, int n1, void *data1, int n2, void *data2) {}
};
template<int N2, typename S1, typename S2>
struct double_storage<-1,N2,S1,S2> {
  template<typename visitor>
	static typename visitor::result_type apply(visitor& v, int n1, void *data1, int n2, void *data2) {}
};

//these are to extract the storage from a variant
template <typename ...TList> struct extract;
template <typename T, typename ...TList>
struct extract<variant<T,TList...>> {
  typedef storage_ops<0,T,TList...> storage;
};

} // namespace variant_impl


//single and double apply visitor functions

template<typename visitor, typename variant>
typename visitor::result_type apply(visitor&& vis, variant& var) {
  return var.visit(vis);
}

template<typename visitor, typename V1, typename V2>
static typename visitor::result_type apply(visitor&& vis, V1& v1, V2& v2) {
  return variant_impl::double_storage<0, 0, typename variant_impl::extract<V1>::storage, typename variant_impl::extract<V2>::storage>
                                     ::apply(vis, v1.which(), v1.data(), v2.which(), v2.data());
}
