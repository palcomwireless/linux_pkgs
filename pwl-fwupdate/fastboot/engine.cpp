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

#include "fastboot.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <vector>

#include "extra_fb_struct.h"
#include "fastboot.h"

///#include <android-base/stringprintf.h>
///const char fb_set_error(char *error_msg);

enum Op {
    OP_DOWNLOAD,
    OP_COMMAND,
    OP_QUERY,
    OP_NOTICE,
    OP_DOWNLOAD_SPARSE,
    OP_WAIT_FOR_DISCONNECT,
    OP_DOWNLOAD_FD,
    OP_UPLOAD,
};

struct Action {
    Action(Op op, const std::string& cmd) : op(op), cmd(cmd) {}

    Op op;
    std::string cmd;
    std::string msg;

    std::string product;

    void* data = NULL;
    // The protocol only supports 32-bit sizes, so you'll have to break
    // anything larger into multiple chunks.
    uint32_t size = 0;

    int fd = -1;

    int (*func)(Action& a, int status, const char* resp, fastboot_data_t *fastboot_data_ptr) = NULL;

    double start = -1;
};

static std::vector<std::unique_ptr<Action>> action_list[SUPPORT_MAX_DEVICE];

int push_fastboot_output_msg( char *msg, fastboot_data_t *fastboot_data_ptr );
int fb_command(Transport* transport, const std::string& cmd, fastboot_data_t *fastboot_data_ptr);
int fb_command_response(Transport* transport, const std::string& cmd, char* response, fastboot_data_t *fastboot_data_ptr);
int64_t fb_download_data(Transport* transport, const void* data, uint32_t size, fastboot_data_t *fastboot_data_ptr);
int64_t fb_download_data_fd(Transport* transport, int fd, uint32_t size, fastboot_data_t *fastboot_data_ptr);
int fb_download_data_sparse(Transport* transport, struct sparse_file* s, fastboot_data_t *fastboot_data_ptr);


bool fb_getvar(Transport* transport, const std::string& key, std::string* value, fastboot_data_t *fastboot_data_ptr ) {
    std::string cmd = "getvar:" + key;

    char buf[FB_RESPONSE_SZ + 1];
    memset(buf, 0, sizeof(buf));
    if (fb_command_response(transport, cmd, buf, fastboot_data_ptr)) {
        return false;
    }
    *value = buf;
    return true;
}

static int cb_default(Action& a, int status, const char* resp, fastboot_data_t *fastboot_data_ptr) {
char gfb_msg[128];
    if (status) {
        ///fprintf(stderr,"FAILED (%s)\n", resp);

        sprintf( gfb_msg, "FAILED (%s)\n", resp );
        push_fastboot_output_msg( gfb_msg, fastboot_data_ptr );

    } else {
        double split = now();
        ///fprintf(stderr, "OKAY [%7.3fs]\n", (split - a.start));

        sprintf( gfb_msg, "OKAY [%7.3fs]\n", (split - a.start) );
        push_fastboot_output_msg( gfb_msg, fastboot_data_ptr );

        a.start = split;
    }
    return status;
}

