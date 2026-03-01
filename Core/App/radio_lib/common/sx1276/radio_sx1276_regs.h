/**
 * @file radio_sx1276_regs.h
 * @brief Mapa rejestrów SX1276 i definicje bitów używane w bibliotece.
 */

#ifndef APP_RADIO_LIB_COMMON_SX1276_RADIO_SX1276_REGS_H_
#define APP_RADIO_LIB_COMMON_SX1276_RADIO_SX1276_REGS_H_

/* Rejestry wspólne */
#define SX1276_REG_FIFO                    0x00U
#define SX1276_REG_OP_MODE                 0x01U
#define SX1276_REG_FRF_MSB                 0x06U
#define SX1276_REG_FRF_MID                 0x07U
#define SX1276_REG_FRF_LSB                 0x08U
#define SX1276_REG_PA_CONFIG               0x09U
#define SX1276_REG_LNA                     0x0CU
#define SX1276_REG_FIFO_ADDR_PTR           0x0DU
#define SX1276_REG_FIFO_TX_BASE_ADDR       0x0EU
#define SX1276_REG_FIFO_RX_BASE_ADDR       0x0FU
#define SX1276_REG_FIFO_RX_CURRENT_ADDR    0x10U
#define SX1276_REG_IRQ_FLAGS_MASK          0x11U
#define SX1276_REG_IRQ_FLAGS               0x12U
#define SX1276_REG_RX_NB_BYTES             0x13U
#define SX1276_REG_PKT_SNR_VALUE           0x19U
#define SX1276_REG_PKT_RSSI_VALUE          0x1AU
#define SX1276_REG_MODEM_CONFIG_1          0x1DU
#define SX1276_REG_MODEM_CONFIG_2          0x1EU
#define SX1276_REG_SYMB_TIMEOUT_LSB        0x1FU
#define SX1276_REG_PREAMBLE_MSB            0x20U
#define SX1276_REG_PREAMBLE_LSB            0x21U
#define SX1276_REG_PAYLOAD_LENGTH          0x22U
#define SX1276_REG_MODEM_CONFIG_3          0x26U
#define SX1276_REG_DETECTION_OPTIMIZE      0x31U
#define SX1276_REG_INVERT_IQ               0x33U
#define SX1276_REG_DETECTION_THRESHOLD     0x37U
#define SX1276_REG_SYNC_WORD               0x39U
#define SX1276_REG_INVERT_IQ2              0x3BU
#define SX1276_REG_DIO_MAPPING_1           0x40U
#define SX1276_REG_DIO_MAPPING_2           0x41U
#define SX1276_REG_VERSION                 0x42U
#define SX1276_REG_PA_DAC                  0x4DU

/* Maski odczytu/zapisu SPI */
#define SX1276_SPI_WRITE_MASK              0x80U
#define SX1276_SPI_READ_MASK               0x7FU

/* Identyfikator wersji */
#define SX1276_VERSION_ID                  0x12U

/* RegOpMode */
#define SX1276_OPMODE_LONG_RANGE_MODE      0x80U
#define SX1276_OPMODE_ACCESS_SHARED_REG    0x40U
#define SX1276_OPMODE_LOW_FREQ_MODE_ON     0x08U
#define SX1276_OPMODE_MODE_MASK            0x07U
#define SX1276_MODE_SLEEP                  0x00U
#define SX1276_MODE_STDBY                  0x01U
#define SX1276_MODE_FSTX                   0x02U
#define SX1276_MODE_TX                     0x03U
#define SX1276_MODE_FSRX                   0x04U
#define SX1276_MODE_RXCONTINUOUS           0x05U
#define SX1276_MODE_RXSINGLE               0x06U
#define SX1276_MODE_CAD                    0x07U

/* RegIrqFlags */
#define SX1276_IRQ_RX_TIMEOUT              0x80U
#define SX1276_IRQ_RX_DONE                 0x40U
#define SX1276_IRQ_PAYLOAD_CRC_ERROR       0x20U
#define SX1276_IRQ_VALID_HEADER            0x10U
#define SX1276_IRQ_TX_DONE                 0x08U
#define SX1276_IRQ_CAD_DONE                0x04U
#define SX1276_IRQ_FHSS_CHANGE_CHANNEL     0x02U
#define SX1276_IRQ_CAD_DETECTED            0x01U

/* RegDioMapping1 (tryb LoRa) */
#define SX1276_DIO0_MAP_RX_DONE            0x00U
#define SX1276_DIO0_MAP_TX_DONE            0x40U
#define SX1276_DIO0_MAP_CAD_DONE           0x80U

#define SX1276_DIO1_MAP_RX_TIMEOUT         0x00U
#define SX1276_DIO1_MAP_FHSS_CHANGE        0x10U
#define SX1276_DIO1_MAP_CAD_DETECTED       0x20U

/* RegPaDac */
#define SX1276_PA_DAC_ENABLE_20DBM         0x87U
#define SX1276_PA_DAC_DISABLE_17DBM        0x84U

#endif /* APP_RADIO_LIB_COMMON_SX1276_RADIO_SX1276_REGS_H_ */
