// holder.hpp

#pragma once

#include <cinttypes>
#include <vector>

namespace nrgprf
{

    template<typename T>
    class holder
    {
    protected:
        uint32_t _id;
        std::vector<T> _values;

    public:
        holder(uint32_t id);

        void reserve(uint32_t count);
        uint32_t id() const;
        size_t size() const;

        template<typename... Args>
        size_t add(Args&&... args);

        const T& get(size_t idx) const;
        const T& first() const;
        const T& last() const;

        T& get(size_t idx);
        T& first();
        T& last();
    };

}

// implementation

template<typename T>
nrgprf::holder<T>::holder(uint32_t id) :
    _id(id)
{
}

template<typename T>
void nrgprf::holder<T>::reserve(uint32_t count)
{
    _values.reserve(count);
}

template<typename T>
uint32_t nrgprf::holder<T>::id() const
{
    return _id;
}

template<typename T>
size_t nrgprf::holder<T>::size() const
{
    return _values.size();
}

template<typename T>
template<typename... Args>
size_t nrgprf::holder<T>::add(Args&&... args)
{
    _values.emplace_back(std::forward<Args>(args)...);
    return _values.size() - 1;
}

template<typename T>
const T& nrgprf::holder<T>::get(size_t idx) const
{
    return _values[idx];
}

template<typename T>
const T& nrgprf::holder<T>::first() const
{
    return _values.front();
}

template<typename T>
const T& nrgprf::holder<T>::last() const
{
    return _values.back();
}

template<typename T>
T& nrgprf::holder<T>::get(size_t idx)
{
    return _values[idx];
}

template<typename T>
T& nrgprf::holder<T>::first()
{
    return _values.front();
}

template<typename T>
T& nrgprf::holder<T>::last()
{
    return _values.back();
}
