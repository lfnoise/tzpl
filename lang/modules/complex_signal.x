import synthdef.*;

struct ComplexSignal { re S, im S };

fn complex(re S, im S) ComplexSignal = ComplexSignal { re: re, im: im };
fn complex(re S, im AsSignal) ComplexSignal = ComplexSignal { re: re, im: im asSignal};
fn complex(re AsSignal, im S) ComplexSignal = ComplexSignal { re: re asSignal, im: im };

fn +(a ComplexSignal, b ComplexSignal) ComplexSignal = ComplexSignal { re: a.re + b.re, im: a.im + b.im };
fn +(a ComplexSignal, b AsSignal) ComplexSignal = ComplexSignal { re: a.re + b, im: a.im };
fn +(a AsSignal, b ComplexSignal) ComplexSignal = ComplexSignal { re: a + b.re, im: b.im };

fn -(a ComplexSignal, b ComplexSignal) ComplexSignal = ComplexSignal { re: a.re - b.re, im: a.im - b.im };
fn -(a ComplexSignal, b AsSignal) ComplexSignal = ComplexSignal { re: a.re - b, im: a.im };
fn -(a AsSignal, b ComplexSignal) ComplexSignal = ComplexSignal { re: a - b.re, im: b.im };

fn *(a ComplexSignal, b ComplexSignal) ComplexSignal = 
	ComplexSignal { re: a.re * b.re - a.im * b.im, im: a.re * b.im + a.im * b.re };
fn *(a ComplexSignal, b AsSignal) ComplexSignal = ComplexSignal { re: a.re * b, im: a.im * b };
fn *(a AsSignal, b ComplexSignal) ComplexSignal = ComplexSignal { re: a * b.re, im: a * b.im };

fn /(a ComplexSignal, b ComplexSignal) ComplexSignal {
	let denom = b.re sq + b.im sq;
	ComplexSignal { 
		re: (a.re * b.re + a.im * b.im) / denom, 
		im: (a.im * b.re - a.re * b.im) / denom }
}
fn /(a ComplexSignal, b AsSignal) = a / b asSignal;
fn /(a AsSignal, b ComplexSignal) = a asSignal / b;




