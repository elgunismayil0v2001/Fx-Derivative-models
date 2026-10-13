#include "fxval/models/heston.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>

#include "fxval/numerics/quadrature.hpp"

namespace fxval::models {
namespace {

using Complex = std::complex<double>;
using pricing::OptionType;

// log(1 + z) / z, with the limit 1 at z = 0 and a series where the direct formula loses digits.
Complex log1p_over_z(Complex z) noexcept {
  if (std::abs(z) < 1e-3) {
    return 1.0 - z / 2.0 + z * z / 3.0 - z * z * z / 4.0 + z * z * z * z / 5.0;
  }
  return std::log(1.0 + z) / z;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------------------------
Result<HestonParams> HestonParams::create(
    double v0, double kappa, double theta, double xi, double rho) {
  const auto bad = [](const char* name, double value, const char* rule) -> Result<HestonParams> {
    std::ostringstream message;
    message << "Heston parameter " << name << " must be " << rule << ", got " << value;
    return Error{ErrorCode::invalid_argument, message.str()};
  };
  if (!std::isfinite(v0) || v0 < 0.0 || v0 > max_variance) {
    return bad("v0 (initial variance)", v0, "finite and in [0, 25]; it is a variance, not a vol");
  }
  if (!std::isfinite(kappa) || kappa <= 0.0 || kappa > max_kappa) {
    return bad("kappa", kappa, "finite and in (0, 100]");
  }
  if (!std::isfinite(theta) || theta <= 0.0 || theta > max_variance) {
    return bad("theta (long-run variance)", theta, "finite and in (0, 25]");
  }
  if (!std::isfinite(xi) || xi < 0.0 || xi > max_vol_of_vol) {
    return bad("xi (vol of vol)", xi, "finite and in [0, 10]");
  }
  if (!std::isfinite(rho) || rho < -1.0 || rho > 1.0) {
    return bad("rho", rho, "finite and in [-1, 1]");
  }
  return HestonParams(v0, kappa, theta, xi, rho);
}

bool HestonParams::feller_satisfied() const noexcept {
  return 2.0 * kappa_ * theta_ >= xi_ * xi_;
}

double expected_integrated_variance(const HestonParams& p, double tau) noexcept {
  const double decay = -std::expm1(-p.kappa() * tau) / p.kappa();  // (1 - e^{-kappa tau}) / kappa
  return p.theta() * tau + (p.v0() - p.theta()) * decay;
}

// ---------------------------------------------------------------------------------------------
// Characteristic function (formulation 2 of Albrecher et al., rearranged to be exact at xi = 0)
//
//   a = kappa - rho xi i u,   h = i u + u^2,   d = sqrt(a^2 + xi^2 h),   g = (a - d) / (a + d)
//   D = -h / (a + d) * (1 - e^{-d tau}) / (1 - g e^{-d tau})
//   C = kappa theta / xi^2 * [ (a - d) tau - 2 ln((1 - g e^{-d tau}) / (1 - g)) ]
//   phi = exp(C + D v0)
//
// Since a^2 - d^2 = -xi^2 h, both (a - d) / xi^2 = -h / (a + d) and g / xi^2 = -h / (a + d)^2
// are computed directly, so nothing is divided by xi^2 and the cancellation that ruins the
// textbook form for small xi does not occur.
// ---------------------------------------------------------------------------------------------
Complex log_forward_characteristic_function(const HestonParams& p, double tau, Complex u) noexcept {
  const double xi2 = p.xi() * p.xi();
  const Complex iu(-u.imag(), u.real());
  const Complex h = iu + u * u;
  const Complex a = p.kappa() - p.rho() * p.xi() * iu;
  if (h == Complex(0.0, 0.0)) {
    // u = 0 or u = -i. phi is exactly 1 there (total probability, and the martingale property
    // E[S_T] = F), but the formulas below are 0/0. The point is a removable singularity.
    return {1.0, 0.0};
  }
  const Complex d = std::sqrt(a * a + xi2 * h);  // principal root: Re(d) >= 0
  // a + d without cancellation: when Re(a) < 0 (rho xi > kappa, possible for large vol of vol),
  // d is close to -a and a + d loses digits, so use a + d = xi^2 h / (d - a) instead.
  const Complex apd = (a.real() >= 0.0) ? a + d : xi2 * h / (d - a);
  const Complex e = std::exp(-d * tau);
  const Complex g_over_xi2 = -h / (apd * apd);
  const Complex g = xi2 * g_over_xi2;
  const Complex one_minus_g = 1.0 - g;
  const Complex big_d = -h / apd * (1.0 - e) / (1.0 - g * e);
  const Complex z = g * (1.0 - e) / one_minus_g;  // ln((1 - g e)/(1 - g)) = ln(1 + z)
  const Complex z_over_xi2 = g_over_xi2 * (1.0 - e) / one_minus_g;
  const Complex big_c =
      p.kappa() * p.theta() * (-h * tau / apd - 2.0 * z_over_xi2 * log1p_over_z(z));
  return std::exp(big_c + big_d * p.v0());
}

// ---------------------------------------------------------------------------------------------
// Price
//
// With F the forward and x = ln(F / K), Lewis (2001) gives, for X = ln(S_T / F),
//   E[(F e^X - K)^+] = F - (sqrt(F K) / pi) * Integral_0^inf  Re[ e^{i u x} phi(u - i/2) ] / (u^2 +
//   1/4) du
// and by parity E[(K - F e^X)^+] = K - (the same integral term). The out-of-the-money option is
// priced directly as min(F, K) - I (the cancellation is then against a small number), and the
// in-the-money one follows from put-call parity.
// ---------------------------------------------------------------------------------------------
Result<double> heston_price(const HestonVanilla& market,
                            const HestonParams& params,
                            OptionType type) {
  const double s = market.spot.value();
  const double k = market.strike.value();
  const double rd = market.domestic.value();
  const double rf = market.foreign.value();
  const double tau = market.expiry.value();
  const double df_d = std::exp(-rd * tau);
  const double forward = s * std::exp((rd - rf) * tau);
  const bool is_call = (type == OptionType::call);

  if (!std::isfinite(forward) || !std::isfinite(df_d) || forward <= 0.0) {
    return Error{ErrorCode::numerical_failure, "Heston forward or discount factor is not finite"};
  }

  const double total_variance = expected_integrated_variance(params, tau);
  if (tau <= 0.0 || total_variance < 1e-14) {
    // No randomness left: discounted intrinsic value of the forward (spot at zero expiry).
    const double f_eff = (tau <= 0.0) ? s : forward;
    const double intrinsic = is_call ? std::max(f_eff - k, 0.0) : std::max(k - f_eff, 0.0);
    return df_d * intrinsic;
  }

  const double x = std::log(forward / k);
  bool non_finite = false;
  const auto integrand = [&](double t) {
    // u = t / (1 - t) maps [0, 1) onto [0, inf); du = dt / (1 - t)^2.
    const double one_minus_t = 1.0 - t;
    const double u = t / one_minus_t;
    const Complex phi = log_forward_characteristic_function(params, tau, Complex(u, -0.5));
    const Complex phase(std::cos(u * x), std::sin(u * x));
    const double value = (phase * phi).real() / (u * u + 0.25) / (one_minus_t * one_minus_t);
    if (!std::isfinite(value)) {
      non_finite = true;
      return 0.0;
    }
    return value;
  };

  const double scale = std::sqrt(forward * k) / std::numbers::pi;
  // Target: price error below 1e-14 * F (absolute); the integral is multiplied by `scale`.
  const double abs_tol = 1e-14 * forward / scale;
  const numerics::QuadratureResult q =
      numerics::adaptive_gauss_kronrod(integrand, 0.0, 1.0, abs_tol, 1e-13, 4000);
  if (non_finite) {
    return Error{ErrorCode::numerical_failure,
                 "Heston characteristic function produced a non-finite value"};
  }
  if (!q.converged) {
    std::ostringstream message;
    message << "Heston quadrature did not reach its tolerance (error estimate " << q.error << ")";
    return Error{ErrorCode::not_converged, message.str()};
  }

  const double otm = df_d * (std::min(forward, k) - scale * q.value);
  // The out-of-the-money value cannot be negative; tiny negatives are rounding, larger ones are
  // a failure and must not be hidden.
  if (!std::isfinite(otm) || otm < -1e-11 * df_d * forward) {
    return Error{ErrorCode::numerical_failure, "Heston price is not finite or is negative"};
  }
  const double otm_value = std::max(otm, 0.0);
  const bool call_is_otm = (k >= forward);
  const double itm_extra = df_d * std::abs(forward - k);
  const double price = (is_call == call_is_otm) ? otm_value : otm_value + itm_extra;

  const double upper =
      is_call ? df_d * forward : df_d * k;  // call <= S e^{-r_f T}, put <= K e^{-r_d T}
  if (!std::isfinite(price) || price > upper * (1.0 + 1e-9)) {
    return Error{ErrorCode::numerical_failure, "Heston price violates the no-arbitrage bound"};
  }
  return price;
}

}  // namespace fxval::models
