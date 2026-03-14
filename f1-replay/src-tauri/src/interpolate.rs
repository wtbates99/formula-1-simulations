/// Natural cubic spline interpolation using the Thomas algorithm (tridiagonal solver).
#[derive(Debug, Clone)]
pub struct Spline {
    ts: Vec<f64>,
    a: Vec<f64>,
    b: Vec<f64>,
    c: Vec<f64>,
    d: Vec<f64>,
}

impl Spline {
    /// Compute a natural cubic spline from sorted (ts, ys) pairs.
    /// ts must be strictly increasing.
    pub fn new(ts: &[f64], ys: &[f64]) -> Self {
        let n = ts.len();
        assert_eq!(n, ys.len(), "ts and ys must have equal length");

        if n == 0 {
            return Spline {
                ts: vec![],
                a: vec![],
                b: vec![],
                c: vec![],
                d: vec![],
            };
        }
        if n == 1 {
            return Spline {
                ts: ts.to_vec(),
                a: ys.to_vec(),
                b: vec![0.0],
                c: vec![0.0],
                d: vec![0.0],
            };
        }
        if n == 2 {
            let h = ts[1] - ts[0];
            let slope = if h.abs() > 1e-12 {
                (ys[1] - ys[0]) / h
            } else {
                0.0
            };
            return Spline {
                ts: ts.to_vec(),
                a: ys.to_vec(),
                b: vec![slope, slope],
                c: vec![0.0, 0.0],
                d: vec![0.0, 0.0],
            };
        }

        // Number of intervals
        let m = n - 1;

        // Step sizes
        let h: Vec<f64> = (0..m).map(|i| ts[i + 1] - ts[i]).collect();

        // Set up the tridiagonal system for the second derivatives (sigma).
        // For a natural spline: sigma[0] = 0, sigma[n-1] = 0.
        // Interior equations: h[i-1]*sigma[i-1] + 2*(h[i-1]+h[i])*sigma[i] + h[i]*sigma[i+1]
        //   = 3 * ((ys[i+1]-ys[i])/h[i] - (ys[i]-ys[i-1])/h[i-1])
        // We solve for sigma[1..n-2] (the interior nodes).

        let interior = n - 2; // number of unknowns

        let mut diag = vec![0.0f64; interior];
        let mut upper = vec![0.0f64; interior - 1];
        let mut lower = vec![0.0f64; interior - 1];
        let mut rhs = vec![0.0f64; interior];

        for i in 0..interior {
            let gi = i + 1; // global index
            diag[i] = 2.0 * (h[gi - 1] + h[gi]);
            rhs[i] = 3.0 * ((ys[gi + 1] - ys[gi]) / h[gi] - (ys[gi] - ys[gi - 1]) / h[gi - 1]);
        }
        for i in 0..interior - 1 {
            let gi = i + 1;
            upper[i] = h[gi];
            lower[i] = h[gi];
        }

        // Thomas algorithm (forward sweep + back substitution)
        let mut c_prime = vec![0.0f64; interior];
        let mut d_prime = vec![0.0f64; interior];

        // When interior == 1 there are no off-diagonal entries; skip c_prime[0]
        if interior > 1 {
            c_prime[0] = upper[0] / diag[0];
        }
        d_prime[0] = rhs[0] / diag[0];

        for i in 1..interior {
            let denom = diag[i] - lower[i - 1] * c_prime[i - 1];
            if i < interior - 1 {
                c_prime[i] = upper[i] / denom;
            }
            d_prime[i] = (rhs[i] - lower[i - 1] * d_prime[i - 1]) / denom;
        }

        // Back substitution
        let mut sigma = vec![0.0f64; n];
        sigma[n - 1] = 0.0; // natural BC
        sigma[interior] = d_prime[interior - 1]; // sigma[n-2]
        for i in (0..interior - 1).rev() {
            sigma[i + 1] = d_prime[i] - c_prime[i] * sigma[i + 2];
        }
        sigma[0] = 0.0; // natural BC

        // Compute spline coefficients for each segment i in [0, m):
        // S_i(t) = a_i + b_i*(t-t_i) + c_i*(t-t_i)^2 + d_i*(t-t_i)^3
        let mut a_v = vec![0.0f64; m];
        let mut b_v = vec![0.0f64; m];
        let mut c_v = vec![0.0f64; m];
        let mut d_v = vec![0.0f64; m];

        for i in 0..m {
            a_v[i] = ys[i];
            b_v[i] = (ys[i + 1] - ys[i]) / h[i]
                - h[i] * (2.0 * sigma[i] + sigma[i + 1]) / 3.0;
            c_v[i] = sigma[i];
            d_v[i] = (sigma[i + 1] - sigma[i]) / (3.0 * h[i]);
        }

        Spline {
            ts: ts.to_vec(),
            a: a_v,
            b: b_v,
            c: c_v,
            d: d_v,
        }
    }

