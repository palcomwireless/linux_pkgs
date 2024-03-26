/*
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define round_down(a, b) \
    ({ typeof(a) _a = (a); typeof(b) _b = (b); _a - (_a % _b); })

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <algorithm>
#include <vector>

#include <memory.h>
#include <assert.h>
#include <sys/mman.h>

#include <sys/types.h>
#include "Compat.h"

#include "extra_fb_struct.h" 
#include "fastboot.h"
#include "transport.h"


/*
static const char *g_error;
char gfb_error_msg[FB_MAX_MSG_LEN];
//static const char *g_fastboot_info_msg;

#define FB_MAX_INFO_COUNT   16
#define FB_MAX_MSG_LEN   256

char gfb_info_msg[FB_MAX_INFO_COUNT][FB_MAX_MSG_LEN];
int g_fasboot_output_msg_count = 0;
int g_fasboot_output_msg_count_keep = 0;
*/

/*const char *fb_get_error() {
    return g_error;
}*/

void rest_fastboot_output_msg( fastboot_data_t *fastboot_data_ptr )
{
    fastboot_data_ptr->g_fasboot_output_msg_count = 0;
    fastboot_data_ptr->g_fasboot_output_msg_count_keep = 0;
}

int push_fastboot_output_msg( char *msg, fastboot_data_t *fastboot_data_ptr )
{
    strcpy( fastboot_data_ptr->gfb_info_msg[fastboot_data_ptr->g_fasboot_output_msg_count], msg );
    fastboot_data_ptr->g_fasboot_output_msg_count++;
    fastboot_data_ptr->g_fasboot_output_msg_count_keep = fastboot_data_ptr->g_fasboot_output_msg_count;

    return fastboot_data_ptr->g_fasboot_output_msg_count;
}
/*
char *pop_fastboot_output_msg()
{
    if( g_fasboot_output_msg_count == 0 )   return NULL;

    g_fasboot_output_msg_count--;
    return gfb_info_msg[g_fasboot_output_msg_count_keep-g_fasboot_output_msg_count-1];
}
*/

static int64_t check_response(Transport* transport, uint32_t size, char* response, fastboot_data_t *fastboot_data_ptr) {
    char status[65];

    while (true) {
        int r = transport->Read(status, 64);
        if (r < 0) {
            ///g_error = android::base::StringPrintf("status read failed (%s)", strerror(errno));
            sprintf( fastboot_data_ptr->gfb_error_msg, "status read failed (%s)", strerror(errno) );
            //g_error = gfb_error_msg;

            transport->Close();
            return -1;
        }
        status[r] = 0;

        if (r < 4) {
            ///g_error = android::base::StringPrintf("status malformed (%d bytes)", r);
            sprintf( fastboot_data_ptr->gfb_error_msg, "status malformed (%d bytes)", r );
            //g_fastboot_info_msg = gfb_error_msg;
            //g_error = gfb_error_msg;

            transport->Close();
            return -1;
        }

        if (!memcmp(status, "INFO", 4)) {
            sprintf( fastboot_data_ptr->gfb_error_msg, "(bootloader) %s\n", status + 4 );
            push_fastboot_output_msg( fastboot_data_ptr->gfb_error_msg, fastboot_data_ptr );
            
            //printf("\nINFO bootloader: %s\n", gfb_error_msg );

            ///fprintf(stderr,"(bootloader) %s\n", status + 4);
            continue;
        }

        if (!memcmp(status, "OKAY", 4)) {
            if (response) {
                strcpy(response, (char*) status + 4);
            }
            return 0;
        }

        if (!memcmp(status, "FAIL", 4)) {
            if (r > 4) {
                ///g_error = android::base::StringPrintf("remote: %s", status + 4);
                sprintf( fastboot_data_ptr->gfb_error_msg, "remote: %s", status + 4 );
                //g_error = gfb_error_msg;
            } else {
                ;//g_error = "remote failure";
                strcpy(fastboot_data_ptr->gfb_error_msg,"remote failure");
            }
            return -1;
        }

        if (!memcmp(status, "DATA", 4) && size > 0){
            uint32_t dsize = strtol(status + 4, 0, 16);
            if (dsize > size) {
                ///g_error = android::base::StringPrintf("data size too large (%d)", dsize);
                sprintf( fastboot_data_ptr->gfb_error_msg, "data size too large (%d)", dsize );
                //g_error = gfb_error_msg;
                transport->Close();
                return -1;
            }
            return dsize;
        }

        //g_error = "unknown status code";
        strcpy(fastboot_data_ptr->gfb_error_msg,"unknown status code");
        transport->Close();
        break;
    }

    return -1;
}

