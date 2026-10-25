//
//  graph.cpp
//  tensor-ugens
//

#include "graph.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <algorithm>

Wire gZeroWire;
Graph* gGraph = NULL;

static const size_t kBufferAlign = 64;

// Returns NULL on allocation failure; the graph is left unchanged
// (apart from possibly a larger g->buffers capacity).
static WireBuffer* newWireBuffer(Graph* g, int blockSize) {
    if (g->numWireBuffers == g->capWireBuffers) {
        int newCap = std::max(4, 2 * g->capWireBuffers);
        WireBuffer** buffers = (WireBuffer**)realloc(g->buffers, newCap * sizeof(WireBuffer*));
        if (!buffers) return NULL;
        g->buffers = buffers;
        g->capWireBuffers = newCap;
    }
    WireBuffer* buffer = (WireBuffer*)malloc(sizeof(WireBuffer));
    if (!buffer) return NULL;
    // Aligned to a cache line, so vector loads and stores never straddle two.
    size_t bytes = (blockSize * sizeof(Z) + kBufferAlign - 1) / kBufferAlign * kBufferAlign;
    Z* samples = (Z*)aligned_alloc(kBufferAlign, std::max(bytes, kBufferAlign));
    if (!samples) {
        free(buffer);
        return NULL;
    }
    memset(samples, 0, bytes);
    buffer->buffer = samples;
    buffer->count = blockSize;
    buffer->refCount = 0;
    g->buffers[g->numWireBuffers++] = buffer;
    return buffer;
}

// Returns false if the free list couldn't grow. The buffer then stays
// allocated in g->buffers but won't be recycled.
static bool freeWireBuffer(Graph* g, WireBuffer* buffer) {
    if (g->numFreeWireBuffers == g->capFreeWireBuffers) {
        int newCap = std::max(4, 2 * g->capFreeWireBuffers);
        WireBuffer** freeBuffers = (WireBuffer**)realloc(g->freeBuffers, newCap * sizeof(WireBuffer*));
        if (!freeBuffers) return false;
        g->freeBuffers = freeBuffers;
        g->capFreeWireBuffers = newCap;
    }
    g->freeBuffers[g->numFreeWireBuffers++] = buffer;
    return true;
}

static void WireBuffer_retain(WireBuffer* buffer, int n = 1) {
    buffer->refCount += n;
}

// Returns false only if the buffer hit zero refs and couldn't be put on the free list.
static bool WireBuffer_release(Graph* g, WireBuffer* buffer, int n = 1) {
    assert(buffer->refCount >= n);
    buffer->refCount -= n;
    if (n > 0 && buffer->refCount == 0) {
        return freeWireBuffer(g, buffer);
    }
    return true;
}

// Returns NULL on allocation failure.
static WireBuffer* getWireBuffer(Graph* g, int blockSize) {
    if (g->numFreeWireBuffers) {
        return g->freeBuffers[--g->numFreeWireBuffers];
    } else {
        return newWireBuffer(g, blockSize);
    }
}

void Wire_set(Wire* w, WireBuffer* wb) {
    w->buffer = wb->buffer;
    w->stride = 1;
    w->wb = wb;
}

void Wire_set(Wire* w, Z scalar) {
    w->buffer = &w->scalar;
    w->wb = NULL;
    w->scalar = scalar;
    w->splat = scalar;
    w->stride = 0;
}

Shape Shape_broadcast(int n, Output** sources) {
    Shape out = sources[0]->shape;
    for (int i = 1; i < n; ++i) {
        out = Shape_broadcast(&out, &sources[i]->shape);
    }
    return out;
}

void initialize() {
    Wire_set(&gZeroWire, 0.0);
}

// Frees the UGen's memory. Safe on a partially constructed UGen.
static void freeUGen(UGen* u) {
    if (u->ins) {
        for (int i = 0; i < u->numIn; ++i) {
            free(u->ins[i].wires);
        }
        free(u->ins);
    }
    if (u->outs) {
        for (int i = 0; i < u->numOut; ++i) {
            free(u->outs[i].wires);
        }
        free(u->outs);
    }
    free(u->state);
    free(u);
}

// Cleans up a partially constructed UGen and returns NULL for convenience.
static UGen* abandonUGen(UGen* u) {
    freeUGen(u);
    return NULL;
}

static void freeView(Output* v) {
    free(v->viewWires);
    free(v);
}

void Graph_free(Graph* g) {
    for (int i = 0; i < g->numUGens; ++i) {
        freeUGen(g->ugens[i]);
    }
    for (int i = 0; i < g->numConstants; ++i) {
        freeUGen(g->constants[i]);
    }
    for (int i = 0; i < g->numViews; ++i) {
        freeView(g->views[i]);
    }
    for (int i = 0; i < g->numWireBuffers; ++i) {
        free(g->buffers[i]->buffer);
        free(g->buffers[i]);
    }
    free(g->ugens);
    free(g->constants);
    free(g->views);
    free(g->buffers);
    free(g->freeBuffers);
    *g = Graph{};
}

