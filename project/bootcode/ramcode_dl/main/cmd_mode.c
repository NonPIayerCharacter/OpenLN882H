#include "cmd_mode.h"
#include "file_mode.h"
#include "hal/hal_flash.h"
#include "utils/runtime/runtime.h"
#include "utils/crc16.h"
#include "utils/crc32.h"
#include <../../../../../libraries/miniz/miniz.h>
#include "hal/hal_efuse.h"

#define BOOTRAM_CMD_PROMPT   "\r\nbootram> "
#define BOOTRAM_DUMP_COL_NUM (16)

static bootram_cmd_tbl_t bootram_cmd_list[] = {
    {
        "ferase",
        3,
        cmd_flash_erase,
        "Flash erase command.\r\nCommand format: ferase flash_offset erase_size\r\n\tExample: ferase 0xD000 0x4000\r\n"
    },

    {
        "ferase_all",
        1,
        cmd_flash_erase_all,
        "Erase the all flash.\r\nCommand format: ferase_all\r\n\tExample: ferase_all\r\n"
    },

    {
        "fdump",
        4,
        cmd_flash_dump,
        "Flash data dump command.\r\nCommand format: fdump flash_offset dump_size is_rom\r\n\tExample: fdump 0xD000 0x100 0\r\n"
    },

    {
        "fwrite",
        3,
        cmd_flash_write,
        "Flash data write command.\r\nCommand format: fwrite flash_offset write_size\r\n\tExample: fwrite 0xD000 0x100\r\n"
    },

    {
        "fwritez",
        3,
        cmd_flash_write_z,
        "Flash data compressed write command.\r\nCommand format: fwritez flash_offset write_size\r\n\tExample: fwritez 0xD000 0x100\r\n"
    },
		
    {
        "fdumpz",
        3,
        cmd_flash_dump_z,
        "Flash data dump command.\r\nCommand format: fdumpz flash_offset dump_size\r\n\tExample: fdumpz 0xD000 0x100\r\n"
    },

    {
        "upgrade",
        1,
        cmd_flash_upgrade,
        "Download the total flash image by UART0 and burn to flash. After run this command, the PC side can send flash image file through ymode.\r\nCommand formate: upgrade\r\n\tExample: upgrade\r\n"
    },

    {
        "startaddr",
        2,
        cmd_download_startaddr,
        "Download offset.\r\nExample: startaddr 0x1000\r\n"
    },

    {
        "baudrate",
        2,
        cmd_download_baudrate,
        "Set UART baudrate during download.\r\nExample: baudrate 115200"
    },

    {
        "filecount",
        1,
        cmd_download_filecount,
        "Check how many files have been downloaded via ymodem.\r\nExample: filecount \r\n"
    },

    {
        "reboot",
        1,
        cmd_reboot,
        "Chip reboot.\r\n"
    },

    {
        "version",
        1,
        cmd_version,
        "ramcode version.\r\n"
    },
    
    {
        "flash_info",
        1,
        cmd_flash_info,
        "flash_info.\r\n"
    },
    
    //{
    //    "flash_test",
    //    1,
    //    cmd_flash_test,
    //    "flash_test.\r\n"
    //},
    
    {
        "flash_uid",
        1,
        cmd_flash_uid,
        "get flash uid.\r\n"
    },
    
    {
        "flash_id",
        1,
        cmd_flash_id,
        "get flash id.\r\n"
    },

    {
        "flash_crc32",
        3,
        cmd_flash_crc32,
        "Flash CRC32.\r\nCommand format: flash_crc32 flash_offset size\r\n\tExample: flash_crc32 0x0 0x1000\r\n"
    },

    {
        "otp_dump",
        1,
        cmd_otp_dump,
        "Dump OTP\r\n"
    },

    {
        "efuse_dump",
        1,
        cmd_efuse_dump,
        "Dump EFUSE\r\n"
    },
};

bootram_cmd_tbl_t* bootram_find_cmd(const char* cmd)
{
    bootram_cmd_tbl_t* cmdtp      = NULL;
    bootram_cmd_tbl_t* cmdtp_temp = NULL; /*Init value */
    int                i;

    for (i = 0; i < NELEMENTS(bootram_cmd_list); i++) {
        cmdtp = &bootram_cmd_list[i];
        if (strncmp(cmd, cmdtp->cmd, strlen(cmd)) == 0) {
            if (strlen(cmd) == strlen(cmdtp->cmd))
                return cmdtp; /* full match */
            /*
             *record abbreviated command
             */
            cmdtp_temp = cmdtp;
        }
    }

    return cmdtp_temp;
}

