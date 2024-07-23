/*
 * Copyright (C) 2024 Palcom International Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// #define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extra_fb_struct.h"
#include "fdtl.h"

#define HEADER_BUF_SIZE   1024
#define TEMP_IMAGE_FILE_BUF_SIZE   1024
#define FAST_BOOT_RETURN_STR_LEN   256

extern const char *gp_log_output_file;

void output_message_to_file( char *msg_buf )
{
     FILE *Fp = NULL;

     Fp = fopen( gp_log_output_file, "a");
     if( Fp )
     {
         fwrite( msg_buf, 1, strlen( msg_buf ), Fp );
     }
     else
     {
         printf("\n Open log file error: %s \n", gp_log_output_file );
     } 
     fclose( Fp );
}

int start_flash_image_file( char *image_file, char *gp_first_temp_file_name )
{
    char read_buf[ HEADER_BUF_SIZE ];
    char output_message[1024];

    FILE *image_fp = fopen( image_file, "rb");
    if( image_fp == NULL )
    {  
       sprintf( output_message, "\nImage file not existed: %s \n", image_file );
       printf_fdtl_s( output_message );

       return -1;
    }

    int size = fread( read_buf, 1, HEADER_BUF_SIZE, image_fp );
    if( size != HEADER_BUF_SIZE )
    {  
       sprintf( output_message, "Image file read error: %s \n", image_file );
       printf_fdtl_s( output_message );
       return -2;
    }
    fclose( image_fp );

    FILE *FirstTempFp = fopen( gp_first_temp_file_name, "wb");
    if( FirstTempFp == NULL )
    {  
       sprintf( output_message, "Temp file open error \n");
       printf_fdtl_s( output_message );
       return -3;
    }

    //sprintf( output_message, "Temp file open: %s  \n", gp_first_temp_file_name );
    //printf_fdtl_d( output_message );

    size = fwrite( read_buf, 1, HEADER_BUF_SIZE, FirstTempFp );
    if( size != HEADER_BUF_SIZE )
    {  
       sprintf( output_message, "Write temp file error \n");
       printf_fdtl_s( output_message );
       return -4;
    }
    fclose( FirstTempFp );

    //printf("Write first temp file success \n");

    return 1;
}

void get_offset_and_size( char *StrBuf, int *offset, int *size )
{ 
   char *pos, *next_pos;
   int len, c;
   char transfer_string[64];
   char src_string_buf[FAST_BOOT_RETURN_STR_LEN];

   strcpy( src_string_buf, StrBuf );
 
   pos = strstr( StrBuf, ":" ); 
   if( pos )
   {
       pos += 1;
       //printf( "pos: %s \n", pos );
       next_pos = strstr( pos, ":" ); 
       if( next_pos )
       {
           next_pos += 1;  
           //printf( "next pos: %s \n", next_pos );

           for( c = 0 ; c < next_pos - pos - 1 ; c++ )   transfer_string[c] = src_string_buf[(int)(pos-StrBuf)+c];
           transfer_string[c] = 0;
           sscanf( transfer_string, "%x", offset );
           //printf( "image offset: %s, %d \n", transfer_string, *offset );

           len = strlen(StrBuf) - (int)(next_pos-StrBuf);
           for( c = 0 ; c < len ; c++ )   transfer_string[c] = src_string_buf[(int)(next_pos-StrBuf)+c];
           transfer_string[c] = 0;
           sscanf( transfer_string, "%x", size );
           //printf( "image size: %s, %d \n", transfer_string, *size );
       }
  }

}

char *pop_fastboot_output_msg( fastboot_data_t *fastboot_data_ptr )
{
    int idx;
    if( fastboot_data_ptr->g_fasboot_output_msg_count == 0 )   return NULL;

    fastboot_data_ptr->g_fasboot_output_msg_count--;
    idx = fastboot_data_ptr->g_fasboot_output_msg_count_keep - fastboot_data_ptr->g_fasboot_output_msg_count - 1;

    return fastboot_data_ptr->gfb_info_msg[ idx ];
}

int post_process_fastboot( fdtl_data_t *fdtl_data, FILE *fastboot_fp, int flash_step, fastboot_data_t *fastboot_data_ptr )
{
    char transfer_string[1024];
    char output_message[1024];
    int offset, size, image_count;
    char *pos;
    char *fastboot_return_string;
    const char *fb_rtn_str;
   
    fastboot_return_string = (char *) malloc(FAST_BOOT_RETURN_STR_LEN);
    if( fastboot_return_string == NULL )   
    {
        sprintf( output_message, "\n%smemory allocate failed --  fastboot_return_string\n", fdtl_data->g_prefix_string );
        printf_fdtl_d( output_message );
        //if( fastboot_fp )    pclose( fastboot_fp );
        return 0;
    }

    while( 1 ) 
    {
       //if( fastboot_fp )
       //{
           //if( fgets(fastboot_return_string, FAST_BOOT_RETURN_STR_LEN, fastboot_fp) == NULL )   break;
       //}
       //else   
       //{
           fb_rtn_str = pop_fastboot_output_msg( fastboot_data_ptr );

           if( fb_rtn_str )        
           {
               strcpy( fastboot_return_string, fb_rtn_str );
           }
           else    break;
       //}

       sprintf( output_message, "\n%sfastboot output: %s \n", fdtl_data->g_prefix_string, fastboot_return_string);
       printf_fdtl_d( output_message );

       pos = strstr( fastboot_return_string, "FAILED"); 
       if( pos )
       {
          sprintf( output_message, "\n%sDownload failed -- %s \n", fdtl_data->g_prefix_string, fastboot_return_string+6 );
          printf_fdtl_s( output_message );

          if( fastboot_fp )    pclose( fastboot_fp );
          free(fastboot_return_string);
          return -1; 
       }

       switch( flash_step )
       {
           case FASTBOOT_SOP_HDR:
                pos = strstr( fastboot_return_string, "(bootloader)"); 
                if( pos )    
                {
                    pos = strstr( fastboot_return_string, "image_count = "); 
                    if( pos )     
                    {
                        strcpy( transfer_string, (char *)(pos+14) );
                        image_count = atoi(transfer_string);

                        //printf( "image count: %s, %d \n", transfer_string, image_count );

                        break;
                    }
                    pos = strstr( fastboot_return_string, "slot_" ); 
                    if( pos )     
                    {
                        if( fdtl_data->g_fw_image_count < MAX_DOWNLOAD_FW_IMAGES )
                        {
                           strncpy( fdtl_data->g_partition_name[fdtl_data->g_total_image_count], pos, 8 );
                           //printf( "Partition name: %s \n", fdtl_data->g_partition_name[fdtl_data->g_total_image_count] );

                           get_offset_and_size( fastboot_return_string, &offset, &size );
                           fdtl_data->g_image_offset[ fdtl_data->g_total_image_count ] = offset;
                           fdtl_data->g_image_size[ fdtl_data->g_total_image_count ] = size;
                           fdtl_data->g_fw_image_count++;
                           fdtl_data->g_total_image_count++;
                        }
                        else   
                        {
                           sprintf( output_message, "%sFirmware image support up to %d \n", fdtl_data->g_prefix_string, MAX_DOWNLOAD_FW_IMAGES );
                           printf_fdtl_s( output_message );
                        }

                        break;
                    }
                    pos = strstr( fastboot_return_string, "capri_c" ); 
                    if( pos )     
                    {
                        //printf( "PRI Partition, %d\n", fdtl_data->g_pri_count );
                        if( fdtl_data->g_pri_count < MAX_DOWNLOAD_PRI_IMAGES )
                        {
                          get_offset_and_size( fastboot_return_string, &offset, &size );
                          fdtl_data->g_pri_offset[ fdtl_data->g_pri_count]  = offset;
                          fdtl_data->g_pri_size[ fdtl_data->g_pri_count ] = size;

                          //printf( "PRI Partition, offset:%d, size:%d \n", fdtl_data->g_pri_offset[ fdtl_data->g_pri_count], fdtl_data->g_pri_size[ fdtl_data->g_pri_count ] );
                          fdtl_data->g_pri_count++;
                        }
                        else   
                        {
                           sprintf( output_message, "%sCAPRI support up to %d \n", fdtl_data->g_prefix_string, MAX_DOWNLOAD_PRI_IMAGES );
                           printf_fdtl_s( output_message );
                        }

                        break;
                    }
                    pos = strstr( fastboot_return_string, "mcf_c" ); 
                    if( pos )     
                    {
                        if( fdtl_data->g_oem_image_count < MAX_DOWNLOAD_OEM_IMAGES )
                        {
                           strcpy( fdtl_data->g_partition_name[fdtl_data->g_total_image_count], "mcf_c" );
                           //printf( "Partition name: %s \n", fdtl_data->g_partition_name[fdtl_data->g_total_image_count] );

                           get_offset_and_size( fastboot_return_string, &offset, &size );
                           fdtl_data->g_image_offset[ fdtl_data->g_total_image_count ] = offset;
                           fdtl_data->g_image_size[ fdtl_data->g_total_image_count ] = size;
                           fdtl_data->g_oem_image_count++;
                           fdtl_data->g_total_image_count++;
                        }
                        else   
                        {
                           sprintf( output_message, "%sOEM PRI image support up to %d \n", fdtl_data->g_prefix_string, MAX_DOWNLOAD_OEM_IMAGES );
                           printf_fdtl_s( output_message );
                        }
                        break;
                    }
                }
                break;

       }
    }
    //printf("..");

    //if( fastboot_fp )   pclose( fastboot_fp );
    free( fastboot_return_string );

//fastboot flash sop-hdr sop-hdr2.img
//Sending 'sop-hdr' (1 KB)                           OKAY [  0.016s]
//Writing 'sop-hdr'                                  (bootloader) image_count = 2
//(bootloader) slot_x_c:0x006F:0x5252828
//(bootloader) capri_c:0x5252897:0x9A69
  return 1;
}

int write_temp_image_file( const char *image_file_name, int offset, int size, int pri_image, char *temp_file_name, char *prefix_string )
{
    char output_message[1024];
    char read_buf[ TEMP_IMAGE_FILE_BUF_SIZE ];
    int image_size, read_size, write_size;
    FILE *image_fp, *image_temp_fp;

    image_fp = fopen( image_file_name, "rb");
    //printf("Image file open, %s \n", image_file_name );
    if( image_fp == NULL )
    {  
       sprintf( output_message, "\n%sImage file not existed, %s \n", prefix_string, image_file_name );
       printf_fdtl_s( output_message );
       return -1;
    }
    if( pri_image )      image_temp_fp = fopen( temp_file_name, "ab");
    else    image_temp_fp = fopen( temp_file_name, "wb");

    if( image_temp_fp == NULL )
    {  
       sprintf( output_message, "%sImage temp file open error\n", prefix_string );
       printf_fdtl_s( output_message );
       return -2;
    }
    if( pri_image )    
    {
        //printf("seek to pri image: %d, size: %d \n", offset, size );
        if( fseek( image_fp, offset, SEEK_SET) > 0 )
        {
            fclose( image_temp_fp );
            fclose( image_fp );

            sprintf( output_message, "%sPRI image file seek error, %s \n", prefix_string, image_file_name );
            printf_fdtl_s( output_message );
            return -3;
        }
        image_size = size;
    }    
    else
    {
        //printf("seek to fw or oem image: %d, size: %d \n", offset, size );
        if( fseek( image_fp, offset, SEEK_SET) > 0 )
        {
            fclose( image_temp_fp );
            fclose( image_fp );

            sprintf( output_message, "%sImage file seek error, %s \n", prefix_string, image_file_name );
            printf_fdtl_s( output_message );
            return -3;
        }
        image_size = size;
    }

    while( image_size > 0 )
    {
       if( image_size < TEMP_IMAGE_FILE_BUF_SIZE )
             read_size = fread( read_buf, 1, image_size, image_fp );
       else        read_size = fread( read_buf, 1, TEMP_IMAGE_FILE_BUF_SIZE, image_fp );

       //printf("Wrire temp image size: %d \n", read_size );

       if( (read_size > 0) && (read_size <= TEMP_IMAGE_FILE_BUF_SIZE) )
       {  
           write_size = fwrite( read_buf, 1, read_size, image_temp_fp );
           if( write_size != read_size )
           {  
               sprintf( output_message, "%sWrite image temp file error\n", prefix_string );
               printf_fdtl_s( output_message );
               fclose( image_temp_fp );
               fclose( image_fp );

               return -4;
           }
           image_size -= read_size;
       }
       else            
       {  
           sprintf( output_message, "%sRead image file error\n", prefix_string );
           printf_fdtl_s( output_message );
           fclose( image_temp_fp );
           fclose( image_fp );

           return -5;
       }

    }
    fclose( image_temp_fp );
    fclose( image_fp );
    //printf("Write image temp file success \n");

    return 1;

}

