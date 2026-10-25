//
//  graph.hpp
//  tensor-ugens
//
//  Unit generators, the wires between them, and the graph that owns them:
//  construction, constants, views, buffer assignment (Graph_compile) and
//  cleanup.
//

#ifndef graph_h
#define graph_h

#include "vec_math.hpp"
#include "shape.hpp"

const int kDefaultBlockSize = 64;

// A UGen's run function computes n samples of each output wire; n is the
// graph's block size, a multiple of kVecLen. Its run1 function computes one
// sample, for graphs compiled with a block size of 1: subgraphs run lazily, a
// sample at a time when triggered, and graphs with single sample feedback
// between UGens. A UGen's state is the same for both, so either continues
// where the other left off.
typedef void (*UGenRunFun)(struct UGen*, int);
typedef void (*UGenRun1Fun)(struct UGen*);

// A pool buffer. During Graph_compile, refCount is the number of reads of the
// wire it backs that compile hasn't reached yet: one per input port reading it
// (however many of the port's channels do), plus one if the graph reads it. At
// zero it goes back to the pool for a later UGen to reuse.
struct WireBuffer {
	Z* buffer;
	int count;
    int refCount;
};

struct Wire
{
	Z* buffer;
	Z scalar;
	VP splat;        // a scalar wire's value in every lane, for vectorized runs
	int stride;      // 1 for a buffer, 0 for a scalar: in samples, or in vectors
	WireBuffer* wb;  // pool buffer behind buffer, or NULL for a scalar wire
	int uses;        // reads of this wire, counted by Graph_compile
};

extern Wire gZeroWire;

// A UGen output, or a view (see newView). Exactly one of wires and viewWires
// is set.
struct Output
{
    Shape shape;
	int chans;
	Wire* wires;       // a UGen output's own wires
	Wire** viewWires;  // a view's channels: the UGen output wires they refer to
};

struct Input
{
    Output* source;
    Shape shape;  // shape the source is mapped to; chans == Shape_size(&shape)
	int chans;
	Wire** wires;
};

struct UGen 
{
	int numIn, numOut;
	Input* ins;
	Output* outs;
	UGenRunFun run;
	UGenRun1Fun run1;
	void* state;
	bool inPlace;  // see newUGen
};

struct Graph
{
    int numUGens, capUGens;
    int numConstants, capConstants;
    UGen** ugens;  // unit generators in construction order, which is sorted order
    UGen** constants;  // constant values
    int numViews, capViews;
    Output** views;
    bool compiled;
    int blockSize;  // set by Graph_compile
    int numWireBuffers, capWireBuffers;
    int numFreeWireBuffers, capFreeWireBuffers;
    // Each WireBuffer is allocated individually so pointers to it (in freeBuffers
    // and Wire::wb) stay valid when this array grows.
    WireBuffer** buffers;
    WireBuffer** freeBuffers;
};

extern Graph* gGraph;

// Sets gZeroWire. Call once before building graphs.
void initialize();

void Wire_set(Wire* w, WireBuffer* wb);
void Wire_set(Wire* w, Z scalar);

// Returns the wire behind channel p of o. For a view, that is the UGen output
// wire it ultimately refers to.
inline Wire* outputWire(Output* o, int p) {
    return o->viewWires ? o->viewWires[p] : &o->wires[p];
}

// Returns the channel of a src-shaped tensor that channel w of its broadcast to
// outShape reads.
int broadcastChannel(Shape* srcShape, Shape* outShape, int w);

// The broadcast of the shapes of sources[0..n).
Shape Shape_broadcast(int n, Output** sources);

bool mapInputWires(Input* in, Shape* outShape);

// True if every wire of o is a scalar, as for a constant or a view of one.
// stride alone can't tell: a UGen output's wires also have stride 0 until
// Graph_compile assigns their buffers, but their buffer is still NULL, while a
// scalar wire's buffer points at its scalar.
bool isConstant(Output* o);

