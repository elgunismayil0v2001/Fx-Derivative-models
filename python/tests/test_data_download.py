"""Offline tests for data_download. No network access is used.

The NBP fixture uses real values published for 2-5 January 2024. The ECB and FRED fixtures are
synthetic (obviously fake values): they only exercise the parsers' handling of structure,
holidays and missing markers.
"""

import datetime as dt
import json

import data_download as dd
import pytest

NBP_PAYLOAD = {
    "table": "A",
    "currency": "euro",
    "code": "EUR",
    "rates": [
        {"no": "001/A/NBP/2024", "effectiveDate": "2024-01-02", "mid": 4.3434},
        {"no": "002/A/NBP/2024", "effectiveDate": "2024-01-03", "mid": 4.3646},
        {"no": "003/A/NBP/2024", "effectiveDate": "2024-01-04", "mid": 4.3525},
        {"no": "004/A/NBP/2024", "effectiveDate": "2024-01-05", "mid": 4.3484},
    ],
}

ECB_CSV = (
    "KEY,FREQ,CURRENCY,TIME_PERIOD,OBS_VALUE\n"
    "EXR.D.USD.EUR.SP00.A,D,USD,2000-01-03,1.1000\n"
    "EXR.D.USD.EUR.SP00.A,D,USD,2000-01-04,\n"
    "EXR.D.USD.EUR.SP00.A,D,USD,2000-01-05,1.2000\n"
)

FRED_CSV_NEW = "observation_date,SERIES\n2000-01-03,1.50\n2000-01-04,.\n2000-01-05,-0.25\n"
FRED_CSV_OLD = "DATE,SERIES\n2000-01-03,1.50\n2000-01-04,.\n2000-01-05,-0.25\n"


def d(text: str) -> dt.date:
    return dt.date.fromisoformat(text)


# ---- parsers ----
def test_parse_nbp_reads_dates_and_mid_rates():
    assert dd.parse_nbp(NBP_PAYLOAD) == [
        (d("2024-01-02"), 4.3434),
        (d("2024-01-03"), 4.3646),
        (d("2024-01-04"), 4.3525),
        (d("2024-01-05"), 4.3484),
    ]


@pytest.mark.parametrize("bad", [{}, {"rates": [{"effectiveDate": "2024-01-02"}]}, {"rates": 3}])
def test_parse_nbp_rejects_unexpected_shapes(bad):
    with pytest.raises(dd.DataError):
        dd.parse_nbp(bad)


def test_parse_ecb_skips_empty_holiday_values():
    assert dd.parse_ecb_csv(ECB_CSV) == [(d("2000-01-03"), 1.1), (d("2000-01-05"), 1.2)]


def test_parse_ecb_requires_the_expected_columns():
    with pytest.raises(dd.DataError):
        dd.parse_ecb_csv("a,b\n1,2\n")


@pytest.mark.parametrize("text", [FRED_CSV_NEW, FRED_CSV_OLD])
def test_parse_fred_skips_missing_markers_and_ignores_header_name(text):
    assert dd.parse_fred_csv(text) == [(d("2000-01-03"), 1.5), (d("2000-01-05"), -0.25)]


def test_parse_fred_rejects_empty_input():
    with pytest.raises(dd.DataError):
        dd.parse_fred_csv("observation_date,SERIES\n")


# ---- helpers ----
def test_chunk_ranges_cover_the_period_without_gaps_or_overlap():
    windows = list(dd.chunk_ranges(d("2020-01-01"), d("2022-06-15"), 360))
    assert windows[0][0] == d("2020-01-01")
    assert windows[-1][1] == d("2022-06-15")
    for (_, previous_end), (next_start, _) in zip(windows, windows[1:], strict=False):
        assert next_start == previous_end + dt.timedelta(days=1)
    assert all((end - start).days + 1 <= 360 for start, end in windows)


