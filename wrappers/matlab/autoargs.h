#include <type_traits>
#include <typeinfo>
#include <functional>

#include <mex.h>
#include <matrix.h>

#include "MatlabParamParser.h"

typedef void mxFunc(int, mxArray*[], int, const mxArray*[]);
typedef bool fnWrap(int, mxArray*[], int, const mxArray*[]);

// Helper template to strip away type modifiers
template <typename T> struct intrinsic_type { typedef T type; };
template <typename T> struct intrinsic_type<const T> { typedef typename intrinsic_type<T>::type type; };
template <typename T> struct intrinsic_type<T*> { typedef typename intrinsic_type<T>::type type; };
template <typename T> struct intrinsic_type<T&> { typedef typename intrinsic_type<T>::type type; };
template <typename T> struct intrinsic_type<T&&> { typedef typename intrinsic_type<T>::type type; };
template <typename T, size_t N> struct intrinsic_type<const T[N]> { typedef typename intrinsic_type<T>::type type; };
template <typename T, size_t N> struct intrinsic_type<T[N]> { typedef typename intrinsic_type<T>::type type; };
template <typename T> using intrinsic_t = typename intrinsic_type<T>::type;

// Converts matlab cell to type, internally stores the unmodified type.
// Could be specialized for fundamental types?
template <class T>
class argcaster {
protected:
    arg_t arg;
    bool has_value;

    bool test_cell_strict(const mxArray* cell, mxClassID id) { return mxGetClassID(cell) == id && IsScalar(cell); }
public:
    argcaster() : arg(T()), has_value(false) {}

    // trivial types are sent in "cleartext" instead of in arg_t wrapper
    typename std::enable_if<is_trivial_t<T>::value, bool>::type argcaster<T>::load(const mxArray* cell) {
        // Make sure the cell is the right type for this trivial type
        if (!mxIsScalar(cell)) return false; // TODO: Arrays
        auto cls = mxGetClassID(cell);
        switch (cls) {
        case mxUNKNOWN_CLASS:
            mexErrMsgTxt("Error parsing type of argument");
            return false; // mxClassID error
        case mxCELL_CLASS: // TODO: for sure?
        case mxSTRUCT_CLASS: // TODO: Further work to make sure its the correct struct
        case mxFUNCTION_CLASS:
            return false;

        case mxCHAR_CLASS: // strings
            // TODO: figure this out!

        case mxLOGICAL_CLASS:
            arg = arg_t(*static_cast<mxLogical*>(mxGetData(cell)));
            break;
        case mxDOUBLE_CLASS:
            arg = arg_t(*static_cast<mxDouble*>(mxGetData(cell)));
            break;
        case mxSINGLE_CLASS:
            arg = arg_t(*static_cast<mxSingle*>(mxGetData(cell)));
            break;
        case mxINT8_CLASS:
            arg = arg_t(*static_cast<mxInt8*>(mxGetData(cell)));
            break;
        case mxUINT8_CLASS:
            arg = arg_t(*static_cast<mxUint8*>(mxGetData(cell)));
            break;
        case mxINT16_CLASS:
            arg = arg_t(*static_cast<mxInt16*>(mxGetData(cell)));
            break;
        case mxUINT16_CLASS:
            arg = arg_t(*static_cast<mxUint16*>(mxGetData(cell)));
            break;
        case mxINT32_CLASS:
            arg = arg_t(*static_cast<mxInt32*>(mxGetData(cell)));
            break;
        case mxUINT32_CLASS:
            arg = arg_t(*static_cast<mxUint32*>(mxGetData(cell)));
            break;
        case mxINT64_CLASS:
            arg = arg_t(*static_cast<mxInt64*>(mxGetData(cell)));
            break;
        case mxUINT64_CLASS:
            arg = arg_t(*static_cast<mxUint64*>(mxGetData(cell)));
            break;
            
        default: return false;
        }
        
        // to_value_t<T> is used instead of T to handle enums
        has_value = true;
        return arg.can_convert<T>();
    }

    typename std::enable_if<!is_trivial_t<T>::value, bool>::type load(const mxArray* cell) {
        // Make sure the cell is the right type to be a pointer
        if (!test_cell_strict(cell, mxUINT64_CLASS)) return false;

        // pulls arg_t wrapper from matlab
        //arg = *reinterpret_cast<arg_t*>(*static_cast<uint64_t*>(mxGetData(cell)));
        arg = **static_cast<arg_t**>(mxGetData(cell));
        has_value = true;

        // make sure its the right type
        return arg.can_convert<T>();
    }

