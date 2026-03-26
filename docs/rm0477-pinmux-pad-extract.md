# STM32H743 datasheet pinmux extract

- Source PDF: `stm32h743vi.pdf`
- Datasheet title: `STM32H742xI/G STM32H743xI/G`
- Scope: datasheet `pinout, pin description and alternate functions`
- Replaces the earlier partial extract that came from `RM0477`

## Relevant datasheet locations

- `Figure 5. LQFP100 pinout` - page `55`
- `Figure 7. LQFP144 pinout` - page `57`
- `Figure 9. LQFP176 pinout` - page `59`
- `Figure 11. LQFP208 pinout` - page `61`
- `Table 8. Legend/abbreviations used in the pinout table` - page `63`
- `Table 9. Pin/ball definition` - starts at page `64`
- `Table 10. Alternate functions` - pages `89-101`

## Notes

This file is now sourced from the **device datasheet**, which is the right place for the STM32H743 pinmux / alternate-function mapping.

`Table 10` is the core pinmux table. It lists GPIO pins by port and shows the signal mapped to `AF0..AF15`.

## Table 8 reference

```text
STM32H742xI/G STM32H743xI/G                                    Pinout, pin description and alternate functions


                           Table 8. Legend/abbreviations used in the pinout table
           Name                    Abbreviation                                  Definition

                                Unless otherwise specified in brackets below the pin name, the pin function during
         Pin name
                                and after reset is the same as the actual pin name
                                         S            Supply pin
                                          I           Input only pin
          Pin type
                                        I/O           Input / output pin
                                        ANA           Analog-only Input
                                         FT           5 V tolerant I/O
                                         TT           3.3 V tolerant I/O
                                         B            Dedicated BOOT0 pin
                                        RST           Bidirectional reset pin with embedded weak pull-up resistor

        I/O structure                                        Option for TT and FT I/Os

                                         _f           I2C FM+ option
                                         _a           analog option (supplied by VDDA)
                                         _u           USB option (supplied by VDD33USB)
                                         _h           High-speed low-voltage I/O
                                Unless otherwise specified by a note, all I/Os are set as floating inputs during and
           Notes
                                after reset.
                    Alternate
                                Functions selected through GPIOx_AFR registers
                    functions
 Pin functions
                   Additional
                                Functions directly selected/enabled through peripheral registers
                   functions


                 Table 9 and Table 10 show STM32H743xI/G pin/ball definition and alternate functions,
                 respectively. Refer to Table 2 for the features and peripherals available on STM32H742xI/G
                 devices.




                                                    DS12110 Rev 11                                               63/357
                                                                                                                          100
```

## Table 10 - Alternate functions

The block below is the datasheet text extraction of `Table 10. Alternate functions` from pages `89-101`.

