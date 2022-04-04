#include "trap_context.hpp"

namespace tep
{
    template<typename T>
    trap_context::trap_context(T x) :
        self_(std::make_shared<model_t<T>>(std::move(x)))
    {}

    template<typename T>
    trap_context::model_t<T>::model_t(T x)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        :
        data_(std::move(x))
    {}

    template<typename T>
    uintptr_t trap_context::model_t<T>::addr() const noexcept
    {
        return data_.value;
    }

    template<typename T>
    bool trap_context::model_t<T>::is_function_call() const noexcept
    {
        return data_.is_function_call();
    }

    template<typename T>
    void trap_context::model_t<T>::print(std::ostream& os) const
    {
        os << data_;
    }

    template<typename T>
    void trap_context::model_t<T>::print(output_writer& ow) const
    {
        ow << data_;
    }

    template<typename T>
    std::string trap_context::model_t<T>::as_string() const
    {
        return to_string(data_);
    }
}
