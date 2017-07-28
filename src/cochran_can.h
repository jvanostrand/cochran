
typedef enum cochran_file_type_t {
	FILE_WAN,
	FILE_CAN,
} cochran_file_type_t;

typedef struct cochran_can_meta_t {
    cochran_file_type_t file_type;
	unsigned char file_format;
    char model[4];
	unsigned int header_offset;
	unsigned int address_size;
	unsigned int address_count;
    unsigned int log_size;
    unsigned int log_offset;
    unsigned int profile_offset;
    int decode_address[10];
    int decode_key_offset[10];
} cochran_can_meta_t;

typedef int (*cochran_can_foreach_callback_t) (cochran_can_meta_t *meta, const unsigned char *dive, unsigned int dive_size, unsigned int dive_num, int last_dive, void *userdata);

int cochran_can_meta(cochran_can_meta_t *meta, cochran_file_type_t file_type, const unsigned char *cleartext, unsigned int cleartext_size);
int cochran_can_foreach_dive(cochran_can_meta_t *meta, const unsigned char *cleartext, unsigned int cleartext_size, cochran_can_foreach_callback_t callback, void *userdata);
int cochran_can_decode_file(cochran_file_type_t file_type, const unsigned char *ciphertext, unsigned int ciphertext_size, unsigned char *cleartext);
