#define START_BYTES {0xAB, 0xAB}
#define END_BYTES {0xCD, 0xCD}

#define CSI_TAG_LOW 0x00 //low motion - likely just walking - used to detect presence
#define CSI_TAG_HIGH 0x01 //fast motion - likely a fall
#define VIB_TAG 0x02
#define MIC_TAG 0x03


enum Mode {
    NORMAL,
    DEV_CSI,
    DEV_VIB,
    DEV_MIC
};

extern enum Mode mode;
extern bool uart_ready;
