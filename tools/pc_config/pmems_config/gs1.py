"""
GS1 element string parser for DataMatrix content (GS1 General Specifications, section 3).

Input is the RAW decoded text: AIs concatenated, variable-length fields terminated by
the GS character (0x1D) or by the end of the data. The leading FNC1 is already removed
by the decoder (zxing-cpp TextMode.Plain does this).

    >>> parse("0108935049501237" "2100AB12CD\\x1d" "17271231" "10L2611")
    {'01': '08935049501237', '21': '00AB12CD', '17': '271231', '10': 'L2611'}

Why not parse the human readable "(01)...(21)..." form? Parentheses are legal data
characters in some AIs, so the bracketed form can be ambiguous. The raw form is not.
"""
from __future__ import annotations

from datetime import date

GS = "\x1d"

# AI -> (fixed length or None for variable, max length, description).
# Only the AIs relevant for consumer goods are listed; extend if needed.
AI_TABLE: dict[str, tuple[int | None, int, str]] = {
    "00": (18, 18, "SSCC"),
    "01": (14, 14, "GTIN"),
    "02": (14, 14, "GTIN of contained goods"),
    "10": (None, 20, "Batch / lot"),
    "11": (6, 6, "Production date YYMMDD"),
    "13": (6, 6, "Packaging date YYMMDD"),
    "15": (6, 6, "Best before YYMMDD"),
    "16": (6, 6, "Sell by YYMMDD"),
    "17": (6, 6, "Expiration date YYMMDD"),
    "21": (None, 20, "Serial number"),
    "22": (None, 20, "Consumer product variant"),
    "30": (None, 8, "Variable count"),
    "37": (None, 8, "Count of trade items"),
    "91": (None, 90, "Company internal"),
    "92": (None, 90, "Company internal"),
    "93": (None, 90, "Company internal"),
    "240": (None, 30, "Additional product id"),
    "241": (None, 30, "Customer part number"),
    "400": (None, 30, "Customer order number"),
}

DATE_AIS = {"11", "13", "15", "16", "17"}


class GS1Error(ValueError):
    """Content is not a valid GS1 element string."""


def gtin_check_digit_ok(gtin: str) -> bool:
    """Mod-10 check digit of a GTIN-8/12/13/14."""
    if not gtin.isdigit() or len(gtin) not in (8, 12, 13, 14):
        return False
    digits = [int(c) for c in gtin]
    body, check = digits[:-1], digits[-1]
    total = sum(d * (3 if i % 2 == 0 else 1) for i, d in enumerate(reversed(body)))
    return (10 - total % 10) % 10 == check


def parse_date(yymmdd: str) -> date:
    """GS1 date; DD = 00 means the last day of the month."""
    if len(yymmdd) != 6 or not yymmdd.isdigit():
        raise GS1Error(f"invalid date {yymmdd!r}")
    yy, mm, dd = int(yymmdd[:2]), int(yymmdd[2:4]), int(yymmdd[4:])
    year = 2000 + yy if yy < 50 else 1900 + yy          # simplified century rule
    if not 1 <= mm <= 12:
        raise GS1Error(f"invalid month in {yymmdd!r}")
    if dd == 0:
        nxt = date(year + (mm == 12), mm % 12 + 1, 1)
        return date.fromordinal(nxt.toordinal() - 1)
    try:
        return date(year, mm, dd)
    except ValueError as e:
        raise GS1Error(f"invalid date {yymmdd!r}") from e


def _match_ai(data: str, pos: int) -> str:
    for n in (2, 3, 4):
        ai = data[pos:pos + n]
        if ai in AI_TABLE:
            return ai
    raise GS1Error(f"unknown AI at position {pos}: {data[pos:pos + 4]!r}")


def parse(raw: str) -> dict[str, str]:
    """Split a raw GS1 element string into {AI: value}. Raises GS1Error."""
    data = raw.lstrip(GS)
    fields: dict[str, str] = {}
    pos = 0
    while pos < len(data):
        ai = _match_ai(data, pos)
        pos += len(ai)
        fixed, maxlen, _ = AI_TABLE[ai]
        if fixed is not None:
            value = data[pos:pos + fixed]
            if len(value) != fixed:
                raise GS1Error(f"AI {ai}: expected {fixed} characters")
            pos += fixed
            if pos < len(data) and data[pos] == GS:      # tolerated after fixed fields
                pos += 1
        else:
            end = data.find(GS, pos)
            end = len(data) if end < 0 else end
            value = data[pos:end]
            if not 1 <= len(value) <= maxlen:
                raise GS1Error(f"AI {ai}: length {len(value)} not in 1..{maxlen}")
            pos = end + 1
        if ai in fields:
            raise GS1Error(f"AI {ai} repeated")
        if ai in ("01", "02") and not gtin_check_digit_ok(value):
            raise GS1Error(f"AI {ai}: bad GTIN check digit")
        if ai in DATE_AIS:
            parse_date(value)
        fields[ai] = value
    if not fields:
        raise GS1Error("empty content")
    return fields


def to_hri(fields: dict[str, str]) -> str:
    """Human readable form, e.g. (01)08935049501237(21)00AB12CD."""
    return "".join(f"({ai}){v}" for ai, v in fields.items())