static Action& queue_action(Op op, const std::string& cmd, int device_idx) {
    std::unique_ptr<Action> a{new Action(op, cmd)};
    a->func = cb_default;

    action_list[device_idx].push_back(std::move(a));
    return *action_list[device_idx].back();
}
//None
void fb_set_active(const std::string& slot, int device_idx) {
    Action& a = queue_action(OP_COMMAND, "set_active:" + slot, device_idx);
    a.msg = "Setting current slot to '" + slot + "'...";
}
//None
void fb_queue_erase(const std::string& partition, int device_idx) {
    Action& a = queue_action(OP_COMMAND, "erase:" + partition, device_idx);
    a.msg = "Erasing '" + partition + "'...";
}
//
void fb_queue_flash_fd(const std::string& partition, int fd, uint32_t sz, int device_idx, fastboot_data_t *fastboot_data_ptr) {
char gfb_msg[128];
    Action& a = queue_action(OP_DOWNLOAD_FD, "", device_idx);
    a.fd = fd;
    a.size = sz;

    sprintf( gfb_msg, "Sending '%s' (%d KB)...", partition.c_str(), sz / 1024 );
    a.msg = gfb_msg;///android::base::StringPrintf("Sending '%s' (%d KB)...", partition.c_str(), sz / 1024);

    //printf("fb_queue_flash_fd:%s", gfb_msg );
    //push_fastboot_output_msg( gfb_msg, fastboot_data_ptr );

    Action& b = queue_action(OP_COMMAND, "flash:" + partition, device_idx);
    ///b.msg = "Writing '" + partition + "'...";
    sprintf( gfb_msg, "Writing '%s' ....", partition.c_str() );
    b.msg = gfb_msg;

    //push_fastboot_output_msg( gfb_msg, fastboot_data_ptr );
}
//None
#if 0
void fb_queue_flash(const std::string& partition, void* data, uint32_t sz, int device_idx) {
char gfb_msg[128];
    Action& a = queue_action(OP_DOWNLOAD, "", device_idx);
    a.data = data;
    a.size = sz;

    sprintf( gfb_msg, "Sending '%s' (%d KB)...", partition.c_str(), sz / 1024 );
    a.msg = gfb_msg;///android::base::StringPrintf("Sending '%s' (%d KB)...", partition.c_str(), sz / 1024);

    push_fastboot_output_msg( gfb_msg );

    Action& b = queue_action(OP_COMMAND, "flash:" + partition, device_idx);
    ///b.msg = "Writing '" + partition + "'...";
    sprintf( gfb_msg, "Writing '%s' ....", partition.c_str() );
    b.msg = gfb_msg;

    push_fastboot_output_msg( gfb_msg );
}

//None
void fb_queue_flash_sparse(const std::string& partition, struct sparse_file* s, uint32_t sz,
                           size_t current, size_t total, int device_idx) {
char gfb_msg[128];
    Action& a = queue_action(OP_DOWNLOAD_SPARSE, "", device_idx);
    a.data = s;
    a.size = 0;

    sprintf( gfb_msg, "Sending sparse '%s' %zu/%zu (%d KB)...", partition.c_str(), current, total, sz / 1024 );
    a.msg = gfb_msg;///android::base::StringPrintf("Sending sparse '%s' %zu/%zu (%d KB)...", partition.c_str(), current, total, sz / 1024);

    push_fastboot_output_msg( gfb_msg );

    Action& b = queue_action(OP_COMMAND, "flash:" + partition, device_idx);

    sprintf( gfb_msg, "Writing '%s' %zu/%zu...", partition.c_str(), current, total );
    b.msg = gfb_msg;///android::base::StringPrintf("Writing '%s' %zu/%zu...", partition.c_str(), current, total);

    push_fastboot_output_msg( gfb_msg );
}
#endif

static int match(const char* str, const char** value, unsigned count) {
    unsigned n;

    for (n = 0; n < count; n++) {
        const char *val = value[n];
        int len = strlen(val);
        int match;

        if ((len > 1) && (val[len-1] == '*')) {
            len--;
            match = !strncmp(val, str, len);
        } else {
            match = !strcmp(val, str);
        }

        if (match) return 1;
    }

    return 0;
}