// Ensures room to append one more pointer to array. Returns false on
// allocation failure, leaving array unchanged.
template <typename T>
static bool reserveOne(T**& array, int num, int& cap) {
    if (num < cap) return true;
    int newCap = std::max(4, 2 * cap);
    T** grown = (T**)realloc(array, newCap * sizeof(T*));
    if (!grown) return false;
    array = grown;
    cap = newCap;
    return true;
}

// Calls fn once for each distinct buffer-backable wire in wireAt(0..n),
// skipping gZeroWire.
template <typename WireAt, typename Fn>
static void forEachDistinctWire(int n, WireAt wireAt, Fn fn) {
    for (int c = 0; c < n; ++c) {
        Wire* w = wireAt(c);
        if (w == &gZeroWire) continue;
        bool seen = false;
        for (int k = 0; k < c && !seen; ++k) {
            seen = wireAt(k) == w;
        }
        if (!seen) fn(w);
    }
}

int broadcastChannel(Shape* srcShape, Shape* outShape, int w) {
    int* srcDims = srcShape->dims;
    int* outDims = outShape->dims;
    int srcStrides[kMaxRank];
    Shape_strides(srcShape, srcStrides);
    int rem = w, p = 0;
    for (int d = kMaxRank - 1; d >= 0; --d) {
        p += (rem % outDims[d] % srcDims[d]) * srcStrides[d];
        rem /= outDims[d];
    }
    return p;
}

bool mapInputWires(Input* in, Shape* outShape) {
    Output* src = in->source;
    int n = Shape_size(outShape);
    Wire** wires = (Wire**)realloc(in->wires, n * sizeof(Wire*));
    if (!wires && n > 0) return false;  // in->wires and in->chans left untouched
    in->wires = wires;
    in->chans = n;

    for (int w = 0; w < n; ++w) {
        in->wires[w] = outputWire(src, broadcastChannel(&src->shape, outShape, w));
    }
    return true;
}

// Wires in to src (or to gZeroWire if src is NULL) mapped to shape.
// Returns false on allocation failure, leaving in->chans 0.
static bool connectInput(Input* in, Output* src, Shape shape) {
    in->source = src;
    in->shape = shape;
    if (src) return mapInputWires(in, &shape);
    int n = Shape_size(&shape);
    Wire** wires = (Wire**)calloc(n, sizeof(Wire*));
    if (!wires && n > 0) return false;
    for (int j = 0; j < n; ++j) {
        wires[j] = &gZeroWire;
    }
    in->wires = wires;
    in->chans = n;
    return true;
}

Output* newView(Graph* g, Output* src, Shape shape, const int* map) {
    assert(!g->compiled);
    if (!reserveOne(g->views, g->numViews, g->capViews)) return NULL;
    Output* v = (Output*)calloc(1, sizeof(Output));
    if (!v) return NULL;
    int n = Shape_size(&shape);
    v->viewWires = (Wire**)calloc(n, sizeof(Wire*));
    if (!v->viewWires && n > 0) {
        free(v);
        return NULL;
    }
    v->shape = shape;
    v->chans = n;
    for (int c = 0; c < n; ++c) {
        assert(map[c] >= 0 && map[c] < src->chans);
        v->viewWires[c] = outputWire(src, map[c]);
    }
    g->views[g->numViews++] = v;
    return v;
}

// Invariant for cleanup: numIn/numOut and each port's chans are set only after
// the corresponding array exists, and unfilled slots are zeroed by calloc, so
// freeUGen never touches memory that wasn't allocated.
UGen* newUGen(Graph* g, int numIn, Output** sources,
              int numOut, const Shape* outShapes, bool inPlace) {
    assert(!g->compiled);
    if (!reserveOne(g->ugens, g->numUGens, g->capUGens)) return NULL;

    Shape broadcast = Shape_scalar();
    if (!outShapes) {
        for (int i = 0; i < numIn; ++i) {
            if (sources[i]) broadcast = Shape_broadcast(&broadcast, &sources[i]->shape);
        }
    }

    UGen* u = (UGen*)calloc(1, sizeof(UGen));
    if (!u) return NULL;
    u->inPlace = inPlace;

    u->ins = (Input*)calloc(numIn, sizeof(Input));
    if (!u->ins && numIn > 0) return abandonUGen(u);
    u->numIn = numIn;

    u->outs = (Output*)calloc(numOut, sizeof(Output));
    if (!u->outs && numOut > 0) return abandonUGen(u);
    u->numOut = numOut;

    for (int i = 0; i < numIn; ++i) {
        Output* src = sources[i];
        Shape shape = !outShapes ? broadcast
                    : src        ? src->shape
                    :              Shape_scalar();
        if (!connectInput(&u->ins[i], src, shape)) return abandonUGen(u);
    }

    for (int i = 0; i < numOut; ++i) {
        Output& out = u->outs[i];
        out.shape = outShapes ? outShapes[i] : broadcast;
        int chans = Shape_size(&out.shape);
        out.wires = (Wire*)calloc(chans, sizeof(Wire));
        if (!out.wires && chans > 0) return abandonUGen(u);
        out.chans = chans;
    }

    g->ugens[g->numUGens++] = u;
    return u;
}

