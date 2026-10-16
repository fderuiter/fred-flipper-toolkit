function r(n){ if (n === 0) return 0; return 1 + r(n - 1); } print("r10000", r(10000));
