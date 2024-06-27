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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

#include "../inc/unique_fd.h"
#include "../inc/diagnose_usb.h"
#include "../inc/fastboot.h"
#include "../inc/transport.h"
#include "../inc/usb.h"

#include "../inc/extra_fb_struct.h"

#define FASTBOOT_VERSION  "1.0"

using android::base::unique_fd;

#ifndef O_BINARY
#define O_BINARY 0
#endif

///char cur_product[FB_RESPONSE_SZ + 1];

//static const char* serial = nullptr;
///static unsigned short vendor_id = 0;
///static int long_listing = 0;
//static constexpr int64_t RESPARSE_LIMIT = 1 * 1024 * 1024 * 1024;
//static const std::string convert_fbe_marker_filename("convert_fbe");

bool fb_getvar(Transport* transport, const std::string& key, std::string* value, fastboot_data_t *fastboot_data_ptr);
int64_t fb_execute_queue(Transport* transport, int device_idx, fastboot_data_t *fastboot_data_ptr);
void fb_queue_flash_fd(const std::string& partition, int fd, uint32_t sz, int device_idx, fastboot_data_t *fastboot_data_ptr);
void rest_fastboot_output_msg(fastboot_data_t *fastboot_data_ptr);

enum fb_buffer_type {
    FB_BUFFER_FD,
    FB_BUFFER_SPARSE,
};

struct fastboot_buffer {
    enum fb_buffer_type type;
    void* data;
    int64_t sz;
    int fd;
};

//static int64_t get_file_size(int fd) {
int64_t get_file_size(int fd) {
    struct stat sb;
    return fstat(fd, &sb) == -1 ? -1 : sb.st_size;
}

//static void* load_fd(int fd, int64_t* sz) {
void* load_fd(int fd, int64_t* sz) {
    int errno_tmp;
    char* data = nullptr;

    *sz = get_file_size(fd);
    if (*sz < 0) {
        goto oops;
    }

    data = (char*) malloc(*sz);
    if (data == nullptr) goto oops;

    if(read(fd, data, *sz) != *sz) goto oops;
    close(fd);

    return data;

oops:
    errno_tmp = errno;
    close(fd);
    if(data != 0) free(data);
    errno = errno_tmp;
    return 0;
}

//static void* load_file(const std::string& path, int64_t* sz) {
void* load_file(const std::string& path, int64_t* sz) {
    int fd = open(path.c_str(), O_RDONLY | O_BINARY);
    if (fd == -1) return nullptr;
    return load_fd(fd, sz);
}

//static int match_fastboot_with_serial(usb_ifc_info* info, const char* local_serial) {
int match_fastboot_with_serial(usb_ifc_info* info, const char* local_serial) {
    // Require a matching vendor id if the user specified one with -i.
    ///if (vendor_id != 0 && info->dev_vendor != vendor_id) {
        ///return -1;
    ///}

    if (info->ifc_class != 0xff || info->ifc_subclass != 0x42 || info->ifc_protocol != 0x03) {
        return -1;
    }

    // require matching serial number or device path if requested
    // at the command line with the -s option.

    //printf(" match_fastboot_with_serial, local_serial:%s", local_serial );
    //printf(" match_fastboot_with_serial, info->serial_number:%s", info->serial_number );
    if (local_serial && (strcmp(local_serial, info->serial_number) != 0 &&
                   strcmp(local_serial, info->device_path) != 0)) return -1;
    return 0;
}

//static int match_fastboot(usb_ifc_info* info, char *serial) {
int match_fastboot(usb_ifc_info* info, char *serial) {
    return match_fastboot_with_serial(info, serial);
}
#if 0
static int list_devices_callback(usb_ifc_info* info) {
    if (match_fastboot_with_serial(info, nullptr) == 0) {
        std::string serial = info->serial_number;
        if (!info->writable) {
            serial = UsbNoPermissionsShortHelpText();
        }
        if (!serial[0]) {
            serial = "????????????";
        }
        // output compatible with "adb devices"
        if (!long_listing) {
            printf("%s\tfastboot", serial.c_str());
        } else {
            printf("%-22s fastboot", serial.c_str());
            if (strlen(info->device_path) > 0) printf(" %s", info->device_path);
        }
        putchar('\n');
    }

    return -1;
}
#endif

