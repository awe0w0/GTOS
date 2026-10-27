#!/bin/bash
# GTOS-DSH-139 G: run the whole same-image matrix.
#
# Every scene runs the same GTOS.bin. That is the point of the matrix: a difference between two rows is a difference in
# the command line and the core count, never in the image, so the rows are comparable and a row that fails cannot be
# explained by "a different build".
#
# The command lines come from scenes.tsv, which tools/derive-scenes-139.mjs writes from the scene declarations. This
# script does not compose them. It reads them, and it writes down what it was given for every row, so the verifier can
# compare the log against the command line that produced it rather than against a second derivation.
set -u

RUN="${RUN_DIR:?RUN_DIR must name the output directory}"
RT="${RT_ROOT:?RT_ROOT must name the runtime tree}"
IMAGE="${IMAGE:?IMAGE must name the kernel binary}"
HERE="$(cd "$(dirname "$0")" && pwd)"

mkdir -p "$RUN"
# The derived table is not in the repository: it is produced from the scene declarations by derive-scenes-139.mjs, and
# committing it would mean the matrix could run against a table that no longer matches the declarations. A missing table is
# therefore a refusal with the command that produces it, not an empty loop that reports success having run nothing.
if [ ! -f "$HERE/scenes.tsv" ]; then
  echo "run-matrix: tools/scenes.tsv is absent; derive it first:" >&2
  echo "  node tools/derive-scenes-139.mjs tools/scenes.tsv" >&2
  exit 2
fi
printf 'scene\tvcpus\tstop\tappend\trc\te9_bytes\tserial_bytes\n' > "$RUN/matrix.tsv"

# Skip the header, keep the order the derivation wrote.
tail -n +2 "$HERE/scenes.tsv" | while IFS=$'\t' read -r name vcpus stop append marker; do
  [ -n "$name" ] || continue
  line=$(bash "$HERE/run-scene-139.sh" "$name" "$append" "$vcpus" "$IMAGE" "$RUN" "$name")
  rc=$(printf '%s' "$line" | sed -n 's/.*rc=\([0-9]*\).*/\1/p')
  e9=$(printf '%s' "$line" | sed -n 's/.*e9=\([0-9]*\).*/\1/p')
  se=$(printf '%s' "$line" | sed -n 's/.*serial=\([0-9]*\).*/\1/p')
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$vcpus" "$stop" "$append" "$rc" "$e9" "$se" >> "$RUN/matrix.tsv"
  echo "$line"
done
