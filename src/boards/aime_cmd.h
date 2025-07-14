#include <PN532_HSU.h>
#include <PN532.h>
#include "stdinclude.h"

uint8_t AimeKey[6], BanaKey[6];

PN532_HSU pn532_hsu;
PN532 nfc(pn532_hsu);

auto card_light = PicoLed::addLeds<PicoLed::WS2812B>(pio1, 0,
                                                     CARD_LIGHT_PIN, 16, PicoLed::FORMAT_GRB);

enum {
    // kNFCCMD_NFC_THROUGH subcommands
    FELICA_CMD_POLL = 0x00,
    FELICA_CMD_NDA_06 = 0x06,
    FELICA_CMD_NDA_08 = 0x08,
    FELICA_CMD_GET_SYSTEM_CODE = 0x0C,
    FELICA_CMD_NDA_A4 = 0xA4,
};

typedef union packet_req {
    uint8_t bytes[128];
    struct {
        uint8_t frame_len;
        uint8_t addr;
        uint8_t seq_no;
        uint8_t cmd;
        uint8_t payload_len;
        union {
            uint8_t key[6]; //sg_nfc_req_mifare_set_key(bana or aime)
            uint8_t color_payload[3];//sg_led_req_set_color
            struct { //sg_nfc_cmd_mifare_select_tag,sg_nfc_cmd_mifare_authenticate,sg_nfc_cmd_mifare_read_block
                uint8_t uid[4];
                uint8_t block_no;
            };
            struct { //sg_nfc_req_felica_encap
                uint8_t IDm[8];
                uint8_t encap_len;
                uint8_t code;
                uint8_t felica_payload[113];
            };
        };
    };

} packet_req_t;

typedef union packet_res {
    uint8_t bytes[128];
    struct {
        uint8_t frame_len;
        uint8_t addr;
        uint8_t seq_no;
        uint8_t cmd;
        uint8_t status;
        uint8_t payload_len;
        union {
            char version[23]; //sg_nfc_res_get_fw_version,sg_nfc_res_get_hw_version
            uint8_t reset_payload; //sg_led_res_reset
            uint8_t info_payload[9]; //sg_led_res_get_info
            uint8_t block[16]; //sg_nfc_res_mifare_read_block
            struct { //sg_nfc_res_poll
                uint8_t count;
                uint8_t type;
                uint8_t id_len;
                union {
                    uint8_t mifare_uid[4];
                    struct {
                        uint8_t IDm[8];
                        uint8_t PMm[8];
                    };
                };
            };
            struct { //sg_nfc_res_felica_encap
                uint8_t encap_len;
                uint8_t code;
                uint8_t encap_IDm[8];
                union {
                    struct {//FELICA_CMD_POLL
                        uint8_t encap_PMm[8];
                        uint8_t system_code[2];
                    };
                    struct {//NDA06
                        uint8_t NDA06_code[3];
                        uint8_t NDA06_IDm[8];
                        uint8_t NDA06_Data[8];
                    };
                    uint8_t felica_payload[112];
                };
            };
        };
    };
} packet_res_t;

static packet_req_t req;
static packet_res_t res;

static void sg_res_init(uint8_t payload_len = 0) { //初始化模板
    res.frame_len = 6 + payload_len;
    res.addr = req.addr;
    res.seq_no = req.seq_no;
    res.cmd = req.cmd;
    res.status = 0;
    res.payload_len = payload_len;
}

static void sg_nfc_cmd_reset() { //重置读卡器
    card_light.setBrightness(0x10);
    card_light.clear();
    card_light.show();
    nfc.begin();
    nfc.setPassiveActivationRetries(0x10); //设定等待次数,0xFF永远等待
    nfc.SAMConfig();
    if (nfc.getFirmwareVersion()) {
        nfc.SAMConfig();
        sg_res_init();
        res.status = 3;
        return;
    }
    // FastLED.showColor(0xFF0000);
}

static void sg_nfc_cmd_get_fw_version() {
    sg_res_init(23);
    memcpy(res.version, "TN32MSEC003S F/W Ver1.2", 23);
    //  sg_res_init(1);
    //  memset(res.version, 0x94, 1);
}

static void sg_nfc_cmd_get_hw_version() {
    sg_res_init(23);
    memcpy(res.version, "TN32MSEC003S H/W Ver3.0", 23);
    //  sg_res_init(9);
    //  memcpy(res.version, "837-15396", 9);
}

static void sg_nfc_cmd_mifare_set_key_aime() {
    sg_res_init();
    memcpy(AimeKey, req.key, 6);
}

