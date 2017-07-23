#define array_uint32_le(p) (unsigned int) ( (p)[0] + ((p)[1]<<8) + ((p)[2]<<16) + ((p)[3]<<24) ) 
#define array_uint24_le(p) (unsigned int) ( (p)[0] + ((p)[1]<<8) + ((p)[2]<<16) )
#define array_uint16_le(p) (unsigned int) ( (p)[0] + ((p)[1]<<8) )

#define array_uint16_be(p) (unsigned int) ( ((p)[0] << 8 ) +  (p)[1] )
#define array_uint24_be(p) (unsigned int) ( ((p)[0] << 16) + ((p)[1] << 8 ) +  (p)[2] )
#define array_uint32_be(p) (unsigned int) ( ((p)[0] << 24) + ((p)[1] << 16) + ((p)[2] << 8) + (p)[3] )

#define COCHRAN_EPOCH 694242000