static int bootram_parse_line(char* line, char* argv[])
{
    int nargs = 0;

    while (nargs < 4) {
        /* skip any white space */
        while ((*line == ' ') || (*line == '\t')) {
            ++line;
        }

        if (*line == '\0') {
            argv[nargs] = 0;
            return (nargs);
        }

        /* Argument include space should be bracketed by quotation mark */
        if (*line == '\"') {
            /* Skip quotation mark */
            line++;

            /* Begin of argument string */
            argv[nargs++] = line;

            /* Until end of argument */
            while (*line && (*line != '\"')) {
                ++line;
            }
        }
        else {
            argv[nargs++] = line; /* begin of argument string    */

            /* find end of string */
            while (*line && (*line != ' ') && (*line != '\t')) {
                ++line;
            }
        }

        if (*line == '\0') { /* end of line, no more args    */
            argv[nargs] = 0;
            return (nargs);
        }

        *line++ = '\0'; /* terminate current arg     */
    }

    return (nargs);
}

static int bootram_command_execute(bootram_cmd_ctrl_t* cmd_ctrl, char* cmd)
{
    int                ret = -1;
    bootram_cmd_tbl_t* cmdtp;
    char*              argv[CMD_MAXARGS] = {
        NULL,
    };
    int argc;

    /* Extract arguments */
    if ((argc = bootram_parse_line(cmd, argv)) == 0) {
        return ret;
    }

    /* Look up command in command table */
    if ((cmdtp = bootram_find_cmd(argv[0])) == NULL) {
        // bootram_console_printf("\r\nUnknown command '%s' - try 'help'\r\n", argv[0]);
        return ret;
    }

    /* found - check max args */
    if (argc != cmdtp->argc) {
        // bootram_console_printf("\r\n%s\r\n", cmdtp->usage);
        return ret;
    }

    return (cmdtp->cmd_executor)(cmdtp, argc, argv);  // run the cmd
}

int bootram_enter_command_mode(void)
{
    bootram_cmd_ctrl_t* cmd_ctrl = bootram_cmd_get_handle();

    uint8_t ch     = 0;
    int     ret    = -1;
    int     rd_len = 0;

    rd_len = bootram_serial_read(&ch, 1);
    if (rd_len > 0) {
        if (isprint(ch)) {
            //bootram_serial_write(&ch, 1);
            // put this char into console_ctrl.console_buffer
            cmd_ctrl->cmd_line[cmd_ctrl->index++] = ch;
            if (cmd_ctrl->index >= CMD_PBSIZE) {
                memset(cmd_ctrl->cmd_line, 0, CMD_PBSIZE);
                cmd_ctrl->index = 0;
            }
        }
        else if (ch == 0x08) {  // for backspace
            if (cmd_ctrl->index > 0) {
                cmd_ctrl->index--;
                cmd_ctrl->cmd_line[cmd_ctrl->index] = 0;
            }
        }
        else if (ch == '\n' || ch == '\r') {  // for '\r' '\n'
            if (strlen(cmd_ctrl->cmd_line) > 0) {
                ret = bootram_command_execute(cmd_ctrl, cmd_ctrl->cmd_line);
                if (ret == 1) {
                    memset(cmd_ctrl->cmd_line, 0, CMD_PBSIZE);
                    cmd_ctrl->index = 0;
                    return ret;
                }
            }

            memset(cmd_ctrl->cmd_line, 0, CMD_PBSIZE);
            cmd_ctrl->index = 0;
        }else{
            memset(cmd_ctrl->cmd_line, 0, CMD_PBSIZE);
            cmd_ctrl->index = 0;
        }
    }
    return 0;
}

void echo_result(int isPass)
{
    char  result_pass[] = "\r\npppp\r\n";
    char  result_fail[] = "\r\nffff\r\n";
    char* result        = NULL;

    bootram_cmd_ctrl_t* cmd_ctrl = bootram_cmd_get_handle();

    if (isPass) {
        result = result_pass;
    }
    else {
        result = result_fail;
    }

    // for (int k = 0; k < 3; k++)
    for (int i = 0; i < strlen(result); i++) {
        uint8_t ch = result[i];
        bootram_serial_write(&ch, 1);
    }
}

int cmd_flash_info(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
    uint32_t ret        = 0;
    char     buf[50]    = {0};
    uint32_t flash_size = 0;
    
    ret = bootram_flash_info();
    
    if((ret & 0xFF) == 0x15){
        flash_size = 2;
    }else{
        flash_size = 0;
    }
    
    sprintf(buf, "\r\nid:0x%X,flash size:%dM Byte\r\n", ret,flash_size);

    for (int i = 0; i < strlen(buf); i++) {
        uint8_t ch = buf[i];
        bootram_serial_write(&ch, 1);
    }
    
    return 0;
}

int cmd_flash_id(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
    uint32_t ret = bootram_flash_info();
    bootram_serial_write(&ret, 4);
    return 0;
}

