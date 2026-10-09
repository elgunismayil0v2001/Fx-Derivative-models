"""Download the free FX and interest-rate history used by fx-model-validation.

Sources (check each provider's terms of use before redistributing anything):

* nbp  - Narodowy Bank Polski, table A mid rates, https://api.nbp.pl
* ecb  - ECB euro foreign exchange reference rates, https://data-api.ecb.europa.eu
* fred - FRED series as CSV, https://fred.stlouisfed.org

Every output file is a two-column CSV (``date,value``) next to a ``.meta.json`` file that records
where the data came from and when, so any number in a report can be traced back to its download.

Examples::

    python python/data_download.py nbp  --currency EUR --start 2015-01-01 --end 2026-09-30 \\
        --out data/raw/nbp_eur_pln.csv
    python python/data_download.py ecb  --currency USD --start 2015-01-01 --end 2026-09-30 \\
        --out data/raw/ecb_eur_usd.csv
    python python/data_download.py fred --series DGS10 --start 2015-01-01 --end 2026-09-30 \\
        --out data/raw/fred_dgs10.csv

The parsers are pure functions and are tested offline. The network layer is a thin wrapper.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import io
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from collections.abc import Iterator, Sequence
from pathlib import Path

Observation = tuple[dt.date, float]

# NBP allows at most 367 days per series request. Stay safely below it.
NBP_CHUNK_DAYS = 360
USER_AGENT = "fx-model-validation/0.1 (research; contact via repository)"


class DataError(Exception):
    """Raised when downloaded data is missing, malformed or fails a sanity check."""


# --------------------------------------------------------------------------------------
# Parsers (pure functions, no network)
# --------------------------------------------------------------------------------------
def parse_nbp(payload: dict) -> list[Observation]:
    """Parse the JSON returned by /api/exchangerates/rates/a/{code}/{start}/{end}/."""
    try:
        rates = payload["rates"]
        return [(dt.date.fromisoformat(r["effectiveDate"]), float(r["mid"])) for r in rates]
    except (KeyError, TypeError, ValueError) as exc:
        raise DataError(f"unexpected NBP response shape: {exc!r}") from exc


def parse_ecb_csv(text: str) -> list[Observation]:
    """Parse the SDMX-CSV (``format=csvdata``) returned by the ECB data portal."""
    reader = csv.DictReader(io.StringIO(text))
    if reader.fieldnames is None or not {"TIME_PERIOD", "OBS_VALUE"} <= set(reader.fieldnames):
        raise DataError("ECB CSV is missing the TIME_PERIOD or OBS_VALUE column")
    out: list[Observation] = []
    for row in reader:
        raw = (row["OBS_VALUE"] or "").strip()
        if not raw:
            continue  # the ECB leaves holidays empty
        try:
            out.append((dt.date.fromisoformat(row["TIME_PERIOD"]), float(raw)))
        except ValueError as exc:
            raise DataError(f"bad ECB row {row!r}: {exc}") from exc
    return out


def parse_fred_csv(text: str) -> list[Observation]:
    """Parse FRED's ``fredgraph.csv``: first column is the date, second is the value.

    FRED marks missing observations with ``.``. Those rows are skipped. The header name of the
    date column has changed over time, so the columns are read by position.
    """
    rows = list(csv.reader(io.StringIO(text)))
    if len(rows) < 2 or len(rows[0]) < 2:
        raise DataError("FRED CSV has no data rows or fewer than two columns")
    out: list[Observation] = []
    for row in rows[1:]:
        if len(row) < 2:
            continue
        raw = row[1].strip()
        if raw in ("", "."):
            continue
        try:
            out.append((dt.date.fromisoformat(row[0].strip()), float(raw)))
        except ValueError as exc:
            raise DataError(f"bad FRED row {row!r}: {exc}") from exc
    return out


# --------------------------------------------------------------------------------------
# Sanity checks and helpers
# --------------------------------------------------------------------------------------
def chunk_ranges(start: dt.date, end: dt.date, max_days: int) -> Iterator[tuple[dt.date, dt.date]]:
    """Yield consecutive inclusive date windows of at most ``max_days`` days."""
    if end < start:
        raise ValueError("end date is before start date")
    if max_days < 1:
        raise ValueError("max_days must be at least 1")
    cursor = start
    while cursor <= end:
        window_end = min(cursor + dt.timedelta(days=max_days - 1), end)
        yield cursor, window_end
        cursor = window_end + dt.timedelta(days=1)


def clean_observations(obs: Sequence[Observation], *, positive: bool) -> list[Observation]:
    """Sort, drop exact duplicate dates, and reject impossible values.

    Duplicate dates with different values are an error, not something to resolve silently.
    """
    by_date: dict[dt.date, float] = {}
    for day, value in obs:
        if value != value or value in (float("inf"), float("-inf")):  # NaN or infinite
            raise DataError(f"non-finite value on {day}")
        if positive and value <= 0.0:
            raise DataError(f"non-positive value {value} on {day}")
        if day in by_date and by_date[day] != value:
            raise DataError(f"conflicting values for {day}: {by_date[day]} vs {value}")
        by_date[day] = value
    return sorted(by_date.items())


def write_series(
    path: Path, obs: Sequence[Observation], *, source: str, urls: Sequence[str]
) -> None:
    """Write ``date,value`` CSV plus a ``.meta.json`` provenance file next to it."""
    if not obs:
        raise DataError("refusing to write an empty series")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["date", "value"])
        for day, value in obs:
            writer.writerow([day.isoformat(), repr(value)])
    meta = {
        "source": source,
        "urls": list(urls),
        "retrieved_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
        "rows": len(obs),
        "first_date": obs[0][0].isoformat(),
        "last_date": obs[-1][0].isoformat(),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }
    path.with_suffix(path.suffix + ".meta.json").write_text(
        json.dumps(meta, indent=2) + "\n", encoding="utf-8"
    )


# --------------------------------------------------------------------------------------
# Network layer (thin)
# --------------------------------------------------------------------------------------
def fetch(url: str, *, retries: int = 3, timeout: float = 30.0) -> str | None:
    """GET ``url`` and return the body. Returns None for HTTP 404 (NBP uses it for 'no data')."""
    if not url.startswith("https://"):
        raise DataError(f"refusing to fetch a non-HTTPS URL: {url}")
    delay = 1.0
    for attempt in range(1, retries + 1):
        request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})  # noqa: S310
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:  # noqa: S310
                return response.read().decode("utf-8")
        except urllib.error.HTTPError as exc:
            if exc.code == 404:
                return None
            if exc.code < 500 or attempt == retries:
                raise DataError(f"HTTP {exc.code} for {url}") from exc
        except (urllib.error.URLError, TimeoutError) as exc:
            if attempt == retries:
                raise DataError(f"cannot reach {url}: {exc}") from exc
        time.sleep(delay)
        delay *= 2
    raise DataError(f"giving up on {url}")  # pragma: no cover


def download_nbp(
    currency: str, start: dt.date, end: dt.date
) -> tuple[list[Observation], list[str]]:
    observations: list[Observation] = []
    urls: list[str] = []
    for window_start, window_end in chunk_ranges(start, end, NBP_CHUNK_DAYS):
        url = (
            f"https://api.nbp.pl/api/exchangerates/rates/a/{currency.lower()}/"
            f"{window_start.isoformat()}/{window_end.isoformat()}/?format=json"
        )
        body = fetch(url)
        urls.append(url)
        if body is None:
            continue  # no publication days in this window
        observations.extend(parse_nbp(json.loads(body)))
    return observations, urls


def download_ecb(
    currency: str, start: dt.date, end: dt.date
) -> tuple[list[Observation], list[str]]:
    query = urllib.parse.urlencode(
        {"format": "csvdata", "startPeriod": start.isoformat(), "endPeriod": end.isoformat()}
    )
    url = f"https://data-api.ecb.europa.eu/service/data/EXR/D.{currency.upper()}.EUR.SP00.A?{query}"
    body = fetch(url)
    if body is None:
        raise DataError(f"ECB has no series for {currency}")
    return parse_ecb_csv(body), [url]


def download_fred(series: str, start: dt.date, end: dt.date) -> tuple[list[Observation], list[str]]:
    query = urllib.parse.urlencode(
        {"id": series.upper(), "cosd": start.isoformat(), "coed": end.isoformat()}
    )
    url = f"https://fred.stlouisfed.org/graph/fredgraph.csv?{query}"
    body = fetch(url)
    if body is None:
        raise DataError(f"FRED has no series {series}")
    return parse_fred_csv(body), [url]


# --------------------------------------------------------------------------------------
# Command line
# --------------------------------------------------------------------------------------
def _date(text: str) -> dt.date:
    try:
        return dt.date.fromisoformat(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"not an ISO date (YYYY-MM-DD): {text!r}") from exc


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="source", required=True)

    def common(p: argparse.ArgumentParser) -> None:
        p.add_argument("--start", type=_date, required=True, help="first date, YYYY-MM-DD")
        p.add_argument("--end", type=_date, required=True, help="last date, YYYY-MM-DD")
        p.add_argument("--out", type=Path, required=True, help="output CSV path")

    nbp = sub.add_parser("nbp", help="NBP table A mid rate, PLN per 1 unit of CURRENCY")
    nbp.add_argument("--currency", required=True, help="ISO code, e.g. EUR or USD")
    common(nbp)

    ecb = sub.add_parser("ecb", help="ECB reference rate, units of CURRENCY per 1 EUR")
    ecb.add_argument("--currency", required=True, help="ISO code, e.g. USD or PLN")
    common(ecb)

    fred = sub.add_parser("fred", help="a FRED series, e.g. DGS10 (10y US Treasury yield, %%)")
    fred.add_argument("--series", required=True, help="FRED series id")
    common(fred)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.end < args.start:
        print("error: --end is before --start", file=sys.stderr)
        return 2
    try:
        if args.source == "nbp":
            raw, urls = download_nbp(args.currency, args.start, args.end)
            positive = True
        elif args.source == "ecb":
            raw, urls = download_ecb(args.currency, args.start, args.end)
            positive = True
        else:
            raw, urls = download_fred(args.series, args.start, args.end)
            positive = False  # yields can be zero or negative
        series = clean_observations(raw, positive=positive)
        write_series(args.out, series, source=args.source, urls=urls)
    except DataError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(f"wrote {len(series)} rows to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
