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

#include <cstdlib>
#include <map>
#include <fstream>
#include <stdexcept>
#include "hevc_enc_ffmpeg_utils.h"

#define READ_BUFFER_SIZE 1024
#define AUD_NAL_UNIT_TYPE 35

static
std::map<std::string, int> color_primaries_map = {
    { "bt_709", 1 },
    { "unspecified", 2 },
    { "bt_601_625", 5 },
    { "bt_601_525", 6 },
    { "bt_2020", 9 },
};

static
std::map<std::string, int> transfer_characteristics_map = {
    { "bt_709", 1 },
    { "unspecified", 2 },
    { "bt_601_625", 4 },
    { "bt_601_525", 6 },
    { "smpte_st_2084", 16 },
    { "std_b67", 18 },
};
 
static
std::map<std::string, int> matrix_coefficients_map = {
    { "bt_709", 1 },
    { "unspecified", 2 },
    { "bt_601_625", 4 },
    { "bt_601_525", 6 },
    { "bt_2020", 9 },
};

BufferBlob::BufferBlob(void* data_to_copy, size_t size)
{
    data = new char[size];
    data_size = size;
    memcpy(data, data_to_copy, size);
}

BufferBlob::~BufferBlob()
{
    delete[] data;
}

void
init_defaults(hevc_enc_ffmpeg_t* state)
{
    state->data->bit_depth = 0;         /**< Must be set by caller */
    state->data->width = 0;             /**< Must be set by caller */
    state->data->height = 0;            /**< Must be set by caller */
    state->data->frame_rate.clear();    /**< Must be set by caller */
    state->data->color_space = "yuv420p";
    state->data->range = "full";
    state->data->pass_num = 0;
    state->data->data_rate = 15000;
    state->data->max_output_data = 0;

    state->data->light_level_information_sei_present = false;
    state->data->light_level_max_content = 0;
    state->data->light_level_max_frame_average = 0;

    state->data->color_description_present = false;
    state->data->color_primaries = "unspecified";
    state->data->transfer_characteristics = "unspecified";
    state->data->matrix_coefficients = "unspecified";

    state->data->mastering_display_sei_present = false;
    state->data->mastering_display_sei_x1 = 0;
    state->data->mastering_display_sei_y1 = 0;
    state->data->mastering_display_sei_x2 = 0;
    state->data->mastering_display_sei_y2 = 0;
    state->data->mastering_display_sei_x3 = 0;
    state->data->mastering_display_sei_y3 = 0;
    state->data->mastering_display_sei_wx = 0;
    state->data->mastering_display_sei_wy = 0;
    state->data->mastering_display_sei_max_lum = 0;
    state->data->mastering_display_sei_min_lum = 0;

    state->data->multi_pass = "off";
    state->data->stats_file = "";

    state->data->max_pass_num = 2;

#ifdef WIN32
    state->data->in_pipe = INVALID_HANDLE_VALUE;
    state->data->out_pipe = INVALID_HANDLE_VALUE;
    state->data->ffmpeg_bin = "ffmpeg.exe";
    state->data->interpreter = "python.exe";
#else
    state->data->in_pipe = 0;
    state->data->out_pipe = 0;
    state->data->ffmpeg_bin = "ffmpeg";
    state->data->interpreter = "python";
#endif
    state->data->cmd_gen = "";

    state->data->stop_writing_thread = false;
    state->data->stop_reading_thread = false;
    state->data->force_stop_writing_thread = false;
    state->data->force_stop_reading_thread = false;

    state->data->ffmpeg_ret_code = 0;
}

std::string 
run_cmd_get_output(std::string cmd) 
{
    char buffer[READ_BUFFER_SIZE];
    std::string result;
#ifdef WIN32
    std::shared_ptr<FILE> pipe(_popen(cmd.c_str(), "r"), _pclose);
#else
    std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
#endif
    if (!pipe)
    {
        return "";
    }
    while (!feof(pipe.get())) 
    {
        if (fgets(buffer, READ_BUFFER_SIZE, pipe.get()) != NULL)
            result += buffer;
    }
    return result;
}

