#pragma once

#include "visibility.hpp"

#include <nrg/error.hpp>
#include <nonstd/expected.hpp>

#include <utility>

namespace nrgprf
{
    template<typename R, typename... Args>
    NRG_LOCAL result<R> create_reader_impl(std::ostream& os, Args... args)
    {
        error err = error::success();
        R reader(args..., err, os);
        if (err)
            return result<R>{ nonstd::unexpect, std::move(err) };
        return reader;
    }
}
