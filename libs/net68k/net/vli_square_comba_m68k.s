| uECC_vli_square  —  8-word (P-256) looped Comba square, m68k 68020+
|
| OPT roadmap #6 (first half): squaring needs only 36 MULU.L vs the
| multiply's 64 — 28 off-diagonal pairs (each counted twice) plus 8
| diagonal terms.  Replaces the C uECC_vli_square/mul2add path (gcc
| synthesizes the 64-bit dword arithmetic; this keeps everything in
| registers, same as vli_mult_comba_m68k.s).
|
| Column k = 0..14, pairs (i, k-i) with i < k-i, diagonal when k even:
|   rising  k=0..7 :  (k+1)>>1 pairs, left walk from left[0]
|   falling k=8..14:  (15-k)>>1 pairs, left walk from left[k-7]
| Per column: sum the pairs once into s2:s1:s0, double the sum with one
| ADD/ADDX/ADDX (s2 < 2^3 so no overflow out), add the diagonal square
| if k is even — after the pair loop a3 sits exactly on left[k/2] —
| then add the carried words from the previous column.
| Loop/carry model verified on host against schoolbook reference
| (200000 random vectors + all-0xFFFFFFFF edge case).
|
| Register map:
|   d0/d1/d2  s0/s1/s2 column pair-sum accumulators (cleared per column)
|   d3        pair count / inner loop counter (DBRA)
|   d4        MULU high result
|   d5        left[i] load / MULU low result / carry-in scratch
|   d6        permanent zero for ADDX carry into s2
|   d7        k (outer counter 0..14; bit 0 selects diagonal)
|   a0        left base (rising a3 init; falling: walks up per column)
|   a1        a4 init (rising: walks up; falling: fixed left+32)
|   a2        result (post-increment)
|   a3        left walk up   (per column; post-increment)
|   a4        left walk down (per column; pre-decrement)
|   a5/a6     c0/c1 carry words rotated out of the previous column
|
| X-flag safety: each ADD starts a fresh carry chain; DBRA, MOVE,
| MOVEA, MULU, BTST and the branches do not modify X; LSR/SUBQ touch X
| only before a chain starts.
| num_words arg (56(%sp)) is ignored — P-256 8-word only, same contract
| as uECC_vli_mult.

    .text
    .even
    .global uECC_vli_square

uECC_vli_square:
    movem.l %d2-%d7/%a2-%a6,-(%sp)  | 11 regs, 44 bytes
    movea.l 48(%sp),%a2     | a2 = result
    movea.l 52(%sp),%a0     | a0 = left base
    lea     4(%a0),%a1      | k=0 diagonal top: -(a4) reads left[0]
    moveq   #0,%d0          | s0
    moveq   #0,%d1          | s1
    moveq   #0,%d2          | s2
    moveq   #0,%d6          | permanent zero
    suba.l  %a5,%a5         | c0 = 0
    suba.l  %a6,%a6         | c1 = 0
    moveq   #0,%d7          | k = 0

outer_rise:                 | columns k = 0..7
    movea.l %a0,%a3         | left walk up from left[0]
    movea.l %a1,%a4         | walk down from left[k] (after predec)
    move.l  %d7,%d3
    addq.l  #1,%d3
    lsr.l   #1,%d3          | pairs = (k+1)>>1
    bsr     column
    addq.l  #4,%a1          | next diagonal starts one word higher
    addq.l  #1,%d7
    cmpi.l  #8,%d7
    bne.s   outer_rise

    subq.l  #4,%a1          | a1 = left+32, fixed: -(a4) reads left[7]
    addq.l  #4,%a0          | left walk start = left[1] for k=8

outer_fall:                 | columns k = 8..14
    movea.l %a0,%a3
    movea.l %a1,%a4
    moveq   #15,%d3
    sub.l   %d7,%d3
    lsr.l   #1,%d3          | pairs = (15-k)>>1
    bsr     column
    addq.l  #4,%a0          | left start walks up
    addq.l  #1,%d7
    cmpi.l  #15,%d7
    bne.s   outer_fall

    move.l  %a5,(%a2)       | result[15] = final carry word

    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts

column:                     | s = 0 on entry; d3 = pair count
    subq.l  #1,%d3
    bmi.s   no_pairs        | k=0 and k=14 have no off-diagonal pairs
inner:
    move.l  (%a3)+,%d5
    mulu.l  -(%a4),%d4:%d5  | left[i] * left[k-i]
    add.l   %d5,%d0
    addx.l  %d4,%d1
    addx.l  %d6,%d2
    dbra    %d3,inner
no_pairs:
    add.l   %d0,%d0         | s *= 2 — off-diagonal pairs count twice
    addx.l  %d1,%d1
    addx.l  %d2,%d2
    btst    #0,%d7
    bne.s   no_diag         | odd k: no diagonal term
    move.l  (%a3),%d5       | a3 sits on left[k/2] after the pair loop
    mulu.l  %d5,%d4:%d5     | left[k/2]^2
    add.l   %d5,%d0
    addx.l  %d4,%d1
    addx.l  %d6,%d2
no_diag:
    move.l  %a5,%d5         | add carry-in from previous column
    add.l   %d5,%d0
    move.l  %a6,%d5
    addx.l  %d5,%d1
    addx.l  %d6,%d2
    move.l  %d0,(%a2)+      | result[k] = s0
    movea.l %d1,%a5         | rotate: carry for next column
    movea.l %d2,%a6
    moveq   #0,%d0
    moveq   #0,%d1
    moveq   #0,%d2
    rts

| End uECC_vli_square (looped Comba square)