void 
run_cmd_thread_func(std::string cmd, hevc_enc_ffmpeg_data_t* encoding_data)
{
    int ret_code = system(cmd.c_str());
    encoding_data->ffmpeg_ret_code = ret_code;
}

void 
writer_thread_func(hevc_enc_ffmpeg_data_t* encoding_data)
{
#ifdef WIN32
    if (ConnectNamedPipe(encoding_data->in_pipe, NULL) == FALSE)
    {
        return;
    }
#else
    if ((encoding_data->in_pipe = open(encoding_data->temp_file[0].c_str(), O_WRONLY)) == -1)
    {
        return;
    }
#endif
    
    while (encoding_data->stop_writing_thread == false || encoding_data->in_buffer.size() > 0)
    {
        if (encoding_data->force_stop_writing_thread)
        {
            break;
        }

        encoding_data->in_buffer_mutex.lock();
        if (encoding_data->in_buffer.empty())
        {
            encoding_data->in_buffer_mutex.unlock();
            continue;
        }
        BufferBlob* front = encoding_data->in_buffer.front();
        encoding_data->in_buffer_mutex.unlock();
        
        char* data_to_write = front->data;
        size_t data_size = front->data_size;
        size_t left_to_write = data_size;

#ifdef WIN32
        DWORD bytes_written;
        while (left_to_write > 0)
        {
            WriteFile(encoding_data->in_pipe, data_to_write, (DWORD)left_to_write, &bytes_written, NULL);
            left_to_write -= bytes_written;
            data_to_write += bytes_written;
            if (bytes_written == 0) break;
        }
#else
        ssize_t bytes_written;
        while (left_to_write > 0)
        {
            bytes_written = write(encoding_data->in_pipe, data_to_write, left_to_write);
            left_to_write -= bytes_written;
            data_to_write += bytes_written;
            if (bytes_written == 0) break;
        }
#endif
        if (left_to_write == 0)
        {
            encoding_data->in_buffer_mutex.lock();
            delete front;
            encoding_data->in_buffer.pop_front();
            encoding_data->in_buffer_mutex.unlock();
        }
    }
    
#ifdef WIN32
    CloseHandle(encoding_data->in_pipe);
#else
    if (encoding_data->in_pipe != 0)
    {
        close(encoding_data->in_pipe);
    }
#endif
}

void 
reader_thread_func(hevc_enc_ffmpeg_data_t* encoding_data)
{
#ifdef WIN32
    if (ConnectNamedPipe(encoding_data->out_pipe, NULL) == FALSE)
    {
        return;
    }
#else
    if ((encoding_data->out_pipe = open(encoding_data->temp_file[1].c_str(), O_RDONLY)) == -1)
    {
        return;
    }
#endif
    
    size_t last_read_size = 0;

    while (encoding_data->stop_reading_thread == false || last_read_size > 0)
    {
        if (encoding_data->force_stop_reading_thread)
        {
            break;
        }

        char read_data[READ_BUFFER_SIZE];

#ifdef WIN32
        DWORD bytes_read;
        ReadFile(encoding_data->out_pipe, read_data, READ_BUFFER_SIZE, &bytes_read, NULL);
        last_read_size = bytes_read;
#else
        last_read_size = read(encoding_data->out_pipe, read_data, READ_BUFFER_SIZE);
#endif

        if (last_read_size > 0)
        {
            encoding_data->out_buffer_mutex.lock();
            encoding_data->out_buffer.push_back(new BufferBlob(read_data, last_read_size));
            encoding_data->out_buffer_mutex.unlock();
        }
    }
    
#ifdef WIN32
    CloseHandle(encoding_data->out_pipe);
#else
    if (encoding_data->out_pipe != 0)
    {
        close(encoding_data->out_pipe);
    }
#endif
}

