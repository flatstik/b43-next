# test/vphy — b43-next's PHY code on a virtual chip

Compiles the G-PHY files of b43-next as they are (`phy_common.c`, `phy_g.c`,
`lo.c`, `tables.c`, `wa.c`) against the headers of the kernel the module is
built for, into a user program whose bus is b43-tools' virtual chip
(`interpreter/vchipd.py`).

```sh
make [KSRC=...]
B43_STEPS=channels,power,periodic,rfkill \
./b43phy --trace trace.txt -- vchipd.py --corerev 10 --phy-ver 0x4208 \
         --radio-id 0x2050 --radio 1=0x2205017F
```

- What the PHY files call of `main.c` is cut out of it by `extract.py` at
  build time.
- `driver.c` is the kernel around them: the bus, time (`udelay` and `msleep`
  tell the chip) and the functions of `main.c` that need a running ucode or a
  bus reset. `b43_mac_suspend()` and `b43_mac_enable()` only flip `MACCONTROL`.
- `host.c` is the pipe to the chip, the trace and the allocator. The trace has
  one line per access in the op classes of b43-tools' `interpreter/README.md`,
  `WAIT` for the time that passes, `FN.IN`/`FN.OUT` for each PHY function
  entered and left (named through `nm`) and a `#` line at each checkpoint.

The board comes from the environment: `B43_CORE_REV`, `B43_CHIP_ID`,
`B43_BOARDFLAGS`, `B43_PA0B0..2`, `B43_TXPOWER` and the others read in
`driver.c`.

## Field inventory

```sh
make preprocessed
VCHIPD=.../interpreter/vchipd.py ./inventory.sh     # build/inventory/fields.tsv
```

`inventory.sh` runs the G boards and board options listed in it with every
step, then `fields.py` puts the accessor calls of the preprocessed files next
to the traces: one row per bit field of a PHY or radio register, as its
docstring describes. Registers that only a loop over a computed register
number writes are tables in register space and get one `range` row.