UGen* newScalar(Graph* g, Z value) {
    assert(!g->compiled);
    if (!reserveOne(g->constants, g->numConstants, g->capConstants)) return NULL;

    UGen* u = (UGen*)calloc(1, sizeof(UGen));
    if (!u) return NULL;
    u->inPlace = false;

    u->outs = (Output*)calloc(1, sizeof(Output));
    if (!u->outs) return abandonUGen(u);
    u->numOut = 1;

    Output& out = u->outs[0];
    out.shape = Shape_scalar();
    out.wires = (Wire*)calloc(1, sizeof(Wire));
    if (!out.wires) return abandonUGen(u);
    Wire_set(&out.wires[0], value);
    out.chans = 1;

    g->constants[g->numConstants++] = u;
    return u;
}

UGen* newMatrix(Graph* g, Shape shape, Z* values) {
    assert(!g->compiled);
    if (!reserveOne(g->constants, g->numConstants, g->capConstants)) return NULL;

    UGen* u = (UGen*)calloc(1, sizeof(UGen));
    if (!u) return NULL;
    u->inPlace = false;

    u->outs = (Output*)calloc(1, sizeof(Output));
    if (!u->outs) return abandonUGen(u);
    u->numOut = 1;

    Output& out = u->outs[0];
    out.shape = shape;
    int chans = Shape_size(&out.shape);
    out.wires = (Wire*)calloc(chans, sizeof(Wire));
    if (!out.wires && chans > 0) return abandonUGen(u);
    for (int i = 0; i < chans; ++i) {
        Wire_set(&out.wires[i], values[i]);
    }
    out.chans = chans;

    g->constants[g->numConstants++] = u;
    return u;
}

UGen* newRow(Graph* g, int n, Z* values) {
    Shape shape = Shape_row(n);
    return newMatrix(g, shape, values);
}

UGen* newCol(Graph* g, int n, Z* values) {
    Shape shape = Shape_col(n);
    return newMatrix(g, shape, values);
}

UGen* newDiagonal(Graph* g, int n, Z* values) {
    Shape shape = Shape_matrix(n, n);
    
    assert(!g->compiled);
    if (!reserveOne(g->constants, g->numConstants, g->capConstants)) return NULL;

    UGen* u = (UGen*)calloc(1, sizeof(UGen));
    if (!u) return NULL;
    u->inPlace = false;

    u->outs = (Output*)calloc(1, sizeof(Output));
    if (!u->outs) return abandonUGen(u);
    u->numOut = 1;

    Output& out = u->outs[0];
    out.shape = shape;
    int chans = Shape_size(&out.shape);
    out.wires = (Wire*)calloc(chans, sizeof(Wire));
    if (!out.wires && n > 0) return abandonUGen(u);
    for (int i = 0; i < chans; ++i) {
        out.wires[i] = gZeroWire;
    }
    for (int i = 0; i < n; ++i) {
        Wire_set(&out.wires[n*i + i], values[i]);
    }
    out.chans = chans;

    g->constants[g->numConstants++] = u;
    return u;
}

bool isConstant(Output* o) {
    for (int c = 0; c < o->chans; ++c) {
        Wire* w = outputWire(o, c);
        if (w->stride != 0 || !w->buffer) return false;
    }
    return true;
}

// Returns the pool buffer behind channel j of u->ins[i] if an output channel j
// may overwrite it in place, else NULL. That requires:
//  - a buffer-backed wire;
//  - that no input of u reads that buffer at any channel other than j: a
//    broadcast buffer is read to produce several output channels, so writing
//    it for one would corrupt the reads of the others;
//  - that every remaining use of the buffer is one of u's input ports, so no
//    later UGen (or the graph's own output) still reads it. This also refuses a
//    buffer already taken in place by an earlier output of u, since that
//    raised its refCount.
static WireBuffer* inPlaceBuffer(UGen* u, int i, int j) {
    WireBuffer* wb = u->ins[i].wires[j]->wb;
    if (!wb) return NULL;
    int ports = 0;
    for (int k = 0; k < u->numIn; ++k) {
        Input& other = u->ins[k];
        bool reads = false;
        for (int c = 0; c < other.chans; ++c) {
            if (other.wires[c]->wb != wb) continue;
            if (c != j) return NULL;
            reads = true;
        }
        if (reads) ++ports;
    }
    return wb->refCount == ports ? wb : NULL;
}