bool 
get_aud_from_bytestream(std::vector<char> &bytestream, std::vector<hevc_enc_nal_t> &nalus, bool flush, size_t max_data)
{
    unsigned int pos = 0;

    std::vector<int> nalu_start;
    std::vector<int> nalu_end;
    unsigned int aud_end = 0;
    bool aud_open = false;
    bool nalu_open = false;

    // first nalu starts right away because we dont want to skip any input bytes
    nalu_start.push_back(pos);
    nalu_open = true;

    // seek AU start
    while (pos < bytestream.size() && aud_open == false)
    {
        if (bytestream[pos + 0] == 0 && bytestream[pos + 1] == 0 && bytestream[pos + 2] == 0 && bytestream[pos + 3] == 1)
        {
            unsigned char first_nalu_byte = bytestream[pos + 4];
            unsigned char nal_unit_type = first_nalu_byte >> 1;

            if (nal_unit_type == AUD_NAL_UNIT_TYPE)
            {
                pos += 4; // the size of the aud prefix
                aud_open = true;
            }
        }
        else
        {
            pos += 1;
        }
    }

    if (aud_open == false || pos >= max_data)
    {
        return false;
    }

    // mark NAL start and end positions within the AU
    while (pos < bytestream.size() && aud_open == true)
    {
        if (bytestream[pos + 0] == 0 && bytestream[pos + 1] == 0 && bytestream[pos + 2] == 1)
        {
            nalu_end.push_back(pos);
            nalu_start.push_back(pos);
            pos += 3; // the size of start_code_prefix_one_3bytes
        }
        else if (bytestream[pos + 0] == 0 && bytestream[pos + 1] == 0 && bytestream[pos + 2] == 0 && bytestream[pos + 3] == 1)
        {
            unsigned char first_nalu_byte = bytestream[pos + 4];
            unsigned char nal_unit_type = first_nalu_byte >> 1;

            if (nal_unit_type == AUD_NAL_UNIT_TYPE)
            {
                nalu_end.push_back(pos);
                aud_end = pos;
                aud_open = false;
            }
            else
            {
                nalu_end.push_back(pos);
                nalu_start.push_back(pos);
                pos += 4;
            }
        }
        else
        {
            pos += 1;
        }
    }
    
    if (flush && aud_open)
    {
        aud_open = false;
        aud_end = pos;
        nalu_end.push_back(pos);
    }
    if (flush && nalu_end.empty())
    {
        nalu_end.push_back(pos);
    }

    if (aud_open == true || aud_end > max_data)
    {
        return false;
    }

    if (nalu_start.size() != nalu_end.size())
    {
        // ERROR parsing the AU
        return false;
    }

    for (unsigned int nal_idx = 0; nal_idx < nalu_start.size(); nal_idx++)
    {
        hevc_enc_nal_t nal;

        if (nal_idx == 0)
        {
            nal.type = HEVC_ENC_NAL_UNIT_ACCESS_UNIT_DELIMITER;
        }
        else
        {
            nal.type = HEVC_ENC_NAL_UNIT_OTHER;
        }

        nal.size = nalu_end[nal_idx] - nalu_start[nal_idx];
        nal.payload = malloc(nal.size);
        memcpy(nal.payload, bytestream.data() + nalu_start[nal_idx], nal.size);

        nalus.push_back(nal);
    }

    bytestream.erase(bytestream.begin(), bytestream.begin() + aud_end);

    return true;
}

static bool
bin_exists(const std::string& bin, const std::string& arg)
{
    std::string cmd = bin + " " + arg;
    int rt = system(cmd.c_str());
    return (rt == 0);
}

