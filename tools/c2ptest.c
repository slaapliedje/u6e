/* c2ptest.c — host unit test: verify the fast (bit-transpose) chunky->planar c2p
 * produces BYTE-IDENTICAL output to the reference (8-accumulator) c2p, for a 16-pixel
 * group -> 8 plane words. The fast version (8x8 bit transpose, Hacker's Delight)
 * replaces ~528 ops/group with ~24, and the present is compute-bound (verified by
 * bench: doubling the writes adds only 2.2ms). Run before deploying to rdr_tt.c.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* REFERENCE: exactly what rdr_tt.c does today — pixel 0 -> MSB (bit 15) of each plane. */
static void c2p_ref(const uint8_t *px, uint16_t out[8]){
    uint16_t p0=0,p1=0,p2=0,p3=0,p4=0,p5=0,p6=0,p7=0;
    for(int p=0;p<16;p++){ unsigned c=px[p];
        p0=(uint16_t)((p0<<1)|(c&1));      p1=(uint16_t)((p1<<1)|((c>>1)&1));
        p2=(uint16_t)((p2<<1)|((c>>2)&1)); p3=(uint16_t)((p3<<1)|((c>>3)&1));
        p4=(uint16_t)((p4<<1)|((c>>4)&1)); p5=(uint16_t)((p5<<1)|((c>>5)&1));
        p6=(uint16_t)((p6<<1)|((c>>6)&1)); p7=(uint16_t)((p7<<1)|((c>>7)&1));
    }
    out[0]=p0;out[1]=p1;out[2]=p2;out[3]=p3;out[4]=p4;out[5]=p5;out[6]=p6;out[7]=p7;
}

/* 8x8 bit-matrix transpose. Input byte i (i=0..7) in bits [i*8 .. i*8+7] holds
 * pixel i's 8 plane bits (bit k = plane k). Output byte k holds plane k's 8 bits,
 * with pixel i at bit i. (Hacker's Delight transpose8.) */
static uint64_t transpose8(uint64_t x){
    x = (x & 0xAA55AA55AA55AA55ULL) | ((x & 0x00AA00AA00AA00AAULL)<<7) | ((x>>7) & 0x00AA00AA00AA00AAULL);
    x = (x & 0xCCCC3333CCCC3333ULL) | ((x & 0x0000CCCC0000CCCCULL)<<14) | ((x>>14) & 0x0000CCCC0000CCCCULL);
    x = (x & 0xF0F0F0F00F0F0F0FULL) | ((x & 0x00000000F0F0F0F0ULL)<<28) | ((x>>28) & 0x00000000F0F0F0F0ULL);
    return x;
}

/* FAST: two 8x8 transposes (pixels 0-7 -> high byte of each plane word, 8-15 -> low
 * byte). pixel 0 must land at plane-word bit 15, so within each 8-pixel half we load
 * pixel i into byte (7-i): then transpose byte k bit j = pixel (7-j) bit k, i.e. bit 7
 * = pixel 0 -> the byte's MSB = pixel 0, matching the reference. */
static void c2p_fast(const uint8_t *px, uint16_t out[8]){
    uint64_t hi=0, lo=0;
    for(int i=0;i<8;i++){
        hi |= (uint64_t)px[i]   << ((7-i)*8);   /* pixels 0..7  -> high byte of planes */
        lo |= (uint64_t)px[8+i] << ((7-i)*8);   /* pixels 8..15 -> low  byte of planes */
    }
    hi=transpose8(hi); lo=transpose8(lo);
    for(int k=0;k<8;k++){
        unsigned hb=(unsigned)((hi>>(k*8))&0xff);
        unsigned lb=(unsigned)((lo>>(k*8))&0xff);
        out[k]=(uint16_t)((hb<<8)|lb);
    }
}

int main(void){
    int fails=0;
    /* exhaustive-ish: all-same-byte patterns + many random groups */
    for(int v=0;v<256;v++){ uint8_t px[16]; for(int i=0;i<16;i++) px[i]=(uint8_t)v;
        uint16_t a[8],b[8]; c2p_ref(px,a); c2p_fast(px,b);
        for(int k=0;k<8;k++) if(a[k]!=b[k]){ printf("FAIL same=%d plane %d: ref %04x fast %04x\n",v,k,a[k],b[k]); fails++; }
    }
    unsigned seed=12345;
    for(long t=0;t<2000000;t++){ uint8_t px[16];
        for(int i=0;i<16;i++){ seed=seed*1103515245u+12345u; px[i]=(uint8_t)(seed>>16); }
        uint16_t a[8],b[8]; c2p_ref(px,a); c2p_fast(px,b);
        for(int k=0;k<8;k++) if(a[k]!=b[k]){ if(fails<10) printf("FAIL rand plane %d: ref %04x fast %04x\n",k,a[k],b[k]); fails++; }
    }
    printf(fails?"c2p FAST != REF (%d mismatches)\n":"c2p FAST == REF (byte-identical, all tests pass)\n", fails);
    return fails?1:0;
}
