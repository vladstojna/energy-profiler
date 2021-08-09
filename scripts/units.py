#!/usr/bin/env python3

import fractions
from typing import Dict, Iterable, Tuple

_nano = fractions.Fraction(1, 1000000000)
_micro = fractions.Fraction(1, 1000000)
_milli = fractions.Fraction(1, 1000)
_std = fractions.Fraction(1, 1)
_kilo = fractions.Fraction(1000, 1)
_mega = fractions.Fraction(1000000, 1)
_giga = fractions.Fraction(1000000000, 1)
_generic_units = [
    ("n", _nano),
    ("u", _micro),
    ("m", _milli),
    ("", _std),
    ("k", _kilo),
    ("M", _mega),
    ("G", _giga),
]


def _generate_map(suffix: str) -> Dict[str, fractions.Fraction]:
    return {pref + suffix: frac for pref, frac in _generic_units}


class invalid_unit(Exception):
    def __init__(self, message) -> None:
        self.message = message


def _convert(from_unit, to_unit):
    if from_unit.unit_string == to_unit:
        return
    if from_unit.units.get(to_unit) == None:
        raise invalid_unit("Unit not supported")
    tmp = from_unit.fraction / from_unit.units[to_unit]
    from_unit.value *= tmp.numerator
    from_unit.value /= tmp.denominator
    from_unit.fraction = from_unit.units[to_unit]
    from_unit.unit_string = to_unit


def _common_unit(lhs, rhs) -> str:
    if lhs.unit_string == rhs.unit_string:
        return lhs.unit_string
    ulhs = lhs.units.get(rhs.unit_string)
    urhs = rhs.units.get(lhs.unit_string)
    if not ulhs or not urhs:
        raise invalid_unit("Invalid units {} and {}".format(lhs, rhs))
    return rhs.unit_string if urhs > ulhs else lhs.unit_string


def _assert_types(lhs, tp, comment: str) -> None:
    if not isinstance(lhs, tp):
        raise TypeError(comment)


class _scalar_unit:
    def __init__(self, value, fraction, unit_string):
        self.value = value
        self.fraction = fraction
        self.unit_string = unit_string

    def __bool__(self):
        return self.value != 0

    def __str__(self):
        return "{} {}".format(self.value, self.unit_string)

    def convert(self, to_unit):
        _convert(self, to_unit)
        return self


