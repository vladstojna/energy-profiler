#!/usr/bin/env python3

import fractions
from typing import Dict, Iterable, Tuple

nano = fractions.Fraction(1, 1000000000)
micro = fractions.Fraction(1, 1000000)
milli = fractions.Fraction(1, 1000)
base = fractions.Fraction(1, 1)
kilo = fractions.Fraction(1000, 1)
mega = fractions.Fraction(1000000, 1)
giga = fractions.Fraction(1000000000, 1)

Fraction = fractions.Fraction

_generic_units = [
    ("n", nano),
    ("u", micro),
    ("m", milli),
    ("", base),
    ("k", kilo),
    ("M", mega),
    ("G", giga),
]


def _generate_map(suffix: str) -> Dict[str, fractions.Fraction]:
    return {frac: pref + suffix for pref, frac in _generic_units}


class invalid_unit(Exception):
    def __init__(self, message) -> None:
        self.message = message


def _convert(from_unit, to_unit):
    if from_unit.fraction == to_unit:
        return
    if from_unit.units.get(to_unit) == None:
        raise invalid_unit("Unit not supported")
    tmp = from_unit.fraction / to_unit
    from_unit.value *= tmp.numerator
    from_unit.value /= tmp.denominator
    from_unit.fraction = to_unit


def _common_unit(lhs, rhs) -> fractions.Fraction:
    if type(lhs) != type(rhs):
        raise invalid_unit("Invalid common units '{}' and '{}'".format(lhs, rhs))
    if lhs.fraction == rhs.fraction:
        return lhs.fraction
    return rhs.fraction if rhs.fraction < lhs.fraction else lhs.fraction


def _assert_types(lhs, tp, comment: str) -> None:
    if not isinstance(lhs, tp):
        raise TypeError(comment)


class _scalar_unit:
    def __init__(self, value, fraction):
        self.value = value
        self.fraction = fraction

    def __bool__(self):
        return self.value != 0

    def __str__(self):
        return "{} {}".format(self.value, self.units[self.fraction])

    def convert(self, to_frac):
        _convert(self, to_frac)
        return self


