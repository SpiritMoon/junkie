// -*- c-basic-offset: 4; c-backslash-column: 79; indent-tabs-mode: nil -*-
// vim:sw=4 ts=4 sts=4 expandtab
#include <stdlib.h>
#undef NDEBUG
#include <assert.h>
#include <junkie/cpp.h>
#include <junkie/tools/ext.h>
#include <junkie/tools/mutex.h>
#include <junkie/proto/cap.h>
#include <junkie/proto/eth.h>
#include <junkie/proto/ip.h>
#include <junkie/proto/tcp.h>
#include <junkie/proto/pkt_wait_list.h>
#include <junkie/proto/sql.h>
#include "proto/tns.c"
#include "sql_test.h"

static struct parse_test {
    uint8_t const *packet;
    int size;
    enum proto_parse_status ret;         // Expected proto status
    struct sql_proto_info expected;
    enum sql_msg_type msg_type;
} parse_tests[] = {

    {
        // No data response 11.1.0.0.0
        .packet = (uint8_t const []) {
//                                                            TTC- Var0-Sequ Var1 Var2-Erro
            0x00,0x41,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x04,0x01,0x06,0x00,0x02,0x05,
//          Code Var3 Var4 Var------ Var-
            0x7b,0x00,0x00,0x01,0x02,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
//                                             Size ORA-01403 no data found\n--------------
            0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x19,0x4f,0x52,0x41,0x2d,0x30,0x31,0x34,0x30,
//          -------------------------------------------------------------------------------
            0x33,0x3a,0x20,0x6e,0x6f,0x20,0x64,0x61,0x74,0x61,0x20,0x66,0x6f,0x75,0x6e,0x64,
//          ----
            0x0a
        },
        .size = 0x41,
        .ret = PROTO_OK,
        .msg_type = SQL_QUERY,
        .expected = {
            .info = { .head_len = 0x41, .payload = 0},
            .request_status = SQL_REQUEST_COMPLETE,
            .set_values = SQL_ERROR_CODE | SQL_ERROR_MESSAGE | SQL_REQUEST_STATUS | SQL_NB_ROWS,
            .u = { .query = { .nb_rows = 0 } },
            .error_code = "ORA-01403",
            .error_message = "no data found\n",
            .msg_type = SQL_QUERY,
        },
    },

    {
        // One row, one field response 11.1.0.0.0
        .packet = (uint8_t const []) {
            0x00,0x50,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x06,0x22,0x01,0x01,0x00,0x01,
//                                                  END- Sequence- Rows----- ErrorCode-----
            0x0a,0x00,0x00,0x00,0x07,0x02,0xc1,0x02,0x04,0x01,0x06,0x01,0x01,0x02,0x05,0x7b,
//
            0x00,0x00,0x01,0x02,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x01,0x01,0x19,0x4f,0x52,0x41,0x2d,0x30,0x31,0x34,0x30,0x33,
            0x3a,0x20,0x6e,0x6f,0x20,0x64,0x61,0x74,0x61,0x20,0x66,0x6f,0x75,0x6e,0x64,0x0a,
        },
        .size = 0x50,
        .ret = PROTO_OK,
        .msg_type = SQL_QUERY,
        .expected = {
            .info = { .head_len = 0x50, .payload = 0},
            .request_status = SQL_REQUEST_COMPLETE,
            .set_values = SQL_REQUEST_STATUS | SQL_NB_FIELDS | SQL_NB_ROWS | SQL_ERROR_CODE | SQL_ERROR_MESSAGE,
            .u = { .query = { .nb_rows = 1, .nb_fields = 1 } },
            .error_code = "ORA-01403",
            .error_message = "no data found\n",
            .msg_type = SQL_QUERY,
        },
    },

    {
        // Misc error 11.1.0.0.0
        .packet = (uint8_t const []) {
//                                                            TTC- Sequence- Rows ErrorCode
            0x00,0x65,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x04,0x01,0x06,0x00,0x01,0x01,
//          Var3 Var4 Var5----- Var6 Fix1----- Var- Var- Fix------ Var---------------------
            0x00,0x00,0x01,0x02,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x04,0xff,0xff,0xff,0xff,
//          Var- Fix- Var- Var- Var-
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3b,0x4f,0x52,0x41,0x2d,0x30,0x30,
            0x30,0x30,0x31,0x3a,0x20,0x75,0x6e,0x69,0x71,0x75,0x65,0x20,0x63,0x6f,0x6e,0x73,
            0x74,0x72,0x61,0x69,0x6e,0x74,0x20,0x28,0x53,0x59,0x53,0x54,0x45,0x4d,0x2e,0x53,
            0x59,0x53,0x5f,0x43,0x30,0x30,0x37,0x31,0x36,0x31,0x29,0x20,0x76,0x69,0x6f,0x6c,
            0x61,0x74,0x65,0x64,0x0a
        },
        .size = 0x65,
        .ret = PROTO_OK,
        .msg_type = SQL_QUERY,
        .expected = {
            .info = { .head_len = 0x65, .payload = 0},
            .request_status = SQL_REQUEST_ERROR,
            .set_values = SQL_ERROR_CODE | SQL_ERROR_MESSAGE | SQL_REQUEST_STATUS | SQL_NB_ROWS,
            .u = { .query = { .nb_rows = 0 } },
            .error_code = "ORA-00001",
            .error_message = "unique constraint (SYSTEM.SYS_C007161) violated\n",
            .msg_type = SQL_QUERY,
        },
    },

    {
        // Select query 11.1.0.0.0
        .packet = (uint8_t const []) {
//                                                            Quer SubC Fix- OpSi----------
            0x00,0x4c,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x03,0x5e,0x00,0x02,0x80,0x21,
//                    Size-----
            0x00,0x01,0x01,0x14,0x01,0x01,0x0d,0x00,0x00,0x00,0x00,0x04,0x7f,0xff,0xff,0xff,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,'S' ,'E' ,'L' ,'E' ,'C' ,0x54,0x20,
            0x2a,0x20,0x46,0x52,0x4f,0x4d,0x20,0x22,0x54,0x6f,0x74,0x6f,0x22,0x01,0x01,0x00,
            0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00
        },
        .size = 0x4c,
        .ret = PROTO_OK,
        .msg_type = SQL_UNKNOWN,
        .expected = {
            .info = { .head_len = 0x4c, .payload = 0},
            .set_values = SQL_SQL,
            .u = { .query = { .sql = "SELECT * FROM \"Toto\"" } },
            .msg_type = SQL_QUERY,
        },
    },

    {
        // Field description two field 11.1.0.0
        .packet = (uint8_t const []) {
//                                                            TTC  Len  Skip---------------
            0x01,0xbb,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x10,0x17,0x32,0x02,0x80,0x5a,
//          -------------------------------------------------------------------------------
            0xc0,0xd6,0x19,0xa8,0xd3,0x15,0x33,0xc4,0x03,0x8d,0x85,0x43,0x78,0x71,0x0c,0x0a,
//          -------------- Var drop- NumFields Fix- 1st fix------- var1 var2----- var3 var4
            0x06,0x03,0x11,0x01,0x24,0x01,0x02,0x3d,0x02,0x00,0x02,0x00,0x01,0x16,0x00,0x00,
//          dalc var1 var2 fix1 var  fix------ dalc1------------------- dalc dalc var- 2nd
            0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x01,0x02,0x02,0x4e,0x4f,0x00,0x00,0x00,0x01,
//          fix------ var1 var2----- var3 var4 dalc var1 var2---------- fix- var------ fix-
            0x80,0x00,0x00,0x01,0x0e,0x00,0x00,0x00,0x00,0x02,0x03,0x69,0x01,0x01,0x0e,0x01,
//          ---- dalc1------------------------------------------------- dalc dalc var------
            0x08,0x01,0x08,0x08,0x54,0x6f,0x74,0x6f,0x4e,0x61,0x6d,0x65,0x00,0x00,0x01,0x01,
//          dalc--------------------------------------------- var1----- var2---------- TTC
            0x01,0x07,0x07,0x78,0x71,0x0c,0x0a,0x08,0x0e,0x29,0x01,0x01,0x02,0x1f,0xe8,0x08,
//          length--- 1st---------------- 2nd- 3rd------ 4th- 5th- 6th- var- numIgnore 1st-
            0x01,0x06,0x03,0x08,0x0f,0x48,0x00,0x01,0x02,0x00,0x00,0x00,0x00,0x01,0x13,0x00,
//          dalc-------------------------------------------------- var------ 2nd- dalc-----
            0x01,0x08,0x08,0x41,0x4d,0x45,0x52,0x49,0x43,0x41,0x4e,0x01,0x10,0x00,0x01,0x07,
//          --------------------------------------- var------ 3rd- dalc--------------- var-
            0x07,0x41,0x4d,0x45,0x52,0x49,0x43,0x41,0x01,0x09,0x00,0x01,0x01,0x01,0x24,0x00,
//          4th- dalc--------------------------------------------- var------ 5th- dalc-----
            0x00,0x01,0x07,0x07,0x41,0x4d,0x45,0x52,0x49,0x43,0x41,0x01,0x01,0x00,0x01,0x02,
//          -------------- var------ 6th- dalc---------------------------------------------
            0x02,0x2e,0x2c,0x01,0x02,0x00,0x01,0x08,0x08,0x41,0x4c,0x33,0x32,0x55,0x54,0x46,
//          ---- var------ 7th- dalc-------------------------------------------------------
            0x38,0x01,0x0a,0x00,0x01,0x09,0x09,0x47,0x52,0x45,0x47,0x4f,0x52,0x49,0x41,0x4e,
//          var------ 8th- dalc------------------------------------------------------- var-
            0x01,0x0c,0x00,0x01,0x09,0x09,0x44,0x44,0x2d,0x4d,0x4f,0x4e,0x2d,0x52,0x52,0x01,
//          ---- 9th- dalc-------------------------------------------------- var------ 9th-
            0x07,0x00,0x01,0x08,0x08,0x41,0x4d,0x45,0x52,0x49,0x43,0x41,0x4e,0x01,0x08,0x00,
//          dalc---------------------------------------- var------ 10th dalc---------------
            0x01,0x06,0x06,0x42,0x49,0x4e,0x41,0x52,0x59,0x01,0x0b,0x00,0x01,0x0e,0x0e,0x48,
//          ---------------------------------------------------------------- var------ 11th
            0x48,0x2e,0x4d,0x49,0x2e,0x53,0x53,0x58,0x46,0x46,0x20,0x41,0x4d,0x01,0x39,0x00,
//          dalc---------------------------------------------------------------------------
            0x01,0x18,0x18,0x44,0x44,0x2d,0x4d,0x4f,0x4e,0x2d,0x52,0x52,0x20,0x48,0x48,0x2e,
//          ------------------------------------------------------ var------ 12th dalc-----
            0x4d,0x49,0x2e,0x53,0x53,0x58,0x46,0x46,0x20,0x41,0x4d,0x01,0x3a,0x00,0x01,0x12,
//          -------------------------------------------------------------------------------
            0x12,0x48,0x48,0x2e,0x4d,0x49,0x2e,0x53,0x53,0x58,0x46,0x46,0x20,0x41,0x4d,0x20,
//          -------------- var------ 13th dalc---------------------------------------------
            0x54,0x5a,0x52,0x01,0x3b,0x00,0x01,0x1c,0x1c,0x44,0x44,0x2d,0x4d,0x4f,0x4e,0x2d,
//          -------------------------------------------------------------------------------
            0x52,0x52,0x20,0x48,0x48,0x2e,0x4d,0x49,0x2e,0x53,0x53,0x58,0x46,0x46,0x20,0x41,
//          ------------------------ var------ 14th dalc--------------- var------ 15th dalc
            0x4d,0x20,0x54,0x5a,0x52,0x01,0x3c,0x00,0x01,0x01,0x01,0x24,0x01,0x34,0x00,0x01,
//          --------------------------------------- var------ 16th dalc--------------------
            0x06,0x06,0x42,0x49,0x4e,0x41,0x52,0x59,0x01,0x32,0x00,0x01,0x04,0x04,0x42,0x59,
//          --------- var------ 17th dalc----------------------------------- var------ 18th
            0x54,0x45,0x01,0x3d,0x00,0x01,0x05,0x05,0x46,0x41,0x4c,0x53,0x45,0x01,0x3e,0x00,
//          dalc----------------------------------------------------------------- var------
            0x01,0x0b,0x0b,0x80,0x00,0x00,0x00,0x3d,0x3c,0x3c,0x80,0x00,0x00,0x00,0x01,0xa3,
//          End- Sequence- Row- Erro Var3 Var4 Var5-----
            0x04,0x01,0x06,0x00,0x00,0x00,0x00,0x01,0x02,0x00,0x03,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01
        },
        .size = 0x1bb,
        .ret = PROTO_OK,
        .msg_type = SQL_UNKNOWN,
        .expected = {
            .info = { .head_len = 0x1bb, .payload = 0},
            .set_values = SQL_NB_FIELDS | SQL_NB_ROWS,
            .u = { .query = { .nb_fields = 2, .nb_rows = 0 } },
            .msg_type = SQL_QUERY,
        },
    },

    {
        // One row, two fields response 11.1.0.0
        .packet = (uint8_t const []) {
            0x00,0x5a,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x06,0x22,0x01,0x02,0x00,0x01,
            0x0a,0x00,0x00,0x00,0x07,0x02,0xc1,0x02,0x09,0x61,0x4e,0x61,0x6d,0x65,0x44,0x75,
//                    End  Sequence- Row------ Error code----
            0x64,0x65,0x04,0x01,0x05,0x01,0x01,0x02,0x05,0x7b,0x00,0x00,0x01,0x02,0x00,0x03,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,
            0x19,0x4f,0x52,0x41,0x2d,0x30,0x31,0x34,0x30,0x33,0x3a,0x20,0x6e,0x6f,0x20,0x64,
            0x61,0x74,0x61,0x20,0x66,0x6f,0x75,0x6e,0x64,0x0a
        },
        .size = 0x5a,
        .ret = PROTO_OK,
        .msg_type = SQL_UNKNOWN,
        .expected = {
            .info = { .head_len = 0x5a, .payload = 0},
            .request_status = SQL_REQUEST_COMPLETE,
            .set_values = SQL_REQUEST_STATUS | SQL_NB_FIELDS | SQL_NB_ROWS | SQL_ERROR_CODE | SQL_ERROR_MESSAGE,
            .u = { .query = { .nb_rows = 1, .nb_fields = 2 } },
            .error_code = "ORA-01403",
            .error_message = "no data found\n",
            .msg_type = SQL_QUERY,
        },
    },

    {
        // Login query
        .packet = (uint8_t const []) {
//          Length--- CheckSum- Data Rese Header---  DataFlags TTC- Fix1 Fix2 Fix3 Drop-----
            0x00,0x21,0x00,0x00,0x06,0x00,0x00,0x00, 0x00,0x00,0x01,0x06,0x05,0x04,0x03,0x02,
//          --------- Charset?-
            0x01,0x00,0x4a,0x61,0x76,0x61,0x5f,0x54, 0x54,0x43,0x2d,0x38,0x2e,0x32,0x2e,0x30,
            0x00
        },
        .size = 0x21,
        .ret = PROTO_OK,
        .msg_type = SQL_UNKNOWN,
        .expected = {
            .info = { .head_len = 0x21, .payload = 0},
            .msg_type = SQL_STARTUP,
        },
    },

    {
        // Login properties response
        .packet = (uint8_t const []) {
//          Length--- CheckSum- Data Rese Header---  DataFlags TTC- SerV ???? Text-server-Ve
            0x00,0xed,0x00,0x00,0x06,0x00,0x00,0x00, 0x00,0x00,0x01,0x06,0x00,0x49,0x42,0x4d,
//          rsion-(IBMPC/WIN_NT-8.1)--------------------------------------------------- SrvC
            0x50,0x43,0x2f,0x57,0x49,0x4e,0x5f,0x4e, 0x54,0x2d,0x38,0x2e,0x31,0x2e,0x30,0x00,
//          hars Flag CharsetEl
            0x69,0x03,0x01,0x0a,0x00,0x66,0x03,0x40, 0x03,0x01,0x40,0x03,0x66,0x03,0x01,0x66,
            0x03,0x48,0x03,0x01,0x48,0x03,0x66,0x03, 0x01,0x66,0x03,0x52,0x03,0x01,0x52,0x03,
            0x66,0x03,0x01,0x66,0x03,0x61,0x03,0x01, 0x61,0x03,0x66,0x03,0x01,0x66,0x03,0x1f,
            0x03,0x08,0x1f,0x03,0x66,0x03,0x01,0x00, 0x64,0x00,0x00,0x00,0x60,0x01,0x24,0x0f,
            0x05,0x0b,0x0c,0x03,0x0c,0x0c,0x05,0x04, 0x05,0x0d,0x06,0x09,0x07,0x08,0x05,0x05,
            0x05,0x05,0x05,0x0f,0x05,0x05,0x05,0x05, 0x05,0x0a,0x05,0x05,0x05,0x05,0x05,0x04,
            0x05,0x06,0x07,0x08,0x08,0x23,0x47,0x23, 0x23,0x08,0x11,0x23,0x08,0x11,0x41,0xb0,
            0x23,0x00,0x83,0x03,0x69,0x07,0xd0,0x03, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x27,0x06,0x01,
            0x01,0x01,0x0f,0x01,0x01,0x06,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0x7f,0xff,0x03,
            0x0a,0x03,0x03,0x01,0x00,0x7f,0x01,0x7f, 0xff,0x01,0x06,0x01,0x01,0x3f,0x01,0x03,
            0x06,0x00,0x01,0x03,0x02,0x07,0x02,0x01, 0x00,0x01,0x18,0x00,0x03
        },
        .size = 0xed,
        .ret = PROTO_OK,
        .msg_type = SQL_UNKNOWN,
        .expected = {
            .info = { .head_len = 0xed, .payload = 0},
            .set_values = SQL_ENCODING,
            .u = { .startup = { .encoding = SQL_ENCODING_UTF8 } },
            .msg_type = SQL_STARTUP,
        },
    },

    {
        // Another Login properties response
        .packet = (uint8_t const []) {
//          Length--- CheckSum- Data Rese Header---  DataFlags TTC- SerV ???? Text-server-Ve
            0x00,0xe5,0x00,0x00,0x06,0x00,0x00,0x00, 0x00,0x00,0x01,0x06,0x00,0x49,0x42,0x4d,
//          rsion-(IBMPC/WIN_NT-8.1)--------------------------------------------------------
            0x50,0x43,0x2f,0x57,0x49,0x4e,0x5f,0x4e, 0x54,0x2d,0x38,0x2e,0x31,0x2e,0x30,0x00,
//          SrvChars- Flag CharsetEl -------------------------------------------------------
            0x69,0x03,0x01,0x0a,0x00,0x66,0x03,0x40, 0x03,0x01,0x40,0x03,0x66,0x03,0x01,0x66,
//          --------------------------------------------------------------------------------
            0x03,0x48,0x03,0x01,0x48,0x03,0x66,0x03, 0x01,0x66,0x03,0x52,0x03,0x01,0x52,0x03,
//          --------------------------------------------------------------------------------
            0x66,0x03,0x01,0x66,0x03,0x61,0x03,0x01, 0x61,0x03,0x66,0x03,0x01,0x66,0x03,0x1f,
//          ----------------------------------------
            0x03,0x08,0x1f,0x03,0x66,0x03,0x01,0x00, 0x64,0x00,0x00,0x00,0x60,0x01,0x24,0x0f,
            0x05,0x0b,0x0c,0x03,0x0c,0x0c,0x05,0x04, 0x05,0x0d,0x06,0x09,0x07,0x08,0x05,0x05,
            0x05,0x05,0x05,0x0f,0x05,0x05,0x05,0x05, 0x05,0x0a,0x05,0x05,0x05,0x05,0x05,0x04,
            0x05,0x06,0x07,0x08,0x08,0x23,0x47,0x23, 0x23,0x08,0x11,0x23,0x08,0x11,0x41,0xb0,
            0x23,0x00,0x83,0x03,0x69,0x07,0xd0,0x03, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x21,0x06,0x01,
            0x01,0x01,0x0d,0x01,0x01,0x04,0x01,0x01, 0x01,0x01,0x01,0x01,0x01,0xff,0xff,0x03,
            0x08,0x03,0x00,0x01,0x00,0x3f,0x01,0x07, 0x3f,0x01,0x01,0x01,0x01,0x03,0x01,0x05,
            0x02,0x01,0x00,0x01,0x18
        },
        .size = 0xe5,
        .ret = PROTO_OK,
        .msg_type = SQL_UNKNOWN,
        .expected = {
            .info = { .head_len = 0xe5, .payload = 0},
            .set_values = SQL_ENCODING,
            .u = { .startup = { .encoding = SQL_ENCODING_UTF8 } },
            .msg_type = SQL_STARTUP,
        },
    },

    {
        .packet = (uint8_t const []) {
//                                                                      EndM  V0--  V1--  V2--------------
            0x00, 0x58, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x02, 0x03, 0xf9,
//          V3--  V4--  V5--
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x33, 0x4f, 0x52, 0x41, 0x2d, 0x30, 0x31, 0x30, 0x31, 0x37, 0x3a, 0x20,
            0x69, 0x6e, 0x76, 0x61, 0x6c, 0x69, 0x64, 0x20, 0x75, 0x73, 0x65, 0x72, 0x6e, 0x61, 0x6d, 0x65,
            0x2f, 0x70, 0x61, 0x73, 0x73, 0x77, 0x6f, 0x72, 0x64, 0x3b, 0x20, 0x6c, 0x6f, 0x67, 0x6f, 0x6e,
            0x20, 0x64, 0x65, 0x6e, 0x69, 0x65, 0x64, 0x0a
        },
        .size = 0x58,
        .ret = PROTO_OK,
        .msg_type = SQL_STARTUP,
        .expected = {
            .info = { .head_len = 0x58, .payload = 0},
            .set_values = SQL_REQUEST_STATUS | SQL_ERROR_CODE | SQL_ERROR_MESSAGE,
            .request_status = SQL_REQUEST_ERROR,
            .error_code = "ORA-01017",
            .error_message = "invalid username/password; logon denied\n",
            .u = { .startup = { .encoding = SQL_ENCODING_UTF8 } },
            .msg_type = SQL_STARTUP,
        },
    },

    {
        .packet = (uint8_t const []) {
//                                                                      RowP  F0--  V0--------  V1--  V2--
            0x00, 0x6b, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x02, 0x01, 0x02, 0x00, 0x01,
//          ----  V3--  V4--  V5--  Data  Var1------------  Var2------------------------------------------
            0x0a, 0x00, 0x00, 0x00, 0x07, 0x02, 0xc1, 0x02, 0x09, 0x61, 0x4e, 0x61, 0x6d, 0x65, 0x44, 0x75,
//          ----------
            0x64, 0x65, 0x15, 0x01, 0x02, 0x03, 0x07, 0x02, 0xc1, 0x03, 0x07, 0x61, 0x6e, 0x6f, 0x74, 0x68,
//                      End
            0x65, 0x72, 0x04, 0x01, 0x05, 0x01, 0x02, 0x02, 0x05, 0x7b, 0x00, 0x00, 0x01, 0x02, 0x00, 0x03,
            0x00, 0x01, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
            0x01, 0x19, 0x4f, 0x52, 0x41, 0x2d, 0x30, 0x31, 0x34, 0x30, 0x33, 0x3a, 0x20, 0x6e, 0x6f, 0x20,
            0x64, 0x61, 0x74, 0x61, 0x20, 0x66, 0x6f, 0x75, 0x6e, 0x64, 0x0a
        },
        .size = 0x6b,
        .ret = PROTO_OK,
        .msg_type = SQL_UNKNOWN,
        .expected = {
            .info = { .head_len = 0x6b, .payload = 0},
            .set_values = SQL_REQUEST_STATUS | SQL_ERROR_CODE | SQL_ERROR_MESSAGE | SQL_NB_ROWS | SQL_NB_FIELDS,
            .request_status = SQL_REQUEST_COMPLETE,
            .error_code = "ORA-01403",
            .error_message = "no data found\n",
            .u = { .query = { .nb_fields = 2, .nb_rows = 2 } },
            .msg_type = SQL_QUERY,
        },
    },

    {
        .packet = (uint8_t const []) {
//          Leng------                                                  TTC-  Quer  Drop- Options---------
            0x00, 0x3c, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x5e, 0x00, 0x02, 0x80, 0x60,
//          Drop------------  Fix-  Len-  Fix-  VarField--  Fix-------  Var1  Var2  Var3------------------
            0x02, 0x01, 0x06, 0x00, 0x00, 0x01, 0x01, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x04, 0x7f, 0xff, 0xff,
//          ----  Fix-  Var-  Fix-------------------------------  Var-
            0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x14,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
        },
        .size = 0x3c,
        .ret = PROTO_OK,
        .msg_type = SQL_UNKNOWN,
        .expected = {
            .info = { .head_len = 0x3c, .payload = 0},
            .set_values = SQL_SQL,
            .u = { .query = { .sql = "" } },
            .msg_type = SQL_QUERY,
        },
    },

};