int cmd_flash_uid(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
    char     buf[60]    = {0};
    char     uid[16]    = {0};
    char     str_uid[40]= {0};
    
    bootram_flash_uid((uint8_t*)uid);
    
    for(int i = 0; i < 16; i++){
        sprintf(str_uid+i*2, "%02X", uid[i]);
    }
    
    sprintf(buf, "\r\nflash uid:0x%s\r\n", str_uid);
    
    if(strlen(buf) > 60){
        while(1);
    }
    

    for (int i = 0; i < strlen(buf); i++) {
        uint8_t ch = buf[i];
        bootram_serial_write(&ch, 1);
    }
    
    return 0;
}

int cmd_flash_erase(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
    int      ret          = -1;
    uint32_t flash_offset = 0, size = 0;

    if (argv[1]) {
        flash_offset = strtoul(argv[1], NULL, 0);
    }
    if (argv[2]) {
        size = strtoul(argv[2], NULL, 0);
    }
    size = MIN(FALSH_SIZE_MAX - flash_offset, size);
    if (size != 0 && (flash_offset + size) < FALSH_SIZE_MAX) {
        bootram_flash_erase(flash_offset, size);

        echo_result(1);

        return 0;
    }

    // bootram_console_printf("\r\nCommand %s args error!\r\n", argv[0]);
    echo_result(0);

    return ret;
}

int cmd_flash_erase_all(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
    bootram_file_ctrl_t* cmd_ctrl     = bootram_file_get_handle();
    upgrade_ctrl_t*      upgrade_ctrl = &(cmd_ctrl->upgrade_ctrl);
    bootram_flash_chiperase();
    upgrade_ctrl->file_count = 0;

    echo_result(1);

    return 0;
}

int cmd_flash_upgrade(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
    bootram_fsm_t* fsm    = bootram_fsm_get_handle();
    fsm->bootram_cur_mode = BOOTRAM_MODE_CMD_2_DOWNLOAD;

    return 0;
}

void bootram_hexdump(bootram_output_type_enum_t output_type,
                     uint32_t                   offset,
                     uint8_t*                   buff,
                     uint32_t                   size)
{
    volatile uint32_t i = 0, col = 0, row = 0;
    if (BOOTRAM_CONSOLE_OUTPUT == output_type) {
        bootram_console_printf("\r\n");
    }
    else {
        // LOG(LOG_LVL_INFO, "\r\n");
    }
    for (row = 0; row < (size / BOOTRAM_DUMP_COL_NUM); row++) {
        if (BOOTRAM_CONSOLE_OUTPUT == output_type) {
            bootram_console_printf("0x%04X: ", (offset + BOOTRAM_DUMP_COL_NUM * row));
        }
        else {
            // LOG(LOG_LVL_INFO, "0x%04X: ", (offset + BOOTRAM_DUMP_COL_NUM * row));
        }
        for (col = 0; col < BOOTRAM_DUMP_COL_NUM; col++) {
            i = (BOOTRAM_DUMP_COL_NUM * row + col);
            if (BOOTRAM_CONSOLE_OUTPUT == output_type) {
                bootram_console_printf("%02X ", *(buff + i));
            }
            else {
                // LOG(LOG_LVL_INFO, "%02X ", *(buff + i));
            }
        }
        if (BOOTRAM_CONSOLE_OUTPUT == output_type) {
            bootram_console_printf("\r\n");
        }
        else {
            // LOG(LOG_LVL_INFO, "\r\n");
        }
    }
    if (size % BOOTRAM_DUMP_COL_NUM) {
        if (BOOTRAM_CONSOLE_OUTPUT == output_type) {
            bootram_console_printf("0x%04X: ", (offset + BOOTRAM_DUMP_COL_NUM * row));
        }
        else {
            // LOG(LOG_LVL_INFO, "0x%04X: ", (offset + BOOTRAM_DUMP_COL_NUM * row));
        }
        for (col = 0; col < (size % BOOTRAM_DUMP_COL_NUM); col++) {
            i = (BOOTRAM_DUMP_COL_NUM * row + col);
            if (BOOTRAM_CONSOLE_OUTPUT == output_type) {
                bootram_console_printf("%02X ", *(buff + i));
            }
            else {
                // LOG(LOG_LVL_INFO, "%02X ", *(buff + i));
            }
        }

        if (BOOTRAM_CONSOLE_OUTPUT == output_type) {
            bootram_console_printf("\r\n");
        }
        else {
            // LOG(LOG_LVL_INFO, "\r\n");
        }
    }
}
int bootram_console_stdio_write(char* buf, size_t size)
{
    int                 ret      = 0;
    bootram_cmd_ctrl_t* cmd_ctrl = bootram_cmd_get_handle();

    ret = bootram_serial_write((const void*)buf, size);
    return ret;
}

int serial_read_timeout(uint8_t* ch, uint32_t len, int timeout_ms)
{
    uint32_t size = 0;
    uint32_t exp_len = len;
    uint32_t act_len = 0;
    while((timeout_ms--) > 0)
    {
        size = bootram_serial_read(ch, exp_len);
        ch += size;
        act_len += size;
        exp_len -= size;
        if(act_len == len)
        {
            return act_len;
        }
        ln_block_delayms(1);
    }
    return act_len;
}