class Timestamp(_scalar_unit):
    units = _generate_map("s")

    def __init__(self, value, units):
        if self.units.get(units) == None:
            raise invalid_unit("Unsupported unit {}".format(units))
        super().__init__(value, self.units[units], units)

    def __add__(self, other):
        _assert_types(self, type(other), "can only add two timestamps")
        common = _common_unit(self, other)
        return Timestamp(
            self.copy_as(common).value + other.copy_as(common).value, common
        )

    def __sub__(self, other):
        _assert_types(self, type(other), "can only sub two timestamps")
        common = _common_unit(self, other)
        return Timestamp(
            self.copy_as(common).value - other.copy_as(common).value, common
        )

    def __floordiv__(self, scalar):
        return Timestamp(self.value // scalar, self.unit_string)

    def __truediv__(self, scalar):
        return Timestamp(self.value / scalar, self.unit_string)

    def __mul__(self, scalar):
        return Timestamp(self.value * scalar, self.unit_string)

    def __iadd__(self, other):
        _assert_types(self, type(other), "can only add two timestamps")
        self.value += other.copy_as(self.unit_string).value
        return self

    def __isub__(self, other):
        _assert_types(self, type(other), "can only sub two timestamps")
        self.value -= other.copy_as(self.unit_string).value
        return self

    def __idiv__(self, scalar):
        self.value /= scalar
        return self

    def __imul__(self, scalar):
        self.value *= scalar
        return self

    def copy_as(self, to_unit):
        copy = Timestamp(self.value, self.unit_string)
        _convert(copy, to_unit)
        return copy


class Energy(_scalar_unit):
    units = _generate_map("J")

    def __init__(self, value, units):
        if self.units.get(units) == None:
            raise invalid_unit("Unsupported unit {}".format(units))
        super().__init__(value, self.units[units], units)

    def __add__(self, other):
        _assert_types(self, type(other), "can only add two energies")
        common = _common_unit(self, other)
        return Energy(self.copy_as(common).value + other.copy_as(common).value, common)

    def __sub__(self, other):
        _assert_types(self, type(other), "can only sub two energies")
        common = _common_unit(self, other)
        return Energy(self.copy_as(common).value - other.copy_as(common).value, common)

    def __floordiv__(self, scalar):
        return Energy(self.value // scalar, self.unit_string)

    def __truediv__(self, scalar):
        return Energy(self.value / scalar, self.unit_string)

    def __mul__(self, scalar):
        return Energy(self.value * scalar, self.unit_string)

    def __iadd__(self, other):
        _assert_types(self, type(other), "can only add two energies")
        self.value += other.copy_as(self.unit_string).value
        return self

    def __isub__(self, other):
        _assert_types(self, type(other), "can only sub two energies")
        self.value -= other.copy_as(self.unit_string).value
        return self

    def __idiv__(self, scalar):
        self.value /= scalar
        return self

    def __imul__(self, scalar):
        self.value *= scalar
        return self

    def copy_as(self, to_unit):
        copy = Energy(self.value, self.unit_string)
        _convert(copy, to_unit)
        return copy


class Power(_scalar_unit):
    units = _generate_map("W")

    def __init__(self, value, units):
        if self.units.get(units) == None:
            raise invalid_unit("Unsupported unit {}".format(units))
        super().__init__(value, self.units[units], units)

    def __add__(self, other):
        _assert_types(self, type(other), "can only add two powers")
        common = _common_unit(self, other)
        return Power(self.copy_as(common).value + other.copy_as(common).value, common)

    def __sub__(self, other):
        _assert_types(self, type(other), "can only sub two powers")
        common = _common_unit(self, other)
        return Power(self.copy_as(common).value - other.copy_as(common).value, common)

    def __floordiv__(self, scalar):
        return Power(self.value // scalar, self.unit_string)

    def __truediv__(self, scalar):
        return Power(self.value / scalar, self.unit_string)

    def __mul__(self, scalar):
        return Power(self.value * scalar, self.unit_string)

    def __iadd__(self, other):
        _assert_types(self, type(other), "can only add two powers")
        self.value += other.copy_as(self.unit_string).value
        return self

    def __isub__(self, other):
        _assert_types(self, type(other), "can only sub two powers")
        self.value -= other.copy_as(self.unit_string).value
        return self

    def __idiv__(self, scalar):
        self.value /= scalar
        return self

    def __imul__(self, scalar):
        self.value *= scalar
        return self

    def copy_as(self, to_unit):
        copy = Power(self.value, self.unit_string)
        _convert(copy, to_unit)
        return copy


def div(energy: Energy, time: Timestamp) -> Power:
    _assert_types(energy, Energy, "can only divide energy by time")
    _assert_types(time, Timestamp, "can only divide energy by time")
    fraction = energy.fraction / time.fraction
    for u, f in Power.units.items():
        if f == fraction:
            return Power(energy.value / time.value, u)
    raise invalid_unit(
        "Cannot divide {} by {}, unsupported".format(
            energy.unit_string, time.unit_string
        )
    )


def mul(power: Power, time: Timestamp) -> Energy:
    _assert_types(power, Power, "can only multiply power by time")
    _assert_types(time, Timestamp, "can only power power by time")
    fraction = power.fraction * time.fraction
    for u, f in Energy.units.items():
        if f == fraction:
            return Energy(power.value * time.value, u)
    raise invalid_unit(
        "Cannot multiply {} by {}, unsupported".format(
            power.unit_string, time.unit_string
        )
    )


def integrate_power_series(
    series: Iterable[Tuple[Power, Timestamp]], to_unit: str
) -> Energy:
    result = Energy(0, to_unit)
    for ix, (p, t) in enumerate(series[1:], start=1):
        pwr = (p + series[ix - 1][0]) / 2
        tm = t - series[ix - 1][1]
        result += mul(pwr, tm)
    return result


def main():
    import random

    series = [
        (Power(random.randint(0, 100), "mW"), Timestamp(i, "s")) for i in range(50)
    ]
    print(integrate_power_series(series, "J"))


if __name__ == "__main__":
    main()