static int cb_check(Action& a, int status, const char* resp, int invert, fastboot_data_t *fastboot_data_ptr) {
    const char** value = reinterpret_cast<const char**>(a.data);
    unsigned count = a.size;
    unsigned n;
char gfb_msg[128];

    if (status) {
        ///fprintf(stderr,"FAILED (%s)\n", resp);

        sprintf( gfb_msg, "FAILED (%s)\n", resp );
        push_fastboot_output_msg( gfb_msg, fastboot_data_ptr );

        return status;
    }
#if 0
    if (!a.product.empty()) {
        if (a.product != cur_product) {
            double split = now();
            ///fprintf(stderr, "IGNORE, product is %s required only for %s [%7.3fs]\n", cur_product,
            ///        a.product.c_str(), (split - a.start));

            a.start = split;
            return 0;
        }
    }
#endif
    int yes = match(resp, value, count);
    if (invert) yes = !yes;

    if (yes) {
        double split = now();
        ///fprintf(stderr, "OKAY [%7.3fs]\n", (split - a.start)  );

        sprintf( gfb_msg, "OKAY [%7.3fs]\n", (split - a.start) );
        push_fastboot_output_msg( gfb_msg, fastboot_data_ptr );

        a.start = split;
        return 0;
    }
/*
    fprintf(stderr, "FAILED\n\n");
    fprintf(stderr, "Device %s is '%s'.\n", a.cmd.c_str() + 7, resp);
    fprintf(stderr, "Update %s '%s'", invert ? "rejects" : "requires", value[0]);
    for (n = 1; n < count; n++) {
        fprintf(stderr, " or '%s'", value[n]);
    }
    fprintf(stderr, ".\n\n");
*/
    return -1;
}

static int cb_require(Action& a, int status, const char* resp, fastboot_data_t *fastboot_data_ptr) {
    return cb_check(a, status, resp, 0, fastboot_data_ptr);
}

static int cb_reject(Action& a, int status, const char* resp, fastboot_data_t *fastboot_data_ptr) {
    return cb_check(a, status, resp, 1,fastboot_data_ptr);
}
//None
void fb_queue_require(const std::string& product, const std::string& var, bool invert,
                      size_t nvalues, const char** values, int device_idx) {
    Action& a = queue_action(OP_QUERY, "getvar:" + var, device_idx);
    a.product = product;
    a.data = values;
    a.size = nvalues;
    a.msg = "Checking " + var;
    a.func = invert ? cb_reject : cb_require;
    if (a.data == NULL) die("out of memory");
}

static int cb_display(Action& a, int status, const char* resp, fastboot_data_t *fastboot_data_ptr) {
char gfb_msg[128];
    if (status) {
        ///fprintf(stderr, "%s FAILED (%s)\n", a.cmd.c_str(), resp);

        sprintf( gfb_msg, "%s FAILED (%s)\n", a.cmd.c_str(), resp );
        push_fastboot_output_msg( gfb_msg, fastboot_data_ptr );

        return status;
    }
    ///fprintf(stderr, "%s: %s\n", static_cast<const char*>(a.data), resp);
    free(static_cast<char*>(a.data));
    return 0;
}
//None
void fb_queue_display(const std::string& label, const std::string& var, int device_idx) {
    Action& a = queue_action(OP_QUERY, "getvar:" + var, device_idx);
    a.data = xstrdup(label.c_str());
    a.func = cb_display;
}

static int cb_save(Action& a, int status, const char* resp, fastboot_data_t *fastboot_data_ptr) {
char gfb_msg[128];
    if (status) {
        ///fprintf(stderr, "%s FAILED (%s)\n", a.cmd.c_str(), resp);

        sprintf( gfb_msg, "%s FAILED (%s)\n", a.cmd.c_str(), resp );
        push_fastboot_output_msg( gfb_msg, fastboot_data_ptr );

        return status;
    }
    strncpy(reinterpret_cast<char*>(a.data), resp, a.size);
    return 0;
}
//None
void fb_queue_query_save(const std::string& var, char* dest, uint32_t dest_size, int device_idx) {
    Action& a = queue_action(OP_QUERY, "getvar:" + var, device_idx);
    a.data = dest;
    a.size = dest_size;
    a.func = cb_save;
}

