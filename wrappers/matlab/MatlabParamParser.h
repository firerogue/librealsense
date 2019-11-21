#pragma once
#include "mex.h"
#include "matrix.h"
#include <string>
#include <memory>
#include <vector>
#include <array>
#include <type_traits>

template <bool B> using bool_constant = std::integral_constant<bool, B>;

template <typename T> using is_basic_type = std::bool_constant<std::is_arithmetic<T>::value || std::is_pointer<T>::value || std::is_enum<T>::value>;
template <typename T> struct is_array_type : std::false_type {};
template <typename T> struct is_array_type<std::vector<T>> : std::true_type { using inner_t = T; };
struct void_type {};

// make this more complex to be able to handle inheritance?
//      -> initializing class should add base classes to some sort of graph that indicates valid
//         conversions.
template <class T>
bool can_convert(std::type_info& info) {
    return info.hash_code() == typeid(T).hash_code();
}

template <class T> using is_trivial_t = bool_constant<std::is_same<T, bool>::value
    || std::is_same<T, int8_t>::value || std::is_same<T, uint8_t>::value
    || std::is_same<T, int16_t>::value || std::is_same<T, uint16_t>::value
    || std::is_same<T, int32_t>::value || std::is_same<T, uint32_t>::value
    || std::is_same<T, int64_t>::value || std::is_same<T, uint64_t>::value
    || std::is_enum<T>::value>;
template <class T, typename voider = void> struct to_value_t : std::conditional<is_trivial_t<T>, T, void*> {};
template <class T> struct to_value_t<T, std::enable_if<std::is_enum<T>::value>::type> : std::underlying_type<T> {};

template <class T, typename voider = void> struct to_mx_class : std::integral_constant<mxClassID, mxUINT64_CLASS> {};
template <> struct to_mx_class<bool> : std::integral_constant<mxClassID, mxLOGICAL_CLASS> {};
template <> struct to_mx_class<int8_t> : std::integral_constant<mxClassID, mxINT8_CLASS> {};
template <> struct to_mx_class<uint8_t> : std::integral_constant<mxClassID, mxUINT8_CLASS> {};
template <> struct to_mx_class<int16_t> : std::integral_constant<mxClassID, mxINT16_CLASS> {};
template <> struct to_mx_class<uint16_t> : std::integral_constant<mxClassID, mxUINT16_CLASS> {};
template <> struct to_mx_class<int32_t> : std::integral_constant<mxClassID, mxINT32_CLASS> {};
template <> struct to_mx_class<uint32_t> : std::integral_constant<mxClassID, mxUINT32_CLASS> {};
template <> struct to_mx_class<int64_t> : std::integral_constant<mxClassID, mxINT64_CLASS> {};
template <class T> struct to_mx_class<T, std::enable_if<std::is_enum<T>::value>::type> : to_mx_class<std::underlying_type<T>> {};
//template <> struct to_mx_class<uint64_t> : std::integral_constant<mxClassID, mxUINT64_CLASS> {};

class arg_t {
protected:
    std::type_info* tinfo; // TODO: make sure you can arg_t = arg_t despite this.
    union value_t {
        value_t() : ptr(nullptr) {}
        bool b; value_t(bool b) : b(b) {}
        int8_t i8; value_t(int8_t i8) : i8(i8) {}
        uint8_t ui8; value_t(uint8_t ui8) : ui8(ui8) {}
        int16_t i16; value_t(int16_t i16) : i16(i16) {}
        uint16_t ui16; value_t(uint16_t ui16) : ui16(ui16) {}
        int32_t i32; value_t(int32_t i32) : i32(i32) {}
        uint32_t ui32; value_t(uint32_t ui32) : ui32(ui32) {}
        int64_t i64; value_t(int64_t i64) : i64(i64) {}
        uint64_t ui64; value_t(uint64_t ui64) : ui64(ui64) {}
        void* ptr; value_t(void* ptr) : ptr(ptr) {}
    } holder;
    std::function<void(void*)> destroyer;

