
# Enabling the SCPI Serial Port and a Verbose VxWorks Console

The expansion connector of TDS3000(B) oscilloscopes have 2 serial ports. The first one
is the user serial port that accepts SCPI commands for remote control. The traffic is
bidirectional: the user gives commands, the scope returns answers. When active, the
user can use the menu system to configure this port: baud rate (maximum 38400), 7 or
8 data bits, number of stop bits etc.  Let's call this the SCPI serial port.

The second serial port is the VxWorks RTOS console. It's fixed at 38400 baud, and while
there both TX and RX pins, only the TX works in practice. The console is only used to output
status messages.

On TDS3000B versions, SCPI serial port is always enabled and the VxWorks console outputs
quite a bit of status messages, even when no expansion card is plugged in.

On a TDS3000-non-B version oscilloscope without expansion card, the SCPI serial port 
*sometimes*[^sometimes] doesn't work at all and while VxWorks console output works, but it's far less 
chatty. All it outputs during boot is this:

[^sometimes]: On my TDS3052, it can persistently not work for a whole evening, yet the
              following day it works fine, which leads me to think that there is really
              a missing pullup or pulldown somewhere. I consider that a hardware bug.

```
sysClkRateSet using 25 MHz
excInit called
```

EEVBlog user sicco figured out that pulling down the SNI_TENA signal (pin 17) on the expansion
connector enables the TDS3000B behavior. SNI_TENA goes to pin PB[19] of the MPC860 CPU. At
boot, its value is sampled and a value of 0 enables the serial ports. When the network port
of the scope is present, PB[19] is configured by the main application as an output that carries
the TENA signal of the Serial Network Interface that is used to control an Ethernet PHY chip.

TDS3000B scope come standard with a network port built-in. One way or the other, there is also
a pulldown resistor somewhere that is not present on TDS3000 scopes, which only support Ethernet
when a TDS3EM expansion card is plugged in.

Ideally, we want the VxWorks console output to be chatty. How can we make that happen?

One way is by soldering a 10k pulldown resistor on an expansion card. I only learned about this 
whole serial port business after the PCBs came back, but the resistor was still easy to add
thanks to the presence of an SNI debug bus. You can see the blue patch wire and resistor here:

![SNI TENA pulldown resistor and patch wire](./docs/sni_tena_pulldown.jpg)

This worked, but it's not ideal, and I definitely didn't want to spin the board for this.

But there are other ways:

* the RP2350 can drive down SNI_TENA at boot time 

  We can do this as long as we make sure to driving SNI_TENA before the MPC860 firmware 
  configures PB[19] to an output. After copying the main firmware to the DRAM, the boot
  loader always jumps to DRAM address 0x6000, the start of the main firmware. Hitting
  a breakpoint at this address would be the trigger for the RP2350 to stop driving
  SNI_TENA to zero. Breakpoints can be set with the BDM debug port which on the Cordyceps 
  is also under control of the RP2350.

* modify the value read from the PB[19] IO

  From the MPC860 datasheet, we know that the PB[19] is read through register mpc860PBDAT
  at address 0xFF000AC4. With the BDM debug port, the RP2350 can set a watchpoint at this
  address at power up, before the CPU hasn't even executed the very first instruction.

  The instruction that does this read is:
  
  `lwz r3,0x0AC4(r4)    ; r4 = 0xFF000000 / opcode = 0x80640AC4` 

  The watchpoint triggers after the value of mpc860PBDAT has already been written to register
  r3. All that's left to do now is to clear bit 19 of r3, using BDM once again, and send the
  CPU on its merry way.

  The PowerPC ISA can clear bit 19 with one instruction:

  `rlwinm r3,r3,0,20,18    ; r3 &= 0xFFFFEFFF / opcode = 0x54630524`
  
I like the second solution better because it has zero risk of having 2 pins driving against 
each other. I also prefer a watchpoint solution over a breakpoint: there are different
boot loaders in existence. Using a watchpoint makes the trick work on all of them as long as 
they all load mpc860PBDAT into r3. You can use BDM to check that the instruction
at the halted program counter location is 0x80640AC4.

Here's the full PowerPC code that does the check on my TDS3052:

```
         Address   Opcode     Instruction
PC-12    FFC003F0  3CC0FF00   lis    r6,0xFF00       ; r6 = 0xff000000
PC-8     FFC003F4  60C629F0   ori    r6,r6,0x29F0    ; r6 = 0xFF0029F0 -> bootDiagnosticEnable flag
PC-4     FFC003F8  80640AC4   lwz    r3,0x0AC4(r4)   ; Read PBDAT
PC       FFC003FC  70631000   andi.  r3,r3,0x1000    ; Isolate PB[19]
PC+4     FFC00400  2C031000   cmpwi  r3,0x1000       ; Pin 19 high?
PC+8     FFC00404  4182000C   beq    0xFFC00410      ; High -> disable
PC+12    FFC00408  38600001   li     r3,1            ; r3 = enable
PC+16    FFC0040C  48000008   b      0xFFC00414
PC+20    FFC00410  38600000   li     r3,0            ; r3 = disable
PC+24    FFC00414  98660000   stb    r3,0(r6)        ; Store bootDiagnosticEnable flag
```

The code and location can be different for different scopes.

