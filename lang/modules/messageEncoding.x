-- messageEncoding.x -- a compact binary message format for Msg values.
--
-- Losslessly encodes/decodes the Msg enum (message.x) to/from a `Bytes`
-- buffer, and offers a zero-copy Reader for direct field access without
-- materializing a Msg tree. The same buffer is the carrier for inter-thread
-- (silo) and inter-process (e.g. NATS) messaging.
--
-- Wire layout (little-endian) -- mirrored by the C++ reader/writer in
-- shared/tzpl_sexpr_bin.hpp; keep the two in sync.
--
--   header:  'T' 'Z' 'B' version(=2)        (4 bytes)
--            u32 rootOffset                  (offset of the root value slot)
--
--   value slot (9 bytes): tag(u8) + payload(8 bytes), payload by tag:
--            0 bool   -> 0/1
--            1 int    -> i64
--            2 float  -> f64
--            3 symbol -> u64 offset to a string blob
--            4 string -> u64 offset to a string blob
--            5 vec    -> u64 offset to a list node
--
--   string blob:  u32 byteLen + raw UTF-8 bytes
--
--   list node:    u32 count + u32 tagsOffset + u32 payloadsOffset
--                 tagsOffset     -> count tag bytes (one per child)
--                 payloadsOffset -> count * 8 payload bytes (one per child)
--
-- Children live in parallel tag/payload arrays, so child i is reached in O(1):
-- tag at tagsOffset+i, payload at payloadsOffset+i*8.

import message.*;

-- Node kinds; discriminants line up with the wire tags and Msg cases.
enum MsgTag {
    bool,
    int,
    float,
    symbol,
    string,
    vec,
}

fn _msgTagOf(t Int) MsgTag {
    match (t) {
        0: MsgTag.bool;
        1: MsgTag.int;
        2: MsgTag.float;
        3: MsgTag.symbol;
        4: MsgTag.string;
        _: MsgTag.vec;
    }
}

-- =====================================================================
-- Encoding
-- =====================================================================

-- An encoded child's tag + payload, before it is written into a list node's
-- columnar arrays. Floats carry their value separately so they can be written
-- with putF64! (every other payload is an 8-byte integer / offset).
struct Enc { tag Int, isFloat Bool, ipayload Int, fpayload Float }

-- Append a string blob (byteLen + bytes); return its offset.
fn _putBlob(buf Bytes, s String) Int {
    let off = buf byteLength;
    buf putU32!(0);                 -- placeholder length, back-patched below
    let dataOff = buf byteLength;
    buf putUtf8!(s);
    buf setU32At!(off, buf byteLength - dataOff);
    off
}

-- Append all out-of-line data for `o`, returning its tag + payload.
fn _encodeValue(buf Bytes, o Msg) Enc {
    match (o) {
        Msg.bool(b):   Enc{0, false, b ? 1 : 0, 0.0};
        Msg.int(i):    Enc{1, false, i, 0.0};
        Msg.float(f):  Enc{2, true, 0, f};
        Msg.symbol(s): Enc{3, false, _putBlob(buf, s toString), 0.0};
        Msg.string(s): Enc{4, false, _putBlob(buf, s), 0.0};
        Msg.vec(v):    Enc{5, false, _putNode(buf, v), 0.0};
    }
}

fn _putPayload(buf Bytes, e Enc) Void {
    if (e.isFloat) { buf putF64!(e.fpayload); } else { buf putU64!(e.ipayload); }
}

-- Append a list node (children first, then the tag array, payload array, and
-- the node descriptor); return the descriptor's offset.
fn _putNode(buf Bytes, v [Msg]) Int {
    let n = v length;
    var encs [Enc] = [];
    var i = 0;
    while (i < n) { encs push!(_encodeValue(buf, v[i])); i = i + 1; }

    let tagsOff = buf byteLength;
    i = 0;
    while (i < n) { buf putU8!(encs[i].tag); i = i + 1; }

    let paysOff = buf byteLength;
    i = 0;
    while (i < n) { _putPayload(buf, encs[i]); i = i + 1; }

    let nodeOff = buf byteLength;
    buf putU32!(n);
    buf putU32!(tagsOff);
    buf putU32!(paysOff);
    nodeOff
}

-- Encode a Msg to a fresh Bytes message.
fn encode(o Msg) Bytes {
    var buf = bytes();
    buf putU8!(84); buf putU8!(90); buf putU8!(66); buf putU8!(2);  -- "TZB" + v2
    let rootPos = buf byteLength;       -- == 4
    buf putU32!(0);                     -- placeholder rootOffset
    let e = _encodeValue(buf, o);
    let slotOff = buf byteLength;
    buf putU8!(e.tag);
    _putPayload(buf, e);
    buf setU32At!(rootPos, slotOff);
    buf
}

