function each(a, f){ for (let i=0;i<a.length;i++) f(a[i], i); } let sum=0; each([1,2,3], function(v){ sum = sum + v; }); print("cb via global", sum);