bool
parse_init_params
(hevc_enc_ffmpeg_t*              state
,const hevc_enc_init_params_t*   init_params
)
{
    state->data->msg.clear();
    for (unsigned int i = 0; i < init_params->count; i++)
    {
        std::string name(init_params->property[i].name);
        std::string value(init_params->property[i].value);

        if ("bit_depth" == name)
        {
            if (value != "8" && value != "10")
            {
                state->data->msg += "\nInvalid 'bit_depth' value.";
                continue;
            }
            state->data->bit_depth = std::stoi(value);
        }
        else if ("width" == name)
        {
            int width = std::stoi(value);
            if (width < 0)
            {
                state->data->msg += "\nInvalid 'width' value.";
                continue;
            }
            state->data->width = width;
        }
        else if  ("ffmpeg_bin" == name)
        {
            state->data->ffmpeg_bin = value;
        }
        else if ("interpreter" == name)
        {
            state->data->interpreter = value;
        }
        else if ("cmd_gen" == name)
        {
            state->data->cmd_gen = value;
        }
        else if ("multi_pass" == name)
        {
            if (value != "off"
                && value != "1st"
                && value != "nth"
                && value != "last"
                )
            {
                state->data->msg += "\nInvalid 'multi_pass' value.";
                continue;
            }
            state->data->multi_pass = value;
        }
        else if ("stats_file" == name)
        {
            state->data->stats_file = value;
        }
        else if ("temp_file" == name)
        {
            state->data->temp_file.push_back(value);
        }
        else if ("height" == name)
        {
            int height = std::stoi(value);
            if (height < 0)
            {
                state->data->msg += "\nInvalid 'height' value.";
                continue;
            }
            state->data->height = height;
        }
        else if ("color_space" == name)
        {
            if (value == "i420")
            {
                state->data->color_space = "yuv420p";
            }
            else if (value == "i422")
            {
                state->data->color_space = "yuv422p";
            }
            else if (value == "i444")
            {
                state->data->color_space = "yuv444p";
            }
            else
            {
                state->data->msg += "\nInvalid 'color_space' value.";
                continue;
            }
        }
        else if ("frame_rate" == name)
        {
            if (value != "23.976"
                &&  value != "24"
                &&  value != "25"
                &&  value != "29.97"
                &&  value != "30"
                &&  value != "48"
                &&  value != "50"
                &&  value != "59.94"
                &&  value != "60"
                )
            {
                state->data->msg += "\nInvalid 'frame_rate' value.";
                continue;
            }
            state->data->frame_rate = value;
        }
        else if ("range" == name)
        {
            if (value != "limited" && value != "full")
            {
                state->data->msg += "\nInvalid 'range' value.";
                continue;
            }
            state->data->range = value;
        }
        else if ("absolute_pass_num" == name)
        {
            int pass_num = std::stoi(value);
            if (pass_num < 0)
            {
                state->data->msg += "\nInvalid 'pass_num' value.";
                continue;
            }
            state->data->pass_num = pass_num;
        }
        else if ("data_rate" == name)
        {
            int data_rate = std::stoi(value);
            if (data_rate < 0)
            {
                state->data->msg += "\nInvalid 'data_rate' value.";
                continue;
            }
            state->data->data_rate = data_rate;
        }
        else if ("max_vbv_data_rate" == name)
        {
            int max_vbv_data_rate = std::stoi(value);
            if (max_vbv_data_rate < 0)
            {
                state->data->msg += "\nInvalid 'max_vbv_data_rate' value.";
                continue;
            }
            state->data->max_vbv_data_rate = max_vbv_data_rate;
        }
        else if ("vbv_buffer_size" == name)
        {
            int vbv_buffer_size = std::stoi(value);
            if (vbv_buffer_size < 0)
            {
                state->data->msg += "\nInvalid 'vbv_buffer_size' value.";
                continue;
            }
            state->data->vbv_buffer_size = vbv_buffer_size;
        }
        else if ("color_primaries" == name)
        {
            auto it = color_primaries_map.find(value);
            if (it == color_primaries_map.end())
            {
                state->data->msg += "\nInvalid 'color_primaries' value.";
                continue;
            }
            state->data->color_description_present = true;
            state->data->color_primaries = value;
        }
        else if ("transfer_characteristics" == name)
        {
            auto it = transfer_characteristics_map.find(value);
            if (it == transfer_characteristics_map.end())
            {
                state->data->msg += "\nInvalid 'transfer_characteristics' value.";
                continue;
            }
            state->data->color_description_present = true;
            state->data->transfer_characteristics = value;
        }
        else if ("matrix_coefficients" == name)
        {
            auto it = matrix_coefficients_map.find(value);
            if (it == matrix_coefficients_map.end())
            {
                state->data->msg += "\nInvalid 'matrix_coefficients' value.";
                continue;
            }
            state->data->color_description_present = true;
            state->data->matrix_coefficients = value;
        }
        // generate_mastering_display_color_volume_sei
        else if ("mastering_display_sei_x1" == name)
        {
            int mastering_display_sei_x1 = std::stoi(value);
            if (mastering_display_sei_x1 < 0 || mastering_display_sei_x1 > 50000)
            {
                state->data->msg += "\nInvalid 'mastering_display_sei_x1' value.";
                continue;
            }
            state->data->mastering_display_sei_present = true;
            state->data->mastering_display_sei_x1 = mastering_display_sei_x1;
        }
        else if ("mastering_display_sei_y1" == name)
        {
            int mastering_display_sei_y1 = std::stoi(value);
            if (mastering_display_sei_y1 < 0 || mastering_display_sei_y1 > 50000)
            {
                state->data->msg += "\nInvalid 'mastering_display_sei_y1' value.";
                continue;
            }
            state->data->mastering_display_sei_present = true;
            state->data->mastering_display_sei_y1 = mastering_display_sei_y1;
        }
        else if ("mastering_display_sei_x2" == name)
        {
            int mastering_display_sei_x2 = std::stoi(value);
            if (mastering_display_sei_x2 < 0 || mastering_display_sei_x2 > 50000)
            {
                state->data->msg += "\nInvalid 'mastering_display_sei_x2' value.";
                continue;
            }
            state->data->mastering_display_sei_present = true;
            state->data->mastering_display_sei_x2 = mastering_display_sei_x2;
        }
        else if ("mastering_display_sei_y2" == name)
        {
            int mastering_display_sei_y2 = std::stoi(value);
            if (mastering_display_sei_y2 < 0 || mastering_display_sei_y2 > 50000)
            {
                state->data->msg += "\nInvalid 'mastering_display_sei_y2' value.";
                continue;
            }
            state->data->mastering_display_sei_present = true;
            state->data->mastering_display_sei_y2 = mastering_display_sei_y2;
        }
        else if ("mastering_display_sei_x3" == name)
        {
            int mastering_display_sei_x3 = std::stoi(value);
            if (mastering_display_sei_x3 < 0 || mastering_display_sei_x3 > 50000)
            {
                state->data->msg += "\nInvalid 'mastering_display_sei_x3' value.";
                continue;
            }
            state->data->mastering_display_sei_present = true;
            state->data->mastering_display_sei_x3 = mastering_display_sei_x3;
        }
        else if ("mastering_display_sei_y3" == name)
        {
            int mastering_display_sei_y3 = std::stoi(value);
            if (mastering_display_sei_y3 < 0 || mastering_display_sei_y3 > 50000)
            {
                state->data->msg += "\nInvalid 'mastering_display_sei_y3' value.";
                continue;
            }
            state->data->mastering_display_sei_present = true;
            state->data->mastering_display_sei_y3 = mastering_display_sei_y3;
        }
        else if ("mastering_display_sei_wx" == name)
        {
            int mastering_display_sei_wx = std::stoi(value);
            if (mastering_display_sei_wx < 0 || mastering_display_sei_wx > 50000)
            {
                state->data->msg += "\nInvalid 'mastering_display_sei_wx' value.";
                continue;
            }
            state->data->mastering_display_sei_present = true;
            state->data->mastering_display_sei_wx = mastering_display_sei_wx;
        }
        else if ("mastering_display_sei_wy" == name)
        {
            int mastering_display_sei_wy = std::stoi(value);
            if (mastering_display_sei_wy < 0 || mastering_display_sei_wy > 50000)
            {
                state->data->msg += "\nInvalid 'mastering_display_sei_wy' value.";
                continue;
            }
            state->data->mastering_display_sei_present = true;
            state->data->mastering_display_sei_wy = mastering_display_sei_wy;
        }
        else if ("mastering_display_sei_max_lum" == name)
        {
            int mastering_display_sei_max_lum = std::stoi(value);
            if (mastering_display_sei_max_lum < 0 || mastering_display_sei_max_lum > 2000000000)
            {
                state->data->msg += "\nInvalid 'mastering_display_sei_max_lum' value.";
                continue;
            }
            state->data->mastering_display_sei_present = true;
            state->data->mastering_display_sei_max_lum = mastering_display_sei_max_lum;
        }
        else if ("mastering_display_sei_min_lum" == name)
        {
            int mastering_display_sei_min_lum = std::stoi(value);
            if (mastering_display_sei_min_lum < 0 || mastering_display_sei_min_lum > 2000000000)
            {
                state->data->msg += "\nInvalid 'mastering_display_sei_min_lum' value.";
                continue;
            }
            state->data->mastering_display_sei_present = true;
            state->data->mastering_display_sei_min_lum = mastering_display_sei_min_lum;
        }
        // light_level_information_sei
        else if ("light_level_max_content" == name)
        {
            int light_level_max_content = std::stoi(value);
            if (light_level_max_content < 0 || light_level_max_content > 65535)
            {
                state->data->msg += "\nInvalid 'light_level_max_content' value.";
                continue;
            }
            state->data->light_level_information_sei_present = true;
            state->data->light_level_max_content = light_level_max_content;
        }
        else if ("light_level_max_frame_average" == name)
        {
            int light_level_max_frame_average = std::stoi(value);
            if (light_level_max_frame_average < 0 || light_level_max_frame_average > 65535)
            {
                state->data->msg += "\nInvalid 'light_level_max_frame_average' value.";
                continue;
            }
            state->data->light_level_information_sei_present = true;
            state->data->light_level_max_frame_average = light_level_max_frame_average;
        }
        else if ("command_line" == name)
        {
            state->data->command_line.push_back(value);
        }
        else
        {
            state->data->msg += "\nUnknown XML property: " + name;
        }
    }

    // Following properties are guaranteed to be set by caller,
    // thus must be handled by plugin.    
    if (0 == state->data->bit_depth)
    {
        state->data->msg += "\nMissing 'bit_depth' property.";
    }

    if (0 == state->data->width)
    {
        state->data->msg += "\nMissing 'width' property.";
    }

    if (0 == state->data->height)
    {
        state->data->msg += "\nMissing 'height' property.";
    }

    if (state->data->frame_rate.empty())
    {
        state->data->msg += "\nMissing 'frame_rate' property.";
    }
    
    if (state->data->temp_file.size() < 3)
    {
        state->data->msg += "Need more temp files.";
    }

    if (state->data->ffmpeg_bin.empty())
    {
        state->data->msg += "Path to ffmpeg binary is not set.";
    }

    if (!bin_exists(state->data->ffmpeg_bin, "-version"))
    {
        state->data->msg += "Cannot access ffmpeg binary.";
    }

    if (state->data->interpreter.empty())
    {
        state->data->msg += "Path to interpreter binary is not set.";
    }

    return state->data->msg.empty();
}

