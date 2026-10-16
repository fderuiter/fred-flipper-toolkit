function r(n){ if (n === 0) return 0; return 1 + r(n - 1); } print("r100", r(100)); print("r1000", r(1000)); print("r10000", r(10000)); print("r100000", r(100000));
