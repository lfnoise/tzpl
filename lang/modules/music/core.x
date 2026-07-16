-- core.x -- aggregator for the music library core.
--
--     import music.core.*;
--
-- pulls in the shared Event/Pitch types (music.event), tunings and scales
-- (music.tuning), and the extra random helpers (music.rand). The dialect
-- modules (music.pat, music.media, music.shape, music.job, music.tidal)
-- re-export this, so importing any dialect gives the core too.
--
-- RT-safe: everything re-exported here is pure.

export music.event.*;
export music.tuning.*;
export music.rand.*;
