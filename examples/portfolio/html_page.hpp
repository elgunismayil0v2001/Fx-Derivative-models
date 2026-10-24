#pragma once

// The report page: static HTML, CSS and JavaScript. The text "/*REPORT_JSON*/null" is replaced by
// the report (see to_html). No external requests: the page works from a file or an offline machine.

#include <string>

namespace fxval::example {

inline std::string html_page_template() {
  return R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>FX Portfolio Report</title>
<style>
:root {
  --bg: #f6f7f9; --card: #ffffff; --ink: #1b2430; --muted: #5b6676; --line: #dfe3ea;
  --c1: #1f5fbf; --c2: #d9822b; --c3: #2e8b57; --c4: #b3382c; --bar: #9db8e6; --good: #2e8b57; --bad: #b3382c;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #12161c; --card: #1a2029; --ink: #e6eaf0; --muted: #9aa5b4; --line: #2b3441;
    --c1: #6ea2ff; --c2: #f0a65a; --c3: #5cc48a; --c4: #ee7d70; --bar: #3d5f98; --good: #5cc48a; --bad: #ee7d70;
  }
}
:root[data-theme="dark"] {
  --bg: #12161c; --card: #1a2029; --ink: #e6eaf0; --muted: #9aa5b4; --line: #2b3441;
  --c1: #6ea2ff; --c2: #f0a65a; --c3: #5cc48a; --c4: #ee7d70; --bar: #3d5f98; --good: #5cc48a; --bad: #ee7d70;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink);
  font: 15px/1.5 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
main { max-width: 1120px; margin: 0 auto; padding: 24px 16px 56px; }
h1 { font-size: 24px; margin: 0 0 4px; letter-spacing: -0.01em; }
h2 { font-size: 17px; margin: 0 0 4px; }
p.sub, p.note { color: var(--muted); margin: 0 0 14px; font-size: 13.5px; }
.banner { background: var(--card); border: 1px solid var(--line); border-left: 4px solid var(--c2);
  padding: 10px 14px; border-radius: 6px; font-size: 13.5px; color: var(--muted); margin: 14px 0 20px; }
.kpis { display: grid; grid-template-columns: repeat(auto-fit, minmax(210px, 1fr)); gap: 12px; margin-bottom: 20px; }
.kpi { background: var(--card); border: 1px solid var(--line); border-radius: 8px; padding: 14px 16px; }
.kpi .label { font-size: 12.5px; color: var(--muted); }
.kpi .num { font-size: 24px; font-weight: 650; font-variant-numeric: tabular-nums; margin-top: 2px; }
.kpi .hint { font-size: 12px; color: var(--muted); margin-top: 2px; }
section { background: var(--card); border: 1px solid var(--line); border-radius: 8px; padding: 18px 18px 14px; margin-bottom: 16px; }
.grid2 { display: grid; grid-template-columns: 1fr 1fr; gap: 18px; }
@media (max-width: 820px) { .grid2 { grid-template-columns: 1fr; } }
.scroll { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; font-variant-numeric: tabular-nums; font-size: 13.5px; }
th, td { padding: 6px 10px; text-align: right; border-bottom: 1px solid var(--line); white-space: nowrap; }
th:first-child, td:first-child { text-align: left; }
th { color: var(--muted); font-weight: 600; font-size: 12.5px; }
tr.total td { font-weight: 650; border-top: 2px solid var(--line); }
.neg { color: var(--bad); }
.chart { width: 100%; height: auto; display: block; }
.chart text { fill: var(--muted); font-size: 11px; }
.legend { display: flex; gap: 16px; flex-wrap: wrap; font-size: 12.5px; color: var(--muted); margin: 4px 0 2px; }
.legend i { display: inline-block; width: 18px; height: 0; border-top: 3px solid; vertical-align: middle; margin-right: 6px; }
.bars td.bar { width: 38%; padding: 6px 10px; }
.bars .fill { height: 10px; background: var(--bar); border-radius: 2px; }
footer { color: var(--muted); font-size: 12.5px; margin-top: 18px; }
</style>
</head>
<body>
<main>
  <h1 id="title">FX portfolio report</h1>
  <p class="sub" id="subtitle"></p>
  <div class="banner">Synthetic example. The market data, the trades and the return history are invented to show how the
  library's models fit together. Nothing here describes a real book or a real market.</div>
  <div class="kpis" id="kpis"></div>

  <section>
    <h2>Positions and value</h2>
    <p class="note">Each option is valued with the Garman-Kohlhagen formula at the volatility read from the smile at its strike.
    The last two columns reprice the same option with one flat ATM volatility and with Heston calibrated to the whole surface.</p>
    <div class="scroll"><table id="positions"></table></div>
  </section>

  <section>
    <h2>Greeks</h2>
    <p class="note">Position Greeks in domestic currency. Vega is per volatility point, theta per calendar day, rho per basis point,
    gamma is the value change for a 1% spot move from the second-order term.</p>
    <div class="scroll"><table id="greeks"></table></div>
  </section>

  <section>
    <h2>Spot ladder: full revaluation against the delta-gamma expansion</h2>
    <p class="note">Profit and loss of the whole book when the spot moves, volatilities and dates fixed. The gap between the two lines is the
    third and higher order terms, the part a delta-gamma VaR does not see.</p>
    <div class="legend"><span><i style="border-color:var(--c1)"></i>Full revaluation</span><span><i style="border-color:var(--c2);border-top-style:dashed"></i>Delta-gamma</span></div>
    <div id="ladder"></div>
  </section>

  <section>
    <h2>Market risk</h2>
    <p class="note" id="riskNote"></p>
    <div class="grid2">
      <div class="scroll"><table id="var" class="bars"></table></div>
      <div><div id="hist"></div></div>
    </div>
  </section>

  <section>
    <h2>Counterparty credit risk</h2>
    <p class="note" id="creditNote"></p>
    <div class="grid2">
      <div>
        <div class="legend"><span><i style="border-color:var(--c1)"></i>Expected exposure</span><span><i style="border-color:var(--c4);border-top-style:dashed"></i>PFE</span></div>
        <div id="exposure"></div>
      </div>
      <div class="scroll"><table id="cva"></table></div>
    </div>
  </section>

  <footer>Generated by <code>fxval-portfolio</code>. The formulas are in docs/portfolio_mathematics.md.</footer>
</main>
<script>
"use strict";
const R = /*REPORT_JSON*/null;
const $ = (id) => document.getElementById(id);
const isNum = (x) => typeof x === "number" && isFinite(x);
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({"&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;"}[c]));

function num(x, d = 0) {
  if (!isNum(x)) return "n/a";
  return x.toLocaleString("en-US", {minimumFractionDigits: d, maximumFractionDigits: d});
}
function cell(x, d = 0) {
  if (!isNum(x)) return '<td>n/a</td>';
  return '<td' + (x < 0 ? ' class="neg"' : '') + '>' + num(x, d) + '</td>';
}
function compact(x) {
  if (!isNum(x)) return "n/a";
  const a = Math.abs(x);
  if (a >= 1e6) return num(x / 1e6, 2) + "M";
  if (a >= 1e3) return num(x / 1e3, 1) + "k";
  return num(x, 0);
}
function pct(x, d = 2) { return isNum(x) ? num(100 * x, d) + "%" : "n/a"; }

// ---- generic SVG line chart ----
function niceStep(range, ticks) {
  const raw = range / ticks, p = Math.pow(10, Math.floor(Math.log10(raw))), f = raw / p;
  return (f < 1.5 ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10) * p;
}
function lineChart(target, o) {
  const W = o.w || 520, H = o.h || 300, m = {l: 62, r: 14, t: 10, b: 38};
  const xs = o.x, ys = o.series.flatMap((s) => s.y.filter(isNum));
  let x0 = Math.min(...xs), x1 = Math.max(...xs), y0 = Math.min(0, ...ys), y1 = Math.max(0, ...ys);
  if (y1 === y0) { y1 += 1; y0 -= 1; }
  const step = niceStep(y1 - y0, 5);
  y0 = Math.floor(y0 / step) * step; y1 = Math.ceil(y1 / step) * step;
  const X = (v) => m.l + (v - x0) / (x1 - x0 || 1) * (W - m.l - m.r);
  const Y = (v) => H - m.b - (v - y0) / (y1 - y0) * (H - m.t - m.b);
  let s = '<svg class="chart" viewBox="0 0 ' + W + ' ' + H + '" role="img" aria-label="' + esc(o.label) + '">';
  for (let v = y0; v <= y1 + step / 2; v += step) {
    s += '<line x1="' + m.l + '" x2="' + (W - m.r) + '" y1="' + Y(v) + '" y2="' + Y(v) + '" stroke="var(--line)"/>';
    s += '<text x="' + (m.l - 6) + '" y="' + (Y(v) + 4) + '" text-anchor="end">' + esc(o.yFmt(v)) + '</text>';
  }
  const xticks = o.xTicks || 5;
  for (let i = 0; i <= xticks; i++) {
    const v = x0 + (x1 - x0) * i / xticks;
    s += '<text x="' + X(v) + '" y="' + (H - m.b + 16) + '" text-anchor="middle">' + esc(o.xFmt(v)) + '</text>';
  }
  if (y0 < 0 && y1 > 0) s += '<line x1="' + m.l + '" x2="' + (W - m.r) + '" y1="' + Y(0) + '" y2="' + Y(0) + '" stroke="var(--muted)"/>';
  if (o.vline !== undefined && x0 <= o.vline && o.vline <= x1) s += '<line x1="' + X(o.vline) + '" x2="' + X(o.vline) + '" y1="' + m.t + '" y2="' + (H - m.b) + '" stroke="var(--muted)" stroke-dasharray="2 3"/>';
  o.series.forEach((ser) => {
    const pts = xs.map((v, i) => isNum(ser.y[i]) ? X(v).toFixed(1) + "," + Y(ser.y[i]).toFixed(1) : null).filter(Boolean).join(" ");
    s += '<polyline fill="none" stroke="' + ser.color + '" stroke-width="2.2" ' + (ser.dash ? 'stroke-dasharray="6 4" ' : '') + 'points="' + pts + '"/>';
  });
  s += '<text x="' + ((m.l + W - m.r) / 2) + '" y="' + (H - 4) + '" text-anchor="middle">' + esc(o.xLabel) + '</text>';
  s += '</svg>';
  target.innerHTML = s;
}

function histogram(target, h, varLine) {
  const W = 520, H = 300, m = {l: 50, r: 14, t: 10, b: 38};
  const c = h.counts, n = c.length, top = Math.max(1, ...c);
  const X = (v) => m.l + (v - h.lower) / ((h.upper - h.lower) || 1) * (W - m.l - m.r);
  const Y = (v) => H - m.b - v / top * (H - m.t - m.b);
  let s = '<svg class="chart" viewBox="0 0 ' + W + ' ' + H + '" role="img" aria-label="Distribution of historical simulation profit and loss">';
  const bw = (W - m.l - m.r) / n;
  c.forEach((v, i) => { s += '<rect x="' + (m.l + i * bw + 0.5).toFixed(1) + '" y="' + Y(v).toFixed(1) + '" width="' + Math.max(1, bw - 1).toFixed(1) + '" height="' + (H - m.b - Y(v)).toFixed(1) + '" fill="var(--bar)"/>'; });
  s += '<line x1="' + m.l + '" x2="' + (W - m.r) + '" y1="' + (H - m.b) + '" y2="' + (H - m.b) + '" stroke="var(--muted)"/>';
  for (let i = 0; i <= 4; i++) {
    const v = h.lower + (h.upper - h.lower) * i / 4;
    s += '<text x="' + X(v) + '" y="' + (H - m.b + 16) + '" text-anchor="middle">' + compact(v) + '</text>';
  }
  if (isNum(varLine) && -varLine >= h.lower && -varLine <= h.upper) {
    s += '<line x1="' + X(-varLine) + '" x2="' + X(-varLine) + '" y1="' + m.t + '" y2="' + (H - m.b) + '" stroke="var(--c4)" stroke-width="2"/>';
    s += '<text x="' + (X(-varLine) + 5) + '" y="' + (m.t + 12) + '" style="fill:var(--c4)">historical VaR</text>';
  }
  s += '<text x="' + ((m.l + W - m.r) / 2) + '" y="' + (H - 4) + '" text-anchor="middle">one-day profit and loss (USD)</text></svg>';
  target.innerHTML = s;
}

function render() {
  if (!R) { document.body.insertAdjacentHTML("beforeend", "<p>No data embedded.</p>"); return; }
  const ccy = R.pair.slice(3), base = R.pair.slice(0, 3);
  $("title").textContent = base + "/" + ccy + " portfolio report";
  $("subtitle").textContent = "Spot " + num(R.spot, 4) + " · " + ccy + " rate " + pct(R.domestic_rate) + " · " + base + " rate " + pct(R.foreign_rate) + " · amounts in " + ccy;
  const mr = R.market_risk, cr = R.credit, g = R.total_greeks;
  const fhs = mr.methods.find((m) => m.method.startsWith("Filtered")) || mr.methods[mr.methods.length - 1];
  $("kpis").innerHTML = [
    ["Portfolio value", num(R.total_value), "sum of all positions"],
    ["Delta per 1% spot move", num(g.delta * R.spot * 0.01), num(g.delta) + " " + base + " of spot exposure"],
    [pct(mr.level, 1).replace(".0", "") + " one-day VaR", num(fhs.var), fhs.method.toLowerCase() + ", ES " + num(fhs.es)],
    ["CVA (netted)", num(cr.cva), "± " + num(cr.cva_standard_error) + " simulation error"]
  ].map((k) => '<div class="kpi"><div class="label">' + esc(k[0]) + '</div><div class="num">' + esc(k[1]) + '</div><div class="hint">' + esc(k[2]) + '</div></div>').join("");

  // positions
  let t = "<tr><th>Position</th><th>Quantity</th><th>Strike</th><th>Expiry (y)</th><th>Vol</th><th>Value</th><th>Flat ATM vol</th><th>Heston</th></tr>";
  R.positions.forEach((p) => {
    t += "<tr><td>" + esc(p.label) + "</td>" + cell(p.quantity) + "<td>" + num(p.strike, 4) + "</td><td>" + num(p.expiry, 2) + "</td><td>" + (p.kind === "forward" ? "–" : pct(p.volatility)) + "</td>" + cell(p.value) +
      (p.value_flat_atm === null ? "<td>–</td>" : cell(p.value_flat_atm)) + (p.value_heston === null ? "<td>–</td>" : cell(p.value_heston)) + "</tr>";
  });
  t += '<tr class="total"><td>Total</td><td></td><td></td><td></td><td></td>' + cell(R.total_value) + "<td></td><td></td></tr>";
  $("positions").innerHTML = t;
  if (R.heston) {
    $("positions").insertAdjacentHTML("afterend", '<p class="note" style="margin-top:8px">Heston: v0 ' + num(R.heston.v0, 4) + ", kappa " + num(R.heston.kappa, 2) + ", theta " + num(R.heston.theta, 4) + ", xi " + num(R.heston.xi, 2) + ", rho " + num(R.heston.rho, 2) +
      "; rms error against the surface quotes " + num(100 * R.heston.rms_vol_error, 3) + " vol points" + (R.heston.converged ? "" : " (did not converge)") + ".</p>");
  }

  // greeks
  const S = R.spot;
  const gr = (p) => [p.greeks.delta * S, 0.5 * p.greeks.gamma * S * S * 1e-4, p.greeks.vega / 100, p.greeks.vanna / 100 * S, p.greeks.theta / 365, p.greeks.rho_domestic / 1e4, p.greeks.rho_foreign / 1e4];
  let gt = "<tr><th>Position</th><th>Delta (units)</th><th>Delta per 1% move</th><th>Gamma, 1% move</th><th>Vega / vol pt</th><th>Theta / day</th><th>Rho " + esc(ccy) + " / bp</th><th>Rho " + esc(base) + " / bp</th></tr>";
  const row = (label, p, cls) => { const v = gr(p); return "<tr" + (cls ? ' class="' + cls + '"' : "") + "><td>" + esc(label) + "</td>" + cell(p.greeks.delta) + cell(v[0] * 0.01) + cell(v[1]) + cell(v[2]) + cell(v[4]) + cell(v[5]) + cell(v[6]) + "</tr>"; };
  R.positions.forEach((p) => { gt += row(p.label, p); });
  gt += row("Total", {greeks: g}, "total");
  $("greeks").innerHTML = gt;

  // ladder
  lineChart($("ladder"), {
    label: "Profit and loss against the spot move", x: R.ladder.map((l) => l.shock),
    series: [{y: R.ladder.map((l) => l.pnl), color: "var(--c1)"}, {y: R.ladder.map((l) => l.pnl_taylor), color: "var(--c2)", dash: true}],
    xFmt: (v) => pct(v, 0), yFmt: compact, xLabel: "spot move", vline: 0, w: 1040, h: 320, xTicks: 10
  });

  // var
  $("riskNote").textContent = "One-day horizon, " + pct(mr.level, 1) + " level, losses shown as positive numbers. Volatility forecast: GARCH(1,1) fitted to " + num(mr.garch.observations) + " daily returns (" +
    (mr.garch.synthetic ? "synthetic" : "supplied") + "), next day " + pct(mr.garch.sigma_next) + ", persistence " + num(mr.garch.persistence, 3) + (mr.garch.converged ? "" : ", not converged") + ".";
  const vmax = Math.max(1, ...mr.methods.map((m) => Math.max(m.var, m.es)));
  let vt = "<tr><th>Method</th><th>VaR</th><th>ES</th><th></th></tr>";
  mr.methods.forEach((m) => { vt += "<tr><td>" + esc(m.method) + "</td>" + cell(m.var) + cell(m.es) + '<td class="bar"><div class="fill" style="width:' + Math.max(0, Math.min(100, 100 * m.var / vmax)) + '%"></div></td></tr>'; });
  $("var").innerHTML = vt;
  histogram($("hist"), mr.pnl_histogram, mr.historical_var);

  // credit
  $("creditNote").textContent = "One counterparty, one netting set, no collateral. Hazard rate " + pct(cr.hazard_rate, 1) + ", recovery " + pct(cr.recovery, 0) + ", " + num(cr.paths) + " paths of the spot at volatility " + pct(cr.exposure_volatility) + ". EPE (average expected exposure) " + num(cr.epe) + ". Exposure falls when a trade matures, which is why the lines step down at the option expiries.";
  lineChart($("exposure"), {
    label: "Expected exposure and potential future exposure over time", x: cr.dates,
    series: [{y: cr.ee, color: "var(--c1)"}, {y: cr.pfe, color: "var(--c4)", dash: true}],
    xFmt: (v) => num(v, 2), yFmt: compact, xLabel: "years from today", xTicks: 5
  });
  let ct = "<tr><th>Trade</th><th>Standalone CVA</th></tr>";
  cr.standalone.forEach((s) => { ct += "<tr><td>" + esc(s.label) + "</td>" + cell(s.cva) + "</tr>"; });
  ct += '<tr class="total"><td>Sum of standalone</td>' + cell(cr.standalone_sum) + "</tr>";
  ct += "<tr><td>Netted CVA</td>" + cell(cr.cva) + "</tr>";
  ct += "<tr><td>Netting benefit</td>" + cell(cr.netting_benefit) + "</tr>";
  $("cva").innerHTML = ct;
}
render();
</script>
</body>
</html>
)HTML";
}

}  // namespace fxval::example
