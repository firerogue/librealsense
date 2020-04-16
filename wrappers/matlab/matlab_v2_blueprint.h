// This file suggests the skeleton of how type loading could work in the next version of the Matlab binding framework
// with type validation done by the C++ side.

#include <typeinfo>
#include <cstdint>
#include <vector>
#include <string>
#include <functional>

#include <mex.h>
#include <matrix.h>

// This function gets a C++ std::type_info object and returns our type_data object,
// which contains many details relevant to the functioning of the binding layer
detail::type_data* get_type_data(const std::type_info& tinfo);

struct value_and_holder; // some type defined in pybind11, still haven't figured out exactly what it does and whether or not we need it

// union type for storing C++ objects after they've been extracted from Matlab
union value_t {
    int8_t i8; value_t(int8_t i8) : i8(i8) {}
    uint8_t ui8; value_t(uint8_t ui8) : ui8(ui8) {}
    /* ... */
    void * ptr; value_t(void* ptr) : ptr(ptr) {}
};

namespace detail {
    // This struct contains a bunch of details about a type that are important to
    // making the binding layer work. How to populate this will be the main difficulty,
    // but probably the matbind::class_<> and .def(), etc will be in charge of this.
    struct type_data {
        std::type_info* cpptype;
        std::vector<std::pair<std::type_info*, value_t(*)(value_t)>> implicit_casts;
        std::vector<std::function<bool(void*)>> fields;
        void*(*constructor)();
    };

    // When sending a C++ class to matlab, its wrapped in this class first to inject RTTI.
    struct handle {
        const std::type_info* tinfo;
        void * value;
    };
}

// Class in charge of actually extracting C++ objects from matlab
class type_caster_generic {
private:
    const std::type_info* cpptype; // C++ RTTI
    const detail::type_data* tdata; // Out struct with important details
    value_t value{nullptr}; // Actual value once loaded
    // TODO: destructor for value.ptr?
public:
    // Constructors
    type_caster_generic(const std::type_info &type_info)
        : tdata(get_type_data(type_info)), cpptype(&type_info) { }

    type_caster_generic(const detail::type_data *tdata)
        : tdata(tdata), cpptype(tdata ? tdata->cpptype : nullptr) { }

    // public facing function to load type from Matlab
    bool load(mxArray* src, bool convert) {
        load_impl<type_caster_generic>(src, convert);
    };

    // cast is for converting to holder type. as in, matlabParamParser::wrap (still not really sure how it's supposed to work)
    static mxArray* cast(const void* _src, mxArray* parent, void*(*copy_ctor)(const void*), void*(*move_ctor)(const void*), const void* existing_holder = nullptr);
private:
    void load_value(value_and_holder &&v_h); // I'd need to know how value_and_holder applies in pybind11 to know if we need this

    // Lifted directly from pybind11, logic for loading arguments of Type1 from 
    // an object of Type2 when there's a way to implicitly convert Type2->Type1
    bool try_implicit_casts(mxArray* src, bool convert) {
        for (auto &cast : tdata->implicit_casts) {
            type_caster_generic sub_caster(*cast.first);
            if (sub_caster.load(src, convert)) {
                value = cast.second(sub_caster.value);
                return true;
            }
        }
        return false;
    }


    // bool try_direct_conversions(mxArray* src, bool convert); // function in pybind11 for direct conversions. I think this is if you use py::is_convertible<Type1, Type2>?
    bool try_load_foreign_module_local(handle src);

    // Where the actual value loading happens. I'm not 100% sure why the template arg is here, but I think it has something to do with copy and move constructible objects?
    template <typename ThisT> 
    bool load_impl(mxArray* src, bool convert) {
        // TODO: dispose of old values properly
        auto cls = mxGetClassID(src);
        // Select what to do based on the Matlab type of src
        switch (cls) {
        case mxUNKOWN_CLASS:
            mexErrMsgTxt("Error parsing type of argument");
            return false; // mxClassID error
        case mxCell_CLASS:
            // TODO: this one
            break;
        case mxSTRUCT_CLASS:
            return load_obj(src, convert);
        case mxFUNCTION_CLASS:
            // TODO: this one
            break;
        case mxCHAR_CLASS:
            return load_string(src, convert);
        default:
            return load_numeric(src, convert);
        }
    }