// Creates a view of src: a static remapping of its wires to shape, where
// channel c of the view is channel map[c] of src. Transpose, take, drop,
// stride, etc. are all views. A view has no buffers or uses of its own: it
// refers straight to UGen output wires (through any chain of views), so inputs
// connected to it read those wires directly. g owns the view.
// Returns NULL on allocation failure.
Output* newView(Graph* g, Output* src, Shape shape, const int* map);

// Creates a UGen with numIn inputs wired to sources and numOut outputs, and
// appends it to g, which owns it. Only the wiring is built here; buffers are
// assigned by Graph_compile once the whole graph exists. Since every source
// must already exist, construction order is a topological order, and the graph
// runs UGens in that order. Returns NULL on allocation failure.
//
// sources holds one Output per input (it may be NULL when numIn is 0, and a
// NULL entry leaves that input on gZeroWire as a scalar).
//
// outShapes holds one Shape per output, or is NULL for an op whose outputs are
// a broadcast of its inputs:
//  - NULL: every input is broadcast to the common shape of all the inputs, and
//    every output gets that shape.
//  - non-NULL: each input keeps its source's own shape, wired 1:1, and the op
//    maps inputs to outputs itself (reductions, matrix products, ...).
//
// inPlace allows Graph_compile to give channel j of outs[i] the buffer behind
// channel j of ins[i] (see inPlaceBuffer). It is only valid for ops whose
// output channel j depends on input channel j alone, as in elementwise ops over
// broadcast inputs, and whose run function reads a[j] before writing y[j]. Ops
// that combine channels (reductions, matrix products, convolutions) must pass
// false even when the shapes match. (Shape-only ops such as transpose, take,
// drop and stride aren't UGens at all: they are views.)
UGen* newUGen(Graph* g, int numIn, Output** sources,
              int numOut, const Shape* outShapes, bool inPlace = false);

// Constants: zero inputs and one output of scalar wires. g keeps them apart from
// its sorted UGens, since they don't run. Each returns NULL on allocation failure.
UGen* newScalar(Graph* g, Z value);
UGen* newMatrix(Graph* g, Shape shape, Z* values);
UGen* newRow(Graph* g, int n, Z* values);
UGen* newCol(Graph* g, int n, Z* values);
UGen* newDiagonal(Graph* g, int n, Z* values);

// Elementwise UGens with 1 to 5 inputs, broadcast to a common shape.
UGen* newUnOp(Graph* g, Output* a, bool inPlace = false);
UGen* newBinOp(Graph* g, Output* a, Output* b, bool inPlace = false);
UGen* newTriOp(Graph* g, Output* a, Output* b, Output* c, bool inPlace = false);
UGen* newQuadOp(Graph* g, Output* a, Output* b, Output* c, Output* d, bool inPlace = false);
UGen* newQuintOp(Graph* g, Output* a, Output* b, Output* c, Output* d, Output* e, bool inPlace = false);

// Assigns pool buffers to every UGen output wire in g, once the graph is fully
// built. outputs are the Outputs (UGen outputs or views) the graph itself
// reads; their buffers are never recycled. Every other wire's buffer returns
// to the pool after its last reader, for a later UGen to reuse, and in-place
// UGens take over their inputs' buffers where inPlaceBuffer allows.
//
// blockSize is a multiple of kVecLen, or 1 to run the graph a sample at a time
// with each UGen's run1.
//
// Returns false on allocation failure, after which g can only be freed.
bool Graph_compile(Graph* g, Output** outputs, int numOutputs, int blockSize);

// Runs each UGen of a compiled graph in order, for one block of g->blockSize
// samples: with run1 if the block size is 1, else with run. UGens left without
// run functions, when their state couldn't be allocated, are skipped.
void Graph_run(Graph* g);

// Frees everything g owns: its UGens, views and buffers. g is left empty.
void Graph_free(Graph* g);

#endif /* graph_h */
