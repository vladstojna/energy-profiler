// units.hpp

#pragma once

#include <ratio>
#include <chrono>

namespace nrgprf
{

    template<template<typename, typename> typename U, typename Rep, typename Ratio = std::ratio<1>>
    class scalar_unit;

    template<typename Rep, typename Ratio = std::ratio<1>>
    class energy_unit;

    template<typename Rep, typename Ratio = std::ratio<1>>
    class power_unit;

    template<typename ToUnit, typename Rep, typename Ratio>
    constexpr ToUnit unit_cast(const energy_unit<Rep, Ratio>& unit);

    template<typename ToUnit, typename Rep, typename Ratio>
    constexpr ToUnit unit_cast(const power_unit<Rep, Ratio>& unit);


    namespace detail
    {
        template<intmax_t a, intmax_t b>
        struct gcd
        {
            static constexpr intmax_t value = gcd<b, a% b>::value;
        };

        template<intmax_t a>
        struct gcd<a, 0>
        {
            static constexpr intmax_t value = a;
        };

        template<intmax_t b>
        struct gcd<0, b>
        {
            static constexpr intmax_t value = b;
        };

        template<template<typename, typename> typename U, typename CT, typename Ratio1, typename Ratio2>
        struct common_type_impl
        {
        private:
            using gcd_num = gcd<Ratio1::num, Ratio2::num>;
            using gcd_den = gcd<Ratio1::den, Ratio2::den>;
            using common_rep = typename CT::type;
            using common_rat = std::ratio<gcd_num::value, Ratio1::den* Ratio2::den / gcd_den::value>;
        public:
            using type = U<common_rep, common_rat>;
        };

        template<typename... T>
        struct common_type
        {};

        template<typename Rep1, typename Ratio1, typename Rep2, typename Ratio2>
        struct common_type<energy_unit<Rep1, Ratio1>, energy_unit<Rep2, Ratio2>>
            : common_type_impl<energy_unit, std::common_type<Rep1, Rep2>, Ratio1, Ratio2>
        {};

        template<typename Rep1, typename Ratio1, typename Rep2, typename Ratio2>
        struct common_type<power_unit<Rep1, Ratio1>, power_unit<Rep2, Ratio2>>
            : common_type_impl<power_unit, std::common_type<Rep1, Rep2>, Ratio1, Ratio2>
        {};

        template<template<typename, typename> typename Unit,
            typename ToRep,
            typename ToRatio,
            typename FromRep,
            typename FromRatio>
            constexpr Unit<ToRep, ToRatio> unit_cast(const Unit<FromRep, FromRatio>& from)
        {
            using ratio = typename std::ratio_divide<FromRatio, ToRatio>::type;
            using common_type = typename std::common_type<ToRep, FromRep, uintmax_t>::type;
            return Unit<ToRep, ToRatio>(static_cast<ToRep>(
                static_cast<common_type>(from.count()) * static_cast<common_type>(ratio::num) / static_cast<common_type>(ratio::den)
                ));
        }

        template<typename... Cond>
        using _or = std::disjunction<Cond...>;

        template<typename... Cond>
        using _and = std::conjunction<Cond...>;

        template<typename Cond>
        using _not = std::negation<Cond>;

        template<typename... Cond>
        using require = std::enable_if_t<_and<Cond...>::value, bool>;

        template<typename Rep>
        struct is_float : std::is_floating_point<Rep> {};

        template<typename Ratio1, typename Ratio2>
        struct is_divisible
        {
            constexpr inline static bool value = std::ratio_divide<Ratio1, Ratio2>::den == 1;
        };
    }


    template<template<typename, typename> typename U, typename Rep, typename Ratio>
    class scalar_unit
    {
    public:
        using rep = Rep;
        using ratio = Ratio;
        using unit = U<Rep, Ratio>;

    protected:
        constexpr scalar_unit() = default;
        constexpr explicit scalar_unit(rep count) :
            _count(count)
        {}

    public:
        constexpr rep count() const
        {
            return _count;
        }

        constexpr unit& operator+=(const unit& other)
        {
            _count += other._count;
            return static_cast<unit&>(*this);
        }

        constexpr unit& operator-=(const unit& other)
        {
            _count -= other._count;
            return static_cast<unit&>(*this);
        }

        template<typename Rep2>
        constexpr unit& operator/=(const Rep2& other)
        {
            _count /= other;
            return static_cast<unit&>(*this);
        }

        template<typename Rep2>
        constexpr unit& operator*=(const Rep2& other)
        {
            _count *= other;
            return static_cast<unit&>(*this);
        }