static unsigned cur_test;

static void tns_info_check(struct proto_subscriber unused_ *s, struct proto_info const *info_, size_t unused_ cap_len, uint8_t const unused_ *packet, struct timeval const unused_ *now)
{
    // Ignore non sql message
    if (info_->parser->proto != proto_tns) {
        return;
    }
    // Check info against parse_tests[cur_test].expected
    struct sql_proto_info const *const info = DOWNCAST(info_, info, sql_proto_info);
    struct sql_proto_info const *const expected = &parse_tests[cur_test].expected;
    assert(!compare_expected_sql(info, expected));
}

static void parse_check(void)
{
    struct timeval now;
    timeval_set_now(&now);
    struct parser *parser = proto_tns->ops->parser_new(proto_tns);
    struct tns_parser *tns_parser = DOWNCAST(parser, parser, tns_parser);
    assert(tns_parser);
    struct proto_subscriber sub;
    hook_subscriber_ctor(&pkt_hook, &sub, tns_info_check);

    for (cur_test = 0; cur_test < NB_ELEMS(parse_tests); cur_test++) {
        struct parse_test const *test = parse_tests + cur_test;
        printf("Check packet %d of size 0x%x (%d)\n", cur_test, test->size, test->size);
        tns_parser->msg_type = test->msg_type;
        enum proto_parse_status ret = tns_parse(parser, NULL, 0, test->packet, test->size,
                test->size, &now, test->size, test->packet);
        assert(ret == test->ret);
    }
}