bool 
create_pipes(hevc_enc_ffmpeg_data_t* data)
{
#ifdef WIN32
    data->temp_file[0] = std::string("\\\\.\\pipe\\") + data->temp_file[0].substr(data->temp_file[0].rfind("\\") + 1);
    data->temp_file[1] = std::string("\\\\.\\pipe\\") + data->temp_file[1].substr(data->temp_file[1].rfind("\\") + 1);

    data->in_pipe = CreateNamedPipe(data->temp_file[0].c_str(),
        PIPE_ACCESS_OUTBOUND,
        PIPE_WAIT | PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
        1,
        PIPE_BUFFER_SIZE,
        PIPE_BUFFER_SIZE,
        NMPWAIT_USE_DEFAULT_WAIT,
        NULL);

    data->out_pipe = CreateNamedPipe(data->temp_file[1].c_str(),
        PIPE_ACCESS_INBOUND,
        PIPE_WAIT | PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
        1,
        PIPE_BUFFER_SIZE,
        PIPE_BUFFER_SIZE,
        NMPWAIT_USE_DEFAULT_WAIT,
        NULL);

    if (data->in_pipe == NULL || data->out_pipe == NULL)
    {
        return false;
    }
#else
    remove(data->temp_file[0].c_str());
    if (mkfifo(data->temp_file[0].c_str(), S_IWUSR | S_IRUSR | S_IWOTH | S_IROTH) != 0)
    {
        return false;
    }
    remove(data->temp_file[1].c_str());
    if (mkfifo(data->temp_file[1].c_str(), S_IWUSR | S_IRUSR | S_IWOTH | S_IROTH) != 0)
    {
        return false;
    }
#endif

    return true;
}

