Within this directory are directories for three projects:

- @static-lang-3/ is an interpreter for a statically typed programming language that is designed to be able to run on a real-time thread.
- @synthdef-compiler/ takes a description of a graph of audio signal operators and compiles a plugin for computing audio.
- @audio-engine/ An audio engine that can dynamically edit a graph of plugins with optional cross fading. Has a scheduling queue for scheduling events on the real-time thread. Can run multiple audio worker threads called Silos.


The goals of the whole project are:
- Integrate these three projects.
- The programming language should be able to call functions of the synthdef-compiler and audio-engine to create plugins using the synthdef-compiler and schedule events on the audio-engine via the foreign function interface of the language.
- All three should be able to be libraries that can be linked into other applications.
- There should be Open Sound Control and NATs support so that the language and the audio engine can be controlled by commands sent via these protocols.
- The audio engine should be able to load any plugin that conforms to the plugin interface, not just those created by synthdef-compiler.
- The main product is a cross-platform live coding application for creating live music performances. This could also function as an IDE for the language, and a librarian for managing built plugins and language modules.
- Whether to use Dear Imgui, Qt, or something else as a UI needs to be determined.
- The application could grow over time to include video synthesis via composing shaders, visual node graph editor, piano roll and event list editors, etc.

Implementation details
- A plan is needed for how to organize the project. Should it be a single repository, or separate repositories for each part. Where should the @shared/ directory belong? Maybe it should be part of the audio-engine?

