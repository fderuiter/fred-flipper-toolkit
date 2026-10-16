function outer(){ function inner(){ return 42; } return inner(); } print("nested", outer()); print("hoist", typeof later); function later(){}
