# Maple test guide

This is the test guide for maple related changes. These are the common
problem areas and complexities of the maple bus peripherals, that I have
fixed in the past under much sweat, tears and much joy - So please consider
testing them really if you tend to change anything related to that.

## Maple Events

Use [maple_test](maple_test.c) to see if events fire if you press keys

## Keyboard

1. Does the keyboard work? Can you type in `htop` 
2. Can you switch TTYs ?
3. Does ctrl+c work?

Note: The events won't fire in maple_test currently.

## Mouse

1. Does `maple_test` show events for Buttons, Wheel, Movement?
2. Does mouse work under X?

## Controller

1. Does `maple_test` show events for Left, Down, Right, L/R-Trigger, stick

## VMU

1. Plug in a new VMU card - does is it get detected?
2. Does `mtdinfo` and `mtd_debug read` work and don't timeout?
3. Does `mtd_debug write` work?

## Hot Plugging

1. Leave one port empty - does the rest of the devices work?
2. Plug in new devices - do they get detected?
3. Do peripherals still work with plugged in-/out devices?