/*
 This project is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Multiprotocol is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Multiprotocol.  If not, see <http://www.gnu.org/licenses/>.
 */

#if defined(J6PRO_CYRF6936_INO)

#include "iface_cyrf6936.h"

enum PktState {
    J6PRO_BIND,
    J6PRO_BIND_01,
    J6PRO_BIND_03_START,
    J6PRO_BIND_03_CHECK,
    J6PRO_BIND_05_1,
    J6PRO_BIND_05_2,
    J6PRO_BIND_05_3,
    J6PRO_BIND_05_4,
    J6PRO_BIND_05_5,
    J6PRO_BIND_05_6,
    J6PRO_CHANSEL,
    J6PRO_CHAN_1,
    J6PRO_CHAN_2,
    J6PRO_CHAN_3,
    J6PRO_CHAN_4,
};

const uint8_t j6pro_sopcodes[][8] = {
    /* Note these are in order transmitted (LSB 1st) */
    {0x3C, 0x37, 0xCC, 0x91, 0xE2, 0xF8, 0xCC, 0x91},
    {0x9B, 0xC5, 0xA1, 0x0F, 0xAD, 0x39, 0xA2, 0x0F},
    {0xEF, 0x64, 0xB0, 0x2A, 0xD2, 0x8F, 0xB1, 0x2A},
    {0x66, 0xCD, 0x7C, 0x50, 0xDD, 0x26, 0x7C, 0x50},
    {0x5C, 0xE1, 0xF6, 0x44, 0xAD, 0x16, 0xF6, 0x44},
    {0x5A, 0xCC, 0xAE, 0x46, 0xB6, 0x31, 0xAE, 0x46},
    {0xA1, 0x78, 0xDC, 0x3C, 0x9E, 0x82, 0xDC, 0x3C},
    {0xB9, 0x8E, 0x19, 0x74, 0x6F, 0x65, 0x18, 0x74},
    {0xDF, 0xB1, 0xC0, 0x49, 0x62, 0xDF, 0xC1, 0x49},
    {0x97, 0xE5, 0x14, 0x72, 0x7F, 0x1A, 0x14, 0x72},
    {0x82, 0xC7, 0x90, 0x36, 0x21, 0x03, 0xFF, 0x17},
    {0xE2, 0xF8, 0xCC, 0x91, 0x3C, 0x37, 0xCC, 0x91}, //Note: the '03' was '9E' in the Cypress recommended table
    {0xAD, 0x39, 0xA2, 0x0F, 0x9B, 0xC5, 0xA1, 0x0F}, //The following are the same as the 1st 8 above,
    {0xD2, 0x8F, 0xB1, 0x2A, 0xEF, 0x64, 0xB0, 0x2A}, //but with the upper and lower word swapped
    {0xDD, 0x26, 0x7C, 0x50, 0x66, 0xCD, 0x7C, 0x50},
    {0xAD, 0x16, 0xF6, 0x44, 0x5C, 0xE1, 0xF6, 0x44},
    {0xB6, 0x31, 0xAE, 0x46, 0x5A, 0xCC, 0xAE, 0x46},
    {0x9E, 0x82, 0xDC, 0x3C, 0xA1, 0x78, 0xDC, 0x3C},
    {0x6F, 0x65, 0x18, 0x74, 0xB9, 0x8E, 0x19, 0x74},
};
const uint8_t bind_sop_code[] = {0x62, 0xdf, 0xc1, 0x49, 0xdf, 0xb1, 0xc0, 0x49};
const uint8_t data_code[] = {0x02, 0xf9, 0x93, 0x97, 0x02, 0xfa, 0x5c, 0xe3, 0x01, 0x2b, 0xf1, 0xdb, 0x01, 0x32, 0xbe, 0x6f};

static uint8_t radio_ch[4];

static void __attribute__((unused)) j6pro_build_bind_packet()
{
    packet[0] = 0x01;  //Packet type
    packet[1] = 0x01;  //FIXME: What is this? Model number maybe?
    packet[2] = 0x56;  //FIXME: What is this?
    packet[3] = cyrfmfg_id[0];
    packet[4] = cyrfmfg_id[1];
    packet[5] = cyrfmfg_id[2];
    packet[6] = cyrfmfg_id[3];
    packet[7] = cyrfmfg_id[4];
    packet[8] = cyrfmfg_id[5];
}

