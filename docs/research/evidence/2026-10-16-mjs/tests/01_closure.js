function mk(){ var n = 0; return function(){ n = n + 1; return n; }; } var c = mk(); c(); c(); print("closure", c()); var c2 = mk(); print("closure2", c2(), c());