    template <class T>
    typename std::enable_if<is_trivial_t<T>::value, to_value_t<T>::type>::type to_holder(T& value) { return value; }
    template <class T>
    typename std::enable_if<!is_trivial_t<T>::value, void*>::type to_holder(T& value) { return static_cast<void*>(trampoline_t::to_internal<T>(T)); }
    template <class T>
    typename std::enable_if<is_trivial_t<T>::value, to_value_t<T>::type>::type to_holder(T&& value) { return std::move(value); }
    template <class T>
    typename std::enable_if<!is_trivial_t<T>::value, void*>::type to_holder(T&& value) { return static_cast<void*>(trampoline_t::to_internal<T>(std::move(T))); }
    template <class T>
    typename std::enable_if<is_trivial_t<T>::value>::type destroy(void*) {}
    template <class T>
    typename std::enable_if<!is_trivial_t<T>::value>::type destroy(void* ptr) { 
        using internal_t = typename MatlabParamParser::type_traits<T>::rs2_internal_t;
        trampoline_t::destroy<T>(static_cast<internal_t*>(ptr));
    }


    using trampoline_t = typename MatlabParamParser::traits_trampoline;
    
    //arg_t(std::string& value) { static_assert(false, "Figure out strings!"); }
public:
    arg_t() : tinfo(const_cast<std::type_info*>(&typeid(nullptr_t))), holder(), destroyer([](void*) {}) {}
    template <class T>
    arg_t(T& value) : tinfo(const_cast<std::type_info*>(&typeid(T))), holder(to_holder(value)), destroyer(&destroy<T>) {}
    template <class T>
    arg_t(T&& value) : tinfo(const_cast<std::type_info*>(&typeid(T))), holder(to_holder(std::move(value))), destroyer(&destroy<T>) {}

    // TODO: Figure out strings
    //template <> static arg_t make_arg<std::string>(std::string& value) { return arg_t(value); }
    //template <> static arg_t make_arg<char*>(char*& value) { return arg_t(value); }
    //template <> static arg_t make_arg<const char*>(const char*& value) { return arg_t(value); }

    template <class T>
    bool can_convert() {
        return can_convert<T>(*tinfo);
    }

    template <typename T, typename voider = typename std::enable_if<is_trivial_t<T>::value>::type>
    explicit operator T() {
        if (!can_convert<T>(*tinfo)) {
            std::stringstream ss;
            ss << "Failed to get " << typeid(T).name() << "argument from arg at " << this
                << ". Actual type is " << tinfo->name();
            mexErrMsgTxt(ss.str().c_str());
        }
        // casting pointer to union to a pointer of one of the internal types
        // results in a pointer to that member. We can use this to generically
        // grab the right type.
        return *reinterpret_cast<to_value_t<T>*>(&holder); 
    }
    template <typename T, typename voider = typename std::enable_if<!is_trivial_t<T>::value>::type>
    explicit operator T() {
        using internal_t = typename MatlabParamParser::type_traits<T>::rs2_internal_t;

        if (!holder.ptr) throw std::runtime_error("Arg empty"); // TODO: More useful error message

        if (!can_convert<T>(*tinfo)) {
            std::stringstream ss;
            ss << "Failed to get " << typeid(T).name() << "argument from arg at " << this
                << ". Actual type is " << tinfo->name();
            mexErrMsgTxt(ss.str().c_str());
        }

        return trampoline_t::from_internal<T>(static_cast<internal_t*>(holder.ptr));
    }

    template <class T>
    void destroy() {
        if (!can_convert<T>(*tinfo)) {
            std::stringstream ss;
            ss << "Can't destroy arg of type " << typeid(T).name() << " at " << this
                << ". Actual type is " << tinfo->name();
            mexErrMsgTxt(ss.str().c_str());
        }
        deleter(holder);
    }
};

// TODO: consider using nested impl/detail namespace
namespace MatlabParamParser
{
    template <typename T, typename = void> struct type_traits {
        // KEEP THESE COMMENTED. They are only here to show the signature
        // should you choose to declare these functions
        // static rs2_internal_t* to_internal(T&& val);
        // static T from_internal(rs2_internal_t* ptr);
        // static void destroy(rs2_internal_t* ptr);
    };
    struct traits_trampoline {
    private:
        template <typename T> struct detector {
            struct fallback { int to_internal, from_internal, use_cells, use_destroy; };
            struct derived : type_traits<T>, fallback {};
            template <typename U, U> struct checker;
            typedef char ao1[1];
            typedef char ao2[2];
            template <typename U> static ao1& check_to(checker<int fallback::*, &U::to_internal> *);
            template <typename U> static ao2& check_to(...);
            template <typename U> static ao1& check_from(checker<int fallback::*, &U::from_internal> *);
            template <typename U> static ao2& check_from(...);
            template <typename U> static ao1& check_cells(checker<int fallback::*, &U::use_cells> *);
            template <typename U> static ao2& check_cells(...);
            template <typename U> static ao1& check_destroy(checker<int fallback::*, &U::use_destroy> *);
            template <typename U> static ao2& check_destroy(...);
            
            enum { has_to = sizeof(check_to<derived>(0)) == 2 };
            enum { has_from = sizeof(check_from<derived>(0)) == 2 };
            enum { use_cells = sizeof(check_cells<derived>(0)) == 2 };
            enum { has_destroy = sizeof(check_destroy<derived>(0)) == 2 };
        };
        template <typename T> using internal_t = typename type_traits<T>::rs2_internal_t;
    public:
        // selected if type_traits<T>::to_internal exists
        template <typename T> static typename std::enable_if<detector<T>::has_to, internal_t<T>*>::type
            to_internal(T&& val) { return type_traits<T>::to_internal(std::move(val)); }
        // selected if it doesnt
        template <typename T> static typename std::enable_if<!detector<T>::has_to, internal_t<T>*>::type
            to_internal(T&& val) { mexLock(); return new internal_t<T>(val); }