int StartsWithSpecStr( const char *CheckStr, const char *SpecStr )
{
    char *CheckResultPtr;
    CheckResultPtr = (char *)strstr( CheckStr, SpecStr );
    if( CheckResultPtr && ( CheckResultPtr == (char *)CheckStr ) )   return 1;
    else   return 0;
}

// Opens a new Transport connected to a device. If |serial| is non-null it will be used to identify
// a specific device, otherwise the first USB device found will be used.
//
// If |serial| is non-null but invalid, this prints an error message to stderr and returns nullptr.
// Otherwise it blocks until the target is available.
//
// The returned Transport is a singleton, so multiple calls to this function will return the same
// object, and the caller should not attempt to delete the returned Transport.
//static Transport* open_device(fastboot_data_t *fastboot_data_ptr) {
Transport* open_device(fastboot_data_t *fastboot_data_ptr) {
//    static Transport* transport = nullptr;

    Transport *transport;
    bool announce = true;

    //if (transport != nullptr) {
        //return transport;
    //}
    while (true) {
            if( strlen(fastboot_data_ptr->g_device_serial_number) == 0 )
                transport = usb_open( match_fastboot, NULL );
            else
                transport = usb_open( match_fastboot, fastboot_data_ptr->g_device_serial_number );

        if (transport != nullptr) {
            return transport;
        }

        if (announce) {
            announce = false;
            //fprintf(stderr, "< waiting for %s >\n", serial ? serial : "any device");
            //printf("< waiting for %s >\n", fastboot_data_ptr->g_device_serial_number ? fastboot_data_ptr->g_device_serial_number : "any device");
        }
    }
}

#if 0
static void list_devices() {
    // We don't actually open a USB device here,
    // just getting our callback called so we can
    // list all the connected devices.
    usb_open(list_devices_callback);
}
#endif

#define MAX_OPTIONS 32
// Until we get lazy inode table init working in make_ext4fs, we need to
// erase partitions of type ext4 before flashing a filesystem so no stale
// inodes are left lying around.  Otherwise, e2fsck gets very upset.
/*
static bool needs_erase(Transport* transport, const char* partition) {
    std::string partition_type;
    if (!fb_getvar(transport, std::string("partition-type:") + partition, &partition_type)) {
        return false;
    }
    return partition_type == "ext4";
}
*/
//static bool load_buf_fd(Transport* transport, int fd, struct fastboot_buffer* buf) {
bool load_buf_fd(Transport* transport, int fd, struct fastboot_buffer* buf) {
    int64_t sz = get_file_size(fd);
    if (sz == -1) {
        return false;
    }
    if( transport == 0 )  return false;
    lseek64(fd, 0, SEEK_SET);

        buf->type = FB_BUFFER_FD;
        buf->data = nullptr;
        buf->fd = fd;
        buf->sz = sz;

    return true;
}

//static bool load_buf(Transport* transport, const char* fname, struct fastboot_buffer* buf) {
bool load_buf(Transport* transport, const char* fname, struct fastboot_buffer* buf) {
    unique_fd fd(TEMP_FAILURE_RETRY(open(fname, O_RDONLY | O_BINARY)));

    if (fd == -1) {
        return false;
    }

    struct stat s;
    if (fstat(fd, &s)) {
        return false;
    }
    if (!S_ISREG(s.st_mode)) {
        errno = S_ISDIR(s.st_mode) ? EISDIR : EINVAL;
        return false;
    }

    return load_buf_fd(transport, fd.release(), buf);
}
//
//static void flash_buf(const std::string& partition, struct fastboot_buffer *buf, int device_idx, fastboot_data_t *fastboot_data_ptr)
void flash_buf(const std::string& partition, struct fastboot_buffer *buf, int device_idx, fastboot_data_t *fastboot_data_ptr)
{
    switch (buf->type) {
        case FB_BUFFER_FD:
            fb_queue_flash_fd(partition, buf->fd, buf->sz, device_idx, fastboot_data_ptr);
            break;
        default:
            die("unknown buffer type: %d", buf->type);
    }
}