bool
close_pipes(hevc_enc_ffmpeg_data_t* data)
{
#ifdef WIN32
    if (data->in_pipe != INVALID_HANDLE_VALUE)
    {
        DisconnectNamedPipe(data->in_pipe);
    }
    if (data->out_pipe != INVALID_HANDLE_VALUE)
    {
        DisconnectNamedPipe(data->out_pipe);
    }
#endif
    // no need to close pipes on linux, they are just temp files removed by DEE later

    return true;
}

bool
write_cfg_file(hevc_enc_ffmpeg_data_t* data, const std::string& file)
{
    std::ofstream cfg_file(file);
    if (cfg_file.is_open())
    {
        cfg_file << "bit_depth="                        << data->bit_depth                      << "\n";
        cfg_file << "width="                            << data->width                          << "\n";
        cfg_file << "height="                           << data->height                         << "\n";
        cfg_file << "color_space="                      << data->color_space                    << "\n";
        cfg_file << "frame_rate="                       << data->frame_rate                     << "\n";
        cfg_file << "data_rate="                        << data->data_rate                      << "\n";
        cfg_file << "max_vbv_data_rate="                << data->max_vbv_data_rate              << "\n";
        cfg_file << "vbv_buffer_size="                  << data->vbv_buffer_size                << "\n";
        cfg_file << "range="                            << data->range                          << "\n";
        cfg_file << "multipass="                        << data->multi_pass                     << "\n";
        cfg_file << "stats_file="                       << data->stats_file                     << "\n";
        cfg_file << "color_description_present="        << data->color_description_present      << "\n";
        cfg_file << "color_primaries="                  << color_primaries_map.find(data->color_primaries)->second << "\n";
        cfg_file << "transfer_characteristics="         << transfer_characteristics_map.find(data->transfer_characteristics)->second << "\n";
        cfg_file << "matrix_coefficients="              << matrix_coefficients_map.find(data->matrix_coefficients)->second << "\n";
        cfg_file << "mastering_display_sei_present="    << data->mastering_display_sei_present  << "\n";
        cfg_file << "mastering_display_sei_x1="         << data->mastering_display_sei_x1       << "\n";
        cfg_file << "mastering_display_sei_y1="         << data->mastering_display_sei_y1       << "\n";
        cfg_file << "mastering_display_sei_x2="         << data->mastering_display_sei_x2       << "\n";
        cfg_file << "mastering_display_sei_y2="         << data->mastering_display_sei_y2       << "\n";
        cfg_file << "mastering_display_sei_x3="         << data->mastering_display_sei_x3       << "\n";
        cfg_file << "mastering_display_sei_y3="         << data->mastering_display_sei_y3       << "\n";
        cfg_file << "mastering_display_sei_wx="         << data->mastering_display_sei_wx       << "\n";
        cfg_file << "mastering_display_sei_wy="         << data->mastering_display_sei_wy       << "\n";
        cfg_file << "mastering_display_sei_max_lum="    << data->mastering_display_sei_max_lum  << "\n";
        cfg_file << "mastering_display_sei_min_lum="    << data->mastering_display_sei_min_lum  << "\n";
        cfg_file << "light_level_information_sei_present=" << data->light_level_information_sei_present << "\n";
        cfg_file << "light_level_max_content="          << data->light_level_max_content        << "\n";
        cfg_file << "light_level_max_frame_average="    << data->light_level_max_frame_average  << "\n";
        cfg_file << "input_file="                       << data->temp_file[0]                   << "\n";
        cfg_file << "output_file="                      << data->temp_file[1]                   << "\n";
        cfg_file << "ffmpeg_bin="                       << data->ffmpeg_bin                     << "\n";
        cfg_file.close();
        return true;
    }
    else
    {
        return false;
    }
}