static int cb_do_nothing(Action&, int, const char*, fastboot_data_t *) {
    ///fprintf(stderr, "\n");
    return 0;
}
//
void fb_queue_reboot(int device_idx) {
    Action& a = queue_action(OP_COMMAND, "reboot", device_idx);
    a.func = cb_do_nothing;
    a.msg = "Rebooting...";
}
//
void fb_queue_command(const std::string& cmd, const std::string& msg, int device_idx) {
    Action& a = queue_action(OP_COMMAND, cmd, device_idx);
    a.msg = msg;
}
//None
void fb_queue_download(const std::string& name, void* data, uint32_t size, int device_idx) {
    Action& a = queue_action(OP_DOWNLOAD, "", device_idx);
    a.data = data;
    a.size = size;
    a.msg = "Downloading '" + name + "'";
}
//None
void fb_queue_download_fd(const std::string& name, int fd, uint32_t sz, int device_idx) {
char gfb_msg[128];
    Action& a = queue_action(OP_DOWNLOAD_FD, "", device_idx);
    a.fd = fd;
    a.size = sz;

    sprintf( gfb_msg, "Sending '%s' (%d KB)", name.c_str(), sz / 1024 );
    a.msg = gfb_msg;///android::base::StringPrintf("Sending '%s' (%d KB)", name.c_str(), sz / 1024);
}
#if 0
void fb_queue_upload(const std::string& outfile) {
    Action& a = queue_action(OP_UPLOAD, "");
    a.data = xstrdup(outfile.c_str());
    a.msg = "Uploading '" + outfile + "'";
}
#endif
//None
void fb_queue_notice(const std::string& notice, int device_idx) {
    Action& a = queue_action(OP_NOTICE, "", device_idx);
    a.msg = notice;
}
//
void fb_queue_wait_for_disconnect(int device_idx) {
    queue_action(OP_WAIT_FOR_DISCONNECT, "", device_idx);
}
//
int64_t fb_execute_queue(Transport* transport, int device_idx, fastboot_data_t *fastboot_data_ptr ) {
    int64_t status = 0;
    for (auto& a : action_list[device_idx]) {
        a->start = now();
        if (!a->msg.empty()) {
            ///fprintf(stderr, "%s\n", a->msg.c_str());
        }
        if (a->op == OP_DOWNLOAD) {
            status = fb_download_data(transport, a->data, a->size, fastboot_data_ptr);
            status = a->func(*a, status, status ? fastboot_data_ptr->gfb_error_msg : "", fastboot_data_ptr);
            if (status) break;
        } else if (a->op == OP_DOWNLOAD_FD) {
            status = fb_download_data_fd(transport, a->fd, a->size, fastboot_data_ptr);
            status = a->func(*a, status, status ? fastboot_data_ptr->gfb_error_msg : "", fastboot_data_ptr);
            if (status) break;
        } else if (a->op == OP_COMMAND) {
            status = fb_command(transport, a->cmd, fastboot_data_ptr);
            status = a->func(*a, status, status ? fastboot_data_ptr->gfb_error_msg : "", fastboot_data_ptr);
            if (status) break;
        } else if (a->op == OP_QUERY) {
            char resp[FB_RESPONSE_SZ + 1] = {};
            status = fb_command_response(transport, a->cmd, resp, fastboot_data_ptr);
            status = a->func(*a, status, status ? fastboot_data_ptr->gfb_error_msg : resp, fastboot_data_ptr);
            if (status) break;
        } else if (a->op == OP_NOTICE) {
            // We already showed the notice because it's in `Action::msg`.
        } 
#if 0
        else if (a->op == OP_DOWNLOAD_SPARSE) {
            status = fb_download_data_sparse(transport, reinterpret_cast<sparse_file*>(a->data));
            status = a->func(*a, status, status ? fb_get_error().c_str() : "");
            if (status) break;
        } 
#endif
        else if (a->op == OP_WAIT_FOR_DISCONNECT) {
            transport->WaitForDisconnect();
        } 
#if 0
        else if (a->op == OP_UPLOAD) {
            status = fb_upload_data(transport, reinterpret_cast<char*>(a->data));
            status = a->func(*a, status, status ? fb_get_error().c_str() : "");
        } 
#endif
        else {
            die("unknown action: %d", a->op);
        }
    }
    action_list[device_idx].clear();
    return status;
}