#define SOH      0x01
#define STX      0x02
#define EOT      0x04
#define ACK      0x06
#define NAK      0x15
#define CAN      0x18
#define PAD_BYTE 0xFF

#define XM_128_SIZE  128
#define XM_1K_SIZE   1024
#define MAX_RETRY    20
#define MODE_TIMEOUT 3000
#define RESP_TIMEOUT 1000

int cmd_flash_dump(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
    uint32_t flash_offset = 0;
    uint32_t size = 0;
    uint8_t  block_num = 1;
    uint8_t  resp;
    int      ret = -1;

    bool     use_1k = true;
    bool     use_crc = true;
    uint32_t rom_read = 0;
    int      retries;

    uint8_t  packet[3 + XM_1K_SIZE + 2];

    if(argv[1]) flash_offset = strtoul(argv[1], NULL, 0);
    if(argv[2]) size         = strtoul(argv[2], NULL, 0);
    if(argv[3]) rom_read     = strtoul(argv[3], NULL, 0);

    if(size == 0) return -1;

    while(bootram_serial_read(&resp, 1));
    resp = 0;

    int mode_delay = MODE_TIMEOUT;
    while(mode_delay > 0)
    {
        mode_delay -= 100;
        if(serial_read_timeout(&resp, 1, 100))
        {
            if(resp == 'C')
            {
                use_crc = true;
                use_1k = true;
                break;
            }
            else if(resp == NAK)
            {
                use_crc = false;
                use_1k = false;
                break;
            }
            else if(resp == CAN)
            {
                return -1;
            }
        }
    }
    if(mode_delay <= 0)
    {
        resp = CAN;
        bootram_serial_write(&resp, 1);
        bootram_serial_write(&resp, 1);
        bootram_serial_flush();
        bootram_serial_setbaudrate(115200);
        return -1;
    }

    while(size > 0)
    {
        uint32_t data_size = use_1k ? XM_1K_SIZE : XM_128_SIZE;
        uint8_t  header = use_1k ? STX : SOH;
        uint32_t chunk = (size >= data_size) ? data_size : size;

        retries = 0;

        while(retries < MAX_RETRY)
        {
            packet[0] = header;
            packet[1] = block_num;
            packet[2] = ~block_num;

						if(rom_read)
						{
							memcpy(&packet[3], (void*)(BOOTROM_BASE + flash_offset), chunk);
						}
						else
						{
							hal_flash_read(flash_offset, chunk, &packet[3]);
						}
            if(chunk < data_size)
            {
                memset(&packet[3 + chunk], PAD_BYTE, data_size - chunk);
            }

            uint32_t pkt_len = 3 + data_size;

            if(use_crc)
            {
                uint16_t crc = crc16_ccitt((const char*)&packet[3], data_size);
                packet[pkt_len++] = (crc >> 8) & 0xFF;
                packet[pkt_len++] = crc & 0xFF;
            }
            else
            {
                uint8_t sum = 0;
                for(uint32_t i = 0; i < data_size; i++)
                    sum += packet[3 + i];
                packet[pkt_len++] = sum;
            }

            bootram_serial_write(packet, pkt_len);

            if(!serial_read_timeout(&resp, 1, RESP_TIMEOUT))
            {
                retries++;
                continue;
            }

            if(resp == ACK)
            {
                break;
            }

            if(resp == NAK)
            {
                retries++;
                continue;
            }

            if(resp == CAN)
            {
                if(serial_read_timeout(&resp, 1, RESP_TIMEOUT) == 1 && resp == CAN)
                {
                    bootram_serial_flush();
                    bootram_serial_setbaudrate(115200);
                    return -1;
                }
                retries++;
            }
        }

        if(use_1k && retries >= MAX_RETRY / 4)
        {
            use_1k = false;
        }

        if(retries >= MAX_RETRY)
        {
            resp = CAN;
            bootram_serial_write(&resp, 1);
            bootram_serial_write(&resp, 1);
            bootram_serial_flush();
            bootram_serial_setbaudrate(115200);
            return -1;
        }

        flash_offset += chunk;
        size -= chunk;
        block_num++;
    }

    retries = 0;
    while(retries < MAX_RETRY)
    {
        resp = EOT;
        bootram_serial_write(&resp, 1);

        if(serial_read_timeout(&resp, 1, RESP_TIMEOUT) && resp == ACK)
        {
            ret = 0;
            break;
        }

        retries++;
    }

    return ret;
}
#define BUF_LEN 0x1000
int cmd_flash_crc32(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
    uint32_t flash_offset = 0;
    uint32_t size = 0;
    crc32_ctx_t crc_ctx = { 0, };
    uint32_t crc32_result = 0;

    uint8_t  buf[BUF_LEN];

    if(argv[1]) flash_offset = strtoul(argv[1], NULL, 0);
    if(argv[2]) size = strtoul(argv[2], NULL, 0);

    if(size == 0) return -1;
    ln_crc32_init(&crc_ctx);

    for(int i = 0; i < size / BUF_LEN; i++)
    {
        hal_flash_read(flash_offset, BUF_LEN, buf);

        ln_crc32_update(&crc_ctx, buf, BUF_LEN);

        flash_offset += BUF_LEN;
    }

    hal_flash_read(flash_offset, size % BUF_LEN, buf);
    ln_crc32_update(&crc_ctx, buf, size % BUF_LEN);
    crc32_result = ln_crc32_final(&crc_ctx);
    bootram_serial_write(&crc32_result, 4);
    return 0;
}

