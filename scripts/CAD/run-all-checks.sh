#!/usr/bin/env bash
# Every ladder, in one command, as the gate before a push that touched the Design tab.
#
# WHY A SCRIPT AND NOT CI. Three of the four rungs need a running application with an OpenGL
# canvas and synthetic input; GitHub's runners have neither. So the gate is local and explicit:
# run this, read the last line, and do not push a red one. The kernel suite is the only part CI
# can carry, and it already does.
#
#   scripts/CAD/run-all-checks.sh                 # kernel + engine + corpus (every 20th) + gestures + offer
#   FULL=1 scripts/CAD/run-all-checks.sh          # corpus over ALL 997 sheets (~25 min)
#   SKIP_GUI=1 scripts/CAD/run-all-checks.sh      # kernel only, for a machine with no rig
#
# The rig container is expected to be up with the app running and SNAPORCA_MCP set; bring it up
# with scripts/CAD/start-headless-gui.sh inside it. The corpus lives at /corpus in that container.
set -uo pipefail
# ../.. — this script lives in scripts/CAD/, so one level up is scripts/, not the repo root.
# It was scripts/ladder-all.sh when it was written; the move (ea5f25e8b9) fixed the three
# sibling scripts and missed this one, which left every rung looking for its own path under
# scripts/scripts/ and reporting seven instant failures that were all the same typo.
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

C="${C:-snaporca-gui}"
CORPUS="${CORPUS:-/corpus}"
STEP="${STEP:-20}"
[ -n "${FULL:-}" ] && STEP=1
fail=0

step() {
    local name="$1"; shift
    echo
    echo "=== $name ==="
    if "$@"; then echo "--- $name OK"; else echo "--- $name FAILED"; fail=1; fi
}

# Explicit, because docker exec leaves DISPLAY unset and the check scripts then fall back to
# their ":10" default. That default happens to be right for THIS fork's rig and wrong for the
# other one, whose Xvfb is on :11 -- where it produced "FATAL no app window on :10", a message
# that reads like a dead app rather than a wrong display. State it rather than rely on the default.
RIG_DISPLAY="${RIG_DISPLAY:-:10}"

run_in_rig() {                      # copy the script in fresh, then run it there
    docker cp "$1" "$C:/tmp/$(basename "$1")" >/dev/null || return 1
    shift
    docker exec -e DISPLAY="$RIG_DISPLAY" "$C" python3 "$@"
}

# FIRST, and it needs no rig: the offer table the menu is compiled from must be what the atlas
# says. The header calls itself GENERATED and had been hand-edited anyway — which cost four rows
# that existed only in the header, one row wired to the wrong action, and a count of 91 for a
# 92-row array, so the last verb was unreachable (snaporca-z8rs, snaporca-ziam).
step "offer table matches the atlas" python3 docs/ux/mockups/gen_offer_table.py --check

step "kernel suite" scripts/CAD/run-kernel-tests.sh --vol "${KVOL:-snaporca_kerneltest}"

if [ -z "${SKIP_GUI:-}" ]; then
    step "engine ladder (rungs 1-8, scripted geometry)" \
        run_in_rig scripts/CAD/check-sketch-engine.py /tmp/check-sketch-engine.py
    step "corpus rung (real drawings, every ${STEP}th)" \
        run_in_rig scripts/CAD/check-sketch-engine-corpus.py /tmp/check-sketch-engine-corpus.py --corpus "$CORPUS" --step "$STEP"
    step "corpus scale rung (the heaviest sheets)" \
        run_in_rig scripts/CAD/check-sketch-engine-corpus.py /tmp/check-sketch-engine-corpus.py --corpus "$CORPUS" --scale
    step "gesture ladder (mouse and keyboard)" \
        run_in_rig scripts/CAD/check-gui-sketching.py /tmp/check-gui-sketching.py
    # The offer ladder needs TWO extra things the others do not: the app must have been launched
    # with SNAPORCA_KEYTRACE=1 (its [OFFER] lines are the whole instrument), and it reads the
    # generated offer table to predict what each selection should show — which is not in the
    # container's own baked source tree, so it is copied in beside the script — /tmp, where
    # run_in_rig puts the script, is one of the paths the ladder looks in.
    docker cp src/slic3r/GUI/CAD/DesignOffer.hpp "$C:/tmp/DesignOffer.hpp" >/dev/null
    step "offer ladder (right-click, the menu, the verbs behind it)" \
        run_in_rig scripts/CAD/check-gui-context-menu.py /tmp/check-gui-context-menu.py
fi

echo
if [ "$fail" -eq 0 ]; then echo "ALL LADDERS HELD"; else echo "AT LEAST ONE LADDER FAILED"; fi
exit "$fail"
