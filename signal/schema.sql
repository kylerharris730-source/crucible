-- One row per room, alive for minutes. See worker.js for why this is D1 and
-- not KV: a handshake needs the write to be visible to the next read, and KV
-- is eventually consistent across the edge.
CREATE TABLE IF NOT EXISTS rooms (
  code    TEXT PRIMARY KEY,   -- five characters, no ambiguous glyphs
  offer   TEXT NOT NULL,      -- the host's description
  answer  TEXT,               -- the guest's, once they have made one
  expires INTEGER NOT NULL    -- epoch ms; swept on the next write
);

-- The sweep runs on every room creation, so it should not table-scan.
CREATE INDEX IF NOT EXISTS rooms_expires ON rooms (expires);