static int64_t _command_start(Transport* transport, const std::string& cmd, uint32_t size,
                              char* response, fastboot_data_t *fastboot_data_ptr) {
    if (cmd.size() > 64) {
        ///g_error = android::base::StringPrintf("command too large (%zu)", cmd.size());
        sprintf( fastboot_data_ptr->gfb_error_msg, "command too large (%zu)", cmd.size() );
        //g_error = gfb_error_msg;
        return -1;
    }

    if (response) {
        response[0] = 0;
    }

    if (transport->Write(cmd.c_str(), cmd.size()) != static_cast<int>(cmd.size())) {
        ///g_error = android::base::StringPrintf("command write failed (%s)", strerror(errno));
        sprintf( fastboot_data_ptr->gfb_error_msg, "command write failed (%s)", strerror(errno) );
        //g_error = gfb_error_msg;
        transport->Close();
        return -1;
    }

    return check_response(transport, size, response,fastboot_data_ptr);
}

static int64_t _command_write_data(Transport* transport, const void* data, uint32_t size, fastboot_data_t *fastboot_data_ptr) {
    int64_t r = transport->Write(data, size);
    if (r < 0) {
        ///g_error = android::base::StringPrintf("data write failure (%s)", strerror(errno));
        sprintf( fastboot_data_ptr->gfb_error_msg, "data write failure (%s)", strerror(errno) );
        //g_error = gfb_error_msg;
        transport->Close();
        return -1;
    }
    if (r != static_cast<int64_t>(size)) {
        sprintf( fastboot_data_ptr->gfb_error_msg, "data write failure (short transfer)" );
        //g_error = gfb_error_msg;
        //g_error = "data write failure (short transfer)";
        transport->Close();
        return -1;
    }
    return r;
}

#if 0
static int64_t _command_read_data(Transport* transport, void* data, uint32_t size) {
    int64_t r = transport->Read(data, size);
    if (r < 0) {
        g_error = android::base::StringPrintf("data read failure (%s)", strerror(errno));
        transport->Close();
        return -1;
    }
    if (r != (static_cast<int64_t>(size))) {
        g_error = "data read failure (short transfer)";
        transport->Close();
        return -1;
    }
    return r;
}
#endif

static int64_t _command_end(Transport* transport, fastboot_data_t *fastboot_data_ptr) {
    return check_response(transport, 0, 0,fastboot_data_ptr) < 0 ? -1 : 0;
}

static int64_t _command_send(Transport* transport, const std::string& cmd, const void* data,
                             uint32_t size, char* response, fastboot_data_t *fastboot_data_ptr) {
    if (size == 0) {
        return -1;
    }

    int64_t r = _command_start(transport, cmd, size, response, fastboot_data_ptr);
    if (r < 0) {
        return -1;
    }
    r = _command_write_data(transport, data, size, fastboot_data_ptr);
    if (r < 0) {
        return -1;
    }

    r = _command_end(transport, fastboot_data_ptr);
    if (r < 0) {
        return -1;
    }

    return size;
}

