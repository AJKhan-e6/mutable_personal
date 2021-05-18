#pragma once

#include <mutable/util/macro.hpp>
#include <mutable/util/some.hpp>
#include <mutable/util/tag.hpp>
#include <optional>
#include <type_traits>


namespace m {

/** Visitor base class. */
template<typename V, typename Base>
struct Visitor
{
    /** Whether the visited objects are `const`-qualified. */
    static constexpr bool is_const = std::is_const_v<Base>;

    /** The base class of the class hierarchy to visit. */
    using base_type = Base;

    /** The concrete type of the visitor.  Uses the CRTP design. */
    using visitor_type = V;

    /** A helper type to apply the proper `const`-qualification to parameters. */
    template<typename T>
    using Const = std::conditional_t<is_const, const T, T>;

    /** Visit the object `obj`. */
    void operator()(base_type &obj) { static_cast<V*>(this)->operator()(obj); }
};

/*----- Generate a function similar to `std::visit` to easily implement a visitor for the given base class. ----------*/
#define M_GET_INVOKE_RESULT(CLASS) std::invoke_result_t<Vis, Const<CLASS>&>,
#define M_MAKE_STL_VISIT_METHOD(CLASS) void operator()(Const<CLASS> &obj) { \
    if constexpr (std::is_same_v<void, result_type>) { vis(obj); } \
    else { result = vis(obj); } \
}
#define M_MAKE_STL_VISITABLE(VISITOR, BASE_CLASS, CLASS_LIST) \
    template<typename Vis> \
    auto visit(Vis &&vis, BASE_CLASS &obj, tag<VISITOR>&& = tag<VISITOR>()) { \
        struct V : VISITOR { \
            using result_type = std::common_type_t< CLASS_LIST(M_GET_INVOKE_RESULT) std::invoke_result_t<Vis, Const<EVAL(DEFER1(FIRST)(CLASS_LIST(COMMA)))>&> >; \
            std::optional<some<result_type>> result; \
            Vis &&vis; \
            V(Vis &&vis) : vis(std::forward<Vis>(vis)) { } \
            using VISITOR::operator(); \
            CLASS_LIST(M_MAKE_STL_VISIT_METHOD) \
        }; \
        V v(std::forward<Vis>(vis)); \
        v(obj); \
        if constexpr (not std::is_same_v<void, typename V::result_type>) \
            return *std::move(v.result); \
    }

/*----- Declare a visitor to visit the class hierarchy with the given base class and list of subclasses. -------------*/
#define M_DECLARE_VISIT_METHOD(CLASS) virtual void operator()(Const<CLASS>&) = 0;
#define M_DECLARE_VISITOR(NAME, BASE_CLASS, CLASS_LIST) \
    struct NAME : Visitor<NAME, BASE_CLASS> \
    { \
        using super = Visitor<NAME, BASE_CLASS>; \
        template<typename T> using Const = typename super::Const<T>; \
        virtual ~NAME() {} \
        void operator()(BASE_CLASS &obj) { obj.accept(*this); } \
        CLASS_LIST(M_DECLARE_VISIT_METHOD) \
    }; \
    M_MAKE_STL_VISITABLE(NAME, BASE_CLASS, CLASS_LIST)

}