    /// Evaluate the spline at time t.
    /// Clamps t to [t_0, t_n] (no extrapolation).
    pub fn eval(&self, t: f64) -> f64 {
        let n = self.ts.len();
        if n == 0 {
            return 0.0;
        }
        if n == 1 {
            return self.a[0];
        }

        // Clamp
        let t = t.clamp(self.ts[0], self.ts[n - 1]);

        // Binary search for the segment index i such that ts[i] <= t < ts[i+1]
        let m = self.a.len(); // number of segments = n - 1
        let i = match self.ts.binary_search_by(|v| v.partial_cmp(&t).unwrap()) {
            Ok(idx) => {
                // Exact match: use this segment, but cap at last segment
                idx.min(m - 1)
            }
            Err(idx) => {
                // idx is the insertion point; the segment is idx-1
                if idx == 0 { 0 } else { (idx - 1).min(m - 1) }
            }
        };

        let dt = t - self.ts[i];
        self.a[i] + self.b[i] * dt + self.c[i] * dt * dt + self.d[i] * dt * dt * dt
    }

    /// Check if the spline is empty (no data points).
    pub fn is_empty(&self) -> bool {
        self.ts.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_spline_empty() {
        let s = Spline::new(&[], &[]);
        assert_eq!(s.eval(0.0), 0.0);
        assert_eq!(s.eval(1.0), 0.0);
    }

    #[test]
    fn test_spline_single_point() {
        let s = Spline::new(&[1.0], &[42.0]);
        assert_eq!(s.eval(0.0), 42.0);
        assert_eq!(s.eval(1.0), 42.0);
        assert_eq!(s.eval(100.0), 42.0);
    }

    #[test]
    fn test_spline_linear_exact() {
        // Linear data — spline should reproduce it exactly
        let ts = vec![0.0, 1.0, 2.0, 3.0];
        let ys = vec![0.0, 1.0, 2.0, 3.0];
        let s = Spline::new(&ts, &ys);
        assert!((s.eval(0.0) - 0.0).abs() < 1e-10);
        assert!((s.eval(1.0) - 1.0).abs() < 1e-10);
        assert!((s.eval(1.5) - 1.5).abs() < 1e-6);
        assert!((s.eval(3.0) - 3.0).abs() < 1e-10);
    }

    #[test]
    fn test_spline_quadratic_fit() {
        // Quadratic y = x^2: spline should fit closely
        let ts: Vec<f64> = (0..=10).map(|i| i as f64).collect();
        let ys: Vec<f64> = ts.iter().map(|&t| t * t).collect();
        let s = Spline::new(&ts, &ys);
        // Interior points should be very close
        for i in 0..10 {
            let t = i as f64 + 0.5;
            let expected = t * t;
            let got = s.eval(t);
            assert!((got - expected).abs() < 0.1, "t={t}: expected {expected}, got {got}");
        }
    }

    #[test]
    fn test_spline_clamping() {
        let ts = vec![1.0, 2.0, 3.0];
        let ys = vec![10.0, 20.0, 30.0];
        let s = Spline::new(&ts, &ys);
        // Before start → first value
        assert_eq!(s.eval(0.0), 10.0);
        // After end → last value
        assert_eq!(s.eval(100.0), 30.0);
    }

    #[test]
    fn test_spline_two_points() {
        let ts = vec![0.0, 10.0];
        let ys = vec![5.0, 15.0];
        let s = Spline::new(&ts, &ys);
        assert!((s.eval(0.0) - 5.0).abs() < 1e-10);
        assert!((s.eval(10.0) - 15.0).abs() < 1e-10);
        assert!((s.eval(5.0) - 10.0).abs() < 1e-6);
    }

    #[test]
    fn test_spline_non_uniform_spacing() {
        // Non-uniform: 0, 0.1, 5.0, 5.1, 10.0
        let ts = vec![0.0, 0.1, 5.0, 5.1, 10.0];
        let ys = vec![0.0, 0.1, 5.0, 5.1, 10.0]; // linear
        let s = Spline::new(&ts, &ys);
        // Should still be approximately linear
        assert!((s.eval(2.5) - 2.5).abs() < 0.5);
        assert!((s.eval(7.5) - 7.5).abs() < 0.5);
    }
}
