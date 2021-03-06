/*
* BSD 3-Clause License
*
* Copyright (c) 2017, Dolby Laboratories
* All rights reserved.
* 
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
* 
* * Redistributions of source code must retain the above copyright notice, this
*   list of conditions and the following disclaimer.
*
* * Redistributions in binary form must reproduce the above copyright notice,
*   this list of conditions and the following disclaimer in the documentation
*   and/or other materials provided with the distribution.
*
* * Neither the name of the copyright holder nor the names of its
*   contributors may be used to endorse or promote products derived from
*   this software without specific prior written permission.
* 
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "hevc_enc_api.h"
#include "hevc_enc_ffmpeg_utils.h"
#include <fstream>

#define MAX_BUFFERED_PLANES 12

static
const
property_info_t hevc_enc_ffmpeg_info[] =
{
    { "max_pass_num", PROPERTY_TYPE_INTEGER, "Indicates how many passes encoder can perform (0 = unlimited).", "2", NULL, 0, 1, ACCESS_TYPE_READ }
    , { "max_output_data", PROPERTY_TYPE_INTEGER, "Limits number of output bytes (0 = unlimited).", "0", NULL, 0, 1, ACCESS_TYPE_WRITE }
    , { "bit_depth", PROPERTY_TYPE_STRING, NULL, NULL, "8:10", 1, 1, ACCESS_TYPE_WRITE_INIT }
    , { "width", PROPERTY_TYPE_INTEGER, NULL, NULL, NULL, 1, 1, ACCESS_TYPE_WRITE_INIT }
    , { "height", PROPERTY_TYPE_INTEGER, NULL, NULL, NULL, 1, 1, ACCESS_TYPE_WRITE_INIT }
    , { "color_space", PROPERTY_TYPE_STRING, NULL, "i420", "i400:i420:i422:i444", 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "frame_rate", PROPERTY_TYPE_DECIMAL, NULL, NULL, "23.976:24:25:29.97:30:48:50:59.94:60", 1, 1, ACCESS_TYPE_WRITE_INIT }
    , { "data_rate", PROPERTY_TYPE_INTEGER, "Average data rate in kbps.", "15000", NULL, 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "max_vbv_data_rate", PROPERTY_TYPE_INTEGER, "Max VBV data rate in kbps.", "15000", NULL, 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "vbv_buffer_size", PROPERTY_TYPE_INTEGER, "VBV buffer size in kb.", "30000", NULL, 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "range", PROPERTY_TYPE_STRING, NULL, "full", "limited:full", 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "absolute_pass_num", PROPERTY_TYPE_INTEGER, NULL, "0", NULL, 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "stats_file", PROPERTY_TYPE_STRING, NULL, NULL, NULL, 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "multi_pass", PROPERTY_TYPE_STRING, NULL, "off", "off:1st:nth:last", 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "temp_file_num", PROPERTY_TYPE_INTEGER, "Indicates how many temp files this plugin requires.", "3", NULL, 0, 1, ACCESS_TYPE_READ}
    , { "temp_file", PROPERTY_TYPE_INTEGER, "Path to temp file.", NULL, NULL, 2, 2, ACCESS_TYPE_WRITE_INIT }

    // generate_vui_param_videosignaltype
    , { "color_primaries", PROPERTY_TYPE_STRING, NULL, "unspecified", "unspecified:bt_709:bt_601_625:bt_601_525:bt_2020", 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "transfer_characteristics", PROPERTY_TYPE_STRING, NULL, "unspecified", "unspecified:bt_709:bt_601_625:bt_601_525:smpte_st_2084:std_b67", 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "matrix_coefficients", PROPERTY_TYPE_STRING, NULL, "unspecified", "unspecified:bt_709:bt_601_625:bt_601_525:bt_2020", 0, 1, ACCESS_TYPE_WRITE_INIT }

    // generate_mastering_display_color_volume_sei
    , { "mastering_display_sei_x1", PROPERTY_TYPE_INTEGER, "First primary x.", "0", "0:50000", 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "mastering_display_sei_y1", PROPERTY_TYPE_INTEGER, "First primary y.", "0", "0:50000", 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "mastering_display_sei_x2", PROPERTY_TYPE_INTEGER, "Second primary x.", "0", "0:50000", 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "mastering_display_sei_y2", PROPERTY_TYPE_INTEGER, "Second primary y.", "0", "0:50000", 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "mastering_display_sei_x3", PROPERTY_TYPE_INTEGER, "Third primary x.", "0", "0:50000", 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "mastering_display_sei_y3", PROPERTY_TYPE_INTEGER, "Third primary y.", "0", "0:50000", 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "mastering_display_sei_wx", PROPERTY_TYPE_INTEGER, "White point x.", "0", "0:50000", 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "mastering_display_sei_wy", PROPERTY_TYPE_INTEGER, "White point y.", "0", "0:50000", 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "mastering_display_sei_max_lum", PROPERTY_TYPE_INTEGER, "Maximum display luminance.", "0", "0:2000000000", 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "mastering_display_sei_min_lum", PROPERTY_TYPE_INTEGER, "Minimum display luminance.", "0", "0:2000000000", 0, 1, ACCESS_TYPE_WRITE_INIT }

    // generate_light_level_information_sei
    , { "light_level_max_content", PROPERTY_TYPE_INTEGER, NULL, "0", "0:65535", 0, 1, ACCESS_TYPE_WRITE_INIT }
    , { "light_level_max_frame_average", PROPERTY_TYPE_INTEGER, NULL, "0", "0:65535", 0, 1, ACCESS_TYPE_WRITE_INIT }

    // Only properties below (ACCESS_TYPE_USER) can be modified 
    , { "ffmpeg_bin", PROPERTY_TYPE_STRING, "Path to ffmpeg binary.", "ffmpeg", NULL, 0, 1, ACCESS_TYPE_USER }
    , { "command_line", PROPERTY_TYPE_STRING, "Command line to be inserted into the ffmpeg command between the input and output specification.", NULL, NULL, 0, 100, ACCESS_TYPE_USER }
    , { "cmd_gen", PROPERTY_TYPE_STRING, "Path to a script that can generate an ffmpeg command line using the config file as input.", NULL, NULL, 0, 1, ACCESS_TYPE_USER }
    , { "interpreter", PROPERTY_TYPE_STRING, "Path to binary used to read the cmd_gen script.", "python", NULL, 0, 1, ACCESS_TYPE_USER }
};

static
size_t
ffmpeg_get_info
(const property_info_t** info
)
{
    *info = hevc_enc_ffmpeg_info;
    return sizeof(hevc_enc_ffmpeg_info) / sizeof(property_info_t);
}

static
size_t
ffmpeg_get_size()
{
    return sizeof(hevc_enc_ffmpeg_t);
}

static
status_t
ffmpeg_init
(hevc_enc_handle_t handle
, const hevc_enc_init_params_t* init_params
)
{
    hevc_enc_ffmpeg_t* state = (hevc_enc_ffmpeg_t*)handle;
    state->data = new hevc_enc_ffmpeg_data_t;
    init_defaults(state);
    state->lib_initialized = false;
    state->data->msg.clear();

    if (!parse_init_params(state, init_params))
    {
        std::string errmsg = "Parsing init params failed:" + state->data->msg;
        state->data->msg = errmsg;
        return STATUS_ERROR;
    }

    if (0 == state->data->max_output_data)
    {
        state->data->max_output_data = SIZE_MAX;
    }

    if (!create_pipes(state->data))
    {
        state->data->msg = "Creating pipes failed.";
        return STATUS_ERROR;
    }

    std::string ffmpeg_call;

    if (state->data->command_line.size() > (unsigned int)state->data->pass_num && state->data->command_line[state->data->pass_num].empty() == false)
    {
        ffmpeg_call = state->data->ffmpeg_bin;

        // input format
        ffmpeg_call += " -f rawvideo";

        // video size
        ffmpeg_call += " -s " + std::to_string(state->data->width) + "x" + std::to_string(state->data->height);

        // picture format
        ffmpeg_call += " -pix_fmt " + state->data->color_space;
        if (state->data->bit_depth == 10)
        {
            ffmpeg_call += "10le";
        }

        // frame rate
        ffmpeg_call += " -framerate " + state->data->frame_rate;

        // INPUT
        ffmpeg_call += " -i ";
        ffmpeg_call += state->data->temp_file[0];

        // user specified commandline
        ffmpeg_call += " " + state->data->command_line[state->data->pass_num];

        // disable audio
        ffmpeg_call += " -an";

        // overwrite file if present
        ffmpeg_call += " -y";

        // OUTPUT
        ffmpeg_call += " -f hevc ";
        ffmpeg_call += state->data->temp_file[1];
    }
    else
    {
        if (write_cfg_file(state->data, state->data->temp_file[2]) == false)
        {
            state->data->msg = "Error writing encoding parameters to file: " + state->data->temp_file[2];
            return STATUS_ERROR;
        }

        if (state->data->cmd_gen.empty())
        {
            state->data->msg = "No command line or command generation script provided. Use \'cmd_gen\' or \'command_line\' XML parameter.";
            return STATUS_ERROR;
        }

        std::string generate_cmd_call = state->data->interpreter + " " + state->data->cmd_gen + " " + state->data->temp_file[2];
        ffmpeg_call = run_cmd_get_output(generate_cmd_call);
        
        if (ffmpeg_call.empty())
        {
            state->data->msg = "Error building ffmpeg command line by calling: " + generate_cmd_call;
            return STATUS_ERROR;
        }
    }
    
    state->data->ffmpeg_thread = std::thread(run_cmd_thread_func, ffmpeg_call, state->data);
    state->data->writer_thread = std::thread(writer_thread_func, state->data);
    state->data->reader_thread = std::thread(reader_thread_func, state->data);

    return STATUS_OK;
}

static
status_t
ffmpeg_close
(hevc_enc_handle_t handle
)
{
    hevc_enc_ffmpeg_t* state = (hevc_enc_ffmpeg_t*)handle;

    for (unsigned int i = 0; i < state->data->nalus.size(); i++)
    {
        free(state->data->nalus[i].payload);
    }
    state->data->nalus.clear();

    if (state->data->ffmpeg_ret_code != 0)
    {
        state->data->force_stop_writing_thread = true;
        state->data->force_stop_reading_thread = true;
#if WIN32
        CancelIoEx(state->data->in_pipe, NULL);
        CancelIoEx(state->data->out_pipe, NULL);
#else        
        pthread_t writer_id = state->data->writer_thread.native_handle();
        pthread_t reader_id = state->data->reader_thread.native_handle();
        pthread_cancel(writer_id);
        pthread_cancel(reader_id);
#endif
    }

    if (state->data->writer_thread.joinable())
    {
        state->data->writer_thread.join();
    }
    if (state->data->ffmpeg_thread.joinable())
    {
        state->data->ffmpeg_thread.join();
    }
    if (state->data->reader_thread.joinable())
    {
        state->data->reader_thread.join();
    }

    close_pipes(state->data);
    delete state->data;

    return STATUS_OK;
}

static
status_t
ffmpeg_process
(hevc_enc_handle_t          handle
, const hevc_enc_picture_t*  picture
, const size_t               picture_num
, hevc_enc_output_t*         output
)
{
    hevc_enc_ffmpeg_t* state = (hevc_enc_ffmpeg_t*)handle;
 
    if (state->data->ffmpeg_ret_code != 0)
    {
        state->data->msg += "\nffmpeg runtime error.";
        return STATUS_ERROR;
    }

    for (unsigned int i = 0; i < picture_num; i++)
    {
        hevc_enc_picture_t current_pic = picture[i];

        int byte_num = current_pic.bit_depth == 8 ? 1 : 2;
        size_t plane_0_size = current_pic.width * current_pic.height * byte_num;
        size_t plane_1_size = plane_0_size;
        size_t plane_2_size = plane_0_size;

        if (current_pic.color_space == COLOR_SPACE_I420)
        {
            plane_1_size /= 4;
            plane_2_size /= 4;
        }
        else if (current_pic.color_space == COLOR_SPACE_I422)
        {
            plane_1_size /= 2;
            plane_2_size /= 2;
        }
        else if (current_pic.color_space != COLOR_SPACE_I444)
        {
            return STATUS_ERROR;
        }

        bool picture_written_flag = false;
        while (state->data->ffmpeg_thread.joinable() && picture_written_flag == false)
        {
            state->data->in_buffer_mutex.lock();
            if (state->data->in_buffer.size() < MAX_BUFFERED_PLANES)
            {
                state->data->in_buffer.push_back(new BufferBlob(current_pic.plane[0], plane_0_size));
                state->data->in_buffer.push_back(new BufferBlob(current_pic.plane[1], plane_1_size));
                state->data->in_buffer.push_back(new BufferBlob(current_pic.plane[2], plane_2_size));
                picture_written_flag = true;
            }
            state->data->in_buffer_mutex.unlock();
        }
    }
    
    state->data->out_buffer_mutex.lock();
    while (state->data->out_buffer.size() > 0)
    {
        BufferBlob* out_blob = state->data->out_buffer.front();
        state->data->output_bytestream.insert(state->data->output_bytestream.end(), out_blob->data, out_blob->data + out_blob->data_size);
        delete out_blob;
        state->data->out_buffer.pop_front();
    }
    state->data->out_buffer_mutex.unlock();

    for (unsigned int i = 0; i < state->data->nalus.size(); i++)
    {
        free(state->data->nalus[i].payload);
    }
    state->data->nalus.clear();

    if (get_aud_from_bytestream(state->data->output_bytestream, state->data->nalus, false, state->data->max_output_data) == true)
    {
        output->nal = state->data->nalus.data();
        output->nal_num = state->data->nalus.size();
    }
    else
    {
        output->nal = NULL;
        output->nal_num = 0;
    }

    return STATUS_OK;
}

static
status_t
ffmpeg_flush
(hevc_enc_handle_t      handle
, hevc_enc_output_t*    output
, int*                  is_empty
)
{
    hevc_enc_ffmpeg_t* state = (hevc_enc_ffmpeg_t*)handle;

    state->data->stop_writing_thread = true;
    if (state->data->writer_thread.joinable())
    {
        state->data->writer_thread.join();
    }
    
    if (state->data->ffmpeg_thread.joinable())
    {
        state->data->ffmpeg_thread.join();
    }
    
    state->data->stop_reading_thread = true;
    if (state->data->reader_thread.joinable())
    {
        state->data->reader_thread.join();
    }

    *is_empty = 0;

    while (state->data->out_buffer.size() > 0)
    {
        BufferBlob* out_blob = state->data->out_buffer.front();
        state->data->output_bytestream.insert(state->data->output_bytestream.end(), out_blob->data, out_blob->data + out_blob->data_size);
        delete out_blob;
        state->data->out_buffer.pop_front();
    }

    for (unsigned int i = 0; i < state->data->nalus.size(); i++)
    {
        free(state->data->nalus[i].payload);
    }
    state->data->nalus.clear();

    if (get_aud_from_bytestream(state->data->output_bytestream, state->data->nalus, true, state->data->max_output_data) == true)
    {
        output->nal = state->data->nalus.data();
        output->nal_num = state->data->nalus.size();
    }
    else
    {
        output->nal = NULL;
        output->nal_num = 0;
        *is_empty = 1;
    }
    

    return STATUS_OK;
}

static
status_t
ffmpeg_set_property
(hevc_enc_handle_t handle
, const property_t* property
)
{
    hevc_enc_ffmpeg_t* state = (hevc_enc_ffmpeg_t*)handle;
    
    if (property->name == std::string("max_output_data"))
    {
        state->data->max_output_data = atoi(property->value);
        return STATUS_OK;
    }
    
    return STATUS_ERROR;
}

static
status_t
ffmpeg_get_property
(hevc_enc_handle_t handle
, property_t* property
)
{
    hevc_enc_ffmpeg_t* state = (hevc_enc_ffmpeg_t*)handle;

    if (NULL != property->name)
    {
        std::string name(property->name);
        if ("max_pass_num" == name)
        {
            if (state->lib_initialized == true)
            {
                strcpy(property->value, std::to_string(state->data->max_pass_num).c_str());
            }
            else
            {
                strcpy(property->value, "2");
            }
            return STATUS_OK;
        }
        else if ("temp_file_num" == name)
        {
            strcpy(property->value, "3");
            return STATUS_OK;
        }
    }
    
    return STATUS_ERROR;
}

static
const char*
ffmpeg_get_message
(hevc_enc_handle_t handle
)
{
    hevc_enc_ffmpeg_t* state = (hevc_enc_ffmpeg_t*)handle;
    return state->data->msg.empty() ? NULL : state->data->msg.c_str();
}

static
hevc_enc_api_t ffmpeg_plugin_api =
{
    "ffmpeg"
    , ffmpeg_get_info
    , ffmpeg_get_size
    , ffmpeg_init
    , ffmpeg_close
    , ffmpeg_process
    , ffmpeg_flush
    , ffmpeg_set_property
    , ffmpeg_get_property
    , ffmpeg_get_message
};

DLB_EXPORT
hevc_enc_api_t* hevc_enc_get_api()
{
    return &ffmpeg_plugin_api;
}