        // selected if type_traits<T>::from_internal exists
        template <typename T> static typename std::enable_if<detector<T>::has_from, T>::type
            from_internal(internal_t<T>* ptr) { return type_traits<T>::from_internal(ptr); }
        // selected if it doesnt
        template <typename T> static typename std::enable_if<!detector<T>::has_from, T>::type
            from_internal(internal_t<T>* ptr) { return T(*ptr); }

        template <typename T> using use_cells = std::integral_constant<bool, detector<T>::use_cells>;

        // selected if type_traits<T>::destroy exists
        template <typename T> static typename std::enable_if<detector<T>::has_destroy, T>::type
            destroy(internal_t<T>* ptr) { return type_traits<T>::destroy(ptr); }
        // selected if it doesnt
        template <typename T> static typename std::enable_if<!detector<T>::has_destroy, T>::type
            destroy(internal_t<T>* ptr) { delete ptr; mexUnlock(); }
    };

    // TODO: try/catch->err msg?
    template <typename T> static T parse(const mxArray* cell) {
        make_caster<T> caster;
        if (!caster.load(cell))
            /* mexErrMsgTxt("Useful error message") */;
        return T(caster);
    }

    // for trivial types that get sent in "cleartext"
    template <typename T> static typename std::enable_if<is_trivial_t<T>::value, mxArray*>::type wrap(T&& var) {
        mxArray* cell = mxCreateNumericArray(1, 1, to_mx_class<T>::value, mxREAL);
        auto* outp = static_cast<to_value_t<T>::type*>(mxGetData(cell));
        *outp = var;
        return cell;
    }
    // For non-trivial types that get wrapped in arg_t
    template <typename T> static typename std::enable_if<!is_trivial_t<T>::value, mxArray*>::type wrap(T&& var) { 
        mxArray* cell = mxCreateNumericMatrix(1, 1, mxUINT64_CLASS, mxREAL);
        auto* outp = static_cast<uint64_t*>(mxGetData(cell));
        *outp = reinterpret_cast<uint64_t*>(new arg_t(std::move(var)));
        return cell;
    };
    template <typename T> static void destroy(const mxArray* cell) { 
        make_caster<T> caster;
        if (!caster.load(cell))
            /* mexErrMsgTxt("Useful error message") */;
        return caster.destroy();
    }

    template <typename T> static typename std::enable_if<!is_basic_type<T>::value, std::vector<T>>::type parse_array(const mxArray* cells);
    template <typename T> static typename std::enable_if<is_basic_type<T>::value, std::vector<T>>::type parse_array(const mxArray* cells);

    template <typename T> static typename std::enable_if<!is_basic_type<T>::value && !traits_trampoline::use_cells<T>::value, mxArray*>::type wrap_array(const T* var, size_t length);
    template <typename T> static typename std::enable_if<!is_basic_type<T>::value && traits_trampoline::use_cells<T>::value, mxArray*>::type wrap_array(const T* var, size_t length);
    template <typename T> static typename std::enable_if<is_basic_type<T>::value, mxArray*>::type wrap_array(const T* var, size_t length);
};

template <> mxArray* MatlabParamParser::wrap<void_type>(void_type&& var) { return nullptr; }

#include "rs2_type_traits.h"

// simple helper overload to refer std::array and std::vector to wrap_array
template<typename T> struct MatlabParamParser::mx_wrapper_fns<T, typename std::enable_if<is_array_type<T>::value>::type>
{
    static T parse(const mxArray* cell)
    {
        return MatlabParamParser::parse_array<typename is_array_type<T>::inner_t>(cell);
    }
    static mxArray* wrap(T&& var)
    {
        return MatlabParamParser::wrap_array(var.data(), var.size());
    }
};