void *CreateFileMap(const char* origFileName, int fd, off64_t offset, size_t length, bool readOnly)
{

    char*       mFileName;      // original file name, if known
    void*       mBasePtr;       // base of mmap area; page aligned
    size_t      mBaseLength;    // length, measured from "mBasePtr"
    off64_t     mDataOffset;    // offset used when map was created
    void*       mDataPtr;       // start of requested data, offset from base
    size_t      mDataLength;    // length, measured from "mDataPtr"

    static long mPageSize = -1;

    int     prot, flags, adjust;
    off64_t adjOffset;
    size_t  adjLength;

    void* ptr;

    assert(fd >= 0);
    assert(offset >= 0);
    assert(length > 0);

    // init on first use
    if (mPageSize == -1) {
        mPageSize = sysconf(_SC_PAGESIZE);
        if (mPageSize == -1) {
            ///ALOGE("could not get _SC_PAGESIZE\n");
            return NULL;
        }
    }

    adjust = offset % mPageSize;
    adjOffset = offset - adjust;
    adjLength = length + adjust;

    flags = MAP_SHARED;
    prot = PROT_READ;
    if (!readOnly)
        prot |= PROT_WRITE;

    ptr = mmap(NULL, adjLength, prot, flags, fd, adjOffset);
    if (ptr == MAP_FAILED) {
        ///ALOGE("mmap(%lld,%zu) failed: %s\n",
        ///    (long long)adjOffset, adjLength, strerror(errno));
        return NULL;
    }
    mBasePtr = ptr;

    mFileName = origFileName != NULL ? strdup(origFileName) : NULL;
    mBaseLength = adjLength;
    mDataOffset = offset;
    mDataPtr = (char*) mBasePtr + adjust;
    mDataLength = length;

    assert(mBasePtr != NULL);

    ///ALOGV("MAP: base %p/%zu data %p/%zu\n",
    ///    mBasePtr, mBaseLength, mDataPtr, mDataLength);

    return mDataPtr;
}


static int64_t _command_send_fd(Transport* transport, const std::string& cmd, int fd, uint32_t size,
                                char* response, fastboot_data_t *fastboot_data_ptr) {
    //static constexpr uint32_t MAX_MAP_SIZE = 512 * 1024 * 1024;
    static uint32_t MAX_MAP_SIZE = 512 * 1024 * 1024;
    off64_t offset = 0;
    uint32_t remaining = size;

    if (_command_start(transport, cmd, size, response, fastboot_data_ptr) < 0) {
        return -1;
    }

    void *DataPtr;

    while (remaining) {
        //android::FileMap filemap;

        size_t len = std::min(remaining, MAX_MAP_SIZE);

        DataPtr = CreateFileMap( NULL, fd, offset, len, true );
        if( DataPtr == NULL )   return -1;
        ///if (!filemap.create(NULL, fd, offset, len, true)) {
        ///    return -1;
        ///}

        if (_command_write_data(transport, DataPtr, len, fastboot_data_ptr) < 0) {
        ///if (_command_write_data(transport, filemap.getDataPtr(), len) < 0) {
            return -1;
        }

        remaining -= len;
        offset += len;
    }

    if (_command_end(transport, fastboot_data_ptr) < 0) {
        return -1;
    }

    return size;
}

static int _command_send_no_data(Transport* transport, const std::string& cmd, char* response, fastboot_data_t *fastboot_data_ptr) {
    return _command_start(transport, cmd, 0, response, fastboot_data_ptr);
}

int fb_command(Transport* transport, const std::string& cmd, fastboot_data_t *fastboot_data_ptr ) {
    return _command_send_no_data(transport, cmd, 0, fastboot_data_ptr);
}

int fb_command_response(Transport* transport, const std::string& cmd, char* response, fastboot_data_t *fastboot_data_ptr ) {
    return _command_send_no_data(transport, cmd, response, fastboot_data_ptr);
}

int64_t fb_download_data(Transport* transport, const void* data, uint32_t size, fastboot_data_t *fastboot_data_ptr ) {
    ///std::string cmd(   android::base::StringPrintf("download:%08x", size)   );
    sprintf( fastboot_data_ptr->gfb_error_msg, "download:%08x", size );

    return _command_send(transport, fastboot_data_ptr->gfb_error_msg, data, size, 0, fastboot_data_ptr) < 0 ? -1 : 0;
}

