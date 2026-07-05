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

---------------------------------------------------------------------------
-- Bindings

-- Lang callback, fired with the latest value at most once per GUI frame.
fn onChange(w Widget, f (Float) Void) Void = uiOnChange(w.0, f);
fn onChangeXY(w Widget, f (Float, Float) Void) Void = uiOnChangeXY(w.0, f);

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

-- Move the widget and fire its bindings (coalesced, next GUI frame).
fn setValue(w Widget, v Float) Void = uiSetValue(w.0, v);
fn setValueXY(w Widget, x Float, y Float) Void = uiSetValueXY(w.0, x, y);

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
