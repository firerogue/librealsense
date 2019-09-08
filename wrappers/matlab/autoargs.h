#include <type_traits>
#include <functional>

#include <mex.h>
#include <matrix.h>

#include "MatlabParamParser.h"

typedef void mxFunc(int, mxArray*[], int, const mxArray*[]);

// Helper template to strip away type modifiers
template <typename T> struct intrinsic_type { typedef T type; };
template <typename T> struct intrinsic_type<const T> { typedef typename intrinsic_type<T>::type type; };
template <typename T> struct intrinsic_type<T*> { typedef typename intrinsic_type<T>::type type; };
template <typename T> struct intrinsic_type<T&> { typedef typename intrinsic_type<T>::type type; };
template <typename T> struct intrinsic_type<T&&> { typedef typename intrinsic_type<T>::type type; };
template <typename T, size_t N> struct intrinsic_type<const T[N]> { typedef typename intrinsic_type<T>::type type; };
template <typename T, size_t N> struct intrinsic_type<T[N]> { typedef typename intrinsic_type<T>::type type; };
template <typename T> using intrinsic_t = typename intrinsic_type<T>::type;

template <class T>
class argcaster {
private:
    //typename MatlabParamParser::type_traits<T>::rs2_internal_t holder;
    T value;
public:
    argcaster() : value(T()) {}

    bool load(const mxArray* cell) {
        /*holder*/ value = MatlabParamParser::parse<T>(cell);
        return true;
    }

    operator typename T() {
        //return MatlabParamParser::traits_trampoline<T>::from_internal(holder);
        return value;
    }
    operator typename T*() {
        return &value;
    }
};

template <typename type> using make_caster = argcaster<intrinsic_t<type>>;

template <class... Args>
class argloader {
private:
    using indices = std::make_index_sequence<sizeof...(Args)>;
    std::tuple<make_caster<Args>...> args;
    int min, max;
public:
    argloader() : argloader(0, 0) {}
    argloader(int min_args, int max_args) : min(min_args), max(max_args) {}
    
    bool load_args(int inc, const mxArray* inv[]) {
        return load_args_impl(inc, inv, indices{});
    }

    template <typename Return, typename Func>
    typename std::enable_if<!(std::is_void<Return>::value), Return>::type call(Func &&f) && {
        return std::move(*this).template call_impl<Return>(std::forward<Func>(f), indices{});
    }

    template <typename Return, typename Func>
    typename std::enable_if<std::is_void<Return>::value, void_type>::type call(Func &&f) && {
        std::move(*this).template call_impl<Return>(std::forward<Func>(f), indices{});
        return void_type();
    }

private:
    static bool load_args_impl(int, const mxArray*[], std::index_sequence<>) { return true; }

    template <size_t... Is>
    bool load_args_impl(int inc, const mxArray* inv[], std::index_sequence<Is...> /* dummy */) {
        for (bool res : {std::get<Is>(args).load(inv[Is])...}) {
            if (!res) return false;
        }
        return true;
    }

    template <typename Return, typename Func, size_t... Is>
    Return call_impl(Func &&f, std::index_sequence<Is...> /* dummy */) {
        return std::forward<Func>(f)(std::move(Args(std::get<Is>(args)))...);
    }
};

struct func_data {
    std::function<mxFunc> func;
    size_t out, in_min, in_max;

public:
    func_data() : func(), out(0), in_min(0), in_max(0) {};

    // Hack until lambdas can be worked out to allow constructors
    func_data(size_t out_args, size_t min_in_args, size_t max_in_args, std::function<mxFunc> f) :
        out(out_args), in_min(min_in_args), in_max(max_in_args), func(f)
    {}

    // Class method
    template <typename Return, typename Class, typename... Args>
    func_data(size_t min_in_args, size_t max_in_args, Return(Class::*f)(Args...)) :
        out((std::is_void<Return>::value)? 0:1),
        in_min(min_in_args), in_max(max_in_args)
    {
        init([f](Class *c, Args... args) -> Return { return (c->*f)(args...); },
            (Return(*)(Class*, Args...)) nullptr);
    }

    // Const class method
    template <typename Return, typename Class, typename... Args>
    func_data(size_t min_in_args, size_t max_in_args, Return(Class::*f)(Args...) const) :
        out((std::is_void<Return>::value)? 0:1),
        in_min(min_in_args), in_max(max_in_args)
    {
        init([f](const Class *c, Args... args) -> Return { return (c->*f)(args...); },
            (Return(*)(const Class*, Args...)) nullptr);
    }

private:
    template <typename Func, typename Return, typename... Args>
    void init(Func&& f, Return(*)(Args...) /* dummy */) {
        using cast_in = argloader<Args...>;

        func = [f](int outc, mxArray* outv[], int inc, const mxArray* inv[]) {
            cast_in args_converter;

            // Try to parse args as C++ values
            if (!args_converter.load_args(inc, inv))
                return false;

            auto ret = std::move(args_converter).template call<Return>(f);

            if (outc == 0) return true;
            if (outc == 1) outv[0] = MatlabParamParser::wrap(ret);
            // outc > 1, expand returned tuple by hand
            return true;
        };
    }
};