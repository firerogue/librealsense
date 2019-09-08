#ifndef __FACTORY_H__
#define __FACTORY_H__

#include <functional>
#include <map>
#include <mex.h>
#include <matrix.h>
#include "autoargs.h"

class ClassFactory
{
private:
    std::string name;
    std::map<std::string, func_data> funcs;
public:
    ClassFactory(std::string n) : name(n), funcs() {}

    template <typename Func>
    void record(std::string fname, size_t in, Func&& func)
    {
        record(fname, in, in, std::move(func));
    }
    template <typename Func>
    void record(std::string fname, size_t in_min, size_t in_max, Func&& func)
    {
        funcs.emplace(fname, func_data(in_min, in_max, func));
    }
    void record(std::string fname, size_t out, size_t min_in, size_t max_in, std::function<mxFunc> func)
    {
        funcs.emplace(fname, func_data(out, min_in, max_in, func));
    }

    std::string get_name() { return name; }

    func_data get(std::string f){
        auto func = funcs.find(f);
        if (func == funcs.end()) return func_data();
        return func->second;
    }
};

class Factory
{
private:
    std::map<std::string, ClassFactory> classes;
public:
    Factory() = default;
    void record(ClassFactory cls) { classes.emplace(cls.get_name(), cls); }

    func_data get(std::string c, std::string f){
        auto cls = classes.find(c);
        if (cls == classes.end()) return func_data();
        return cls->second.get(f);
    }
};

#endif