def test_chunk_ranges_single_day_and_invalid_input():
    assert list(dd.chunk_ranges(d("2024-01-01"), d("2024-01-01"), 360)) == [
        (d("2024-01-01"), d("2024-01-01"))
    ]
    with pytest.raises(ValueError):
        list(dd.chunk_ranges(d("2024-01-02"), d("2024-01-01"), 360))


def test_clean_sorts_and_removes_exact_duplicates():
    raw = [(d("2024-01-03"), 2.0), (d("2024-01-02"), 1.0), (d("2024-01-03"), 2.0)]
    assert dd.clean_observations(raw, positive=True) == [
        (d("2024-01-02"), 1.0),
        (d("2024-01-03"), 2.0),
    ]


def test_clean_rejects_conflicting_duplicates_nonpositive_and_nonfinite():
    with pytest.raises(dd.DataError):
        dd.clean_observations([(d("2024-01-02"), 1.0), (d("2024-01-02"), 1.1)], positive=True)
    with pytest.raises(dd.DataError):
        dd.clean_observations([(d("2024-01-02"), 0.0)], positive=True)
    with pytest.raises(dd.DataError):
        dd.clean_observations([(d("2024-01-02"), float("nan"))], positive=False)
    # Rates may legitimately be zero or negative.
    assert dd.clean_observations([(d("2024-01-02"), -0.5)], positive=False)


def test_write_series_writes_csv_and_provenance(tmp_path):
    out = tmp_path / "sub" / "series.csv"
    obs = [(d("2024-01-02"), 4.3434), (d("2024-01-03"), 4.3646)]
    dd.write_series(out, obs, source="nbp", urls=["https://example.invalid/x"])
    lines = out.read_text().splitlines()
    assert lines == ["date,value", "2024-01-02,4.3434", "2024-01-03,4.3646"]
    meta = json.loads((tmp_path / "sub" / "series.csv.meta.json").read_text())
    assert meta["rows"] == 2
    assert meta["first_date"] == "2024-01-02"
    assert meta["last_date"] == "2024-01-03"
    assert meta["source"] == "nbp"
    assert len(meta["sha256"]) == 64


def test_write_series_refuses_empty_data(tmp_path):
    with pytest.raises(dd.DataError):
        dd.write_series(tmp_path / "x.csv", [], source="nbp", urls=[])


def test_fetch_refuses_non_https_urls():
    with pytest.raises(dd.DataError):
        dd.fetch("http://example.invalid/data")
    with pytest.raises(dd.DataError):
        dd.fetch("file:///etc/passwd")


# ---- command line, with the network replaced ----
def test_cli_nbp_end_to_end_with_fake_fetch(tmp_path, monkeypatch):
    calls = []

    def fake_fetch(url, **_):
        calls.append(url)
        return json.dumps(NBP_PAYLOAD)

    monkeypatch.setattr(dd, "fetch", fake_fetch)
    out = tmp_path / "eur.csv"
    code = dd.main(
        [
            "nbp",
            "--currency",
            "EUR",
            "--start",
            "2024-01-02",
            "--end",
            "2024-01-05",
            "--out",
            str(out),
        ]
    )
    assert code == 0
    assert len(out.read_text().splitlines()) == 5
    assert calls == [
        "https://api.nbp.pl/api/exchangerates/rates/a/eur/2024-01-02/2024-01-05/?format=json"
    ]


def test_cli_returns_1_on_data_errors_and_2_on_bad_dates(tmp_path, monkeypatch, capsys):
    monkeypatch.setattr(dd, "fetch", lambda url, **_: None)  # NBP "no data" in every window
    args = ["nbp", "--currency", "EUR", "--start", "2024-01-06", "--end", "2024-01-07"]
    assert dd.main([*args, "--out", str(tmp_path / "x.csv")]) == 1
    assert "empty series" in capsys.readouterr().err

    reversed_dates = ["nbp", "--currency", "EUR", "--start", "2024-02-01", "--end", "2024-01-01"]
    assert dd.main([*reversed_dates, "--out", str(tmp_path / "y.csv")]) == 2
