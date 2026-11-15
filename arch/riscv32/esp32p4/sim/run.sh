#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 5 ]; then
    echo "Usage: bash $0 ESP_EMU PARTS_DIR P4_BUILD C6_BUILD OUTPUT_DIR" >&2
    exit 2
fi

emulator=$(realpath "$1")
parts=$(realpath "$2")
p4=$(realpath "$3")
c6=$(realpath "$4")
output=$(realpath -m "$5")
profile=$(dirname "$(realpath "$0")")/a3.json
uart_port=${A3_UART_PORT:-5567}
control_port=${A3_CONTROL_PORT:-5566}
http_port=${A3_HTTP_PORT:-18080}
mkdir -- "$output"
cp -- "$profile" "$parts/button/pushbutton.wasm" "$parts/encoder/ky040.wasm" "$output"
cp -- "$p4/gtos-a3-flash.bin" "$output/p4-flash.bin"

runtime=$(mktemp -d -t gtos-a3-sdio-XXXXXXXX)
bridge="$runtime/bridge.sock"
slave=
cleanup() {
    if [ -n "$slave" ]; then
        kill "$slave" 2>/dev/null || true
        wait "$slave" 2>/dev/null || true
    fi
    rm -f -- "$bridge"
    rmdir -- "$runtime"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

"$emulator" --chip esp32c6 --firmware "$c6/gtos-a3-c6-flash.bin" --elf "$c6/gtos-a3-c6.elf" \
    --hosted "bridge:slave:$bridge" \
    --net "user,restrict=on,hostfwd=tcp:127.0.0.1:$http_port-:8080" \
    --wifi-ssid A3-LAB --wifi-password orbit-lab-test --wifi-auth wpa2-psk \
    > "$output/c6.log" 2>&1 &
slave=$!

deadline=$((SECONDS + 20))
until [ -S "$bridge" ] && grep -aq 'GTOS_A3_C6 READY' "$output/c6.log"; do
    if ! kill -0 "$slave" 2>/dev/null || [ "$SECONDS" -ge "$deadline" ]; then
        echo "C6 startup failed. See $output/c6.log" >&2
        exit 1
    fi
    sleep 0.1
done

echo "A3 UART: 127.0.0.1:$uart_port"
echo "A3 device control: 127.0.0.1:$control_port"
echo "Desktop Orbit status (after opening RADIO): http://127.0.0.1:$http_port/status"
"$emulator" --chip esp32p4 --firmware "$output/p4-flash.bin" --elf "$p4/gtos-a3.elf" \
    --psram-size 16M --cosim-board "$output/a3.json" --save-state \
    --hosted "bridge:host:$bridge" --hosted-reset-gpio 6 --hosted-reset-active-high \
    --net user,restrict=on --uart-tcp "127.0.0.1:$uart_port" \
    --control-tcp "127.0.0.1:$control_port" --exit-on GTOS_A3_VM_STOP
