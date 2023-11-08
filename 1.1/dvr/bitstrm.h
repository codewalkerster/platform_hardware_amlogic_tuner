typedef unsigned int UWORD32;

/*****************************************************************************/
/* Define a macro for inlining of NEXTBITS_32                                */
/*****************************************************************************/
#define     NEXTBITS_32(u4_word, u4_offset, pu4_bitstream)                  \
{                                                                           \
    UWORD32 *pu4_buf =  (pu4_bitstream);                                    \
    UWORD32 u4_word_off = ((u4_offset) >> 5);                               \
    UWORD32 u4_bit_off = (u4_offset) & 0x1F;                                \
                                                                            \
    u4_word = pu4_buf[u4_word_off++] << u4_bit_off;                         \
    if(u4_bit_off)                                                          \
    u4_word |= (pu4_buf[u4_word_off] >> (INT_IN_BITS - u4_bit_off));        \
}                                                                           \

#define INT_IN_BITS 32
/*****************************************************************************/
/* Define a macro for inlining of GETBITS: u4_no_bits shall not exceed 32    */
/*****************************************************************************/
#define     GETBITS(u4_code, u4_offset, pu4_bitstream, u4_no_bits)          \
{                                                                           \
    UWORD32 *pu4_buf =  (pu4_bitstream);                                    \
    UWORD32 u4_word_off = ((u4_offset) >> 5);                               \
    UWORD32 u4_bit_off = (u4_offset) & 0x1F;                                \
    u4_code = pu4_buf[u4_word_off++] << u4_bit_off;                         \
                                                                            \
    if(u4_bit_off)                                                          \
        u4_code |= (pu4_buf[u4_word_off] >> (INT_IN_BITS - u4_bit_off));    \
    u4_code = u4_code >> (INT_IN_BITS - u4_no_bits);                        \
    (u4_offset) += u4_no_bits;                                              \
}                                                                           \

static inline uint32_t CLZ(uint32_t u4_word)
{
  if (u4_word)
    return (__builtin_clz(u4_word));
  else
    return 31;
}
