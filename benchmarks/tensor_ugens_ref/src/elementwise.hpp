//
//  elementwise.hpp
//  tensor-ugens
//
//  Run functions for elementwise UGens with 1 to 6 inputs. Each runs its op
//  a vector at a time when the op accepts V operands, and otherwise a
//  sample at a time.
//

#ifndef elementwise_h
#define elementwise_h

#include <cassert>
#include <type_traits>
#include <utility>
#include "graph.hpp"

// Vector input of a wire: its buffer, or its splatted scalar. Stepping by the
// wire's stride (1 or 0) then advances one vector, or stays on the constant.
inline const VP* vecInput(Wire& w) {
	return w.stride ? (const VP*)w.buffer : &w.splat;
}

// Runs the elementwise Op over u's inputs a vector at a time. Each *Op_run
// below uses this when Op accepts V operands, and otherwise runs Op a sample
// at a time.
template <typename Op, size_t... I>
void vecRun(UGen* u, int n, std::index_sequence<I...>) {
	Op op;
	Output& py = u->outs[0];
	assert(((u->ins[I].chans == py.chans) && ...));
	assert(n % kVecLen == 0);
	int nv = n / kVecLen;
	for (int i = 0; i < py.chans; ++i) {
		const VP* in[] = { vecInput(*u->ins[I].wires[i])... };
		int inc[] = { u->ins[I].wires[i]->stride... };
		V* y = (V*)py.wires[i].buffer;
		
		for (int v = 0; v < nv; ++v) {
			y[v] = op(V(*in[I])...);
			((in[I] += inc[I]), ...);
		}
	}
}

template <typename Op>
void UnOp_run(UGen* u, int n) {
	Op op;
	Input& pa = u->ins[0];
	Output& py = u->outs[0];
	assert(pa.chans == py.chans);
	for (int i = 0; i < py.chans; ++i) {
		// A unary op on a scalar is constant folded instead of run.
		assert(pa.wires[i]->stride == 1);
	}
	if constexpr (std::is_invocable_v<Op, V>) {
		vecRun<Op>(u, n, std::make_index_sequence<1>{});
		return;
	}
	for (int i = 0; i < py.chans; ++i) {
		Z* a = pa.wires[i]->buffer;
		Z* y = py.wires[i].buffer;

		for (int j = 0; j < n; ++j) {
			y[j] = op(a[j]);
		}
	}
}

template <typename Op>
void BinOp_run(UGen* u, int n) {
	Op op;
	Input& pa = u->ins[0];
	Input& pb = u->ins[1];
	Output& py = u->outs[0];
	assert(pa.chans == py.chans);
	assert(pb.chans == py.chans);
	if constexpr (std::is_invocable_v<Op, V, V>) {
		vecRun<Op>(u, n, std::make_index_sequence<2>{});
		return;
	}
	for (int i = 0; i < py.chans; ++i) {
		Wire& wa = *pa.wires[i];
		Wire& wb = *pb.wires[i];
		Wire& wy = py.wires[i];
		Z* a = wa.buffer;
		Z* b = wb.buffer;
		Z* y = wy.buffer;
		
		int ainc = wa.stride;
		int binc = wb.stride;
		
		for (int j = 0; j < n; ++j) {
			y[j] = op(*a, *b);
			a += ainc;
			b += binc;
		}
	}
}

template <typename Op>
void TriOp_run(UGen* u, int n) {
	if constexpr (std::is_invocable_v<Op, V, V, V>) {
		vecRun<Op>(u, n, std::make_index_sequence<3>{});
		return;
	}
	Op op;
	Input& pa = u->ins[0];
	Input& pb = u->ins[1];
	Input& pc = u->ins[2];
	Output& py = u->outs[0];
	assert(pa.chans == py.chans);
	assert(pb.chans == py.chans);
	assert(pc.chans == py.chans);
	for (int i = 0; i < py.chans; ++i) {
		Wire& wa = *pa.wires[i];
		Wire& wb = *pb.wires[i];
		Wire& wc = *pc.wires[i];
		Wire& wy = py.wires[i];
		Z* a = wa.buffer;
		Z* b = wb.buffer;
		Z* c = wc.buffer;
		Z* y = wy.buffer;
		
		int ainc = wa.stride;
		int binc = wb.stride;
		int cinc = wc.stride;
		
		for (int j = 0; j < n; ++j) {
			y[j] = op(*a, *b, *c);
			a += ainc;
			b += binc;
			c += cinc;
		}
	}
}

template <typename Op>
void QuadOp_run(UGen* u, int n) {
	if constexpr (std::is_invocable_v<Op, V, V, V, V>) {
		vecRun<Op>(u, n, std::make_index_sequence<4>{});
		return;
	}
	Op op;
	Input& pa = u->ins[0];
	Input& pb = u->ins[1];
	Input& pc = u->ins[2];
	Input& pd = u->ins[3];
	Output& py = u->outs[0];
	assert(pa.chans == py.chans);
	assert(pb.chans == py.chans);
	assert(pc.chans == py.chans);
	assert(pd.chans == py.chans);
	for (int i = 0; i < py.chans; ++i) {
		Wire& wa = *pa.wires[i];
		Wire& wb = *pb.wires[i];
		Wire& wc = *pc.wires[i];
		Wire& wd = *pd.wires[i];
		Wire& wy = py.wires[i];
		Z* a = wa.buffer;
		Z* b = wb.buffer;
		Z* c = wc.buffer;
		Z* d = wd.buffer;
		Z* y = wy.buffer;
		
		int ainc = wa.stride;
		int binc = wb.stride;
		int cinc = wc.stride;
		int dinc = wd.stride;
		
		for (int j = 0; j < n; ++j) {
			y[j] = op(*a, *b, *c, *d);
			a += ainc;
			b += binc;
			c += cinc;
			d += dinc;
		}
	}
}