// overload for wrapping C-strings. TODO: do we need special parsing too?
template<> mxArray* MatlabParamParser::mx_wrapper_fns<const char *>::wrap(const char*&& str)
{
    return mxCreateString(str);
}

template<> std::string MatlabParamParser::mx_wrapper_fns<std::string>::parse(const mxArray* cell)
{
    auto str = mxArrayToString(cell);
    auto ret = std::string(str);
    mxFree(str);
    return ret;
}
template<> mxArray* MatlabParamParser::mx_wrapper_fns<std::string>::wrap(std::string&& str)
{
    return mx_wrapper_fns<const char*>::wrap(str.c_str());
}

template<> struct MatlabParamParser::mx_wrapper_fns<std::chrono::nanoseconds>
{
    static std::chrono::nanoseconds parse(const mxArray* cell)
    {
        auto ptr = static_cast<double*>(mxGetData(cell));
        return std::chrono::nanoseconds{ static_cast<long long>(*ptr * 1e6) }; // convert from milliseconds, smallest time unit that's easy to work with in matlab
    }
    static mxArray* wrap(std::chrono::nanoseconds&& dur)
    {
        auto cell = mxCreateNumericMatrix(1, 1, mxDOUBLE_CLASS, mxREAL);
        auto ptr = static_cast<double*>(mxGetData(cell));
        *ptr = dur.count() / 1.e6; // convert to milliseconds, smallest time unit that's easy to work with in matlab
        return cell;
    }
};

template <typename T> static typename std::enable_if<!is_basic_type<T>::value, std::vector<T>>::type MatlabParamParser::parse_array(const mxArray* cells)
{
    using wrapper_t = mx_wrapper<T>;
    using internal_t = typename type_traits<T>::rs2_internal_t;

    std::vector<T> ret;
    auto length = mxGetNumberOfElements(cells);
    ret.reserve(length);
    auto ptr = static_cast<typename wrapper_t::type*>(mxGetData(cells)); // array of uint64_t's
    for (int i = 0; i < length; ++i) {
        ret.push_back(traits_trampoline::from_internal<T>(reinterpret_cast<internal_t*>(ptr[i])));
    }
    return ret;
}
template <typename T> static typename std::enable_if<is_basic_type<T>::value, std::vector<T>>::type MatlabParamParser::parse_array(const mxArray* cells)
{
    using wrapper_t = mx_wrapper<T>;

    std::vector<T> ret;
    auto length = mxGetNumberOfElements(cells);
    ret.reserve(length);
    auto ptr = static_cast<typename wrapper_t::type*>(mxGetData(cells)); // array of uint64_t's
    for (int i = 0; i < length; ++i) {
        ret.push_back(ptr[i]);
    }
    return ret;
}
template <typename T> static typename std::enable_if<!is_basic_type<T>::value && !MatlabParamParser::traits_trampoline::use_cells<T>::value, mxArray*>::type
MatlabParamParser::wrap_array(const T* var, size_t length)
{
    auto cells = mxCreateNumericMatrix(1, length, MatlabParamParser::mx_wrapper<T>::value::value, mxREAL);
    auto ptr = static_cast<typename mx_wrapper<T>::type*>(mxGetData(cells));
    for (int x = 0; x < length; ++x)
        ptr[x] = reinterpret_cast<typename mx_wrapper<T>::type>(traits_trampoline::to_internal<T>(T(var[x])));
    
    return cells;
}

template <typename T> static typename std::enable_if<!is_basic_type<T>::value && MatlabParamParser::traits_trampoline::use_cells<T>::value, mxArray*>::type
MatlabParamParser::wrap_array(const T* var, size_t length)
{
    auto cells = mxCreateCellMatrix(1, length);
    for (int x = 0; x < length; ++x)
        mxSetCell(cells, x, wrap(T(var[x])));

    return cells;
}

template <typename T> static typename std::enable_if<is_basic_type<T>::value, mxArray*>::type MatlabParamParser::wrap_array(const T* var, size_t length)
{
    auto cells = mxCreateNumericMatrix(1, length, MatlabParamParser::mx_wrapper<T>::value::value, mxREAL);
    auto ptr = static_cast<typename mx_wrapper<T>::type*>(mxGetData(cells));
    for (int x = 0; x < length; ++x)
        ptr[x] = typename mx_wrapper<T>::type(var[x]);

    return cells;
}
#include "types.h"