-- =====================================================================
-- Zero-copy reader -- direct access without building a Msg tree.
-- A Reader is a cursor over one value slot: a tag byte at `tagOff` and an
-- 8-byte payload at `valOff`. (They are adjacent for the root, and live in
-- separate columnar arrays for list children.)
-- =====================================================================

struct Reader { buf Bytes, tagOff Int, valOff Int }

-- True if `b` looks like a TZB v2 message (header magic + version).
fn isMessage(b Bytes) Bool {
    b byteLength >= 9
        && b u8At(0) == 84 && b u8At(1) == 90
        && b u8At(2) == 66 && b u8At(3) == 2
}

-- A cursor at the root value slot.
fn reader(b Bytes) Reader {
    let rootOff = b u32At(4);
    Reader{b, rootOff, rootOff + 1}
}

fn tag(r Reader) MsgTag { _msgTagOf(r.buf u8At(r.tagOff)) }

fn asBool(r Reader)  Bool   { r.buf i64At(r.valOff) != 0 }
fn asInt(r Reader)   Int    { r.buf i64At(r.valOff) }
fn asFloat(r Reader) Float  { r.buf f64At(r.valOff) }

-- Read the string blob referenced by this slot's payload offset.
fn asStr(r Reader) String {
    let off = r.buf i64At(r.valOff);
    let len = r.buf u32At(off);
    r.buf utf8At(off + 4, len)
}

-- Number of children (vec slots only).
fn childCount(r Reader) Int {
    let nodeOff = r.buf i64At(r.valOff);
    r.buf u32At(nodeOff)
}

-- Cursor for child i (vec slots only); O(1).
fn child(r Reader, i Int) Reader {
    let nodeOff = r.buf i64At(r.valOff);
    let tagsOff = r.buf u32At(nodeOff + 4);
    let paysOff = r.buf u32At(nodeOff + 8);
    Reader{r.buf, tagsOff + i, paysOff + i * 8}
}

-- All child cursors of a vec slot.
fn children(r Reader) [Reader] {
    let n = _clampCount(r);
    var out [Reader] = [];
    var i = 0;
    while (i < n) { out push!(child(r, i)); i = i + 1; }
    out
}

-- =====================================================================
-- Decoding -- materialize a full Msg tree (built on the reader).
--
-- decode is total and crash-safe on untrusted input: the header is validated
-- (isMessage), child counts are clamped to the buffer length, and recursion is
-- depth-capped, so a truncated or corrupt buffer yields a small/empty Msg
-- rather than reading out of bounds, looping forever, or overflowing the stack.
-- =====================================================================

-- A vec's stored child count, clamped to a sane range (a valid message always
-- has count <= byteLength, since each child occupies >= 1 byte of arrays).
fn _clampCount(r Reader) Int {
    let n = childCount(r);
    let cap = r.buf byteLength;
    if (n < 0 || n > cap) { 0 } else { n }
}

-- `budget` is a shared node allowance (Ref[Int]): every decoded node spends one,
-- so a corrupt buffer whose nodes all look like vecs cannot amplify into an
-- exponential tree -- total nodes are bounded by the buffer length. `depth`
-- separately caps nesting so deep input cannot overflow the VM call stack.
fn _decodeAt(r Reader, depth Int, budget Ref<Int>) Msg {
    if (depth > 256 || deref(budget) <= 0) { return Msg.bool(false); }
    setref(deref(budget) - 1, budget);
    match (tag(r)) {
        MsgTag.bool:   Msg.bool(asBool(r));
        MsgTag.int:    Msg.int(asInt(r));
        MsgTag.float:  Msg.float(asFloat(r));
        MsgTag.symbol: Msg.symbol(toSymbol(asStr(r)));
        MsgTag.string: Msg.string(asStr(r));
        MsgTag.vec:    Msg.vec(_decodeChildren(r, depth, budget));
    }
}

fn _decodeChildren(r Reader, depth Int, budget Ref<Int>) [Msg] {
    let n = _clampCount(r);
    var out [Msg] = [];
    var i = 0;
    while (i < n && deref(budget) > 0) {
        out push!(_decodeAt(child(r, i), depth + 1, budget));
        i = i + 1;
    }
    out
}

-- Decode a TZB message back into a Msg. Returns an empty vec if `b` is not a
-- valid message (callers handling untrusted input should isMessage-check first).
fn decode(b Bytes) Msg {
    if (isMessage(b)) {
        _decodeAt(reader(b), 0, ref(b byteLength + 16))
    } else {
        Msg.vec([Msg]())
    }
}