    private:
        rep _count;
    };

    template<typename Rep, typename Ratio>
    class energy_unit : public scalar_unit<energy_unit, Rep, Ratio>
    {
    public:
        constexpr energy_unit() = default;
        constexpr explicit energy_unit(Rep count) :
            scalar_unit<energy_unit, Rep, Ratio>(count)
        {}

        template<
            typename Rep2,
            typename Ratio2,
            detail::require<
            std::is_convertible<const Rep2&, Rep>,
            detail::_or<
            detail::is_float<Rep>,
            detail::_and<
            detail::is_divisible<Ratio2, Ratio>,
            detail::_not<detail::is_float<Rep2>
            >>>> = true>
            constexpr energy_unit(const energy_unit<Rep2, Ratio2>& other) :
            energy_unit(nrgprf::unit_cast<energy_unit>(other).count())
        {}
    };

    template<typename Rep, typename Ratio>
    class power_unit : public scalar_unit<power_unit, Rep, Ratio>
    {
    public:
        constexpr power_unit() = default;
        constexpr explicit power_unit(Rep count) :
            scalar_unit<power_unit, Rep, Ratio>(count)
        {}

        template<
            typename Rep2,
            typename Ratio2,
            detail::require<
            std::is_convertible<const Rep2&, Rep>,
            detail::_or<
            detail::is_float<Rep>,
            detail::_and<
            detail::is_divisible<Ratio2, Ratio>,
            detail::_not<detail::is_float<Rep2>
            >>>> = true>
            constexpr power_unit(const power_unit<Rep2, Ratio2>& other) :
            power_unit(nrgprf::unit_cast<power_unit>(other).count())
        {}
    };


    template<typename Rep>
    using watts = power_unit<Rep>;
    template<typename Rep>
    using milliwatts = power_unit<Rep, std::milli>;
    template<typename Rep>
    using microwatts = power_unit<Rep, std::micro>;
    template<typename Rep>
    using nanowatts = power_unit<Rep, std::nano>;

    template<typename Rep>
    using joules = energy_unit<Rep>;
    template<typename Rep>
    using millijoules = energy_unit<Rep, std::milli>;
    template<typename Rep>
    using microjoules = energy_unit<Rep, std::micro>;
    template<typename Rep>
    using nanojoules = energy_unit<Rep, std::nano>;


    template<typename ToUnit, typename Rep, typename Ratio>
    constexpr ToUnit unit_cast(const energy_unit<Rep, Ratio>& unit)
    {
        return detail::unit_cast<energy_unit, typename ToUnit::rep, typename ToUnit::ratio, Rep, Ratio>(unit);
    }

    template<typename ToUnit, typename Rep, typename Ratio>
    constexpr ToUnit unit_cast(const power_unit<Rep, Ratio>& unit)
    {
        return detail::unit_cast<power_unit, typename ToUnit::rep, typename ToUnit::ratio, Rep, Ratio>(unit);
    }

    template<template<typename, typename> typename U, typename LhsRep, typename LhsRatio, typename RhsRep, typename RhsRatio>
    constexpr bool operator==(const U<LhsRep, LhsRatio>& lhs, const U<RhsRep, RhsRatio>& rhs)
    {
        using lhs_unit = U<LhsRep, LhsRatio>;
        using rhs_unit = U<RhsRep, RhsRatio>;
        using common_type = typename detail::common_type<lhs_unit, rhs_unit>::type;
        return common_type(lhs).count() == common_type(rhs).count();
    }

    template<template<typename, typename> typename U, typename LhsRep, typename LhsRatio, typename RhsRep, typename RhsRatio>
    constexpr bool operator!=(const U<LhsRep, LhsRatio>& lhs, const U<RhsRep, RhsRatio>& rhs)
    {
        return !(lhs == rhs);
    }

    template<template<typename, typename> typename U, typename LhsRep, typename LhsRatio, typename RhsRep, typename RhsRatio>
    constexpr bool operator<(const U<LhsRep, LhsRatio>& lhs, const U<RhsRep, RhsRatio>& rhs)
    {
        using lhs_unit = U<LhsRep, LhsRatio>;
        using rhs_unit = U<RhsRep, RhsRatio>;
        using common_type = typename detail::common_type<lhs_unit, rhs_unit>::type;
        return common_type(lhs).count() < common_type(rhs).count();
    }

