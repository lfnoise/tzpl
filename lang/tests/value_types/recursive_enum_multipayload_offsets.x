-- Regression test for the recursive-enum inline-payload offset bug introduced
-- by commit c1f37d4 ("inline recursive-enum payloads"), which broke every
-- delay-using synthdef compiled through lang/modules/synthdef.x.
--
-- A large recursive enum (the SignalExprKind shape) has a multi-payload case
-- `delay(DelayVar, DelayOp)` whose FIRST payload, DelayVar, is a multi-word
-- struct that is itself reachable through the recursion (it carries
-- Option<SignalExpr>). The classifier's recursion detection was reset on every
-- re-classification, so whether DelayVar was seen as recursive depended on the
-- entry point: reached mid-cycle it was mis-classified as a non-recursive
-- multi-word inline value, so the SECOND payload (DelayOp) was read at the
-- wrong offset and the VM crashed. The fix makes isRecursive_ sticky and
-- iterates type re-classification to a fixpoint.
--
-- The crash needs both the large enum (so the cycle-break point is order
-- dependent) and a build-then-destructure path; this reproduces both, starting
-- with a directly-constructed value and then the dynamic-scope build path.
enum Rate { constant, init, reset, event, audio }
struct NumType(Int);
enum ControlWarp { linear, exponential, step Float, signedSquare, cubed }
struct ControlSpec { lo Float, hi Float, init Float, warp ControlWarp }
type ID = Int;
struct SignalExpr { id ID, ins [SignalExpr], kind SignalExprKind }
type S = SignalExpr;
enum UnaryOp { neg, abs }
enum BinaryOp { add, sub, mul }
enum CompareOp { lt, le, eq, ne, ge, gt }
enum CastOp { i32, i64, f32, f64 }
enum VecOp { at, reverse, reduce(BinaryOp, Int) }
enum Interpolation { none, linear, cubic, lagrange, sinc }
enum DelayOp { maxDelayTime, init Int, read Int, vread Interpolation, write }
enum RandOp { irand(Int, Int), frand(Float, Float), unipolar, bipolar, bits }
struct DelayVar { id ID, maxDelay Option<S> }
struct BufferVar { id ID }
enum BufferOp { fixRead(Int, Int, Int), vread(Interpolation, Int, Int), write(Int, Int), length }
struct SignalGraph { exprs [SignalExpr], delays [DelayVar], buffers [BufferVar], root SignalExpr }
enum SignalExprKind {
	sampleRate, sampleDur,
	int ([Int], NumType), float ([Float], NumType),
	unop UnaryOp, binop BinaryOp, compareop CompareOp, castop CastOp,
	random (RandOp, Rate, Int), vecop VecOp,
	inlet(NumType, Int, String), outlet(String),
	control(ControlSpec, Int, String), noteParam(ControlSpec, Int, String),
	delay(DelayVar, DelayOp), buffer(BufferVar, BufferOp),
	if_(SignalGraph, SignalGraph), for_(Int, SignalGraph), switch_([SignalGraph]),
	select, select2, varexpr String,
	voicer(Int, SignalGraph), spectralChain(Int, Int, SignalGraph), spectralFrame Int,
	debug(String, Int, Int),
}

fn probe(e SignalExpr) Void {
	match (e.kind) {
		delay(dv, op): {
			match (op) {
				maxDelayTime: println("max");
				init(n): println("init " $ n toString);
				read(n): println("read " $ n toString);
				vread(i): println("vread");
				write: println("write");
			}
		}
		_: println("other");
	}
}

let dvv = DelayVar { id: 0, maxDelay: Option<S>.none };
let arr [SignalExpr] = [SignalExpr { id: 0, ins: [S](), kind: SignalExprKind.delay(dvv, DelayOp.read(1)) }];
for (e : arr) { probe(e); }
println("DONE");

-- Replicate the _makeTopGraph dynamic-scope build path
fn _nextId() ID { let o = `ids; `ids = `ids + 1; o }
fn _add(e S) S { `cur = `cur push(e); e }
fn mkRead(d DelayVar) S {
	SignalExpr { id: _nextId(), ins: [S](), kind: SignalExprKind.delay(d, DelayOp.read(1)) } _add
}
fn build() SignalGraph {
	var `cur [SignalExpr] = [];
	var `ids Int = 0;
	let dv = DelayVar { id: 0, maxDelay: Option<S>.none };
	let r = mkRead(dv);
	SignalGraph { exprs: `cur, delays: [DelayVar](), buffers: [BufferVar](), root: r }
}
println("=== dynamic-scope build path ===");
let g = build();
println("exprs " $ g.exprs length toString);
for (e : g.exprs) { probe(e); }
println("DONE2");