static void __attribute__((unused)) j6pro_build_data_packet()
{
    uint8_t num_channels = 8;
    uint8_t i;
    uint32_t upperbits = 0;
    packet[0] = 0xaa; //FIXME what is this?
    for (i = 0; i < 12; i++) {
        if (i >= num_channels) {
            packet[i+1] = 0xff;
            continue;
        }
        int16_t value = map(Servo_data[i],PPM_MIN_100,PPM_MAX_100,0,1024);
        if (value < 0)
            value = 0;
        if (value > 0x3ff)
            value = 0x3ff;
        packet[i+1] = value & 0xff;
        upperbits |= (value >> 8) << (i * 2);
    }
    packet[13] = upperbits & 0xff;
    packet[14] = (upperbits >> 8) & 0xff;
    packet[15] = (upperbits >> 16) & 0xff;
}

static void __attribute__((unused)) j6pro_cyrf_init()
{
    /* Initialise CYRF chip */
    CYRF_WriteRegister(CYRF_28_CLK_EN, 0x02);
    CYRF_WriteRegister(CYRF_32_AUTO_CAL_TIME, 0x3c);
    CYRF_WriteRegister(CYRF_35_AUTOCAL_OFFSET, 0x14);
    CYRF_WriteRegister(CYRF_1C_TX_OFFSET_MSB, 0x05);
    CYRF_WriteRegister(CYRF_1B_TX_OFFSET_LSB, 0x55);
    CYRF_WriteRegister(CYRF_0F_XACT_CFG, 0x25);
    CYRF_SetPower(0x05);
    CYRF_WriteRegister(CYRF_06_RX_CFG, 0x8a);
    CYRF_SetPower(0x28);
    CYRF_WriteRegister(CYRF_12_DATA64_THOLD, 0x0e);
    CYRF_WriteRegister(CYRF_10_FRAMING_CFG, 0xee);
    CYRF_WriteRegister(CYRF_1F_TX_OVERRIDE, 0x00);
    CYRF_WriteRegister(CYRF_1E_RX_OVERRIDE, 0x00);
    CYRF_ConfigDataCode(data_code, 16);
    CYRF_WritePreamble(0x023333);

    CYRF_GetMfgData(cyrfmfg_id);
	//Model match
	cyrfmfg_id[3]+=RX_num;
}

static void __attribute__((unused)) cyrf_bindinit()
{
/* Use when binding */
       //0.060470# 03 2f
       CYRF_WriteRegister(CYRF_03_TX_CFG, 0x28 | 0x07); //Use max power for binding in case there is no telem module

       CYRF_ConfigRFChannel(0x52);
       CYRF_ConfigSOPCode(bind_sop_code);
       CYRF_ConfigCRCSeed(0x0000);
       CYRF_WriteRegister(CYRF_06_RX_CFG, 0x4a);
       CYRF_WriteRegister(CYRF_05_RX_CTRL, 0x83);
       //0.061511# 13 20

       CYRF_ConfigRFChannel(0x52);
       //0.062684# 0f 05
       CYRF_WriteRegister(CYRF_0F_XACT_CFG, 0x25);
       //0.062792# 0f 05
       CYRF_WriteRegister(CYRF_02_TX_CTRL, 0x40);
       j6pro_build_bind_packet(); //01 01 e9 49 ec a9 c4 c1 ff
       //CYRF_WriteDataPacketLen(packet, 0x09);
}

static void __attribute__((unused)) cyrf_datainit()
{
/* Use when already bound */
       //0.094007# 0f 05
       uint8_t sop_idx = (0xff & (cyrfmfg_id[0] + cyrfmfg_id[1] + cyrfmfg_id[2] + cyrfmfg_id[3] - cyrfmfg_id[5])) % 19;
       uint16_t crc =  (0xff & (cyrfmfg_id[1] - cyrfmfg_id[4] + cyrfmfg_id[5])) |
                ((0xff & (cyrfmfg_id[2] + cyrfmfg_id[3] - cyrfmfg_id[4] + cyrfmfg_id[5])) << 8);
       CYRF_WriteRegister(CYRF_0F_XACT_CFG, 0x25);
       CYRF_ConfigSOPCode(j6pro_sopcodes[sop_idx]);
       CYRF_ConfigCRCSeed(crc);
}

static void __attribute__((unused)) j6pro_set_radio_channels()
{
    //FIXME: Query free channels
    //lowest channel is 0x08, upper channel is 0x4d?
    CYRF_FindBestChannels(radio_ch, 3, 5, 8, 77);
    radio_ch[3] = radio_ch[0];
}

