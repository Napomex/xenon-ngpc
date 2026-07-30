$MAXIMUM
; dmaprog.asm - Micro-DMA channel programming for the raster bar-split.
; Programs DMAS/DMAD/DMAC/DMAM for a word-mode (src++ -> fixed dst) channel.
; u32 args (always 4 bytes on stack -> no near/far ambiguity, cf. kuroi vramq).
;
; void dma_prog_ch0_u16(u32 src, u32 dst, u32 count);
; void dma_prog_ch1_u16(u32 src, u32 dst, u32 count);
;   (xsp+0)  return address (far) 4 bytes
;   (xsp+4)  src   (u32) 4 bytes  -> DMASn (table address)
;   (xsp+8)  dst   (u32) 4 bytes  -> DMADn (register address, e.g. 0x8034)
;   (xsp+12) count (u32) 4 bytes  -> DMACn (transfer count, low 16 used)
; DMAM = 0x09 : word transfer, source increments, destination fixed.

        module  dmaprog

        public  _dma_prog_ch0_u16
        public  _dma_prog_ch1_u16

DMAPROG section code large

_dma_prog_ch0_u16:
        ld      xwa,(xsp+4)
        ldcl    dmas0,xwa
        ld      xwa,(xsp+8)
        ldcl    dmad0,xwa
        ld      xwa,(xsp+12)
        ldcw    dmac0,wa
        ld      a,9
        ldcb    dmam0,a
        ret

_dma_prog_ch1_u16:
        ld      xwa,(xsp+4)
        ldcl    dmas1,xwa
        ld      xwa,(xsp+8)
        ldcl    dmad1,xwa
        ld      xwa,(xsp+12)
        ldcw    dmac1,wa
        ld      a,9
        ldcb    dmam1,a
        ret

        end
