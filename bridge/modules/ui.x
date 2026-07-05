-- ui.x
-- Script wrapper for the ui FFI: live control widgets rendered by the app.
--
-- Widgets are created by code and bound to running audio, two ways:
--   * fast path: bindControl(w, node, "cutoff") -- the GUI thread sends
--     setControl directly to the engine on drag, without entering the VM.
--   * flexible path: onChange(w, fn(v Float) Void { ... }) -- a lang
--     callback, coalesced to the latest value once per GUI frame.
--
-- Constructors are idempotent upserts keyed by (panel, name): re-running
-- code adopts an existing widget (keeping its current value and layout),
-- refreshes its spec, and clears old bindings for fresh rebinding.
--
-- Value mapping reuses synthdef.ControlSpec, so a widget can be derived
-- entirely from a synthdef's control declaration:
--   control(node, "cutoff")  -- one widget from the def's ControlSpec
--   controls(node)           -- materialize the node's whole interface

import ui_ffi.*;
import synthdef.*;

struct Widget(Int);

-- Warp payload (step size for .step; 0.0 otherwise).
fn _stepParam(w ControlWarp) Float {
	match (w) {
		ControlWarp.step(sz): sz;
		_: 0.0;
	}
}

-- Target panel for subsequent constructors ("" = default Controls panel).
fn panel(name String) Void = uiPanel(name);

-- The current target panel name.
fn currentPanel() String = uiGetPanel();

---------------------------------------------------------------------------
-- Constructors (upsert by (panel, name); return a Widget handle)

fn slider(name String, spec ControlSpec) Widget =
	Widget(uiSlider(name, spec.lo, spec.hi, spec.init,
	                spec.warp ordinal, spec.warp _stepParam));

fn slider(name String, lo Float, hi Float, init Float) Widget =
	Widget(uiSlider(name, lo, hi, init, 0, 0.0));

fn number(name String, init Float) Widget = Widget(uiNumber(name, init));

fn button(name String) Widget = Widget(uiButton(name));

fn toggle(name String, init Bool = false) Widget = Widget(uiToggle(name, init));

fn xy(name String, xspec ControlSpec, yspec ControlSpec) Widget =
	Widget(uiXY(name,
	            xspec.lo, xspec.hi, xspec.init, xspec.warp ordinal, xspec.warp _stepParam,
	            yspec.lo, yspec.hi, yspec.init, yspec.warp ordinal, yspec.warp _stepParam));

-- N vertical bars mapped through a ControlSpec (drag across to paint).
fn multislider(name String, n Int, spec ControlSpec) Widget =
	Widget(uiMultiSlider(name, n, spec.lo, spec.hi, spec.init,
	                     spec.warp ordinal, spec.warp _stepParam));

fn multislider(name String, n Int) Widget =
	Widget(uiMultiSlider(name, n, 0.0, 1.0, 0.0, 0, 0.0));

-- rows x cols grid of 0/1 toggle cells (a step sequencer surface).
fn matrix(name String, rows Int, cols Int) Widget =
	Widget(uiMatrix(name, rows, cols));

-- Grid note editor over `beats` beats (16th grid). Notes are read/written
-- as flat (pitch, startBeat, durBeats) triplets; see notes()/setNotes().
fn pianoRoll(name String, beats Float = 4.0) Widget =
	Widget(uiPianoRoll(name, beats));

-- Static text.
fn label(name String, text String) Widget = Widget(uiLabel(name, text));

---------------------------------------------------------------------------
-- Bindings

-- Lang callback, fired with the latest value at most once per GUI frame.
fn onChange(w Widget, f (Float) Void) Void = uiOnChange(w.0, f);
fn onChangeXY(w Widget, f (Float, Float) Void) Void = uiOnChangeXY(w.0, f);

-- Vector callback for multislider/matrix (all values) and pianoRoll
-- (note triplets), fired coalesced like onChange.
fn onChangeVec(w Widget, f ([Float]) Void) Void = uiOnChangeVec(w.0, f);

-- Engine fast path: control resolved by name on the node's def.
-- Returns an audio_engine error code (0 = ok).
fn bindControl(w Widget, node Int, control String, silo Int = 0) Int =
	uiBindControl(w.0, node, control, silo);

-- Bind the Y axis of an xy widget.
fn bindControlY(w Widget, node Int, control String, silo Int = 0) Int =
	uiBindControlY(w.0, node, control, silo);

---------------------------------------------------------------------------
-- Values

fn value(w Widget) Float = uiValue(w.0);
fn valueY(w Widget) Float = uiValueY(w.0);
fn values(w Widget) [Float] = uiValues(w.0);