//static std::string get_current_slot(Transport* transport, fastboot_data_t *fastboot_data_ptr)
std::string get_current_slot(Transport* transport, fastboot_data_t *fastboot_data_ptr)
{
    std::string current_slot;
    if (fb_getvar(transport, "current-slot", &current_slot, fastboot_data_ptr)) {
        if (current_slot == "_a") return "a"; // Legacy support
        if (current_slot == "_b") return "b"; // Legacy support
        return current_slot;
    }
    return "";
}

//static void do_for_partition(Transport* transport, const std::string& part, const std::string& slot,
void do_for_partition(Transport* transport, const std::string& part, const std::string& slot,
                             const std::function<void(const std::string&)>& func, bool force_slot, fastboot_data_t *fastboot_data_ptr) {
    std::string has_slot;
    std::string current_slot;

    if (!fb_getvar(transport, "has-slot:" + part, &has_slot,fastboot_data_ptr)) {
        /* If has-slot is not supported, the answer is no. */
        has_slot = "no";
    }
    if (has_slot == "yes") {
        if (slot == "") {
            current_slot = get_current_slot(transport,fastboot_data_ptr);
            if (current_slot == "") {
                die("Failed to identify current slot");
            }
            func(part + "_" + current_slot);
        } else {
            func(part + '_' + slot);
        }
    } else {
        if (force_slot && slot != "") {
             fprintf(stderr, "Warning: %s does not support slots, and slot %s was requested.\n",
                     part.c_str(), slot.c_str());
        }
        func(part);
    }
}

/* This function will find the real partition name given a base name, and a slot. If slot is NULL or
 * empty, it will use the current slot. If slot is "all", it will return a list of all possible
 * partition names. If force_slot is true, it will fail if a slot is specified, and the given
 * partition does not support slots.
 */
//static void do_for_partitions(Transport* transport, const std::string& part, const std::string& slot,
void do_for_partitions(Transport* transport, const std::string& part, const std::string& slot,
                              const std::function<void(const std::string&)>& func, bool force_slot, fastboot_data_t *fastboot_data_ptr) {
    std::string has_slot;

        do_for_partition(transport, part, slot, func, force_slot, fastboot_data_ptr);

}

//static void do_flash(Transport* transport, const char* pname, const char* fname, int device_idx, fastboot_data_t *fastboot_data_ptr) {
void do_flash(Transport* transport, const char* pname, const char* fname, int device_idx, fastboot_data_t *fastboot_data_ptr) {
    struct fastboot_buffer buf;

    if (!load_buf(transport, fname, &buf)) {
        die("cannot load '%s': %s", fname, strerror(errno));
    }
    flash_buf(pname, &buf, device_idx, fastboot_data_ptr);
}

//static std::string next_arg(std::vector<std::string>* args) {
std::string next_arg(std::vector<std::string>* args) {
    if (args->empty()) printf/*syntax_error*/("expected argument");
    std::string result = args->front();
    args->erase(args->begin());
    return result;
}
#if 0
static void do_bypass_unlock_command(std::vector<std::string>* args) {
    if (args->empty()) printf/*syntax_error*/("missing unlock_bootloader request");

    std::string filename = next_arg(args);

    int64_t sz;
    void* data = load_file(filename.c_str(), &sz);
    if (data == nullptr) die("could not load '%s': %s", filename.c_str(), strerror(errno));
    fb_queue_download("unlock_message", data, sz);
    fb_queue_command("flashing unlock_bootloader", "unlocking bootloader");
}
#endif
//static void do_oem_command(char *cmd, char *arg1, char *arg2, int device_idx )
void do_oem_command(char *cmd, char *arg1, char *arg2, int device_idx )
{
    char command[128];
    if( arg1 && (arg2 == NULL) )    sprintf( command, "%s %s", cmd, arg1 );
    else if( arg1 && arg2 )         sprintf( command, "%s %s %s", cmd, arg1, arg2 );
    else                            strcpy( command, "" );
    fb_queue_command(command, "", device_idx);

    //printf( "fast oem command: %s", command );
}
/*
void init_fastboot()
{
    serial = getenv("ANDROID_SERIAL");
}
*/
#define FASTBOOT_EARSE_COMMAND     0
#define FASTBOOT_REBOOT_COMMAND    1
#define FASTBOOT_FLASH_COMMAND     2
#define FASTBOOT_OEM_COMMAND       3
#define FASTBOOT_FLASHING_COMMAND  4

