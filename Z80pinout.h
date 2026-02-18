

// Address Bus (A0 - A16)
#define ADDR_MASK  (0xFFFF << 6) // Bits 0-16

// Data Bus (D0 - D7)
#define DATA_MASK  (0xFF << 22)

// /RD
#define RD_PIN      30
#define READ_MASK   (1u << RD_PIN)

// /WR
#define WR_PIN      31
#define WRITE_MASK  (1u << WR_PIN)

// /MREQ
#define MREQ_PIN    32
#define MREQ_MASK   (1ULL << MREQ_PIN)

// /IORQ
#define IORQ_PIN    33
#define IORQ_MASK   (1ULL << IORQ_PIN)

// /M1
#define M1_PIN      34
// #define M1_MASK (1u << M1_PIN)

// /WAIT
#define WAIT_PIN    35
// #define WAIT_MASK (1u << WAIT_PIN)

// /NMI
#define NMI_PIN     36
// #define NMI_MASK (1u << NMI_PIN)

// /INT
#define INT_PIN     37
// #define INT_MASK (1u << INT_PIN)
