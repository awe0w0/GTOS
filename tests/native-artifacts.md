# Native artifact identity

Run with Node: node tests/native-artifacts.test.mjs. The test imports the actual tools/native-artifacts.mjs and exercises canonical staging, dependency pairing, ordered link inputs, shape destinations, opaque Windows/UNC/Linux source paths, invalid identities, duplicates and 512 repeated calls. It does not run project objects or validate source pins; the caller must verify each source before copying.

artifactLayout accepts a canonical /var/tmp/<work> root and ordered {slug, has_depfile} identities. A source path is deliberately not an input to path construction. Every consumer must use the returned staged_linux, depfile_linux and destination values; do not derive a basename from an opaque source pin.