uint16_t ReadJ6Pro()
{
	uint32_t start;

    switch(phase) {
        case J6PRO_BIND:
            cyrf_bindinit();
            phase = J6PRO_BIND_01;
            //no break because we want to send the 1st bind packet now
        case J6PRO_BIND_01:
            CYRF_ConfigRFChannel(0x52);
            CYRF_SetTxRxMode(TX_EN);
            //0.062684# 0f 05
            CYRF_WriteRegister(CYRF_0F_XACT_CFG, 0x25);
            //0.062684# 0f 05
            CYRF_WriteDataPacketLen(packet, 0x09);
            phase = J6PRO_BIND_03_START;
            return 3000; //3msec
        case J6PRO_BIND_03_START:
            {
                start=micros();
                while (micros()-start < 500)				// Wait max 500µs
                    if(CYRF_ReadRegister(CYRF_04_TX_IRQ_STATUS) & 0x06)
                        break;
            }
            CYRF_ConfigRFChannel(0x53);
            CYRF_SetTxRxMode(RX_EN);
            CYRF_WriteRegister(CYRF_06_RX_CFG, 0x4a);
            CYRF_WriteRegister(CYRF_05_RX_CTRL, 0x83);
            phase = J6PRO_BIND_03_CHECK;
            return 30000; //30msec
        case J6PRO_BIND_03_CHECK:
            {
            uint8_t rx = CYRF_ReadRegister(CYRF_07_RX_IRQ_STATUS);
            if((rx & 0x1a) == 0x1a) {
                rx = CYRF_ReadRegister(CYRF_0A_RX_LENGTH);
                if(rx == 0x0f) {
                    rx = CYRF_ReadRegister(CYRF_09_RX_COUNT);
                    if(rx == 0x0f) {
                        //Expected and actual length are both 15
                        CYRF_ReadDataPacketLen(packet, rx);
                        if (packet[0] == 0x03 &&
                            packet[3] == cyrfmfg_id[0] &&
                            packet[4] == cyrfmfg_id[1] &&
                            packet[5] == cyrfmfg_id[2] &&
                            packet[6] == cyrfmfg_id[3] &&
                            packet[7] == cyrfmfg_id[4] &&
                            packet[8] == cyrfmfg_id[5])
                        {
                            //Send back Ack
                            packet[0] = 0x05;
                            CYRF_ConfigRFChannel(0x54);
                            CYRF_SetTxRxMode(TX_EN);
                            phase = J6PRO_BIND_05_1;
                            return 2000; //2msec
                         }
                    }
                }
            }
            phase = J6PRO_BIND_01;
            return 500;
            }
        case J6PRO_BIND_05_1:
        case J6PRO_BIND_05_2:
        case J6PRO_BIND_05_3:
        case J6PRO_BIND_05_4:
        case J6PRO_BIND_05_5:
        case J6PRO_BIND_05_6:
            CYRF_WriteRegister(CYRF_0F_XACT_CFG, 0x25);
            CYRF_WriteDataPacketLen(packet, 0x0f);
            phase = phase + 1;
            return 4600; //4.6msec
        case J6PRO_CHANSEL:
            BIND_DONE;
            j6pro_set_radio_channels();
            cyrf_datainit();
            phase = J6PRO_CHAN_1;
        case J6PRO_CHAN_1:
            //Keep transmit power updated
            CYRF_SetPower(0);
            j6pro_build_data_packet();
            //return 3400;
        case J6PRO_CHAN_2:
            //return 3500;
        case J6PRO_CHAN_3:
            //return 3750
        case J6PRO_CHAN_4:
            CYRF_ConfigRFChannel(radio_ch[phase - J6PRO_CHAN_1]);
            CYRF_SetTxRxMode(TX_EN);
            CYRF_WriteDataPacket(packet);
            if (phase == J6PRO_CHAN_4) {
                phase = J6PRO_CHAN_1;
                return 13900;
            }
            phase = phase + 1;
            return 3550;
    }
    return 0;
}

uint16_t initJ6Pro()
{
    CYRF_Reset();
    j6pro_cyrf_init();

	if(IS_AUTOBIND_FLAG_on) {
        phase = J6PRO_BIND;
    }
    else {
        phase = J6PRO_CHANSEL;
    }
    return 2400;
}

#endif