/**
 * @brief
 * @param mblock
 * @param _upgrade_ctrl
 * @return int
 */

#if (defined(DEBUG_FLASH_ROBUST) && (DEBUG_FLASH_ROBUST == 1))
static int flash_robust(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
    const uint32_t FLASH_ERASE_SIZE_MIN = FALSH_SIZE_4K;
    const uint32_t FLASH_START_ADDRESS  = FLASH_BASE_OFFSET;
    const uint32_t FLASH_STOP_ADDRESS = FLASH_START_ADDRESS + FALSH_SIZE_MAX - FLASH_ERASE_SIZE_MIN;

    uint8_t  page_data[FLASH_PAGE_SIZE] = {0};
    uint8_t  read_back[FLASH_PAGE_SIZE] = {0};
    uint16_t total_page_num             = 0;  // pages in a sector.

    uint32_t offset = FLASH_START_ADDRESS;

    uint32_t page_pass_count  = 0;
    uint32_t total_pass_count = 0;
    uint8_t  flag_continue    = 1;

    for (int i = 0; i < FLASH_PAGE_SIZE; i++) {
        page_data[i] = i;
    }

    while (flag_continue) {
        offset += FLASH_ERASE_SIZE_MIN;
        if (offset > FLASH_STOP_ADDRESS) {
            offset = FLASH_START_ADDRESS;
            total_pass_count += 1;
        }

        // erase a sector (4K)
        bootram_flash_erase(offset, FLASH_ERASE_SIZE_MIN);

        // program a sector
        total_page_num = FLASH_ERASE_SIZE_MIN / FLASH_PAGE_SIZE;
        for (int i = 0; i < total_page_num; i++) {
            bootram_flash_write(offset + i * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE, page_data);

            // read back and compare.
            memset(read_back, 0, FLASH_PAGE_SIZE * sizeof(uint8_t));
            bootram_flash_read(offset + i * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE, read_back);

            if (memcmp(read_back, page_data, FLASH_PAGE_SIZE)) {
                // ERROR!!!
                flag_continue = 0;
                break;
            }
        }

        if (flag_continue) {
            page_pass_count += 1;
            bootram_console_printf("pass count, page = %d, total = %d\r\n", page_pass_count,
                                   total_pass_count);
        }
    }

    bootram_console_printf("\r\nflash_robust run over, page = %d, total = %d\r\n", page_pass_count,
                           total_pass_count);

    return 1;
}
#endif  // !DEBUG_FLASH_ROBUST

/**
 * @brief
 * usage:
 * startaddr 0x0000
 * startaddr 0x1000
 * startaddr 0x80000
 *
 * @param cmdtbl
 * @param argc
 * @param argv
 * @return int
 */
int cmd_download_startaddr(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
    bootram_file_ctrl_t* cmd_ctrl     = bootram_file_get_handle();
    upgrade_ctrl_t*      upgrade_ctrl = &(cmd_ctrl->upgrade_ctrl);

    uint32_t sa     = 0;
    char*    endptr = NULL;

    if (argv[1]) {
        sa                               = strtoul(argv[1], &endptr, 16);
        upgrade_ctrl->flash_write_offset = upgrade_ctrl->start_addr = sa;
        echo_result(1);
        return 0;
    }

    upgrade_ctrl->flash_write_offset = upgrade_ctrl->start_addr = sa;
    echo_result(0);
    return -1;
}

/**
 * @brief
 * Usage:
 * baudrate 115200
 *
 * @param cmdtbl
 * @param argc
 * @param argv
 * @return int
 */
int cmd_download_baudrate(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
    bootram_cmd_ctrl_t* cmd_ctrl = bootram_cmd_get_handle();

    uint32_t baudrate = CFG_UART_BAUDRATE_CONSOLE;
    char*    endptr   = NULL;
    if (argv[1]) {
        baudrate = strtoul(argv[1], &endptr, 10);
        bootram_serial_setbaudrate(baudrate);
        bootram_serial_flush();
        return 0;
    }

    return -1;
}

