/// See Wikipedia [entry](https://en.wikipedia.org/wiki/Binary_GCD_algorithm).
/// Stein's algo is based on 4 identities:
///
/// 1. `gcd(u, 0) = u`
///
/// 2. `gcd(2 * u, 2 * v) = 2 * gcd(u, v)`, where both `2 * u` and `2 * v` are even
///
/// 3. `gcd(2 * u, v) = gcd(u, v)`, where `2 * u` is even and `v` is odd
///
/// 4. `gcd(u, v) = gcd(u, v - u)`, where both `u` and `v` are odd and `v >= u`
///
/// ## Identities 1, 2, 3
///
/// Identity 1 means that, for any non-zero `u` and 0, `u` is the GCD because zero can be
/// divided by any non-zero number wholly.
///
/// Identity 2 means that every time both `u` and `v` are even, we can factor out 2 into
/// the GCD. E.g. for 12 and 20 GCD will include 2 reducing the numbers to 6 and 10. Then
/// we repeat: another 2 goes into GCD making it 4 and reducing numbers to 3 and 5.
///
/// Identity 3 means that an even `2 * u` and an odd `v` can't have 2 as one of their
/// common divisors, i.e. their GCD will not include 2. So we can reduce `2 * u` to just
/// `u' and have the same GCD. E.g. for 12 and 15 GCD will not include 2 so we can reduce
/// numbers to 6 and 15.
///
/// ## Identity 4
///
/// Both `u` and `v` are odd and `v` is greater or equal to `u`.
///
/// Suppose `GCD(u, v) = c`, which means they can be rewritten as `u = a * c` and
/// `v = b * c`, where `b >= a` because `v >= u`. Since `c` is the _greatest_ common
/// divisor, `GCD(a, b) = 1`, i.e. `a` and `b` have no common divisors. Otherwise those
/// divisors would necessarily be part of `c`.
///
/// We claim that `GCD(u, v) = GCD(u, v - u) = c`.
///
/// Rewrite `GCD(u, v - u)` as `GCD(a * c, (b - a) * c)`. For `GCD(a * c, (b - a) * c)`
/// to be equal to `c`, `a` and `b - a` should have no common divisors.
///
/// Suppose `a` and `b - a` have a common divisor `k > 1`, then they can be rewritten as
/// `a = n * k` and `b - a = m * k`. Then `b = a + m * k = n * k + m * k = (n + m) * k`.
/// Then `GCD(a, b) = GCD(n * k, (n + m) * k) > 1` which contradicts the initial
/// `GCD(a, b) = 1`.
///
/// ## Algorithm
///
/// 0. Check identity 1 right away. If `u = 0`, then `GCD = v` and exit. If `v = 0`, then
///    `GCD = u` and exit.
///
/// 1. Repeatedly remove all 2's using identity 2 until either `u` or `v` becomes odd.
///    Remember the number of 2's removed as those are part of the final GCD.
///
/// 2. Repeatedly remove all 2's from either `u` or `v` (whichever remained even at step
///    1 above) using identity 3 until they both become odd.
///
/// 3. Check if `v >= u`. If not, swap `u` and `v` to satisfy the precondition of the
///    identity 4. For our numbers 5 and 3 we swap to get 3 and 5.
///
/// 4. Apply identity 4 to reduce `v` to `v - u`. Because both `u` and `v` are odd,
///    `v - u` is even. For our numbers 3 and 5 we get 3 and 2.
///
/// 5. Check if new `v` is 0. If it is, apply identity 1 to get `GCD(u, 0) = u`. Obtain
///    the final GCD by multiplying `u` by all 2's that were remembered at step 1. Exit.
///
/// 6. New `v` is greater than 0 and is even (see step 4). Repeatedly remove all 2's from
///    `v` using identity 3 so it's odd again. Goto step 3.
///
/// ## Example
///
/// Using numbers `u = 40` and `v = 48` we get:
///
/// Step 1. Remove three 2's which reduces 40 and 48 to 5 and 6. We remember to multiply
/// the GCD by 8 (i.e. 2 * 2 * 2) later to get the final GCD.
///
/// Step 2. Remove 2 from 6 to make it odd. 5 and 6 reduce to 5 and 3.
///
/// Step 3. `v = 3 < u = 5`. Swap `u` and `v` to get `u = 3` and `v = 5`.
///
/// Step 4. Reduce `v = 5` to `v - u = 5 - 3`. We get `u = 3` and `v = 2`.
///
/// Step 5. `v = 2 > 0`. Continue to step 6.
///
/// Step 6. Remove 2 from `v = 2` to get `u = 3` and `v = 1`. Goto step 3.
///
/// Step 3. `v = 1 < u = 3`. Swap to get `u = 1` and `v = 3`.
///
/// Step 4. Reduce `v = 3` to `v - u = 3 - 1`. We get `u = 1` and `v = 2`.
///
/// Step 5. `v = 2 > 0`. Continue to step 6.
///
/// Step 6. Remove 2 from `v = 2` to get `u = 1` and `v = 1`. Goto step 3.
///
/// Step 3. `v = 1 >= u = 1`. Ok, no swap needed.
///
/// Step 4. Reduce `v = 1` to `v - u = 1 - 1`. We get `u = 1` and `v = 0`.
///
/// Step 5. `v = 0`. `GCD(1, 0) = 1`. Multiply by three 2's from step 1 to get the final
/// GCD of 8. Exit.

pub fn gcd(mut u: u64, mut v: u64) -> u64 {
    // step 0: check if identity 1 works right away
    if u == 0 {
        return v;
    } else if v == 0 {
        return u;
    }

    // steps 1 and 2: remove all 2's until both u and v are odd, remember how many 2's
    // were in common - those are part of GCD
    let twos_u = u.trailing_zeros();
    u >>= twos_u;
    let twos_v = v.trailing_zeros();
    v >>= twos_v;
    let twos_gcd = std::cmp::min(twos_u, twos_v);

    loop {
        // step 3: swap u and v if not v >= u
        if v < u {
            std::mem::swap(&mut u, &mut v);
        }

        // step 4: reduce v to v - u
        v -= u;

        // step 5: if v = 0, return GCD multiplied by 2's remembered at step 1
        if v == 0 {
            return u << twos_gcd;
        }

        // step 6: v > 0 and even, remove all 2's make it odd again
        v >>= v.trailing_zeros();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        assert_eq!(gcd(0, 0), 0);
        assert_eq!(gcd(3, 0), 3);
        assert_eq!(gcd(0, 3), 3);
        assert_eq!(gcd(1, 1), 1);
        assert_eq!(gcd(8 * 3, 8 * 5), 8);
        assert_eq!(gcd(8 * 3, 8 * 2 * 5), 8);
        assert_eq!(gcd(5, 3), 1);
        assert_eq!(gcd(7, 7), 7);
    }
}
