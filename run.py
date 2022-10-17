from subprocess import check_output

firmware_list = [
("Infinitime", "/home/smj/purs3/InfiniTime/build/src/pinetime-app-1.10.0.out.bc"),
("nrf52_kbd_rev_c", "/home/smj/purs3/nrf52-keyboard/keyboard/lot60-ble/_build/rev_c/nrf52_kbd.out.bc"),
("nrf52_kbd_rev_e", "/home/smj/purs3/nrf52-keyboard/keyboard/lot60-ble/_build/rev_e/nrf52_kbd.out.bc"),
("nrf52_kbd_rev_e_3led", "/home/smj/purs3/nrf52-keyboard/keyboard/lot60-ble/_build/rev_e_3led/nrf52_kbd.out.bc"),
("nrf52_kbd_rev_f", "/home/smj/purs3/nrf52-keyboard/keyboard/lot60-ble/_build/rev_f/nrf52_kbd.out.bc"),
("nrf52_kbd_rev_f_3", "/home/smj/purs3/nrf52-keyboard/keyboard/lot60-ble/_build/rev_f_3led/nrf52_kbd.out.bc"),
("nrf52_kbd_rev_g", "/home/smj/purs3/nrf52-keyboard/keyboard/lot60-ble/_build/rev_g/nrf52_kbd.out.bc"),
("nrf52_kbd_rev_g_3led", "/home/smj/purs3/nrf52-keyboard/keyboard/lot60-ble/_build/rev_g_3led/nrf52_kbd.out.bc"),
("nrf52_kbd_rev_g_832", "/home/smj/purs3/nrf52-keyboard/keyboard/lot60-ble/_build/rev_g_832/nrf52_kbd.out.bc"),
]

LLVM_DIR='/usr/lib/llvm-14'

for name, bc in firmware_list:
    check_output(f"{LLVM_DIR}/bin/opt -load-pass-plugin lib/libFindMMIOFunc.so -load-pass-plugin lib/libFindHALBypass.so --passes='print<hal-bypass>' --disable-output {bc} 2> {name}.out", shell=True)