static void check_lookup_query()
{
    uint8_t packet[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, '0' , '1', '2', '3', '4', '5', '6', '7', '8',
    };
    struct cursor cursor = { .head = packet, .cap_len = sizeof(packet) };
    bool is_chunked;
    uint8_t sql_size = 0;
    assert(!lookup_query(&cursor, &is_chunked, sql_size));
    uint8_t packet2[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d,
        '0',  '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 0x00, 0x00, 0x00,
    };
    struct cursor cursor2 = { .head = packet2, .cap_len = sizeof(packet2) };
    assert(lookup_query(&cursor2, &is_chunked, sql_size));
}

int main(void)
{
    log_init();
    ext_init();
    mutex_init();
    objalloc_init();
    streambuf_init();
    proto_init();
    pkt_wait_list_init();
    ref_init();
    cap_init();
    eth_init();
    ip_init();
    ip6_init();
    tcp_init();
    tns_init();
    log_set_level(LOG_DEBUG, NULL);
    log_set_file("tns_check.log");

    parse_check();
    check_lookup_query();

    doomer_stop();
    tns_fini();
    tcp_fini();
    ip6_fini();
    ip_fini();
    eth_fini();
    cap_fini();
    ref_fini();
    pkt_wait_list_fini();
    proto_fini();
    streambuf_fini();
    objalloc_fini();
    mutex_fini();
    ext_fini();
    log_fini();
    return EXIT_SUCCESS;
}