    // this loads simple numeric types (e.g. int, float, etc)
    bool load_numeric(mxArray* src, bool convert) {
        // TODO: Handle non-scalar and complex cases?
        auto cls = mxGetClassID(src);
        switch (cls) {
        // in each case, set value correctly, the return true if this is an exact type match.
        case mxLOGICAL_CLASS:
            value = value_t(*static_cast<mxLogical*>(mxGetData(src)));
            if (*cpptype == typeid(mxLogical)) return true;
            break;
        case mxDOUBLE_CLASS:
            value = value_t(*static_cast<mxDouble*>(mxGetData(src)));
            if (*cpptype == typeid(mxDouble)) return true;
            break;
        case mxSINGLE_CLASS:
            value = value_t(*static_cast<mxSingle*>(mxGetData(src)));
            if (*cpptype == typeid(mxSingle)) return true;
            break;
        case mxINT8_CLASS:
            value = value_t(*static_cast<mxInt8*>(mxGetData(src)));
            if (*cpptype == typeid(mxInt8)) return true;
            break;
        case mxUINT8_CLASS:
            value = value_t(*static_cast<mxUint8*>(mxGetData(src)));
            if (*cpptype == typeid(mxUint8)) return true;
            break;
        case mxINT16_CLASS:
            value = value_t(*static_cast<mxInt16*>(mxGetData(src)));
            if (*cpptype == typeid(mxInt16)) return true;
            break;
        case mxUINT16_CLASS:
            value = value_t(*static_cast<mxUint16*>(mxGetData(src)));
            if (*cpptype == typeid(mxUint16)) return true;
            break;
        case mxINT32_CLASS:
            value = value_t(*static_cast<mxInt32*>(mxGetData(src)));
            if (*cpptype == typeid(mxInt32)) return true;
            break;
        case mxUINT32_CLASS:
            value = value_t(*static_cast<mxUint32*>(mxGetData(src)));
            if (*cpptype == typeid(mxUint32)) return true;
            break;
        case mxINT64_CLASS:
            value = value_t(*static_cast<mxInt64*>(mxGetData(src)));
            if (*cpptype == typeid(mxInt64)) return true;
            break;
        case mxUINT64_CLASS:
            value = value_t(*static_cast<mxUint64*>(mxGetData(src)));
            if (*cpptype == typeid(mxUint64)) return true;
            break;
            
        default: return false;
        }

        // All numeric types can be implicitly converted amongst one another,
        // so return true.
        // TODO: Warn on narrowing conversion?
        // TODO: Enums
        return convert;
    }

    // Strings!
    bool load_string(mxArray* src, bool convert) {
        // TODO: Validation
        // TODO: just put str in value.ptr and set the destroyer to be mxFree?
        auto str = mxArrayToString(src);
        value = value_t(new std::string(str));
        mxFree(str);
        return true;
    }

    // Static helper function for creating subcasters for reading individual fields out of a mxStruct.
    template <typename T>
    static T read_field(mxArray* src, bool convert, const char* fieldName, const char* errMsg) {
        type_caster_generic caster(typeid(T));
        auto mxptr = mxGetField(src, 0, fieldName);
        if (!mxptr || !caster.load(mxptr, convert)) {
            mexErrMsgTxt(errMsg);
        }
        return caster.get<T>();
    }

    // Loads C++ classes and C structs. TODO: Possible to combine logic paths?
    bool load_obj(mxArray* src, bool convert) {
        auto StructType_f = read_field<std::string>(src, false, "mxStructType",
                                                    "Failed to obtain struct type data");
        if (StructType_f == "struct") {
            return load_struct(src, convert);
        } else if (StructType_f == "class") {
            return load_class(src, convert);
        }
    }

    // Verifies struct type, creates a new C++ object, and populates the
    // C++ object's fields from the Matlab object's fields.
    // TODO: mxTypeData or mxTypeName? TypeName would allow pure-matlab functions to create skeleton structs,
    //       whereas TypeData would require a call to C++ machinery, but potentially be less abusable?
    bool load_struct(mxArray* src, bool convert) {
        auto mxptr = mxGetField(src, 0, "mxTypeData");
        // TODO: Split out the various errors?
        if (!mxptr || mxGetClassID(mxptr) != mxUINT64_CLASS || !mxIsScalar(mxptr) || mxIsComplex(mxptr))
            mexErrMsgTxt("Failed to obtain struct type data");
        auto TypeData_f = reinterpret_cast<std::type_info*>(mxGetData(mxptr));
        if (*TypeData_f == *cpptype) {
            void * structure = tdata->constructor();
            for (auto field : tdata->fields) {
                if (!field(structure)) // load the fields
                    return false;
            }
            return true;
        }
        // TODO: conversions
        return false;
    }

    bool load_class(mxArray* src, bool convert) {
        auto mxptr = mxGetField(src, 0, "mxHandle");
        // TODO: Split out the various errors?
        if (!mxptr mxGetClassID(mxptr) != mxUINT64_CLASS || !mxIsScalar(mxptr) || mxIsComplex(mxptr))
            mexErrMsgTxt("Failed to obtain object handle");
        auto Handle_f = *reinterpret_cast<detail::handle*>(mxGetData(mxptr));
        if (*cpptype == *Handle_f.tinfo) {
            value = value_t(Handle_f.value);
            return true;
        }

        // TODO: conversions
        return false;
    }
};