/**
 * @brief Echo to PC how many files have been downloaded via ymodem.
 * fc:3
 * fc:8
 * fc:12
 * @param cmdtbl
 * @param argc
 * @param argv
 * @return int
 */
int cmd_download_filecount(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
    bootram_file_ctrl_t* cmd_ctrl     = bootram_file_get_handle();
    upgrade_ctrl_t*      upgrade_ctrl = &(cmd_ctrl->upgrade_ctrl);
    char                 buf[10]      = {0};

    sprintf(buf, "\r\nfc:%d\r\n", upgrade_ctrl->file_count);

    for (int i = 0; i < strlen(buf); i++) {
        uint8_t ch = buf[i];
        bootram_serial_write(&ch, 1);
    }

    return 0;
}

int cmd_reboot(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
    echo_result(1);

    bootram_user_reboot();
    return 0;
}

/**
 * @brief ram code version reply.
 *
 * @param cmdtbl unused.
 * @param argc unused.
 * @param argv unused.
 * @return int return 0.
 */
int cmd_version(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
#define RAMCODE_VERSION "\r\nRAMCODE\r\n"

    bootram_cmd_ctrl_t* cmd_ctrl = bootram_cmd_get_handle();

    bootram_serial_write(RAMCODE_VERSION, strlen(RAMCODE_VERSION));

    return 0;
}


int cmd_flash_dump_z(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
	uint32_t flash_offset = 0;
	uint32_t size = 0;
	uint8_t block_num = 1;
	uint8_t resp = 0;
	int retry;
	int ret;
	bool use_1k = true;
	bool use_crc = true;

	uint8_t packet[3 + XM_1K_SIZE + 2];

	if(argv[1]) flash_offset = strtoul(argv[1], NULL, 0);
	if(argv[2]) size = strtoul(argv[2], NULL, 0);

	while(bootram_serial_read(&resp, 1));
	resp = 0;

	int mode_delay = MODE_TIMEOUT;
	while(mode_delay > 0)
	{
		mode_delay -= 100;
		if(serial_read_timeout(&resp, 1, 100))
		{
			if(resp == 'C')
			{
				use_crc = true;
				use_1k = true;
				break;
			}
			else if(resp == NAK)
			{
				use_crc = false;
				use_1k = false;
				break;
			}
			else if(resp == CAN)
			{
				return -1;
			}
		}
	}
	if(mode_delay <= 0)
	{
		resp = CAN;
		bootram_serial_write(&resp, 1);
		bootram_serial_write(&resp, 1);
		bootram_serial_flush();
		bootram_serial_setbaudrate(115200);
		return -1;
	}

	z_stream stream;

	memset(&stream, 0, sizeof(stream));

	const uint8_t* src = (const uint8_t*)(CACHE_FLASH_BASE + flash_offset);

	uint32_t remaining = size;

	if(mz_deflateInit2(&stream, 5, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY) != Z_OK)
	{
		resp = CAN;
		bootram_serial_write(&resp, 1);
		bootram_serial_write(&resp, 1);
		bootram_serial_flush();
		bootram_serial_setbaudrate(115200);
		return -1;
	}

	bool finished = false;

	flash_cache_init(0);          //Init Flash cache
	while(!finished)
	{
		uint32_t block_size;
		uint8_t header;

		if(use_1k)
		{
			block_size = XM_1K_SIZE;
			header = STX;
		}
		else
		{
			block_size = 128;
			header = SOH;
		}

		memset(packet, 0xFF, sizeof(packet));

		packet[0] = header;
		packet[1] = block_num;
		packet[2] = ~block_num;

		stream.next_out = &packet[3];
		stream.avail_out = block_size;

		while(stream.avail_out)
		{
			if(stream.avail_in == 0 && remaining)
			{
				uint32_t n = remaining;

				if(n > block_size) n = block_size;

				stream.next_in = (unsigned char*)(src + (size - remaining));

				stream.avail_in = n;

				remaining -= n;
			}

			ret = deflate(&stream, remaining ? Z_NO_FLUSH : Z_FINISH);

			if(ret == Z_STREAM_END)
			{
				finished = true;
				break;
			}

			if(ret != Z_OK)
			{
				deflateEnd(&stream);
				flash_cache_disable();
				return -1;
			}
		}

		uint32_t pkt_len = 3 + block_size;

		if(use_crc)
		{
			uint16_t crc = crc16_ccitt((const char*)&packet[3], block_size);

			packet[pkt_len++] = crc >> 8;
			packet[pkt_len++] = crc & 0xff;
		}
		else
		{
			uint8_t sum = 0;

			for(uint32_t i = 0; i < block_size; i++)
			{
				sum += packet[3 + i];
			}

			packet[pkt_len++] = sum;
		}

		retry = 0;

		while(retry < 10)
		{
			bootram_serial_write(packet, pkt_len);

			if(serial_read_timeout(&resp, 1, 5000) && resp == ACK)
			{
				break;
			}

			retry++;
		}

		//if(use_1k && retry >= 7)
		//{
		//	use_1k = false;
		//}

		if(retry >= 10)
		{
			deflateEnd(&stream);

			resp = CAN;
			bootram_serial_write(&resp, 1);
			bootram_serial_write(&resp, 1);

			flash_cache_disable();
			return -1;
		}

		block_num++;
	}

	deflateEnd(&stream);

	retry = 0;

	while(retry < 10)
	{
		resp = EOT;
		bootram_serial_write(&resp, 1);
		bootram_serial_write(&resp, 1);


		if(serial_read_timeout(&resp, 1, 5000) && resp == ACK)
		{
			flash_cache_disable();
			return -1;
		}

		retry++;
	}

	resp = CAN;
	bootram_serial_write(&resp, 1);
	bootram_serial_write(&resp, 1);
	flash_cache_disable();
	return 0;
}