template <typename Op>
void QuintOp_run(UGen* u, int n) {
	if constexpr (std::is_invocable_v<Op, V, V, V, V, V>) {
		vecRun<Op>(u, n, std::make_index_sequence<5>{});
		return;
	}
	Op op;
	Input& pa = u->ins[0];
	Input& pb = u->ins[1];
	Input& pc = u->ins[2];
	Input& pd = u->ins[3];
	Input& pe = u->ins[4];
	Output& py = u->outs[0];
	assert(pa.chans == py.chans);
	assert(pb.chans == py.chans);
	assert(pc.chans == py.chans);
	assert(pd.chans == py.chans);
	assert(pe.chans == py.chans);
	for (int i = 0; i < py.chans; ++i) {
		Wire& wa = *pa.wires[i];
		Wire& wb = *pb.wires[i];
		Wire& wc = *pc.wires[i];
		Wire& wd = *pd.wires[i];
		Wire& we = *pe.wires[i];
		Wire& wy = py.wires[i];
		Z* a = wa.buffer;
		Z* b = wb.buffer;
		Z* c = wc.buffer;
		Z* d = wd.buffer;
		Z* e = we.buffer;
		Z* y = wy.buffer;
		
		int ainc = wa.stride;
		int binc = wb.stride;
		int cinc = wc.stride;
		int dinc = wd.stride;
		int einc = we.stride;
		
		for (int j = 0; j < n; ++j) {
			y[j] = op(*a, *b, *c, *d, *e);
			a += ainc;
			b += binc;
			c += cinc;
			d += dinc;
			e += einc;
		}
	}
}

template <typename Op>
void SextOp_run(UGen* u, int n) {
	if constexpr (std::is_invocable_v<Op, V, V, V, V, V, V>) {
		vecRun<Op>(u, n, std::make_index_sequence<6>{});
		return;
	}
	Op op;
	Input& pa = u->ins[0];
	Input& pb = u->ins[1];
	Input& pc = u->ins[2];
	Input& pd = u->ins[3];
	Input& pe = u->ins[4];
	Input& pf = u->ins[5];
	Output& py = u->outs[0];
	assert(pa.chans == py.chans);
	assert(pb.chans == py.chans);
	assert(pc.chans == py.chans);
	assert(pd.chans == py.chans);
	assert(pe.chans == py.chans);
	assert(pf.chans == py.chans);
	for (int i = 0; i < py.chans; ++i) {
		Wire& wa = *pa.wires[i];
		Wire& wb = *pb.wires[i];
		Wire& wc = *pc.wires[i];
		Wire& wd = *pd.wires[i];
		Wire& we = *pe.wires[i];
		Wire& wf = *pf.wires[i];
		Wire& wy = py.wires[i];
		Z* a = wa.buffer;
		Z* b = wb.buffer;
		Z* c = wc.buffer;
		Z* d = wd.buffer;
		Z* e = we.buffer;
		Z* f = wf.buffer;
		Z* y = wy.buffer;
		
		int ainc = wa.stride;
		int binc = wb.stride;
		int cinc = wc.stride;
		int dinc = wd.stride;
		int einc = we.stride;
		int finc = wf.stride;
		
		for (int j = 0; j < n; ++j) {
			y[j] = op(*a, *b, *c, *d, *e, *f);
			a += ainc;
			b += binc;
			c += cinc;
			d += dinc;
			e += einc;
			f += finc;
		}
	}
}


// Single-sample versions, for graphs compiled with a block size of 1. They
// compute one sample per call, so they stay scalar. A scalar wire's buffer
// points at its scalar, so every input reads through its buffer.
template <typename Op, size_t... I>
void scalarRun1(UGen* u, std::index_sequence<I...>) {
	Op op;
	Output& py = u->outs[0];
	assert(((u->ins[I].chans == py.chans) && ...));
	for (int i = 0; i < py.chans; ++i) {
		*py.wires[i].buffer = op(*u->ins[I].wires[i]->buffer...);
	}
}

template <typename Op>
void UnOp_run1(UGen* u) { scalarRun1<Op>(u, std::make_index_sequence<1>{}); }

template <typename Op>
void BinOp_run1(UGen* u) { scalarRun1<Op>(u, std::make_index_sequence<2>{}); }

template <typename Op>
void TriOp_run1(UGen* u) { scalarRun1<Op>(u, std::make_index_sequence<3>{}); }

template <typename Op>
void QuadOp_run1(UGen* u) { scalarRun1<Op>(u, std::make_index_sequence<4>{}); }

template <typename Op>
void QuintOp_run1(UGen* u) { scalarRun1<Op>(u, std::make_index_sequence<5>{}); }

template <typename Op>
void SextOp_run1(UGen* u) { scalarRun1<Op>(u, std::make_index_sequence<6>{}); }

#endif /* elementwise_h */