class Time(_scalar_unit):
    units = _generate_map("s")

    def __init__(self, value, fraction=base):
        if self.units.get(fraction) == None:
            raise invalid_unit("Unsupported fraction {}".format(fraction))
        super().__init__(value, fraction)

    def __add__(self, other):
        _assert_types(self, type(other), "can only add two timestamps")
        common = _common_unit(self, other)
        return Time(self.copy_as(common).value + other.copy_as(common).value, common)

    def __sub__(self, other):
        _assert_types(self, type(other), "can only sub two timestamps")
        common = _common_unit(self, other)
        return Time(self.copy_as(common).value - other.copy_as(common).value, common)

    def __floordiv__(self, scalar):
        return Time(self.value // scalar, self.fraction)

    def __truediv__(self, scalar):
        return Time(self.value / scalar, self.fraction)

    def __mul__(self, scalar):
        return Time(self.value * scalar, self.fraction)

    def __iadd__(self, other):
        _assert_types(self, type(other), "can only add two timestamps")
        self.value += other.copy_as(self.fraction).value
        return self

    def __isub__(self, other):
        _assert_types(self, type(other), "can only sub two timestamps")
        self.value -= other.copy_as(self.fraction).value
        return self

    def __idiv__(self, scalar):
        self.value /= scalar
        return self

    def __imul__(self, scalar):
        self.value *= scalar
        return self

    def copy_as(self, to_unit):
        copy = Time(self.value, self.fraction)
        _convert(copy, to_unit)
        return copy


class Energy(_scalar_unit):
    units = _generate_map("J")

    def __init__(self, value, fraction=base):
        if self.units.get(fraction) == None:
            raise invalid_unit("Unsupported fraction {}".format(fraction))
        super().__init__(value, fraction)

    def __add__(self, other):
        _assert_types(self, type(other), "can only add two energies")
        common = _common_unit(self, other)
        return Energy(self.copy_as(common).value + other.copy_as(common).value, common)

    def __sub__(self, other):
        _assert_types(self, type(other), "can only sub two energies")
        common = _common_unit(self, other)
        return Energy(self.copy_as(common).value - other.copy_as(common).value, common)

    def __floordiv__(self, scalar):
        return Energy(self.value // scalar, self.fraction)

    def __truediv__(self, scalar):
        return Energy(self.value / scalar, self.fraction)

    def __mul__(self, scalar):
        return Energy(self.value * scalar, self.fraction)

    def __iadd__(self, other):
        _assert_types(self, type(other), "can only add two energies")
        self.value += other.copy_as(self.fraction).value
        return self

    def __isub__(self, other):
        _assert_types(self, type(other), "can only sub two energies")
        self.value -= other.copy_as(self.fraction).value
        return self

    def __idiv__(self, scalar):
        self.value /= scalar
        return self

    def __imul__(self, scalar):
        self.value *= scalar
        return self

    def copy_as(self, to_unit):
        copy = Energy(self.value, self.fraction)
        _convert(copy, to_unit)
        return copy


class Power(_scalar_unit):
    units = _generate_map("W")

    def __init__(self, value, fraction=base):
        if self.units.get(fraction) == None:
            raise invalid_unit("Unsupported fraction {}".format(fraction))
        super().__init__(value, fraction)

    def __add__(self, other):
        _assert_types(self, type(other), "can only add two powers")
        common = _common_unit(self, other)
        return Power(self.copy_as(common).value + other.copy_as(common).value, common)

    def __sub__(self, other):
        _assert_types(self, type(other), "can only sub two powers")
        common = _common_unit(self, other)
        return Power(self.copy_as(common).value - other.copy_as(common).value, common)

    def __floordiv__(self, scalar):
        return Power(self.value // scalar, self.fraction)

    def __truediv__(self, scalar):
        return Power(self.value / scalar, self.fraction)

    def __mul__(self, scalar):
        return Power(self.value * scalar, self.fraction)

    def __iadd__(self, other):
        _assert_types(self, type(other), "can only add two powers")
        self.value += other.copy_as(self.fraction).value
        return self

    def __isub__(self, other):
        _assert_types(self, type(other), "can only sub two powers")
        self.value -= other.copy_as(self.fraction).value
        return self

    def __idiv__(self, scalar):
        self.value /= scalar
        return self

    def __imul__(self, scalar):
        self.value *= scalar
        return self

    def copy_as(self, to_unit):
        copy = Power(self.value, self.fraction)
        _convert(copy, to_unit)
        return copy


def div(energy: Energy, time: Time) -> Power:
    _assert_types(energy, Energy, "can only divide energy by time")
    _assert_types(time, Time, "can only divide energy by time")
    fraction = energy.fraction / time.fraction
    if fraction in Energy.units:
        return Power(energy.value * time.value, fraction)
    raise invalid_unit("Cannot divide '{}' by '{}', unsupported".format(energy, time))


def mul(power: Power, time: Time) -> Energy:
    _assert_types(power, Power, "can only multiply power by time")
    _assert_types(time, Time, "can only power power by time")
    fraction = power.fraction * time.fraction
    if fraction in Energy.units:
        return Energy(power.value * time.value, fraction)
    raise invalid_unit("Cannot multiply '{}' by '{}', unsupported".format(power, time))


def integrate_power_series(
    series: Iterable[Tuple[Power, Time]], to_unit: fractions.Fraction = base
) -> Energy:
    result = Energy(0, to_unit)
    for ix, (p, t) in enumerate(series[1:], start=1):
        pwr = p + series[ix - 1][0]
        pwr /= 2
        tm = t - series[ix - 1][1]
        result += mul(pwr, tm)
    return result


def main():
    import random

    series = [(Power(random.randint(0, 100)), Time(i)) for i in range(50)]
    print(integrate_power_series(series))


if __name__ == "__main__":
    main()
