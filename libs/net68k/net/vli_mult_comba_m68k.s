| uECC_vli_mult  —  8×8 word (P-256) looped Comba multiply, m68k 68020+
|
| OPT roadmap #5: I-cache-resident alternative to a fully-unrolled
| multiply (~1.1 KB — blows the 68030's 256-byte I-cache every
| call, so on the Classic II's 16-bit bus every instruction is fetched
| from RAM each time, and the caller's code is evicted too).
| This version is ~130 bytes: both inner loops + outer code stay cached
| after the first diagonal, trading ~6 cycles of DBRA per product for
| zero instruction-fetch traffic.
|
| Two-phase Comba (column-wise) schoolbook, 64 MULU.L total:
|   rising  k=0..7 :  products left[0..k]   × right[k..0]   (1..8 terms)
|   falling k=8..14:  products left[k-7..7] × right[7..k-7] (7..1 terms)
| Pointer-walked: left post-increment, right pre-decrement per diagonal.
| Loop/carry model verified on host against schoolbook reference
| (200000 random vectors + all-0xFFFFFFFF edge case).
|
| Register map:
|   d0/d1/d2  r0/r1/r2 column accumulators (fixed roles; rotated by
|             MOVE/MOVE/CLR after each column store)
|   d3        inner loop counter (DBRA)
|   d4        MULU high result
|   d5        left[i] load / MULU low result
|   d6        permanent zero for ADDX carry into r2
|   d7        outer counter (rising: k = inner count; falling: countdown)
|   a0        left base (falling: left diagonal start, walks up)
|   a1        right diagonal start (rising: walks up; falling: fixed +32)
|   a2        result (post-increment)
|   a3        left walk  (per diagonal)
|   a4        right walk (per diagonal, pre-decrement)
|
| X-flag safety: each product's ADD starts a fresh carry chain; DBRA,
| MOVE, MULU and the branches do not modify X.
| num_words arg (52(%sp)) is ignored — P-256 8×8 only, same contract as
| the unrolled version.

    .text
    .even
    .global uECC_vli_mult

uECC_vli_mult:
    movem.l %d2-%d7/%a2-%a4,-(%sp)  | 9 regs, 36 bytes
    movea.l 40(%sp),%a2     | a2 = result
    movea.l 44(%sp),%a0     | a0 = left base
    movea.l 48(%sp),%a1     | a1 = right base
    addq.l  #4,%a1          | rising k=0 diagonal top: -(a4) reads right[0]
    moveq   #0,%d0          | r0
    moveq   #0,%d1          | r1
    moveq   #0,%d2          | r2
    moveq   #0,%d6          | permanent zero
    moveq   #0,%d7          | k = 0 (doubles as inner DBRA count)

outer_rise:                 | diagonals k = 0..7, k+1 products each
    movea.l %a0,%a3         | left walk  = left[0]
    movea.l %a1,%a4         | right walk = right[k+1] (predec → right[k])
    move.l  %d7,%d3         | inner count = k
inner_rise:
    move.l  (%a3)+,%d5
    mulu.l  -(%a4),%d4:%d5  | left[i] * right[k-i]
    add.l   %d5,%d0
    addx.l  %d4,%d1
    addx.l  %d6,%d2
    dbra    %d3,inner_rise
    move.l  %d0,(%a2)+      | result[k] = r0
    move.l  %d1,%d0         | rotate accumulators
    move.l  %d2,%d1
    moveq   #0,%d2
    addq.l  #4,%a1          | next diagonal starts one word higher
    addq.l  #1,%d7
    cmpi.l  #8,%d7
    bne.s   outer_rise

    subq.l  #4,%a1          | a1 = right+32, fixed: -(a4) reads right[7]
    addq.l  #4,%a0          | left diagonal start = left[1]
    moveq   #6,%d7          | falling inner DBRA count: 6 down to 0

outer_fall:                 | diagonals k = 8..14, 15-k products each
    movea.l %a0,%a3
    movea.l %a1,%a4
    move.l  %d7,%d3
inner_fall:
    move.l  (%a3)+,%d5
    mulu.l  -(%a4),%d4:%d5  | left[i] * right[k-i]
    add.l   %d5,%d0
    addx.l  %d4,%d1
    addx.l  %d6,%d2
    dbra    %d3,inner_fall
    move.l  %d0,(%a2)+      | result[k] = r0
    move.l  %d1,%d0
    move.l  %d2,%d1
    moveq   #0,%d2
    addq.l  #4,%a0          | left start walks up
    subq.l  #1,%d7
    bpl.s   outer_fall

    move.l  %d0,(%a2)       | result[15] = final carry word

    movem.l (%sp)+,%d2-%d7/%a2-%a4
    rts

| End uECC_vli_mult (looped Comba)