-- Move the widget and fire its bindings (coalesced, next GUI frame).
fn setValue(w Widget, v Float) Void = uiSetValue(w.0, v);
fn setValueXY(w Widget, x Float, y Float) Void = uiSetValueXY(w.0, x, y);
fn setValues(w Widget, vals [Float]) Void = uiSetValues(w.0, vals);

-- Piano roll notes: flat (pitch, startBeat, durBeats) triplets.
fn notes(w Widget) [Float] = uiNotes(w.0);
fn setNotes(w Widget, ns [Float]) Void = uiSetNotes(w.0, ns);

-- Layout frame within the widget's panel (arrange mode sets this by
-- dragging; code can too). w/h <= 0 keeps the default size.
fn setFrame(w Widget, x Float, y Float, wd Float, ht Float) Void =
	uiSetFrame(w.0, x, y, wd, ht);

-- Piano roll pitch range: bottom pitch + number of rows.
fn rollRange(w Widget, lowPitch Int, rows Int) Void =
	uiRollRange(w.0, lowPitch, rows);

---------------------------------------------------------------------------
-- show(): materialize a value as its natural widget (editable where it
-- makes sense; read back with value()/values()).

fn show(name String, v Float) Widget = Widget(uiShowFloat(name, v));
fn show(name String, v Bool) Widget = Widget(uiShowBool(name, v));
fn show(name String, v [Float]) Widget = Widget(uiShowVec(name, v));
fn show(name String, v String) Widget = Widget(uiShowString(name, v));

fn remove(w Widget) Void = uiRemove(w.0);
fn clear() Void = uiClear();

---------------------------------------------------------------------------
-- Tap widgets (engine readback)

-- Level meter on a node outlet (rms bar + peak marker).
fn meter(name String, node Int, outlet Int = 0, silo Int = 0) Widget =
	Widget(uiMeter(name, node, outlet, silo));

-- Oscilloscope on a node outlet (channel 0).
fn scope(name String, node Int, outlet Int = 0, silo Int = 0) Widget =
	Widget(uiScope(name, node, outlet, silo));

-- Tap reads for scripts (bypass the GUI display mirror). Note the scope
-- FIFO has one consumer stream: samples taken here don't also reach the
-- GUI scope display.
fn peakLevel(w Widget) Float = uiTapPeak(w.0);
fn rmsLevel(w Widget) Float = uiTapRms(w.0);

-- Channel count captured by a tap widget's scope.
fn tapChannels(w Widget) Int = uiTapChans(w.0);

-- Raw scope samples: interleaved frames of tapChannels(w) channels
-- (the count is clamped to whole frames).
fn samples(w Widget, max Int = 4096) [Float] = uiTapSamples(w.0, max);

-- Static data plot; update with setData.
fn plot(name String, data [Float]) Widget = Widget(uiPlot(name, data));
fn setData(w Widget, data [Float]) Void = uiSetData(w.0, data);

-- Overview of the audio file loaded into a node's buffer slot
-- (see audio_engine.loadBuffer).
fn waveform(name String, node Int, buf Int) Widget =
	Widget(uiWaveform(name, node, buf));

---------------------------------------------------------------------------
-- Synthdef-derived widgets

-- The def name of a live node ("" if unknown).
fn defName(node Int) String = uiNodeDefName(node);

-- One widget named, ranged, warped, and bound from the def's control
-- declaration. Returns Widget(0) if the node or control is unknown.
-- Defaults into a panel named after the node's synthdef, so each synth
-- gets its own panel (window, or notebook cell of that name).
fn control(node Int, name String, silo Int = 0) Widget {
	let p = uiNodeDefName(node);
	if (p length == 0) {
		Widget(uiControl(node, name, silo))
	} else {
		control(node, name, p, silo)
	}
}

-- Materialize the node's whole interface, into a panel named after the
-- node's synthdef (falls back to the current panel if the def name is
-- unknown). Use the explicit-panel form below to place them elsewhere,
-- or when running several instances of the SAME def.
fn controls(node Int, silo Int = 0) [Widget] {
	let p = uiNodeDefName(node);
	if (p length == 0) {
		uiControls(node, silo) map(fn(id Int) Widget { Widget(id) })
	} else {
		controls(node, p, silo)
	}
}

-- Variants targeting a named panel directly -- e.g. a notebook panel
-- cell: controls(node, "mixer"). The previous target panel is restored.
fn control(node Int, name String, panel String, silo Int = 0) Widget {
	let prev = uiGetPanel();
	uiPanel(panel);
	let w = Widget(uiControl(node, name, silo));
	uiPanel(prev);
	w
}

fn controls(node Int, panel String, silo Int = 0) [Widget] {
	let prev = uiGetPanel();
	uiPanel(panel);
	let ws = uiControls(node, silo) map(fn(id Int) Widget { Widget(id) });
	uiPanel(prev);
	ws
}