#ifdef	__cplusplus
extern "C" {
#endif

int fastboot_main( int exe_case, char *argv1, char *argv2, char *argv3, int device_idx );
int check_fastboot_download_port( char *argv );

#ifdef	__cplusplus
}
#endif

int check_fastboot_download_port( char *argv )
{
    fastboot_data_t *fastboot_data_ptr;
    fastboot_data_ptr = (fastboot_data_t *)argv;
    Transport* transport;

    if( strlen(fastboot_data_ptr->g_device_serial_number) == 0 )
        transport = usb_open( match_fastboot, NULL );
    else
        transport = usb_open( match_fastboot, fastboot_data_ptr->g_device_serial_number );

    if( transport != nullptr )
    {
       transport->Close();
       delete transport;
       return 1;
    }
    return 0;
}

int fastboot_main( int exe_case, char *argv1, char *argv2, char *argv3, int device_idx )
{
    bool wants_reboot = false;
    bool skip_reboot = false;
    //bool erase_first = true;
    std::string slot_override;
    std::string next_active;

    //serial = argv3;
    fastboot_data_t *fastboot_data_ptr;
    fastboot_data_ptr = (fastboot_data_t *)argv3;
    //printf( "\nfastboot: device %d, serial:%s\n", device_idx, fastboot_data_ptr->g_device_serial_number );
    Transport* transport = open_device(fastboot_data_ptr);
    if (transport == nullptr) {
        return 1;
    }

    rest_fastboot_output_msg( fastboot_data_ptr );
        
    //if( command == "erase") 
    /*if( exe_case == FASTBOOT_EARSE_COMMAND )
    {
        if( argv1 )
        {
            std::string partition = argv1;//next_arg(&args);
            auto erase = [&](const std::string& partition) 
            {
                std::string partition_type;
                fb_queue_erase(partition);
            };
            do_for_partitions(transport, partition, slot_override, erase, true);
       }
    } 
    //else if (command == "reboot") 
    else*/ if( exe_case == FASTBOOT_REBOOT_COMMAND )
    {
        wants_reboot = true;
    } 
    //else if (command == "flash") 
    else if( exe_case == FASTBOOT_FLASH_COMMAND )
    {
        if( argv1 && argv2 )
        {
            std::string pname = argv1;//next_arg(&args);

            std::string fname;
            //if( !args.empty() ) 
            {
               fname = argv2;//next_arg(&args);
            } 
            if( fname.empty() )   die("cannot determine image filename for '%s'", pname.c_str());

            auto flash = [&](const std::string &partition) 
            {
               /*if( erase_first && needs_erase(transport, partition.c_str()) ) 
               {
                   fb_queue_erase(partition);
               }*/
               do_flash(transport, partition.c_str(), fname.c_str(), device_idx, fastboot_data_ptr);
            };
            do_for_partitions(transport, pname.c_str(), slot_override, flash, true, fastboot_data_ptr);
        }
    } 
    //else if( command == "oem" ) 
    else if( exe_case == FASTBOOT_OEM_COMMAND )
    {
        do_oem_command( (char *)"oem", argv1, argv2, device_idx );
    } 
    //else if (command == "flashing") 
    else if( exe_case == FASTBOOT_FLASHING_COMMAND )
    {
        if( argv1 == NULL ) 
        //if( args.empty() ) 
        {
            printf/*syntax_error*/("missing 'flashing' command");
        } 
        else
        {
            do_oem_command( (char *)"flashing", argv1, argv2, device_idx );
        } 
    } 
    else 
    {
        printf( "unknown command %d", exe_case );
    }
    //}


    if (wants_reboot && !skip_reboot) {
        fb_queue_reboot(device_idx);
        fb_queue_wait_for_disconnect(device_idx);
    }

    int status = fb_execute_queue(transport, device_idx, fastboot_data_ptr) ? EXIT_FAILURE : EXIT_SUCCESS;
    //fprintf(stderr, "Finished. Total time: %.3fs\n", (now() - start));

    transport->Close();
    delete transport;
    return status;
}