// Assigns buffers to u's outputs and releases its inputs' uses.
// Returns false on allocation failure.
static bool compileUGen(Graph* g, UGen* u, int blockSize) {
    for (int i = 0; i < u->numOut; ++i) {
        Output& out = u->outs[i];
        bool shapesMatch = i < u->numIn && Shape_equal(&out.shape, &u->ins[i].shape);
        for (int j = 0; j < out.chans; ++j) {
            Wire& w = out.wires[j];
            WireBuffer* wb = NULL;
            if (u->inPlace && shapesMatch) {
                wb = inPlaceBuffer(u, i, j);
            }
            if (!wb) wb = getWireBuffer(g, blockSize);
            if (!wb) return false;
            // The extra reference holds the buffer until u is done, so it can't
            // return to the pool while u's inputs are being released, even if
            // nothing reads this wire.
            WireBuffer_retain(wb, w.uses + 1);
            Wire_set(&w, wb);
        }
    }

    // Outputs were assigned before any input is released, so an op that isn't
    // in place never gets its own input's buffer.
    for (int i = 0; i < u->numIn; ++i) {
        Input& in = u->ins[i];
        forEachDistinctWire(in.chans, [&](int c) { return in.wires[c]; },
                            [&](Wire* w) { if (w->wb) WireBuffer_release(g, w->wb); });
    }
    for (int i = 0; i < u->numOut; ++i) {
        Output& out = u->outs[i];
        for (int j = 0; j < out.chans; ++j) {
            WireBuffer_release(g, out.wires[j].wb);
        }
    }
    return true;
}

bool Graph_compile(Graph* g, Output** outputs, int numOutputs, int blockSize) {
    assert(!g->compiled);
    // Vectorized run functions need whole vectors; a block of 1 uses run1.
    assert(blockSize == 1 || (blockSize > 0 && blockSize % kVecLen == 0));
    g->compiled = true;
    g->blockSize = blockSize;

    // Count each wire's uses: one per input port that reads it (however many
    // of the port's channels do), plus one if the graph reads it.
    for (int u = 0; u < g->numUGens; ++u) {
        UGen* ugen = g->ugens[u];
        for (int i = 0; i < ugen->numIn; ++i) {
            Input& in = ugen->ins[i];
            forEachDistinctWire(in.chans, [&](int c) { return in.wires[c]; },
                                [](Wire* w) { ++w->uses; });
        }
    }
    for (int i = 0; i < numOutputs; ++i) {
        Output* o = outputs[i];
        forEachDistinctWire(o->chans, [&](int c) { return outputWire(o, c); },
                            [](Wire* w) { ++w->uses; });
    }

    for (int u = 0; u < g->numUGens; ++u) {
        // Every UGen that runs has both run functions.
        assert(!g->ugens[u]->run == !g->ugens[u]->run1);
        if (!compileUGen(g, g->ugens[u], blockSize)) return false;
    }
    return true;
}

void Graph_run(Graph* g) {
    assert(g->compiled);
    if (g->blockSize == 1) {
        for (int i = 0; i < g->numUGens; ++i) {
            UGen* u = g->ugens[i];
            if (u->run1) u->run1(u);
        }
    } else {
        for (int i = 0; i < g->numUGens; ++i) {
            UGen* u = g->ugens[i];
            if (u->run) u->run(u, g->blockSize);
        }
    }
}

UGen* newUnOp(Graph* g, Output* a, bool inPlace) {
    return newUGen(g, 1, &a, 1, &a->shape, inPlace);
}

UGen* newBinOp(Graph* g, Output* a, Output* b, bool inPlace) {
    Output* sources[2] = {a, b};
    return newUGen(g, 2, sources, 1, NULL, inPlace);
}

UGen* newTriOp(Graph* g, Output* a, Output* b, Output* c, bool inPlace) {
    Output* sources[3] = {a, b, c};
    return newUGen(g, 3, sources, 1, NULL, inPlace);
}

UGen* newQuadOp(Graph* g, Output* a, Output* b, Output* c, Output* d, bool inPlace) {
    Output* sources[4] = {a, b, c, d};
    return newUGen(g, 4, sources, 1, NULL, inPlace);
}

UGen* newQuintOp(Graph* g, Output* a, Output* b, Output* c, Output* d, Output* e, bool inPlace) {
    Output* sources[5] = {a, b, c, d, e};
    return newUGen(g, 5, sources, 1, NULL, inPlace);
}