```text
Table 10. Alternate functions




                                                                                                                                                                                                                                STM32H742xI/G STM32H743xI/G
                                    AF0        AF1          AF2          AF3          AF4           AF5          AF6          AF7          AF8         AF9         AF10        AF11        AF12       AF13      AF14    AF15
                                                                                                                                                       SAI4/
                                                                                                                                                                               I2C4/
                                                                                                                                                     FDCAN1/      SAI2/4/
                                                                                   I2C1/2/3/4/                                          SPI6/SAI2/                            UART7/      TIM1/8/
                                                                       LPUART/                                  SPI2/3/     SPI2/3/6/                FDACN2/       TIM8/
                                                                                    USART1/                                                 4/                               SWPMI1/       FMC/       TIM1/
                          Port              TIM1/2/16/      SAI1/        TIM8/                                  SAI1/3/    USART1/2/                 TIM13/14/   QUADSPI/
                                                                                     TIM15/      SPI1/2/3/4/                            UART4/5/8                             TIM1/8/    SDMMC1/      DCMI/    UART5/
                                    SYS     17/LPTIM1/    TIM3/4/5/   LPTIM2/3/4                                 I2C4/        3/6/                   QUADSPI/    SDMMC2/                                                SYS
                                                                                    LPTIM2/       5/6/CEC                                /LPUART                              DFSDM/      MDIOS/      LCD/      LCD
                                             HRTIM1      12/HRTIM1    /5/HRTIM1/                                UART4/       UART7/                    FMC/      OTG_HS1/
                                                                                    DFSDM/                                              /SDMMC1/                             SDMMC2/     OTG_FS1/     COMP
                                                                        DFSDM                                   DFSDM       SDMMC1                   SDMMC2/     OTG_FS2/
                                                                                      CEC                                               SPDIFRX1                              MDIOS/       LCD
                                                                                                                                                       LCD/      LCD/CRS
                                                                                                                                                                                ETH
                                                                                                                                                     SPDIFRX1
                                                                                                                           USART2_
                                            TIM2_CH1/                               TIM15_                                   CTS/                    SDMMC2_     SAI2_SD_    ETH_MII_                                   EVENT
                           PA0       -                   TIM5_CH1     TIM8_ETR                        -            -                    UART4_TX                                             -          -        -
                                            TIM2_ETR                                 BKIN                                  USART2_                     CMD           B         CRS                                       OUT
                                                                                                                             NSS
                                                                                                                                                                               ETH_
                                                                                                                            USART2_
                                                                                                                                                                              MII_RX_
                                                                       LPTIM3_      TIM15_                                    RTS                    QUADSPI_      SAI2_                                                EVENT
                           PA1       -      TIM2_CH2     TIM5_CH2                                     -            -                    UART4_RX                             CLK/ETH_        -          -      LCD_R2
                                                                         OUT         CH1N                                  /USART2_                   BK1_IO3     MCLK_B                                                 OUT
                                                                                                                                                                             RMII_REF_
                                                                                                                              DE
                                                                                                                                                                                CLK
                                                                       LPTIM4_      TIM15_                                 USART2_      SAI2_SCK                                          MDIOS_                        EVENT
                           PA2       -      TIM2_CH3     TIM5_CH3                                     -            -                                     -           -       ETH_MDIO                   -      LCD_R1
                                                                         OUT         CH1                                     TX            _B                                              MDIO                          OUT
                                                                       LPTIM5_      TIM15_                                 USART2_                               OTG_HS_     ETH_MII_                                   EVENT
                           PA3       -      TIM2_CH4     TIM5_CH4                                     -            -                        -         LCD_B2                                 -          -      LCD_B5
                                                                         OUT         CH2                                     RX                                  ULPI_D0       COL                                       OUT
                                                                                                 SPI1_NSS/     SPI3_NSS/   USART2_                                                       OTG_HS_     DCMI_      LCD_    EVENT
DS12110 Rev 11




                           PA4    D1PWREN       -        TIM5_ETR         -             -                                               SPI6_NSS         -           -           -
                                                                                                  I2S1_WS       I2S3_WS      CK                                                            SOF       HSYNC     VSYNC     OUT
                                            TIM2_CH1/                   TIM8_                    SPI1_SCK/                                                       OTG_HS_                                                EVENT
                           PA5    D2PWREN                    -                          -                          -           -        SPI6_SCK         -                       -           -          -      LCD_R4
                                            TIM2_ETR                    CH1N                      I2S1_CK                                                        ULPI_CK                                                 OUT
                 Port A




                                                                                                   SPI1_
                                                                                                                                          SPI6_       TIM13_     TIM8_BKIN    MDIOS_     TIM1_BKIN   DCMI_              EVENT
                           PA6       -      TIM1_BKIN    TIM3_CH1     TIM8_BKIN         -          MISO/           -           -                                                                               LCD_G2
                                                                                                                                          MISO         CH1       _COMP12       MDC       _COMP12     PIXCLK              OUT




                                                                                                                                                                                                                                Pinout, pin description and alternate functions
                                                                                                 I2S1_SDI
                                                                                                                                                                             ETH_MII_
                                                                                                   SPI1_                                                                      RX_DV/
                                              TIM1_                     TIM8_                                                             SPI6_       TIM14_                               FMC_                         EVENT
                           PA7       -                   TIM3_CH2                       -          MOSI/           -           -                                     -         ETH_                     -        -
                                              CH1N                      CH1N                                                              MOSI         CH1                                SDNWE                          OUT
                                                                                                 I2S1_SDO                                                                    RMII_CRS
                                                                                                                                                                               _DV
                                                                                                                                                                                          TIM8_
                                                          HRTIM_        TIM8_                                              USART1_                               OTG_FS_                                                EVENT
                           PA8     MCO1     TIM1_CH1                               I2C3_SCL           -            -                        -            -                   UART7_RX    BKIN2_      LCD_B3    LCD_R6
                                                           CHB2         BKIN2                                                CK                                    SOF                                                   OUT
                                                                                                                                                                                         COMP12
                                                          HRTIM_      LPUART1_       I2C3_       SPI2_SCK/                 USART1_                                           ETH_TX_                                    EVENT
                           PA9       -      TIM1_CH2                                                               -                        -            -           -                       -       DCMI_D0   LCD_R5
                                                           CHC1          TX          SMBA         I2S2_CK                    TX                                                ER                                        OUT
                                                          HRTIM_      LPUART1_                                             USART1_                               OTG_FS_      MDIOS_                                    EVENT
                           PA10      -      TIM1_CH3                                    -             -            -                        -            -                                LCD_B4     DCMI_D1   LCD_B1
                                                           CHC2          RX                                                  RX                                     ID         MDIO                                      OUT
                                                                                                                           USART1_
                                                          HRTIM_      LPUART1_                   SPI2_NSS/                   CTS/                    FDCAN1_     OTG_FS_                                                EVENT
                           PA11      -      TIM1_CH4                                    -                      UART4_RX                     -                                    -           -          -      LCD_R4
                                                           CHD1         CTS                       I2S2_WS                  USART1_                     RX          DM                                                    OUT
                                                                                                                             NSS
89/357

                                                                                                          Table 10. Alternate functions
90/357




                                                                                                                                                                                                                                Pinout, pin description and alternate functions
                                    AF0        AF1          AF2          AF3          AF4           AF5          AF6          AF7          AF8         AF9         AF10       AF11      AF12       AF13        AF14     AF15
                                                                                                                                                       SAI4/
                                                                                                                                                                              I2C4/
                                                                                                                                                     FDCAN1/      SAI2/4/
                                                                                   I2C1/2/3/4/                                          SPI6/SAI2/                           UART7/     TIM1/8/
                                                                       LPUART/                                  SPI2/3/     SPI2/3/6/                FDACN2/       TIM8/
                                                                                    USART1/                                                 4/                              SWPMI1/      FMC/      TIM1/
                          Port              TIM1/2/16/      SAI1/        TIM8/                                  SAI1/3/    USART1/2/                 TIM13/14/   QUADSPI/
                                                                                     TIM15/      SPI1/2/3/4/                            UART4/5/8                            TIM1/8/   SDMMC1/     DCMI/      UART5/
                                    SYS     17/LPTIM1/    TIM3/4/5/   LPTIM2/3/4                                 I2C4/        3/6/                   QUADSPI/    SDMMC2/                                                SYS
                                                                                    LPTIM2/       5/6/CEC                                /LPUART                             DFSDM/     MDIOS/     LCD/        LCD
                                             HRTIM1      12/HRTIM1    /5/HRTIM1/                                UART4/       UART7/                    FMC/      OTG_HS1/
                                                                                    DFSDM/                                              /SDMMC1/                            SDMMC2/    OTG_FS1/    COMP
                                                                        DFSDM                                   DFSDM       SDMMC1                   SDMMC2/     OTG_FS2/
                                                                                      CEC                                               SPDIFRX1                             MDIOS/      LCD
                                                                                                                                                       LCD/      LCD/CRS
                                                                                                                                                                               ETH
                                                                                                                                                     SPDIFRX1
                                                                      LPUART1_                                             USART1_
                                                          HRTIM_        RTS/                     SPI2_SCK/                   RTS/                    FDCAN1_     OTG_FS_                                                EVENT
                           PA12      -      TIM1_ETR                                    -                      UART4_TX                 SAI2_FS_B                              -          -          -        LCD_R5
                                                           CHD2       LPUART1_                    I2S2_CK                  USART1_                     TX          DP                                                    OUT
                                                                         DE                                                   DE
                                  JTMS-                                                                                                                                                                                 EVENT
                           PA13                 -            -            -             -             -            -           -            -            -          -          -          -          -          -
                 Port A




                                  SWDIO                                                                                                                                                                                  OUT
                                   JTCK-                                                                                                                                                                                EVENT
                           PA14                 -            -            -             -             -            -           -            -            -          -          -          -          -          -
                                  SWCLK                                                                                                                                                                                  OUT
                                                                                                                                         UART4_
                                            TIM2_CH1/     HRTIM_                                 SPI1_NSS/     SPI3_NSS/                                                                                                EVENT
                           PA15    JTDI                                   -           CEC                                  SPI6_NSS       RTS/           -          -       UART7_TX      -          -          -
                                            TIM2_ETR       FLT1                                   I2S1_WS       I2S3_WS                                                                                                  OUT
                                                                                                                                        UART4_DE
                                              TIM1_                     TIM8_                                  DFSDM1_                   UART4_                  OTG_HS_    ETH_MII_                                    EVENT
                           PB0       -                   TIM3_CH3                       -             -                        -                     LCD_R3                               -          -        LCD_G1
                                              CH2N                      CH2N                                    CKOUT                     CTS                    ULPI_D1     RXD2                                        OUT
                                              TIM1_                     TIM8_                                  DFSDM1_                                           OTG_HS_    ETH_MII_                                    EVENT
DS12110 Rev 11




                           PB1       -                   TIM3_CH4                       -             -                        -            -        LCD_R6                               -          -        LCD_G0
                                              CH3N                      CH3N                                    DATIN1                                           ULPI_D2     RXD3                                        OUT
                                                                                                                             SPI3_
                                                                                   DFSDM1_                     SAI1_SD_                 SAI4_SD_     QUADSPI_               ETH_TX_                                     EVENT
                           PB2    RTC_OUT       -         SAI1_D1         -                           -                      MOSI/                               SAI4_D1                  -          -          -
                                                                                    CKIN1                          A                        A          CLK                    ER                                         OUT
                                                                                                                           I2S3_SDO
                                  JTDO/
                                                          HRTIM_                                 SPI1_SCK/     SPI3_SCK/                             SDMMC2_      CRS_                                                  EVENT
                           PB3    TRACE     TIM2_CH2                      -             -                                      -        SPI6_SCK                            UART7_RX      -          -          -
                                                           FLT4                                   I2S1_CK       I2S3_CK                                D2         SYNC                                                   OUT
                                   SWO
                                                                                                   SPI1_         SPI3_
                                             TIM16_                    HRTIM_                                              SPI2_NSS/      SPI6_      SDMMC2_                                                            EVENT
                           PB4    NJTRST                 TIM3_CH1                       -          MISO/         MISO/                                              -       UART7_TX      -          -          -
                 Port B




                                              BKIN                      EEV6                                                I2S2_WS       MISO         D3                                                                OUT
                                                                                                 I2S1_SDI      I2S3_SDI
                                                                                                   SPI1_                     SPI3_
                                             TIM17_                    HRTIM_        I2C1_                      I2C4_                     SPI6_      FDCAN2_     OTG_HS_    ETH_PPS_    FMC_                            EVENT
                           PB5       -                   TIM3_CH2                                  MOSI/                     MOSI/                                                                DCMI_D10   UART5_RX
                                              BKIN                      EEV7         SMBA                       SMBA                      MOSI         RX        ULPI_D7      OUT      SDCKE1                            OUT
                                                                                                 I2S1_SDO                  I2S3_SDO
                                             TIM16_                    HRTIM_                                              USART1_      LPUART1_     FDCAN2_     QUADSPI_   DFSDM1_     FMC_                            EVENT




                                                                                                                                                                                                                                STM32H742xI/G STM32H743xI/G
                           PB6       -                   TIM4_CH1                  I2C1_SCL         CEC        I2C4_SCL                                                                           DCMI_D5    UART5_TX
                                              CH1N                      EEV8                                                 TX            TX          TX        BK1_NCS     DATIN5     SDNE1                            OUT
                                             TIM17_                    HRTIM_                                              USART1_      LPUART1_                            DFSDM1_                DCMI_                EVENT
                           PB7       -                   TIM4_CH2                  I2C1_SDA           -        I2C4_SDA                                  -          -                  FMC_NL                   -
                                              CH1N                      EEV9                                                 RX            RX                                CKIN5                 VSYNC                 OUT
                                             TIM16_                   DFSDM1_                                              SDMMC1_                   FDCAN1_     SDMMC2_     ETH_      SDMMC1_                          EVENT
                           PB8       -                   TIM4_CH3                  I2C1_SCL           -        I2C4_SCL                 UART4_RX                                                  DCMI_D6     LCD_B6
                                              CH1                      CKIN7                                                 CKIN                      RX          D4       MII_TXD3     D4                              OUT
                                             TIM17_                   DFSDM1_                    SPI2_NSS/                 SDMMC1_                   FDCAN1_     SDMMC2_     I2C4_     SDMMC1_                          EVENT
                           PB9       -                   TIM4_CH4                  I2C1_SDA                    I2C4_SDA                 UART4_TX                                                  DCMI_D7     LCD_B7
                                              CH1                      DATIN7                     I2S2_WS                    CDIR                      TX          D5        SMBA        D5                              OUT

                                                                                                           Table 10. Alternate functions




                                                                                                                                                                                                                                  STM32H742xI/G STM32H743xI/G
                                    AF0         AF1          AF2          AF3          AF4           AF5          AF6         AF7          AF8         AF9         AF10       AF11       AF12        AF13        AF14     AF15
                                                                                                                                                       SAI4/
                                                                                                                                                                              I2C4/
                                                                                                                                                     FDCAN1/      SAI2/4/
                                                                                    I2C1/2/3/4/                                         SPI6/SAI2/                           UART7/      TIM1/8/
                                                                        LPUART/                                 SPI2/3/     SPI2/3/6/                FDACN2/       TIM8/
                                                                                     USART1/                                                4/                              SWPMI1/       FMC/      TIM1/
                          Port               TIM1/2/16/      SAI1/        TIM8/                                 SAI1/3/    USART1/2/                 TIM13/14/   QUADSPI/
                                                                                      TIM15/      SPI1/2/3/4/                           UART4/5/8                            TIM1/8/    SDMMC1/     DCMI/       UART5/
                                    SYS      17/LPTIM1/    TIM3/4/5/   LPTIM2/3/4                                I2C4/        3/6/                   QUADSPI/    SDMMC2/                                                  SYS
                                                                                     LPTIM2/       5/6/CEC                               /LPUART                             DFSDM/      MDIOS/     LCD/         LCD
                                              HRTIM1      12/HRTIM1    /5/HRTIM1/                               UART4/       UART7/                    FMC/      OTG_HS1/
                                                                                     DFSDM/                                             /SDMMC1/                            SDMMC2/     OTG_FS1/    COMP
                                                                         DFSDM                                  DFSDM       SDMMC1                   SDMMC2/     OTG_FS2/
                                                                                       CEC                                              SPDIFRX1                             MDIOS/       LCD
                                                                                                                                                       LCD/      LCD/CRS
                                                                                                                                                                               ETH
                                                                                                                                                     SPDIFRX1
                                                           HRTIM_       LPTIM2_                   SPI2_SCK/     DFSDM1_    USART3_                   QUADSPI_    OTG_HS_    ETH_MII_                                      EVENT
                           PB10      -       TIM2_CH3                               I2C2_SCL                                                -                                              -           -        LCD_G4
                                                           SCOUT          IN1                      I2S2_CK       DATIN7      TX                      BK1_NCS     ULPI_D3     RX_ER                                         OUT
                                                                                                                                                                             ETH_MII_
                                                           HRTIM_       LPTIM2_                                 DFSDM1_    USART3_                               OTG_HS_      TX_EN/                                      EVENT
                           PB11      -       TIM2_CH4                               I2C2_SDA           -                                    -            -                                 -           -        LCD_G5
                                                            SCIN          ETR                                    CKIN7       RX                                  ULPI_D4    ETH_RMII_                                      OUT
                                                                                                                                                                              TX_EN
                                                                                                                                                                            ETH_MII_T
                                                                                      I2C2_       SPI2_NSS/     DFSDM1_    USART3_                   FDCAN2_     OTG_HS_                OTG_HS_    TIM1_BKIN              EVENT
                           PB12      -       TIM1_BKIN        -            -                                                                -                               XD0/ETH_                           UART5_RX
                                                                                      SMBA         I2S2_WS       DATIN1      CK                        RX        ULPI_D5                   ID      _COMP12                 OUT
                                                                                                                                                                            RMII_TXD0
                 Port B




                                                                                                                           USART3_                                           ETH_MII_
                                               TIM1_                    LPTIM2_                   SPI2_SCK/     DFSDM1_      CTS/                    FDCAN2_     OTG_HS_      TXD1/                                       EVENT
                           PB13      -                        -                          -                                                  -                                              -           -       UART5_TX
                                               CH1N                       OUT                      I2S2_CK       CKIN1     USART3_                     TX        ULPI_D6    ETH_RMII_                                      OUT
                                                                                                                             NSS                                              TXD1
                                                                                                                           USART3_
                                                                                                    SPI2_                                UART4_
DS12110 Rev 11




                                               TIM1_       TIM12_        TIM8_      USART1_                     DFSDM1_      RTS/                    SDMMC2_                            OTG_HS_                           EVENT
                           PB14      -                                                              MISO/                                 RTS/                      -           -                      -          -
                                               CH2N         CH1          CH2N         TX                         DATIN2    USART3_                     D0                                 DM                               OUT
                                                                                                  I2S2_SDI                              UART4_DE
                                                                                                                              DE
                                                                                                    SPI2_
                                   RTC_        TIM1_       TIM12_        TIM8_      USART1_                     DFSDM1_                  UART4_      SDMMC2_                            OTG_HS_                           EVENT
                           PB15                                                                     MOSI/                      -                                    -           -                      -          -
                                   REFIN       CH3N         CH2          CH3N         RX                         CKIN2                    CTS          D1                                 DP                               OUT
                                                                                                  I2S2_SDO




                                                                                                                                                                                                                                  Pinout, pin description and alternate functions
                                                                       DFSDM1_                                  DFSDM1_                                          OTG_HS_                 FMC_                             EVENT
                           PC0       -           -            -                          -             -                       -        SAI2_FS_B        -                      -                      -        LCD_R5
                                                                        CKIN0                                    DATIN4                                          ULPI_STP               SDNWE                              OUT
                                                                                                    SPI2_
                                                                       DFSDM1_      DFSDM1_                     SAI1_SD_                SAI4_SD_     SDMMC2_                            MDIOS_                            EVENT
                           PC1    TRACED0        -         SAI1_D1                                  MOSI/                      -                                 SAI4_D1    ETH_MDC                    -          -
                                                                        DATIN0       CKIN4                          A                       A          CK                                MDC                               OUT
                                                                                                  I2S2_SDO
                                                                                                    SPI2_
                                                                       DFSDM1_                                  DFSDM1_                                          OTG_HS_    ETH_MII_     FMC_                             EVENT
                           PC2    C1DSLEEP       -            -                          -          MISO/                      -            -            -                                             -          -
                                                                        CKIN1                                    CKOUT                                           ULPI_DIR    TXD2        SDNE0                             OUT
                                                                                                  I2S2_SDI
                                                                                                    SPI2_                                                                     ETH_
                 Port C




                                                                       DFSDM1_                                                                                   OTG_HS_                 FMC_                             EVENT
                           PC3    C1SLEEP        -            -                          -          MOSI/          -           -            -            -                   MII_TX_                   -          -
                                                                        DATIN1                                                                                   ULPI_NXT               SDCKE0                             OUT
                                                                                                  I2S2_SDO                                                                    CLK
                                                                                                                                                                             ETH_MII_
                                                                       DFSDM1_                                                                       SPDIFRX1                 RXD0/      FMC_                             EVENT
                           PC4    C2DSLEEP       -            -                          -        I2S1_MCK         -           -            -                       -                                  -          -
                                                                        CKIN2                                                                          _IN3                 ETH_RMII_    SDNE0                             OUT
                                                                                                                                                                              RXD0
                                                                                                                                                                             ETH_MII_
                                                                       DFSDM1_                                                                       SPDIFRX1                 RXD1/      FMC_      COMP1_                 EVENT
                           PC5    C2SLEEP        -         SAI1_D3                       -             -           -           -            -                    SAI4_D3                                          -
                                                                        DATIN2                                                                         _IN4                 ETH_RMII_   SDCKE0      OUT                    OUT
                                                                                                                                                                              RXD1
91/357

                                                                                                          Table 10. Alternate functions
92/357




                                                                                                                                                                                                                              Pinout, pin description and alternate functions
                                    AF0        AF1          AF2          AF3          AF4           AF5          AF6          AF7          AF8         AF9         AF10       AF11      AF12       AF13       AF14    AF15
                                                                                                                                                       SAI4/
                                                                                                                                                                              I2C4/
                                                                                                                                                     FDCAN1/      SAI2/4/
                                                                                   I2C1/2/3/4/                                          SPI6/SAI2/                           UART7/     TIM1/8/
                                                                       LPUART/                                  SPI2/3/     SPI2/3/6/                FDACN2/       TIM8/
                                                                                    USART1/                                                 4/                              SWPMI1/      FMC/      TIM1/
                          Port              TIM1/2/16/      SAI1/        TIM8/                                  SAI1/3/    USART1/2/                 TIM13/14/   QUADSPI/
                                                                                     TIM15/      SPI1/2/3/4/                            UART4/5/8                            TIM1/8/   SDMMC1/     DCMI/     UART5/
                                    SYS     17/LPTIM1/    TIM3/4/5/   LPTIM2/3/4                                 I2C4/        3/6/                   QUADSPI/    SDMMC2/                                              SYS
                                                                                    LPTIM2/       5/6/CEC                                /LPUART                             DFSDM/     MDIOS/     LCD/       LCD
                                             HRTIM1      12/HRTIM1    /5/HRTIM1/                                UART4/       UART7/                    FMC/      OTG_HS1/
                                                                                    DFSDM/                                              /SDMMC1/                            SDMMC2/    OTG_FS1/    COMP
                                                                        DFSDM                                   DFSDM       SDMMC1                   SDMMC2/     OTG_FS2/
                                                                                      CEC                                               SPDIFRX1                             MDIOS/      LCD
                                                                                                                                                       LCD/      LCD/CRS
                                                                                                                                                                               ETH
                                                                                                                                                     SPDIFRX1
                                             HRTIM_                                DFSDM1_                                 USART6_      SDMMC1_       FMC_       SDMMC2_               SDMMC1_                LCD_    EVENT
                           PC6       -                   TIM3_CH1     TIM8_CH1                   I2S2_MCK          -                                                           -                  DCMI_D0
                                              CHA1                                  CKIN3                                    TX          D0DIR        NWAIT        D6                    D6                  HSYNC     OUT
                                             HRTIM_                                DFSDM1_                                 USART6_      SDMMC1_                  SDMMC2_               SDMMC1_                        EVENT
                           PC7     TRGIO                 TIM3_CH2     TIM8_CH2                        -        I2S3_MCK                              FMC_NE1                SWPMI_TX              DCMI_D1    LCD_G6
                                              CHA2                                  DATIN3                                   RX          D123DIR                   D7                    D7                            OUT
                                                                                                                                         UART5_
                                             HRTIM_                                                                        USART6_                   FMC_NE2/               SWPMI_     SDMMC1_                        EVENT
                           PC8    TRACED1                TIM3_CH3     TIM8_CH3          -             -            -                      RTS/                      -                             DCMI_D2      -
                                              CHB1                                                                           CK                      FMC_NCE                  RX         D0                            OUT
                                                                                                                                        UART5_DE
                                                                                                                                         UART5_      QUADSPI_                SWPMI_    SDMMC1_                        EVENT
                           PC9     MCO2         -        TIM3_CH4     TIM8_CH4     I2C3_SDA      I2S_CKIN          -           -                                 LCD_G3                           DCMI_D3    LCD_B2
                                                                                                                                          CTS         BK1_IO0               SUSPEND      D1                            OUT
                                                          HRTIM_      DFSDM1_                                  SPI3_SCK/   USART3_                   QUADSPI_                          SDMMC1_                        EVENT
                           PC10      -          -                                       -             -                                 UART4_TX                    -          -                  DCMI_D8    LCD_R2
                                                           EEV1        CKIN5                                    I2S3_CK      TX                       BK1_IO1                            D2                            OUT
                 Port C




                                                                                                                 SPI3_
                                                          HRTIM_      DFSDM1_                                              USART3_                   QUADSPI_                          SDMMC1_                        EVENT
                           PC11      -          -                                       -             -          MISO/                  UART4_RX                    -          -                  DCMI_D4      -
                                                           FLT2        DATIN5                                                RX                      BK2_NCS                             D3                            OUT
                                                                                                               I2S3_SDI
DS12110 Rev 11




                                                                                                                 SPI3_
                                                          HRTIM_                                                           USART3_                                                     SDMMC1_                        EVENT
                           PC12   TRACED3       -                         -             -             -          MOSI/                  UART5_TX         -          -          -                  DCMI_D9      -
                                                           EEV2                                                              CK                                                          CK                            OUT
                                                                                                               I2S3_SDO
                                                                                                                                                                                                                      EVENT
                           PC13      -          -            -            -             -             -            -           -            -            -          -          -          -          -         -
                                                                                                                                                                                                                       OUT
                                                                                                                                                                                                                      EVENT
                           PC14      -          -            -            -             -             -            -           -            -            -          -          -          -          -         -
                                                                                                                                                                                                                       OUT
                                                                                                                                                                                                                      EVENT
                           PC15      -          -            -            -             -             -            -           -            -            -          -          -          -          -         -
                                                                                                                                                                                                                       OUT
                 Port D




                                                                      DFSDM1_                                  SAI3_SCK                              FDCAN1_                           FMC_D2/                        EVENT
                           PD0       -          -            -                          -             -                        -        UART4_RX                    -          -                     -         -
                                                                       CKIN6                                      _A                                   RX                              FMC_DA2                         OUT

                                                                      DFSDM1_                                  SAI3_SD_                              FDCAN1_                           FMC_D3/                        EVENT
                           PD1       -          -            -                          -             -                        -        UART4_TX                    -          -                     -         -




                                                                                                                                                                                                                              STM32H742xI/G STM32H743xI/G
                                                                       DATIN6                                      A                                   TX                              FMC_DA3                         OUT
                                                                                                                                                                                       SDMMC1_                        EVENT
                           PD2    TRACED2       -        TIM3_ETR         -             -             -            -           -        UART5_RX         -          -          -                  DCMI_D11     -
                                                                                                                                                                                         CMD                           OUT
                                                                                                                           USART2_
                                                                      DFSDM1_                    SPI2_SCK/                   CTS/                                                                                     EVENT
                           PD3       -          -            -                          -                          -                        -            -          -          -       FMC_CLK    DCMI_D5    LCD_G7
                                                                       CKOUT                      I2S2_CK                  USART2_                                                                                     OUT
                                                                                                                             NSS
                                                                                                                           USART2_
                                                          HRTIM_                                                             RTS/                                                                                     EVENT
                           PD4       -          -                         -             -             -        SAI3_FS_A                    -            -          -          -       FMC_NOE       -         -
                                                           FLT3                                                            USART2_                                                                                     OUT
                                                                                                                              DE

                                                                                                      Table 10. Alternate functions




                                                                                                                                                                                                                           STM32H742xI/G STM32H743xI/G
                                  AF0      AF1          AF2          AF3          AF4           AF5          AF6          AF7          AF8         AF9         AF10       AF11        AF12      AF13       AF14    AF15
                                                                                                                                                   SAI4/
                                                                                                                                                                           I2C4/
                                                                                                                                                 FDCAN1/      SAI2/4/
                                                                               I2C1/2/3/4/                                          SPI6/SAI2/                            UART7/     TIM1/8/
                                                                   LPUART/                                  SPI2/3/     SPI2/3/6/                FDACN2/       TIM8/
                                                                                USART1/                                                 4/                               SWPMI1/      FMC/      TIM1/
                          Port          TIM1/2/16/      SAI1/        TIM8/                                  SAI1/3/    USART1/2/                 TIM13/14/   QUADSPI/
                                                                                 TIM15/      SPI1/2/3/4/                            UART4/5/8                             TIM1/8/   SDMMC1/     DCMI/     UART5/
                                  SYS   17/LPTIM1/    TIM3/4/5/   LPTIM2/3/4                                 I2C4/        3/6/                   QUADSPI/    SDMMC2/                                               SYS
                                                                                LPTIM2/       5/6/CEC                                /LPUART                              DFSDM/     MDIOS/     LCD/       LCD
                                         HRTIM1      12/HRTIM1    /5/HRTIM1/                                UART4/       UART7/                    FMC/      OTG_HS1/
                                                                                DFSDM/                                              /SDMMC1/                             SDMMC2/    OTG_FS1/    COMP
                                                                    DFSDM                                   DFSDM       SDMMC1                   SDMMC2/     OTG_FS2/
                                                                                  CEC                                               SPDIFRX1                              MDIOS/      LCD
                                                                                                                                                   LCD/      LCD/CRS
                                                                                                                                                                            ETH
                                                                                                                                                 SPDIFRX1
                                                     HRTIM_EE                                                          USART2_                                                                                     EVENT
                           PD5     -        -                         -             -             -            -                        -            -           -          -       FMC_NWE       -         -
                                                        V3                                                               TX                                                                                         OUT
                                                                                             SPI3_MOS
                                                                  DFSDM1_      DFSDM1_                     SAI1_SD_    USART2_      SAI4_SD_                             SDMMC2_     FMC_                          EVENT
                           PD6     -        -         SAI1_D1                                 I/I2S3_                                                -        SAI4_D1                          DCMI_D10   LCD_B2
                                                                   CKIN4        DATIN1                         A         RX             A                                  CK        NWAIT                          OUT
                                                                                                SDO
                                                                                             SPI1_MOS
                                                                  DFSDM1_                                  DFSDM1_     USART2_                   SPDIFRX1                SDMMC2_                                   EVENT
                           PD7     -        -            -                          -         I/I2S1_                                   -                        -                  FMC_NE1       -         -
                                                                   DATIN4                                   CKIN1        CK                        _IN1                    CMD                                      OUT
                                                                                                SDO
                                                                  DFSDM1_                                  SAI3_SCK    USART3_                   SPDIFRX1                           FMC_D13/                       EVENT
                           PD8     -        -            -                          -             -                                     -                        -          -                     -         -
                                                                   CKIN3                                      _B         TX                        _IN2                             FMC_DA13                        OUT
                                                                  DFSDM1_                                  SAI3_SD_    USART3_                                                      FMC_D14/                       EVENT
                           PD9     -        -            -                          -             -                                     -            -           -          -                     -         -
                                                                   DATIN3                                      B         RX                                                         FMC_DA14                        OUT
                                                                  DFSDM1_                                              USART3_                                                      FMC_D15/                       EVENT
                           PD10    -        -            -                          -             -        SAI3_FS_B                    -            -           -          -                     -       LCD_B3
                                                                   CKOUT                                                 CK                                                         FMC_DA15                        OUT
                 Port D
DS12110 Rev 11




                                                                                                                       USART3_
                                                                   LPTIM2_       I2C4_                                   CTS/                    QUADSPI_    SAI2_SD_                                              EVENT
                           PD11    -        -            -                                        -            -                        -                                   -       FMC_A16       -         -
                                                                     IN2         SMBA                                  USART3_                    BK1_IO0        A                                                  OUT
                                                                                                                         NSS
                                                                                                                       USART3_




                                                                                                                                                                                                                           Pinout, pin description and alternate functions
                                         LPTIM1_                   LPTIM2_                                               RTS/                    QUADSPI_                                                          EVENT
                           PD12    -                 TIM4_CH1                  I2C4_SCL           -            -                        -                    SAI2_FS_A      -       FMC_A17       -         -
                                           IN1                       IN1                                               USART3_                    BK1_IO1                                                           OUT
                                                                                                                          DE
                                         LPTIM1_                                                                                                 QUADSPI_    SAI2_SCK                                              EVENT
                           PD13    -                 TIM4_CH2         -        I2C4_SDA           -            -           -            -                                   -       FMC_A18       -         -
                                           OUT                                                                                                    BK1_IO3       _A                                                  OUT
                                                                                                             SAI3_                   UART8_                                         FMC_D0/                        EVENT
                           PD14    -        -        TIM4_CH3         -             -             -                        -                         -           -          -                     -         -
                                                                                                            MCLK_B                    CTS                                           FMC_DA0                         OUT
                                                                                                                                     UART8_
                                                                                                             SAI3_                                                                  FMC_D1/                        EVENT
                           PD15    -        -        TIM4_CH4         -             -             -                        -          RTS/           -           -          -                     -         -
                                                                                                            MCLK_A                                                                  FMC_DA1                         OUT
                                                                                                                                    UART8_DE
93/357

                                                                                                           Table 10. Alternate functions
94/357




                                                                                                                                                                                                                                 Pinout, pin description and alternate functions
                                    AF0        AF1          AF2          AF3          AF4           AF5          AF6          AF7          AF8         AF9         AF10       AF11        AF12       AF13       AF14     AF15
                                                                                                                                                       SAI4/
                                                                                                                                                                               I2C4/
                                                                                                                                                     FDCAN1/      SAI2/4/
                                                                                   I2C1/2/3/4/                                          SPI6/SAI2/                            UART7/     TIM1/8/
                                                                       LPUART/                                  SPI2/3/     SPI2/3/6/                FDACN2/       TIM8/
                                                                                    USART1/                                                 4/                               SWPMI1/      FMC/      TIM1/
                          Port              TIM1/2/16/      SAI1/        TIM8/                                  SAI1/3/    USART1/2/                 TIM13/14/   QUADSPI/
                                                                                     TIM15/      SPI1/2/3/4/                            UART4/5/8                             TIM1/8/   SDMMC1/     DCMI/      UART5/
                                    SYS     17/LPTIM1/    TIM3/4/5/   LPTIM2/3/4                                 I2C4/        3/6/                   QUADSPI/    SDMMC2/                                                 SYS
                                                                                    LPTIM2/       5/6/CEC                                /LPUART                              DFSDM/     MDIOS/     LCD/        LCD
                                             HRTIM1      12/HRTIM1    /5/HRTIM1/                                UART4/       UART7/                    FMC/      OTG_HS1/
                                                                                    DFSDM/                                              /SDMMC1/                             SDMMC2/    OTG_FS1/    COMP
                                                                        DFSDM                                   DFSDM       SDMMC1                   SDMMC2/     OTG_FS2/
                                                                                      CEC                                               SPDIFRX1                              MDIOS/      LCD
                                                                                                                                                       LCD/      LCD/CRS
                                                                                                                                                                                ETH
                                                                                                                                                     SPDIFRX1
                                             LPTIM1_                   HRTIM_      LPTIM2_                                                                         SAI2_                                                 EVENT
                           PE0       -                   TIM4_ETR                                     -            -           -        UART8_RX         -                      -       FMC_NBL0   DCMI_D2        -
                                               ETR                      SCIN         ETR                                                                          MCLK_A                                                  OUT
                                             LPTIM1_                   HRTIM_                                                                                                                                            EVENT
                           PE1       -                       -                          -             -            -           -        UART8_TX         -           -          -       FMC_NBL1   DCMI_D3        -
                                               IN2                     SCOUT                                                                                                                                              OUT
                                  TRACE                                                                          SAI1_                    SAI4_      QUADSPI_                ETH_MII_                                    EVENT
                           PE2                  -        SAI1_CK1         -             -        SPI4_SCK                      -                                 SAI4_CK1               FMC_A23        -          -
                                   CLK                                                                          MCLK_A                   MCLK_A       BK1_IO2                 TXD3                                        OUT
                                                                                    TIM15_                     SAI1_SD_                 SAI4_SD_                                                                         EVENT
                           PE3    TRACED0       -            -            -                           -                        -                         -           -          -       FMC_A19        -          -
                                                                                     BKIN                          B                        B                                                                             OUT
                                                                      DFSDM1_       TIM15_                                                                                                                               EVENT
                           PE4    TRACED1       -         SAI1_D2                                SPI4_NSS      SAI1_FS_A       -        SAI4_FS_A        -        SAI4_D2       -       FMC_A20    DCMI_D4     LCD_B0
                                                                       DATIN3        CH1N                                                                                                                                 OUT
                                                                      DFSDM1_       TIM15_         SPI4_       SAI1_SCK                 SAI4_SCK                                                                         EVENT
                           PE5    TRACED2       -        SAI1_CK2                                                              -                         -       SAI4_CK2       -       FMC_A21    DCMI_D6     LCD_G0
                                                                       CKIN3         CH1           MISO           _A                       _A                                                                             OUT
                                                                                                                                                                              TIM1_
                                              TIM1_                                 TIM15_         SPI4_       SAI1_SD_                 SAI4_SD_                 SAI2_MCL                                                EVENT
DS12110 Rev 11




                           PE6    TRACED3                 SAI1_D1         -                                                    -                     SAI4_D1                 BKIN2_     FMC_A22    DCMI_D7     LCD_G1
                                              BKIN2                                  CH2           MOSI            A                        A                      K_B                                                    OUT
                                                                                                                                                                             COMP12
                 Port E




                                                                      DFSDM1_                                                                                    QUADSPI_               FMC_D4/                          EVENT
                           PE7       -      TIM1_ETR         -                          -             -            -       UART7_RX         -            -                      -                      -          -
                                                                       DATIN2                                                                                     BK2_IO0               FMC_DA4                           OUT
                                              TIM1_                   DFSDM1_                                                                                    QUADSPI_               FMC_D5/    COMP2_                EVENT
                           PE8       -                       -                          -             -            -       UART7_TX         -            -                      -                                 -
                                              CH1N                     CKIN2                                                                                      BK2_IO1               FMC_DA5     OUT                   OUT
                                                                                                                            UART7_
                                                                      DFSDM1_                                                                                    QUADSPI_               FMC_D6/                          EVENT
                           PE9       -      TIM1_CH1         -                          -             -            -         RTS/           -            -                      -                      -          -
                                                                       CKOUT                                                                                      BK2_IO2               FMC_DA6                           OUT
                                                                                                                           UART7_DE
                                            TIM1_CH2                  DFSDM1_                                               UART7_                               QUADSPI_               FMC_D7/                          EVENT
                           PE10      -                       -                          -             -            -                        -            -                      -                      -          -
                                                N                      DATIN4                                                CTS                                  BK2_IO3               FMC_DA7                           OUT
                                                                      DFSDM1_                                                                                    SAI2_SD_               FMC_D8/                          EVENT
                           PE11      -      TIM1_CH2         -                          -        SPI4_NSS          -           -            -            -                      -                      -       LCD_G3
                                                                       CKIN4                                                                                         B                  FMC_DA8                           OUT
                                              TIM1_                   DFSDM1_                                                                                    SAI2_SCK               FMC_D9/F   COMP1_                EVENT




                                                                                                                                                                                                                                 STM32H742xI/G STM32H743xI/G
                           PE12      -                       -                          -        SPI4_SCK          -           -            -            -                      -                              LCD_B4
                                              CH3N                     DATIN5                                                                                       _B                   MC_DA9     OUT                   OUT
                                                                      DFSDM1_                      SPI4_                                                                                FMC_D10/   COMP2_                EVENT
                           PE13      -      TIM1_CH3         -                          -                          -           -            -            -       SAI2_FS_B      -                              LCD_DE
                                                                       CKIN5                       MISO                                                                                 FMC_DA10    OUT                   OUT
                                                                                                 SPI4_MOS                                                        SAI2_MCL               FMC_D11/                         EVENT
                           PE14      -      TIM1_CH4         -            -             -                          -           -            -            -                      -                      -       LCD_CLK
                                                                                                     I                                                             K_B                  FMC_DA11                          OUT
                                                                                                                                                                                                   TIM1_BKIN
                 Port E




                                                                                                                                                                                        FMC_D12/   _COMP12/              EVENT
                           PE15      -      TIM1_BKIN        -            -             -             -            -           -            -            -           -          -                              LCD_R7
                                                                                                                                                                                        FMC_DA12     COMP_                OUT
                                                                                                                                                                                                   TIM1_BKIN

                                                                                                       Table 10. Alternate functions




                                                                                                                                                                                                                          STM32H742xI/G STM32H743xI/G
                                  AF0      AF1          AF2          AF3          AF4           AF5          AF6          AF7          AF8         AF9         AF10      AF11       AF12       AF13       AF14    AF15
                                                                                                                                                   SAI4/
                                                                                                                                                                          I2C4/
                                                                                                                                                 FDCAN1/      SAI2/4/
                                                                               I2C1/2/3/4/                                          SPI6/SAI2/                           UART7/     TIM1/8/
                                                                   LPUART/                                  SPI2/3/     SPI2/3/6/                FDACN2/       TIM8/
                                                                                USART1/                                                 4/                              SWPMI1/      FMC/      TIM1/
                          Port          TIM1/2/16/      SAI1/        TIM8/                                  SAI1/3/    USART1/2/                 TIM13/14/   QUADSPI/
                                                                                 TIM15/      SPI1/2/3/4/                            UART4/5/8                            TIM1/8/   SDMMC1/     DCMI/     UART5/
                                  SYS   17/LPTIM1/    TIM3/4/5/   LPTIM2/3/4                                 I2C4/        3/6/                   QUADSPI/    SDMMC2/                                              SYS
                                                                                LPTIM2/       5/6/CEC                                /LPUART                             DFSDM/     MDIOS/     LCD/       LCD
                                         HRTIM1      12/HRTIM1    /5/HRTIM1/                                UART4/       UART7/                    FMC/      OTG_HS1/
                                                                                DFSDM/                                              /SDMMC1/                            SDMMC2/    OTG_FS1/    COMP
                                                                    DFSDM                                   DFSDM       SDMMC1                   SDMMC2/     OTG_FS2/
                                                                                  CEC                                               SPDIFRX1                             MDIOS/      LCD
                                                                                                                                                   LCD/      LCD/CRS
                                                                                                                                                                           ETH
                                                                                                                                                 SPDIFRX1
                                                                                                                                                                                                                  EVENT
                           PF0     -        -            -            -        I2C2_SDA           -            -           -            -            -          -          -       FMC_A0        -         -
                                                                                                                                                                                                                   OUT
                                                                                                                                                                                                                  EVENT
                           PF1     -        -            -            -        I2C2_SCL           -            -           -            -            -          -          -       FMC_A1        -         -
                                                                                                                                                                                                                   OUT
                                                                                 I2C2_                                                                                                                            EVENT
                           PF2     -        -            -            -                           -            -           -            -            -          -          -       FMC_A2        -         -
                                                                                 SMBA                                                                                                                              OUT
                                                                                                                                                                                                                  EVENT
                           PF3     -        -            -            -             -             -            -           -            -            -          -          -       FMC_A3        -         -
                                                                                                                                                                                                                   OUT
                                                                                                                                                                                                                  EVENT
                           PF4     -        -            -            -             -             -            -           -            -            -          -          -       FMC_A4        -         -
                                                                                                                                                                                                                   OUT
                                                                                                                                                                                                                  EVENT
                           PF5     -        -            -            -             -             -            -           -            -            -          -          -       FMC_A5        -         -
                                                                                                                                                                                                                   OUT
                                         TIM16_                                                            SAI1_SD_                 SAI4_SD_     QUADSPI_                                                         EVENT
                           PF6     -                     -            -             -        SPI5_NSS                  UART7_RX                                 -          -          -          -         -
DS12110 Rev 11




                                          CH1                                                                  B                        B         BK1_IO3                                                          OUT
                                         TIM17_                                                              SAI1_                    SAI4_      QUADSPI_                                                         EVENT
                 Port F




                           PF7     -                     -            -             -        SPI5_SCK                  UART7_TX                                 -          -          -          -         -
                                          CH1                                                               MCLK_B                   MCLK_B       BK1_IO2                                                          OUT
                                                                                                                        UART7_
                                         TIM16_                                                SPI5_       SAI1_SCK                 SAI4_SCK      TIM13_     QUADSPI_                                             EVENT
                           PF8     -                     -            -             -                                    RTS/                                              -          -          -         -
                                          CH1N                                                 MISO           _B                       _B          CH1        BK1_IO0                                              OUT
                                                                                                                       UART7_DE




                                                                                                                                                                                                                          Pinout, pin description and alternate functions
                                         TIM17_                                                SPI5_                    UART7_                    TIM14_     QUADSPI_                                             EVENT
                           PF9     -                     -            -             -                      SAI1_FS_B                SAI4_FS_B                              -          -          -         -
                                          CH1N                                                 MOSI                      CTS                       CH1        BK1_IO1                                              OUT
                                         TIM16_                                                                                                  QUADSPI_                                                         EVENT
                           PF10    -                  SAI1_D3         -             -             -            -           -            -                    SAI4_D3       -          -       DCMI_D11   LCD_DE
                                          BKIN                                                                                                     CLK                                                             OUT
                                                                                               SPI5_                                                         SAI2_SD_               FMC_                          EVENT
                           PF11    -        -            -            -             -                          -           -            -            -                     -                  DCMI_D12     -
                                                                                               MOSI                                                              B                 SDNRAS                          OUT
                                                                                                                                                                                                                  EVENT
                           PF12    -        -            -            -             -             -            -           -            -            -          -          -       FMC_A6        -         -
                                                                                                                                                                                                                   OUT
                                                                  DFSDM1_        I2C4_                                                                                                                            EVENT
                           PF13    -        -            -                                        -            -           -            -            -          -          -       FMC_A7        -         -
                                                                   DATIN6        SMBA                                                                                                                              OUT
                                                                  DFSDM1_                                                                                                                                         EVENT
                           PF14    -        -            -                     I2C4_SCL           -            -           -            -            -          -          -       FMC_A8        -         -
                                                                   CKIN6                                                                                                                                           OUT
                 Port F




                                                                                                                                                                                                                  EVENT
                           PF15    -        -            -            -        I2C4_SDA           -            -           -            -            -          -          -       FMC_A9        -         -
                                                                                                                                                                                                                   OUT
95/357

                                                                                                      Table 10. Alternate functions
96/357




                                                                                                                                                                                                                           Pinout, pin description and alternate functions
                                  AF0      AF1          AF2          AF3          AF4           AF5         AF6         AF7          AF8         AF9         AF10        AF11        AF12      AF13       AF14     AF15
                                                                                                                                                 SAI4/
                                                                                                                                                                         I2C4/
                                                                                                                                               FDCAN1/      SAI2/4/
                                                                               I2C1/2/3/4/                                        SPI6/SAI2/                            UART7/      TIM1/8/
                                                                   LPUART/                                 SPI2/3/    SPI2/3/6/                FDACN2/       TIM8/
                                                                                USART1/                                               4/                               SWPMI1/       FMC/      TIM1/
                          Port          TIM1/2/16/      SAI1/        TIM8/                                 SAI1/3/   USART1/2/                 TIM13/14/   QUADSPI/
                                                                                 TIM15/      SPI1/2/3/4/                          UART4/5/8                             TIM1/8/    SDMMC1/     DCMI/     UART5/
                                  SYS   17/LPTIM1/    TIM3/4/5/   LPTIM2/3/4                                I2C4/       3/6/                   QUADSPI/    SDMMC2/                                                 SYS
                                                                                LPTIM2/       5/6/CEC                              /LPUART                              DFSDM/      MDIOS/     LCD/       LCD
                                         HRTIM1      12/HRTIM1    /5/HRTIM1/                               UART4/      UART7/                    FMC/      OTG_HS1/
                                                                                DFSDM/                                            /SDMMC1/                             SDMMC2/     OTG_FS1/    COMP
                                                                    DFSDM                                  DFSDM      SDMMC1                   SDMMC2/     OTG_FS2/
                                                                                  CEC                                             SPDIFRX1                              MDIOS/       LCD
                                                                                                                                                 LCD/      LCD/CRS
                                                                                                                                                                          ETH
                                                                                                                                               SPDIFRX1
                                                                                                                                                                                                                   EVENT
                           PG0     -        -            -            -             -             -           -          -            -            -           -           -       FMC_A10       -          -
                                                                                                                                                                                                                    OUT
                                                                                                                                                                                                                   EVENT
                           PG1     -        -            -            -             -             -           -          -            -            -           -           -       FMC_A11       -          -
                                                                                                                                                                                                                    OUT
                                                                                                                                                                       TIM8_BKIN                                   EVENT
                           PG2     -        -            -        TIM8_BKIN         -             -           -          -            -            -           -                   FMC_A12       -          -
                                                                                                                                                                       _COMP12                                      OUT
                                                                                                                                                                        TIM8_
                                                                    TIM8_                                                                                                                                          EVENT
                           PG3     -        -            -                          -             -           -          -            -            -           -       BKIN2_      FMC_A13       -          -
                                                                    BKIN2                                                                                                                                           OUT
                                                                                                                                                                       COMP12
                                                                                                                                                                        TIM1_
                                          TIM1_                                                                                                                                    FMC_A14/                        EVENT
                           PG4     -                     -            -             -             -           -          -            -            -           -       BKIN2_                    -          -
                                          BKIN2                                                                                                                                    FMC_BA0                          OUT
                                                                                                                                                                       COMP12
                                                                                                                                                                                   FMC_A15/                        EVENT
                           PG5     -    TIM1_ETR         -            -             -             -           -          -            -            -           -           -                     -          -
                                                                                                                                                                                   FMC_BA1                          OUT
DS12110 Rev 11




                                         TIM17_       HRTIM_                                                                                               QUADSPI_                                                EVENT
                 Port G




                           PG6     -                                  -             -             -           -          -            -            -                       -       FMC_NE3    DCMI_D12   LCD_R7
                                          BKIN         CHE1                                                                                                BK1_NCS                                                  OUT
                                                      HRTIM_                                                SAI1_    USART6_                                                                                       EVENT
                           PG7     -        -                         -             -             -                                   -            -           -           -       FMC_INT    DCMI_D13   LCD_CLK
                                                       CHE2                                                MCLK_A      CK                                                                                           OUT
                                                                                                                     USART6_
                                                                                                                       RTS/       SPDIFRX1                             ETH_PPS_    FMC_SDC                         EVENT
                           PG8     -        -            -        TIM8_ETR          -        SPI6_NSS         -                                    -           -                                 -       LCD_G7
                                                                                                                     USART6_        _IN3                                 OUT          LK                            OUT
                                                                                                                        DE
                                                                                               SPI1_
                                                                                                                     USART6_      SPDIFRX1     QUADSPI_                            FMC_NE2/    DCMI_               EVENT
                           PG9     -        -            -            -             -          MISO/          -                                            SAI2_FS_B       -                                -
                                                                                                                       RX           _IN4        BK2_IO2                            FMC_NCE     VSYNC                OUT
                                                                                             I2S1_SDI
                                                      HRTIM_                                 SPI1_NSS/                                                     SAI2_SD_                                                EVENT
                           PG10    -        -                         -             -                         -          -            -        LCD_G3                      -       FMC_NE3    DCMI_D2    LCD_B2
                                                       FLT5                                   I2S1_WS                                                          B                                                    OUT




                                                                                                                                                                                                                           STM32H742xI/G STM32H743xI/G
                                                                                                                                                                        ETH_MII_
                                         LPTIM1_      HRTIM_                                 SPI1_SCK/                            SPDIFRX1                 SDMMC2_       TX_EN/                                    EVENT
                           PG11    -                                  -             -                         -          -                         -                                  -       DCMI_D3    LCD_B3
                                           IN2         EEV4                                   I2S1_CK                               _IN1                     D2        ETH_RMII_                                    OUT
                                                                                                                                                                         TX_EN

                                                                                                           Table 10. Alternate functions




                                                                                                                                                                                                                             STM32H742xI/G STM32H743xI/G
                                    AF0        AF1          AF2          AF3          AF4           AF5         AF6         AF7          AF8         AF9         AF10       AF11       AF12       AF13       AF14    AF15
                                                                                                                                                     SAI4/
                                                                                                                                                                            I2C4/
                                                                                                                                                   FDCAN1/      SAI2/4/
                                                                                   I2C1/2/3/4/                                        SPI6/SAI2/                           UART7/      TIM1/8/
                                                                       LPUART/                                 SPI2/3/    SPI2/3/6/                FDACN2/       TIM8/
                                                                                    USART1/                                               4/                              SWPMI1/       FMC/      TIM1/
                          Port              TIM1/2/16/      SAI1/        TIM8/                                 SAI1/3/   USART1/2/                 TIM13/14/   QUADSPI/
                                                                                     TIM15/      SPI1/2/3/4/                          UART4/5/8                            TIM1/8/    SDMMC1/     DCMI/     UART5/
                                    SYS     17/LPTIM1/    TIM3/4/5/   LPTIM2/3/4                                I2C4/       3/6/                   QUADSPI/    SDMMC2/                                               SYS
                                                                                    LPTIM2/       5/6/CEC                              /LPUART                             DFSDM/      MDIOS/     LCD/       LCD
                                             HRTIM1      12/HRTIM1    /5/HRTIM1/                               UART4/      UART7/                    FMC/      OTG_HS1/
                                                                                    DFSDM/                                            /SDMMC1/                            SDMMC2/     OTG_FS1/    COMP
                                                                        DFSDM                                  DFSDM      SDMMC1                   SDMMC2/     OTG_FS2/
                                                                                      CEC                                             SPDIFRX1                             MDIOS/       LCD
                                                                                                                                                     LCD/      LCD/CRS
                                                                                                                                                                             ETH
                                                                                                                                                   SPDIFRX1
                                                                                                                         USART6_                                           ETH_MII_
                                             LPTIM1_      HRTIM_                                   SPI6_                   RTS/       SPDIFRX1                              TXD1/                                    EVENT
                           PG12      -                                    -             -                         -                                 LCD_B4        -                   FMC_NE4       -       LCD_B1
                                               IN1         EEV5                                    MISO                  USART6_        _IN2                              ETH_RMII_                                   OUT
                                                                                                                            DE                                              TXD1
                                                                                                                         USART6_                                           ETH_MII_
                                             LPTIM1_      HRTIM_                                                           CTS/                                             TXD0/                                    EVENT
                           PG13   TRACED0                                 -             -        SPI6_SCK         -                       -            -          -                   FMC_A24       -       LCD_R0
                                               OUT        EEV10                                                          USART6_                                          ETH_RMII_                                   OUT
                 Port G




                                                                                                                           NSS                                              TXD0
                                                                                                                                                                           ETH_MII_
                                             LPTIM1_                                               SPI6_                 USART6_                   QUADSPI_                 TXD1/                                    EVENT
                           PG14   TRACED1                    -            -             -                         -                       -                       -                   FMC_A25       -       LCD_B0
                                               ETR                                                 MOSI                    TX                       BK2_IO3               ETH_RMII_                                   OUT
                                                                                                                                                                            TXD1
                                                                                                                         USART6_
                                                                                                                           CTS/                                                        FMC_                          EVENT
                           PG15      -          -            -            -             -             -           -                       -            -          -           -                  DCMI_D13     -
                                                                                                                         USART6_                                                      SDNCAS                          OUT
DS12110 Rev 11




                                                                                                                           NSS
                                                                                                                                                                                                                     EVENT
                           PH0       -          -            -            -             -             -           -          -            -            -          -           -          -          -         -
                                                                                                                                                                                                                      OUT
                                                                                                                                                                                                                     EVENT
                           PH1       -          -            -            -             -             -           -          -            -            -          -           -          -          -         -
                                                                                                                                                                                                                      OUT




                                                                                                                                                                                                                             Pinout, pin description and alternate functions
                                             LPTIM1_                                                                                               QUADSPI_    SAI2_SCK   ETH_MII_     FMC_                          EVENT
                           PH2       -                       -            -             -             -           -          -            -                                                         -       LCD_R0
                                               IN2                                                                                                  BK2_IO0       _B        CRS       SDCKE0                          OUT
                                                                                                                                                   QUADSPI_     SAI2_     ETH_MII_     FMC_                          EVENT
                           PH3       -          -            -            -             -             -           -          -            -                                                         -       LCD_R1
                                                                                                                                                    BK2_IO1    MCLK_B       COL        SDNE0                          OUT
                                                                                                                                                               OTG_HS_                                               EVENT
                           PH4       -          -            -            -        I2C2_SCL           -           -          -            -        LCD_G5                     -          -          -       LCD_G4
                 Port H




                                                                                                                                                               ULPI_NXT                                               OUT
                                                                                                                                                                                       FMC_                          EVENT
                           PH5       -          -            -            -        I2C2_SDA      SPI5_NSS         -          -            -            -          -           -                     -         -
                                                                                                                                                                                      SDNWE                           OUT
                                                          TIM12_                     I2C2_                                                                                ETH_MII_     FMC_                          EVENT
                           PH6       -          -                         -                      SPI5_SCK         -          -            -            -          -                              DCMI_D8      -
                                                           CH1                       SMBA                                                                                  RXD2        SDNE1                          OUT
                                                                                                   SPI5_                                                                  ETH_MII_    FMC_SDC                        EVENT
                           PH7       -          -            -            -        I2C3_SCL                       -          -            -            -          -                              DCMI_D9      -
                                                                                                   MISO                                                                    RXD3         KE1                           OUT
                                                                                                                                                                                                  DCMI_              EVENT
                           PH8       -          -        TIM5_ETR         -        I2C3_SDA           -           -          -            -            -          -           -       FMC_D16               LCD_R2
                                                                                                                                                                                                  HSYNC               OUT
                                                          TIM12_                     I2C3_                                                                                                                           EVENT
                           PH9       -          -                         -                           -           -          -            -            -          -           -       FMC_D17    DCMI_D0    LCD_R3
                                                           CH2                       SMBA                                                                                                                             OUT
97/357

                                                                                                      Table 10. Alternate functions
98/357




                                                                                                                                                                                                                          Pinout, pin description and alternate functions
                                  AF0      AF1          AF2          AF3          AF4           AF5         AF6         AF7          AF8         AF9         AF10        AF11        AF12      AF13       AF14    AF15
                                                                                                                                                 SAI4/
                                                                                                                                                                         I2C4/
                                                                                                                                               FDCAN1/      SAI2/4/
                                                                               I2C1/2/3/4/                                        SPI6/SAI2/                            UART7/      TIM1/8/
                                                                   LPUART/                                 SPI2/3/    SPI2/3/6/                FDACN2/       TIM8/
                                                                                USART1/                                               4/                               SWPMI1/       FMC/      TIM1/
                          Port          TIM1/2/16/      SAI1/        TIM8/                                 SAI1/3/   USART1/2/                 TIM13/14/   QUADSPI/
                                                                                 TIM15/      SPI1/2/3/4/                          UART4/5/8                             TIM1/8/    SDMMC1/     DCMI/     UART5/
                                  SYS   17/LPTIM1/    TIM3/4/5/   LPTIM2/3/4                                I2C4/       3/6/                   QUADSPI/    SDMMC2/                                                SYS
                                                                                LPTIM2/       5/6/CEC                              /LPUART                              DFSDM/      MDIOS/     LCD/       LCD
                                         HRTIM1      12/HRTIM1    /5/HRTIM1/                               UART4/      UART7/                    FMC/      OTG_HS1/
                                                                                DFSDM/                                            /SDMMC1/                             SDMMC2/     OTG_FS1/    COMP
                                                                    DFSDM                                  DFSDM      SDMMC1                   SDMMC2/     OTG_FS2/
                                                                                  CEC                                             SPDIFRX1                              MDIOS/       LCD
                                                                                                                                                 LCD/      LCD/CRS
                                                                                                                                                                          ETH
                                                                                                                                               SPDIFRX1
                                                                                 I2C4_                                                                                                                            EVENT
                           PH10    -        -        TIM5_CH1         -                           -           -          -            -            -           -           -       FMC_D18    DCMI_D1    LCD_R4
                                                                                 SMBA                                                                                                                              OUT
                                                                                                                                                                                                                  EVENT
                           PH11    -        -        TIM5_CH2         -        I2C4_SCL           -           -          -            -            -           -           -       FMC_D19    DCMI_D2    LCD_R5
                                                                                                                                                                                                                   OUT
                                                                                                                                                                                                                  EVENT
                           PH12    -        -        TIM5_CH3         -        I2C4_SDA           -           -          -            -            -           -           -       FMC_D20    DCMI_D3    LCD_R6
                 Port H




                                                                                                                                                                                                                   OUT
                                                                    TIM8_                                                                      FDCAN1_                                                            EVENT
                           PH13    -        -            -                          -             -           -          -        UART4_TX                     -           -       FMC_D21       -       LCD_G2
                                                                    CH1N                                                                         TX                                                                OUT
                                                                    TIM8_                                                                      FDCAN1_                                                            EVENT
                           PH14    -        -            -                          -             -           -          -        UART4_RX                     -           -       FMC_D22    DCMI_D4    LCD_G3
                                                                    CH2N                                                                         RX                                                                OUT
                                                                    TIM8_                                                                                                                                         EVENT
                           PH15    -        -            -                          -             -           -          -            -            -           -           -       FMC_D23    DCMI_D11   LCD_G4
                                                                    CH3N                                                                                                                                           OUT
                                                                                             SPI2_NSS/                                                                                                            EVENT
                            PI0    -        -        TIM5_CH4         -             -                         -          -            -            -           -           -       FMC_D24    DCMI_D13   LCD_G5
DS12110 Rev 11




                                                                                              I2S2_WS                                                                                                              OUT
                                                                                                                                                                        TIM8_
                                                                    TIM8_                    SPI2_SCK/                                                                                                            EVENT
                            PI1    -        -            -                          -                         -          -            -            -           -       BKIN2_      FMC_D25    DCMI_D8    LCD_G6
                                                                    BKIN2                     I2S2_CK                                                                                                              OUT
                                                                                                                                                                       COMP12
                                                                                               SPI2_
                                                                                                                                                                                                                  EVENT
                            PI2    -        -            -        TIM8_CH4          -          MISO/          -          -            -            -           -           -       FMC_D26    DCMI_D9    LCD_G7
                                                                                                                                                                                                                   OUT
                                                                                             I2S2_SDI
                                                                                               SPI2_
                                                                                                                                                                                                                  EVENT
                            PI3    -        -            -        TIM8_ETR          -          MOSI/          -          -            -            -           -           -       FMC_D27    DCMI_D10     -
                                                                                                                                                                                                                   OUT
                 Port I




                                                                                             I2S2_SDO
                                                                                                                                                             SAI2_     TIM8_BKIN                                  EVENT
                            PI4    -        -            -        TIM8_BKIN         -             -           -          -            -            -                               FMC_NBL2   DCMI_D5    LCD_B4
                                                                                                                                                            MCLK_A     _COMP12                                     OUT
                                                                                                                                                           SAI2_SCK                            DCMI_              EVENT
                            PI5    -        -            -        TIM8_CH1          -             -           -          -            -            -                       -       FMC_NBL3              LCD_B5
                                                                                                                                                              _A                               VSYNC               OUT




                                                                                                                                                                                                                          STM32H742xI/G STM32H743xI/G
                                                                                                                                                           SAI2_SD_                                               EVENT
                            PI6    -        -            -        TIM8_CH2          -             -           -          -            -            -                       -       FMC_D28    DCMI_D6    LCD_B6
                                                                                                                                                               A                                                   OUT
                                                                                                                                                                                                                  EVENT
                            PI7    -        -            -        TIM8_CH3          -             -           -          -            -            -       SAI2_FS_A       -       FMC_D29    DCMI_D7    LCD_B7
                                                                                                                                                                                                                   OUT
                                                                                                                                                                                                                  EVENT
                            PI8    -        -            -            -             -             -           -          -            -            -           -           -          -          -         -
                                                                                                                                                                                                                   OUT

                                                                                                        Table 10. Alternate functions




                                                                                                                                                                                                                        STM32H742xI/G STM32H743xI/G
                                  AF0        AF1          AF2          AF3          AF4           AF5         AF6         AF7          AF8         AF9         AF10      AF11       AF12      AF13     AF14     AF15
                                                                                                                                                   SAI4/
                                                                                                                                                                          I2C4/
                                                                                                                                                 FDCAN1/      SAI2/4/
                                                                                 I2C1/2/3/4/                                        SPI6/SAI2/                           UART7/     TIM1/8/
                                                                     LPUART/                                 SPI2/3/    SPI2/3/6/                FDACN2/       TIM8/
                                                                                  USART1/                                               4/                              SWPMI1/      FMC/     TIM1/
                          Port            TIM1/2/16/      SAI1/        TIM8/                                 SAI1/3/   USART1/2/                 TIM13/14/   QUADSPI/
                                                                                   TIM15/      SPI1/2/3/4/                          UART4/5/8                            TIM1/8/   SDMMC1/    DCMI/   UART5/
                                  SYS     17/LPTIM1/    TIM3/4/5/   LPTIM2/3/4                                I2C4/       3/6/                   QUADSPI/    SDMMC2/                                            SYS
                                                                                  LPTIM2/       5/6/CEC                              /LPUART                             DFSDM/     MDIOS/    LCD/     LCD
                                           HRTIM1      12/HRTIM1    /5/HRTIM1/                               UART4/      UART7/                    FMC/      OTG_HS1/
                                                                                  DFSDM/                                            /SDMMC1/                            SDMMC2/    OTG_FS1/   COMP
                                                                      DFSDM                                  DFSDM      SDMMC1                   SDMMC2/     OTG_FS2/
                                                                                    CEC                                             SPDIFRX1                             MDIOS/      LCD
                                                                                                                                                   LCD/      LCD/CRS
                                                                                                                                                                           ETH
                                                                                                                                                 SPDIFRX1
                                                                                                                                                 FDCAN1_                                               LCD_     EVENT
                            PI9     -         -            -            -             -             -           -          -        UART4_RX                    -          -       FMC_D30      -
                                                                                                                                                   RX                                                 VSYNC      OUT
                                                                                                                                                                        ETH_MII_                       LCD_     EVENT
                           PI10     -         -            -            -             -             -           -          -            -            -          -                  FMC_D31      -
                                                                                                                                                                         RX_ER                        HSYNC      OUT
                                                                                                                                                             OTG_HS_                                            EVENT
                           PI11     -         -            -            -             -             -           -          -            -        LCD_G6                    -          -         -        -
                                                                                                                                                             ULPI_DIR                                            OUT
                 Port I




                                                                                                                                                                        ETH_TX_                        LCD_     EVENT
                           PI12     -         -            -            -             -             -           -          -            -            -          -                     -         -
                                                                                                                                                                          ER                          HSYNC      OUT
                                                                                                                                                                                                       LCD_     EVENT
                           PI13     -         -            -            -             -             -           -          -            -            -          -          -          -         -
                                                                                                                                                                                                      VSYNC      OUT
                                                                                                                                                                                                                EVENT
                           PI14     -         -            -            -             -             -           -          -            -            -          -          -          -         -     LCD_CLK
                                                                                                                                                                                                                 OUT
                                                                                                                                                                                                                EVENT
                           PI15     -         -            -            -             -             -           -          -            -        LCD_G2         -          -          -         -     LCD_R0
DS12110 Rev 11




                                                                                                                                                                                                                 OUT
                                                                                                                                                                                                                EVENT
                           PJ0      -         -            -            -             -             -           -          -            -        LCD_R7         -          -          -         -     LCD_R1
                                                                                                                                                                                                                 OUT
                                                                                                                                                                                                                EVENT
                           PJ1      -         -            -            -             -             -           -          -            -            -          -          -          -         -     LCD_R2
                                                                                                                                                                                                                 OUT




                                                                                                                                                                                                                        Pinout, pin description and alternate functions
                                                                                                                                                                                                                EVENT
                           PJ2      -         -            -            -             -             -           -          -            -            -          -          -          -         -     LCD_R3
                                                                                                                                                                                                                 OUT
                                                                                                                                                                                                                EVENT
                           PJ3      -         -            -            -             -             -           -          -            -            -          -          -          -         -     LCD_R4
                                                                                                                                                                                                                 OUT
                 Port J




                                                                                                                                                                                                                EVENT
                           PJ4      -         -            -            -             -             -           -          -            -            -          -          -          -         -     LCD_R5
                                                                                                                                                                                                                 OUT
                                                                                                                                                                                                                EVENT
                           PJ5      -         -            -            -             -             -           -          -            -            -          -          -          -         -     LCD_R6
                                                                                                                                                                                                                 OUT
                                                                                                                                                                                                                EVENT
                           PJ6      -         -            -        TIM8_CH2          -             -           -          -            -            -          -          -          -         -     LCD_R7
                                                                                                                                                                                                                 OUT
                                                                      TIM8_                                                                                                                                     EVENT
                           PJ7    TRGIN       -            -                          -             -           -          -            -            -          -          -          -         -     LCD_G0
                                                                      CH2N                                                                                                                                       OUT
                                            TIM1_                                                                                                                                                               EVENT
                           PJ8      -                      -        TIM8_CH1          -             -           -          -        UART8_TX         -          -          -          -         -     LCD_G1
                                            CH3N                                                                                                                                                                 OUT
99/357

                                                                                                          Table 10. Alternate functions
100/357




                                                                                                                                                                                                                          Pinout, pin description and alternate functions
                                   AF0        AF1          AF2          AF3          AF4           AF5         AF6         AF7          AF8         AF9         AF10        AF11       AF12      AF13     AF14    AF15
                                                                                                                                                    SAI4/
                                                                                                                                                                            I2C4/
                                                                                                                                                  FDCAN1/      SAI2/4/
                                                                                  I2C1/2/3/4/                                        SPI6/SAI2/                            UART7/      TIM1/8/
                                                                      LPUART/                                 SPI2/3/    SPI2/3/6/                FDACN2/       TIM8/
                                                                                   USART1/                                               4/                               SWPMI1/       FMC/     TIM1/
                          Port             TIM1/2/16/      SAI1/        TIM8/                                 SAI1/3/   USART1/2/                 TIM13/14/   QUADSPI/
                                                                                    TIM15/      SPI1/2/3/4/                          UART4/5/8                             TIM1/8/    SDMMC1/    DCMI/   UART5/
                                   SYS     17/LPTIM1/    TIM3/4/5/   LPTIM2/3/4                                I2C4/       3/6/                   QUADSPI/    SDMMC2/                                             SYS
                                                                                   LPTIM2/       5/6/CEC                              /LPUART                              DFSDM/      MDIOS/    LCD/     LCD
                                            HRTIM1      12/HRTIM1    /5/HRTIM1/                               UART4/      UART7/                    FMC/      OTG_HS1/
                                                                                   DFSDM/                                            /SDMMC1/                             SDMMC2/     OTG_FS1/   COMP
                                                                       DFSDM                                  DFSDM      SDMMC1                   SDMMC2/     OTG_FS2/
                                                                                     CEC                                             SPDIFRX1                              MDIOS/       LCD
                                                                                                                                                    LCD/      LCD/CRS
                                                                                                                                                                             ETH
                                                                                                                                                  SPDIFRX1
                                                                       TIM8_                                                                                                                                      EVENT
                           PJ9      -      TIM1_CH3         -                          -             -           -          -        UART8_RX         -           -           -          -         -     LCD_G2
                                                                       CH1N                                                                                                                                        OUT
                                             TIM1_                                                SPI5_                                                                                                           EVENT
                           PJ10     -                       -        TIM8_CH2          -                         -          -            -            -           -           -          -         -     LCD_G3
                                             CH2N                                                 MOSI                                                                                                             OUT
                                                                     TIM8_CH2                     SPI5_                                                                                                           EVENT
                           PJ11     -      TIM1_CH2         -                          -                         -          -            -            -           -           -          -         -     LCD_G4
                                                                         N                        MISO                                                                                                             OUT
                 Port J




                                                                                                                                                                                                                  EVENT
                           PJ12   TRGOUT       -            -            -             -             -           -          -            -        LCD_G3          -           -          -         -     LCD_B0
                                                                                                                                                                                                                   OUT
                                                                                                                                                                                                                  EVENT
                           PJ13     -          -            -            -             -             -           -          -            -         LCD_B4         -           -          -         -     LCD_B1
                                                                                                                                                                                                                   OUT
                                                                                                                                                                                                                  EVENT
                           PJ14     -          -            -            -             -             -           -          -            -            -           -           -          -         -     LCD_B2
                                                                                                                                                                                                                   OUT
                                                                                                                                                                                                                  EVENT
                           PJ15     -          -            -            -             -             -           -          -            -            -           -           -          -         -     LCD_B3
DS12110 Rev 11




                                                                                                                                                                                                                   OUT
                                             TIM1_                                                                                                                                                                EVENT
                           PK0      -                       -        TIM8_CH3          -        SPI5_SCK         -          -            -            -           -           -          -         -     LCD_G5
                                             CH1N                                                                                                                                                                  OUT
                                                                       TIM8_                                                                                                                                      EVENT
                           PK1      -      TIM1_CH1         -                          -        SPI5_NSS         -          -            -            -           -           -          -         -     LCD_G6
                                                                       CH3N                                                                                                                                        OUT
                                                                                                                                                              TIM8_BKIN   TIM1_BKIN                               EVENT
                           PK2      -      TIM1_BKIN        -        TIM8_BKIN         -             -           -          -            -            -                                  -         -     LCD_G7
                                                                                                                                                              _COMP12     _COMP12                                  OUT
                                                                                                                                                                                                                  EVENT
                           PK3      -          -            -            -             -             -           -          -            -            -           -           -          -         -     LCD_B4
                                                                                                                                                                                                                   OUT
                 Port K




                                                                                                                                                                                                                  EVENT
                           PK4      -          -            -            -             -             -           -          -            -            -           -           -          -         -     LCD_B5
                                                                                                                                                                                                                   OUT
                                                                                                                                                                                                                  EVENT
                           PK5      -          -            -            -             -             -           -          -            -            -           -           -          -         -     LCD_B6
                                                                                                                                                                                                                   OUT




                                                                                                                                                                                                                          STM32H742xI/G STM32H743xI/G
                                                                                                                                                                                                                  EVENT
                           PK6      -          -            -            -             -             -           -          -            -            -           -           -          -         -     LCD_B7
                                                                                                                                                                                                                   OUT
                                                                                                                                                                                                                  EVENT
                           PK7      -          -            -            -             -             -           -          -            -            -           -           -          -         -     LCD_DE
                                                                                                                                                                                                                   OUT

STM32H742xI/G STM32H743xI/G                                                Electrical characteristics (rev Y)


6           Electrical characteristics (rev Y)

6.1           Parameter conditions
              Unless otherwise specified, all voltages are referenced to VSS.

6.1.1         Minimum and maximum values
              Unless otherwise specified the minimum and maximum values are guaranteed in the worst
              conditions of junction temperature, supply voltage and frequencies by tests in production on
              100% of the devices with an junction temperature at TJ = 25 °C and TJ = TJmax (given by the
              selected temperature range).
              Data based on characterization results, design simulation and/or technology characteristics
              are indicated in the table footnotes. Based on characterization, the minimum and maximum
              values refer to sample tests and represent the mean value plus or minus three times the
              standard deviation (mean±3σ).

6.1.2         Typical values
              Unless otherwise specified, typical data are based on TJ = 25 °C, VDD = 3.3 V (for the
              1.7 V ≤ VDD ≤ 3.6 V voltage range). They are given only as design guidelines and are not
              tested.
              Typical ADC accuracy values are determined by characterization of a batch of samples from
              a standard diffusion lot over the full temperature range, where 95% of the devices have an
              error less than or equal to the value indicated (mean±2σ).

6.1.3         Typical curves
              Unless otherwise specified, all typical curves are given only as design guidelines and are
              not tested.

6.1.4         Loading capacitor
              The loading conditions used for pin parameter measurement are shown in Figure 13.

6.1.5         Pin input voltage
              The input voltage measurement on a pin of the device is described in Figure 14.

         Figure 13. Pin loading conditions                       Figure 14. Pin input voltage



                           MCU pin                                               MCU pin

        C = 50 pF                                                    VIN




                                         MS19011V2                                              MS19010V2




                                              DS12110 Rev 11                                          101/357
                                                                                                                316
```