int64_t fb_download_data_fd(Transport* transport, int fd, uint32_t size, fastboot_data_t *fastboot_data_ptr ) {
    ///std::string cmd(android::base::StringPrintf("download:%08x", size));
    sprintf( fastboot_data_ptr->gfb_error_msg, "download:%08x", size );

    return _command_send_fd(transport, fastboot_data_ptr->gfb_error_msg, fd, size, 0, fastboot_data_ptr) < 0 ? -1 : 0;
}
#if 0
int64_t fb_upload_data(Transport* transport, const char* outfile) {
    // positive return value is the upload size sent by the device
    int64_t r = _command_start(transport, "upload", std::numeric_limits<int32_t>::max(), nullptr);
    if (r <= 0) {
        ///g_error = android::base::StringPrintf("command start failed (%s)", strerror(errno));
        return r;
    }

    std::string data;
    data.resize(r);
    if ((r = _command_read_data(transport, &data[0], data.size())) == -1) {
        return r;
    }

    if (!WriteStringToFile(data, outfile, true)) {
        ///g_error = android::base::StringPrintf("write to '%s' failed", outfile);
        return -1;
    }

    return _command_end(transport);
}

#define TRANSPORT_BUF_SIZE 1024
static char transport_buf[TRANSPORT_BUF_SIZE];
static int transport_buf_len;

static int fb_download_data_sparse_write(void *priv, const void *data, int len)
{
    int r;
    Transport* transport = reinterpret_cast<Transport*>(priv);
    int to_write;
    const char* ptr = reinterpret_cast<const char*>(data);

    if (transport_buf_len) {
        to_write = std::min(TRANSPORT_BUF_SIZE - transport_buf_len, len);

        memcpy(transport_buf + transport_buf_len, ptr, to_write);
        transport_buf_len += to_write;
        ptr += to_write;
        len -= to_write;
    }

    if (transport_buf_len == TRANSPORT_BUF_SIZE) {
        r = _command_write_data(transport, transport_buf, TRANSPORT_BUF_SIZE);
        if (r != TRANSPORT_BUF_SIZE) {
            return -1;
        }
        transport_buf_len = 0;
    }

    if (len > TRANSPORT_BUF_SIZE) {
        if (transport_buf_len > 0) {
            g_error = "internal error: transport_buf not empty";
            return -1;
        }
        to_write = round_down(len, TRANSPORT_BUF_SIZE);
        r = _command_write_data(transport, ptr, to_write);
        if (r != to_write) {
            return -1;
        }
        ptr += to_write;
        len -= to_write;
    }

    if (len > 0) {
        if (len > TRANSPORT_BUF_SIZE) {
            g_error = "internal error: too much left for transport_buf";
            return -1;
        }
        memcpy(transport_buf, ptr, len);
        transport_buf_len = len;
    }

    return 0;
}

static int fb_download_data_sparse_flush(Transport* transport) {
    if (transport_buf_len > 0) {
        int64_t r = _command_write_data(transport, transport_buf, transport_buf_len);
        if (r != static_cast<int64_t>(transport_buf_len)) {
            return -1;
        }
        transport_buf_len = 0;
    }
    return 0;
}

int fb_download_data_sparse(Transport* transport, struct sparse_file* s) {
    int size = sparse_file_len(s, true, false);
    if (size <= 0) {
        return -1;
    }

    std::string cmd(android::base::StringPrintf("download:%08x", size));
    int r = _command_start(transport, cmd, size, 0);
    if (r < 0) {
        return -1;
    }

    r = sparse_file_callback(s, true, false, fb_download_data_sparse_write, transport);
    if (r < 0) {
        return -1;
    }

    r = fb_download_data_sparse_flush(transport);
    if (r < 0) {
        return -1;
    }

    return _command_end(transport);
}
#endif