static void sg_nfc_cmd_mifare_set_key_bana() {
    sg_res_init();
    memcpy(BanaKey, req.key, 6);
}

static void sg_led_cmd_reset() {
    sg_res_init();
    // FastLED.showColor(0);
}

static void sg_led_cmd_get_info() {
    sg_res_init(9);
    static uint8_t info[9] = {'1', '5', '0', '8', '4', 0xFF, 0x10, 0x00, 0x12};
    memcpy(res.info_payload, info, 9);
}

static void sg_led_cmd_set_color() {
    card_light.fill(PicoLed::RGB(req.color_payload[0], req.color_payload[1], req.color_payload[2]));
    card_light.show();
}

static void sg_nfc_cmd_radio_on() {
    sg_res_init();
    nfc.setRFField(0x00, 0x01);
}

static void sg_nfc_cmd_radio_off() {
    sg_res_init();
    nfc.setRFField(0x00, 0x00);
}

static void sg_nfc_cmd_poll() { //卡号发送
    uint16_t SystemCode;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, res.mifare_uid, &res.id_len)) {
        sg_res_init(0x07);
        res.count = 1;
        res.type = 0x10;
        return;
    } else if (nfc.felica_Polling(0xFFFF, 0x00, res.IDm, res.PMm, &SystemCode, 0x0F) == 1) {//< 0: error
        sg_res_init(0x13);
        res.count = 1;
        res.type = 0x20;
        res.id_len = 0x10;
        return;
    } else {
        sg_res_init(1);
        res.count = 0;
        return;
    }
}

static void sg_nfc_cmd_mifare_select_tag() {
    sg_res_init();
}

static void sg_nfc_cmd_aime_authenticate() {
    sg_res_init();
    //AuthenticateBlock(uid,uidLen,block,keyType(A=0,B=1),keyData)
    if (nfc.mifareclassic_AuthenticateBlock(req.uid, 4, req.block_no, 1, AimeKey)) {
        return;
    } else {
        res.status = 1;
    }
}

static void sg_nfc_cmd_bana_authenticate() {
    sg_res_init();
    //AuthenticateBlock(uid,uidLen,block,keyType(A=0,B=1),keyData)
    if (nfc.mifareclassic_AuthenticateBlock(req.uid, 4, req.block_no, 0, BanaKey)) {
        return;
    } else {
        res.status = 1;
    }
}

static void sg_nfc_cmd_mifare_read_block() {//读取卡扇区数据
    if (nfc.mifareclassic_ReadDataBlock(req.block_no, res.block)) {
        sg_res_init(0x10);
        return;
    }
    sg_res_init();
    res.status = 1;
}

static void sg_nfc_cmd_felica_encap() {
    uint16_t SystemCode;
    if (nfc.felica_Polling(0xFFFF, 0x01, res.encap_IDm, res.encap_PMm, &SystemCode, 0x0F) == 1) {
        SystemCode = SystemCode >> 8 | SystemCode << 8;//SystemCode，大小端反转注意
    } else {
        sg_res_init();
        res.status = 1;
        return;
    }
    uint8_t code = req.code;
    res.code = code + 1;
    switch (code) {
        case FELICA_CMD_POLL:
            sg_res_init(0x14);
            memcpy(res.system_code, &SystemCode, 2);
            break;
        case FELICA_CMD_GET_SYSTEM_CODE:
            sg_res_init(0x0D);
            res.felica_payload[0] = 0x01;//未知
            res.felica_payload[1] = SystemCode & 0xFF;//SystemCode
            res.felica_payload[2] = SystemCode >> 8;
            break;
        case FELICA_CMD_NDA_A4:
            sg_res_init(0x0B);
            res.felica_payload[0] = 0x00;
            break;
        case FELICA_CMD_NDA_06:
            sg_res_init(0x1D);
            memcpy(res.NDA06_Data, res.encap_PMm, 8);//未知，填补数据用
            memcpy(res.NDA06_IDm, res.encap_IDm, 8);
            res.NDA06_code[0] = 0x00;
            res.NDA06_code[1] = 0x00;
            res.NDA06_code[2] = 0x01;//未知
            break;
        case FELICA_CMD_NDA_08:
            sg_res_init(0x0C);
            res.felica_payload[0] = 0x00;
            res.felica_payload[1] = 0x00;
            break;
        default:
            sg_res_init();
            res.status = 1;
    }
    res.encap_len = res.payload_len;
}
