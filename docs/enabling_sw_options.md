# Enabling Software Options

In firmware version v3.41, there are 4 software options that require
an I2C dongle to enable:

* TDS3VID - Extended Video
* TDS3TMT - Telecom Mask Test
* TDS3LIM - Limit Test
* TDS3AAM - Advanced Analysis

A hack to enabling these features was created years ago by inserting an I2C
chip that had the feature string (e.g. TDS3VID) programmed in them.

It's trivial to find the location in the firmware where these software
functions are enabled: just trace the references to the "TDS2VID" string.

For each feature, the firmware loops through 4 I2C ports (there are 
only 2 on the actual scope) and compares thet extract strings:

```c
int softwareVideoOptionAvailable(void)
{
    for (int slot = 0; slot < 4; ++slot) {
        if (strcmp(optionKeys[slot], "TDS3VID") == 0 ||
            strcmp(optionKeys[slot], "TDS3ENG") == 0)
            return 1;
    }

    ...
}
```

Here's the assembly code:

```
; softwareVideoOptionAvailable()

; Prologue: save registers to stack
001C48B0  stwu    r1, -0x18(r1)
001C48B4  mflr    r0
001C48B8  stw     r29, 0x0C(r1)
001C48BC  stw     r30, 0x10(r1)
001C48C0  stw     r31, 0x14(r1)
001C48C4  stw     r0, 0x1C(r1)

001C48C8  li      r30, 0             ; slot index
001C48CC  lis     r9, 0x34
001C48D0  addi    r29, r9, 0x118     ; option-name pointer table
001C48D4  lis     r9, 0x42
001C48D8  addi    r31, r9, 0x0C70    ; first cached option-key string

; Check this slot against TDS3VID.
001C48DC  lwz     r3, 8(r29)         ; pointer to "TDS3VID"
001C48E0  mr      r4, r31
001C48E4  bl      0x002756E4         ; strcmp
001C48E8  cmpwi   cr1, r3, 0
001C48EC  beq     cr1, 0x001C4904    ; equal: available
...
```

The patch is trivial:

```
; softwareVideoOptionAvailable()
001C48B0  li      r3,1
001C48B4  blr
```

In other words, the function gets replaced like this:

```c
int softwareVideoOptionAvailable(void) {
    return 1;
}
```