    bool load_default(arg_t darg) {
        if (has_value) return false;
        if (!darg.can_convert<T>())
            return false;
        arg = darg;
        return true;
    }

    void destroy() {
        if (!has_value) return;
        arg.destroy<T>();
        has_value = false;
    }

    operator typename T() {
        if (has_value) return T(arg);
        return T();
    }
};

// Helper template to make a caster from arbitrary (modified) type
template <typename type> using make_caster = argcaster<intrinsic_t<type>>;

// Stores all the arguments for a specific function call and executes the call
template <class... Args>
class argloader {
private:
    using indices = std::index_sequence_for<Args>;
    std::tuple<make_caster<Args>...> args;
    int min, max;
public:
    argloader() : argloader(0, 0) {}
    argloader(int min_args, int max_args) : min(min_args), max(max_args) {}
    
    // Try to convert arguments from the Matlab C API to C++ objects
    bool load_args(int inc, const mxArray* inv[], std::vector<arg_t>& defaults) {
        return load_args_impl(inc, inv, defaults, indices{});
    }

    // Uses stored arguments to do the actual function call (non-void return)
    template <typename Return, typename Func>
    typename std::enable_if<!(std::is_void<Return>::value), Return>::type call(Func &&f) && {
        return std::move(*this).template call_impl<Return>(std::forward<Func>(f), indices{});
    }

    // Uses stored arguments to do the actual function call (void return)
    template <typename Return, typename Func>
    typename std::enable_if<std::is_void<Return>::value, void_type>::type call(Func &&f) && {
        std::move(*this).template call_impl<Return>(std::forward<Func>(f), indices{});
        return void_type();
    }

private:
    static bool load_args_impl(int, const mxArray*[], std::index_sequence<>) { return true; }

    template <size_t... Is>
    bool load_args_impl(int inc, const mxArray* inv[], std::vector<arg_t>& defaults, std::index_sequence<Is...> /* dummy */) {
        for (bool res : {std::get<Is>(args).load(inv[Is]) || std::get<Is>(args).load_default(defaults[Is])...}) {
            // An arg failed to get the right type.
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
    std::function<fnWrap> func;
    std::vector<arg_t> def_args;
    size_t out, in_min, in_max;

public:
    func_data() : func(), def_args(in_max), out(0), in_min(0), in_max(0) {};

    // Hack until lambdas can be worked out to allow constructors
    func_data(size_t out_args, size_t min_in_args, size_t max_in_args, std::function<mxFunc> f) :
        out(out_args), in_min(min_in_args), in_max(max_in_args),
        func([f](int outc, mxArray* outv[], int inc, const mxArray* inv[]) { f(outc, outv, inc, inv); return true; }
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
    template<typename T>
    bool wrap_val(int outc, mxArray* outv[], int idx, T& val) {
        if (idx >= outc) return false;
        outv[idx] = MatlabParamParser::wrap(val);
        return true;
    }

    template<template<typename...> class TType, typename... Ts, size_t... Is>
    void wrap_outs_impl(int outc, mxArray* outv[], TType<Ts...>& ret, std::index_sequence<Is...> /* dummy */) {
        (wrap_val(outc, outv, Is, std::get<Is>(ret)) && ...);
    }

    template<template<typename...> class TType, typename... Ts>
    void wrap_outs(int outc, xmArray* outv[], TType<Ts...>& ret) {
        wrap_outs_impl(outc, outv, ret, std::index_sequence_for<Ts...>{});
    }

    template <typename Func, typename Return, typename... Args>
    void init(Func&& f, Return(*)(Args...) /* dummy */) {
        using cast_in = argloader<Args...>;

        func = [f, out](int outc, mxArray* outv[], int inc, const mxArray* inv[]) {
            cast_in args_converter;

            // Try to parse args as C++ values
            if (!args_converter.load_args(inc, inv, def_args))
                return false;

            auto ret = std::move(args_converter).template call<Return>(f);

            if (out == 0) // function declares no outputs
                return true;
            if (out == 1 && outc == 1) // function declares one output and user requests it
                outv[0] = MatlabParamParser::wrap(ret);
            if (out >= 2) // function declares multiple outputs
                wrap_outs(outc, outv, ret);
            return true;
        };
    }
};