    template<template<typename, typename> typename U, typename LhsRep, typename LhsRatio, typename RhsRep, typename RhsRatio>
    constexpr bool operator<=(const U<LhsRep, LhsRatio>& lhs, const U<RhsRep, RhsRatio>& rhs)
    {
        using lhs_unit = U<LhsRep, LhsRatio>;
        using rhs_unit = U<RhsRep, RhsRatio>;
        using common_type = typename detail::common_type<lhs_unit, rhs_unit>::type;
        return common_type(lhs).count() <= common_type(rhs).count();
    }

    template<template<typename, typename> typename U, typename LhsRep, typename LhsRatio, typename RhsRep, typename RhsRatio>
    constexpr bool operator>(const U<LhsRep, LhsRatio>& lhs, const U<RhsRep, RhsRatio>& rhs)
    {
        return !(lhs <= rhs);
    }

    template<template<typename, typename> typename U, typename LhsRep, typename LhsRatio, typename RhsRep, typename RhsRatio>
    constexpr bool operator>=(const U<LhsRep, LhsRatio>& lhs, const U<RhsRep, RhsRatio>& rhs)
    {
        return !(lhs < rhs);
    }

    template<template<typename, typename> typename U, typename LhsRep, typename LhsRatio, typename RhsRep, typename RhsRatio>
    constexpr typename detail::common_type<U<LhsRep, LhsRatio>, U<RhsRep, RhsRatio>>::type
        operator+(const U<LhsRep, LhsRatio>& lhs, const U<RhsRep, RhsRatio>& rhs)
    {
        using lhs_unit = U<LhsRep, LhsRatio>;
        using rhs_unit = U<RhsRep, RhsRatio>;
        using common_type = typename detail::common_type<lhs_unit, rhs_unit>::type;
        return common_type(common_type(lhs).count() + common_type(rhs).count());
    }

    template<template<typename, typename> typename U, typename LhsRep, typename LhsRatio, typename RhsRep, typename RhsRatio>
    constexpr typename detail::common_type<U<LhsRep, LhsRatio>, U<RhsRep, RhsRatio>>::type
        operator-(const U<LhsRep, LhsRatio>& lhs, const U<RhsRep, RhsRatio>& rhs)
    {
        using lhs_unit = U<LhsRep, LhsRatio>;
        using rhs_unit = U<RhsRep, RhsRatio>;
        using common_type = typename detail::common_type<lhs_unit, rhs_unit>::type;
        return common_type(common_type(lhs).count() - common_type(rhs).count());
    }

    template<template<typename, typename> typename U, typename LhsRep, typename LhsRatio, typename RhsRep>
    constexpr U<typename std::common_type<LhsRep, RhsRep>::type, LhsRatio>
        operator*(const U<LhsRep, LhsRatio>& lhs, const RhsRep& rhs)
    {
        using common_type = typename std::common_type<LhsRep, RhsRep>::type;
        return U<common_type, LhsRatio>(lhs.count() * rhs);
    }

    template<template<typename, typename> typename U, typename LhsRep, typename LhsRatio, typename RhsRep>
    constexpr U<typename std::common_type<LhsRep, RhsRep>::type, LhsRatio>
        operator/(const U<LhsRep, LhsRatio>& lhs, const RhsRep& rhs)
    {
        using common_type = typename std::common_type<LhsRep, RhsRep>::type;
        return U<common_type, LhsRatio>(lhs.count() / rhs);
    }

    template<typename Rep, typename Ratio, typename DRep, typename DPeriod>
    constexpr energy_unit<double, Ratio>
        operator*(const power_unit<Rep, Ratio>& lhs, const std::chrono::duration<DRep, DPeriod>& rhs)
    {
        std::chrono::duration<double> secs = std::chrono::duration_cast<decltype(secs)>(rhs);
        return energy_unit<double, Ratio>(lhs.count() * secs.count());
    }

    template<typename Rep, typename Ratio, typename DRep, typename DPeriod>
    constexpr energy_unit<double, Ratio>
        operator*(const std::chrono::duration<DRep, DPeriod>& lhs, const power_unit<Rep, Ratio>& rhs)
    {
        return rhs * lhs;
    }

    template<typename Rep, typename Ratio, typename DRep, typename DPeriod>
    constexpr power_unit<double, Ratio>
        operator/(const energy_unit<Rep, Ratio>& lhs, const std::chrono::duration<DRep, DPeriod>& rhs)
    {
        std::chrono::duration<double> secs = std::chrono::duration_cast<decltype(secs)>(rhs);
        return power_unit<double, Ratio>(lhs.count() / secs.count());
    }

}