int cmd_flash_write(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
    uint32_t flash_offset = 0;
    uint32_t max_size     = 0;
    uint8_t  block_num    = 1;
    uint8_t  resp;
    int      ret          = -1;

    bool     use_crc = true;
    //bool     use_1k  = true;
    int      retries;

    uint8_t  packet[3 + XM_1K_SIZE + 2];

    if(argv[1]) flash_offset = strtoul(argv[1], NULL, 0);
    if(argv[2]) max_size     = strtoul(argv[2], NULL, 0);

    if(max_size == 0)
        return -1;

    while(bootram_serial_read(&resp, 1));
    bootram_flash_erase(flash_offset, max_size);
    resp = 'C';
    bootram_serial_write(&resp, 1);

    while(1)
    {
        uint32_t data_size;
        uint32_t pkt_len;

        retries = 0;

        while(retries < MAX_RETRY)
        {
            if(!serial_read_timeout(&resp, 1, MODE_TIMEOUT))
            {
                resp = use_crc ? 'C' : NAK;
                bootram_serial_write(&resp, 1);
                retries++;
                continue;
            }

            if(resp == SOH)
            {
                data_size = XM_128_SIZE;
                //use_1k = false;
                break;
            }
            else if(resp == STX)
            {
                data_size = XM_1K_SIZE;
                //use_1k = true;
                break;
            }
            else if(resp == EOT)
            {
                resp = ACK;
                bootram_serial_write(&resp, 1);
                ret = 0;
                return ret;
            }
            else if(resp == CAN)
            {
                if(serial_read_timeout(&resp, 1, RESP_TIMEOUT) && resp == CAN)
                    return -1;
            }

            retries++;
        }
        uint32_t write_size = (max_size >= data_size) ? data_size : max_size;

        if(retries >= MAX_RETRY)
            goto abort;

        pkt_len = 2 + data_size + (use_crc ? 2 : 1);
        if(!serial_read_timeout(&packet[1], pkt_len, RESP_TIMEOUT))
            goto nak;

        if(packet[1] != block_num || packet[2] != (uint8_t)~block_num)
            goto nak;

        if(use_crc)
        {
            uint16_t crc_rx = ((uint16_t)packet[3 + data_size] << 8) |
                               packet[3 + data_size + 1];
            uint16_t crc = crc16_ccitt((const char*)&packet[3], data_size);
            if(crc != crc_rx)
                goto nak;
        }
        else
        {
            uint8_t sum = 0;
            for(uint32_t i = 0; i < data_size; i++)
                sum += packet[3 + i];
            if(sum != packet[3 + data_size])
                goto nak;
        }

        bootram_flash_write(flash_offset, write_size, &packet[3]);

        flash_offset += write_size;
        max_size     -= write_size;
        block_num++;

        resp = ACK;
        bootram_serial_write(&resp, 1);
        continue;

nak:
        resp = NAK;
        bootram_serial_write(&resp, 1);
        retries++;
        continue;
    }

abort:
    resp = CAN;
    bootram_serial_write(&resp, 1);
    bootram_serial_write(&resp, 1);
    bootram_serial_flush();
    bootram_serial_setbaudrate(115200);
    return -1;
}

