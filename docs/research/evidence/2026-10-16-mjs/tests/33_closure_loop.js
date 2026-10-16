let fs = []; for (let i = 0; i < 3; i++) { fs.push(function(){ return i; }); } print("loop closures", fs[0](), fs[1](), fs[2]());