int cmd_flash_write_z(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
	uint32_t flash_offset = 0;
	uint32_t max_size = 0;
	uint8_t block_num = 1;
	uint8_t resp;

	bool use_crc = true;
	int retries;

	uint8_t packet[3 + XM_1K_SIZE + 2];

	mz_stream stream;

	if(argv[1]) flash_offset = strtoul(argv[1], NULL, 0);

	if(argv[2]) max_size = strtoul(argv[2], NULL, 0);

	if(max_size == 0) return -1;

	memset(&stream, 0, sizeof(stream));

	if(mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) return -1;

	while(bootram_serial_read(&resp, 1));

	bootram_flash_erase(flash_offset, max_size);

	resp = 'C';
	bootram_serial_write(&resp, 1);

	while(1)
	{
		uint32_t data_size;
		uint32_t pkt_len;

		retries = 0;

		while(retries < MAX_RETRY)
		{
			if(!serial_read_timeout(&resp, 1, MODE_TIMEOUT))
			{
				resp = 'C';
				bootram_serial_write(&resp, 1);
				retries++;
				continue;
			}

			if(resp == SOH)
			{
				data_size = XM_128_SIZE;
				break;
			}
			else if(resp == STX)
			{
				data_size = XM_1K_SIZE;
				break;
			}
			else if(resp == EOT)
			{
				stream.next_in = NULL;
				stream.avail_in = 0;

				for(;;)
				{
					stream.next_out = g_cache_buffer;
					stream.avail_out = sizeof(g_cache_buffer);

					int status = mz_inflate(&stream, MZ_NO_FLUSH);

					uint32_t produced = sizeof(g_cache_buffer) - stream.avail_out;

					if(produced)
					{
						if(produced > max_size) goto abort;

						bootram_flash_write(flash_offset, produced, g_cache_buffer);
						flash_offset += produced;
						max_size -= produced;
					}

					if(status == MZ_STREAM_END) break;

					if(status != MZ_OK && status != MZ_BUF_ERROR) goto abort;

					if(status == MZ_OK && stream.avail_in == 0 && produced == 0) break;
				}

				mz_inflateEnd(&stream);

				resp = ACK;
				bootram_serial_write(&resp, 1);

				return 0;
			}
			else if(resp == CAN)
			{
				if(serial_read_timeout(&resp, 1, RESP_TIMEOUT) && resp == CAN) goto abort;
			}

			retries++;
		}

		if(retries >= MAX_RETRY) goto abort;

		pkt_len = 2 + data_size + (use_crc ? 2 : 1);

		if(!serial_read_timeout(&packet[1], pkt_len, RESP_TIMEOUT)) goto nak;

		if(packet[1] != block_num || packet[2] != (uint8_t)~block_num) goto nak;

		if(use_crc)
		{
			uint16_t crc_rx = ((uint16_t)packet[3 + data_size] << 8) | packet[3 + data_size + 1];

			uint16_t crc = crc16_ccitt((const char*)&packet[3], data_size);

			if(crc != crc_rx) goto nak;
		}
		else
		{
			uint8_t sum = 0;

			for(uint32_t i = 0; i < data_size; i++) sum += packet[3 + i];

			if(sum != packet[3 + data_size]) goto nak;
		}

		stream.next_in = &packet[3];
		stream.avail_in = data_size;

		for(;;)
		{
			stream.next_out = g_cache_buffer;
			stream.avail_out = sizeof(g_cache_buffer);

			int status = mz_inflate(&stream, MZ_NO_FLUSH);

			uint32_t produced = sizeof(g_cache_buffer) - stream.avail_out;

			if(produced)
			{
				if(produced > max_size) goto abort;

				bootram_flash_write(flash_offset, produced, g_cache_buffer);
				flash_offset += produced;
				max_size -= produced;
			}

			if(status == MZ_STREAM_END)
				break;

			if(status != MZ_OK && status != MZ_BUF_ERROR)
				goto abort;

			if(stream.avail_in == 0 && stream.avail_out != 0)
				break;
		}

		block_num++;

		resp = ACK;
		bootram_serial_write(&resp, 1);
		continue;

	nak:
		resp = NAK;
		bootram_serial_write(&resp, 1);
		retries++;
		continue;
	}

abort:

	mz_inflateEnd(&stream);

	resp = CAN;
	bootram_serial_write(&resp, 1);
	bootram_serial_write(&resp, 1);

	bootram_serial_flush();
	bootram_serial_setbaudrate(115200);

	return -1;
}

int cmd_otp_dump(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
		uint8_t fotp_data[0x402] = {0};
		hal_flash_security_area_read(0, 0x400, (uint8_t*)&fotp_data);
		uint16_t crc = crc16_ccitt((const char*)&fotp_data, 0x400);
		fotp_data[0x400] = crc >> 8;
		fotp_data[0x401] = crc & 0xff;
    bootram_serial_write(&fotp_data, sizeof(fotp_data));
    return 0;
}

int cmd_efuse_dump(bootram_cmd_tbl_t* cmdtbl, int argc, char* argv[])
{
		uint32_t efuse_data[16] = {0};
		for (int i = 0; i < 8; ++i) {
				efuse_data[i] = hal_efuse_read_shadow_reg(i);
		}
		for (int i = 8; i < 16; ++i) {
				efuse_data[i] = hal_efuse_read_corrent_reg(i - 8);
		}
		uint16_t crc = crc16_ccitt((const char*)&efuse_data, sizeof(efuse_data));
    bootram_serial_write(&efuse_data, sizeof(efuse_data));
    bootram_serial_write(&crc, sizeof(crc));
    return 0;
}
