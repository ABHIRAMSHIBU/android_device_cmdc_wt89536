/*
 * Copyright (C) 2017 Nikolay Karev
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_amplifier"

#include <time.h>
#include <system/audio.h>
#include <platform.h>

#include <stdlib.h>

#include <fcntl.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>

#include <hardware/audio_amplifier.h>
#include <hardware/hardware.h>
#include <audio_hw.h>

#define DEVICE_PATH "/sys/audio_amplifier/enable"

int speaker_ref_count = 0;

static int is_speaker(uint32_t snd_device) {
    int speaker = 0;

    switch (snd_device) {
        case SND_DEVICE_OUT_SPEAKER:
        case SND_DEVICE_OUT_SPEAKER_REVERSE:
        case SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES:
        case SND_DEVICE_OUT_VOICE_SPEAKER:
        case SND_DEVICE_OUT_SPEAKER_AND_HDMI:
        case SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET:
        case SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET:
            speaker = 1;
            break;
    }

    return speaker;
}

static inline int write_int(char const* path, int value)
{
    int fd;
    static int already_warned = 0;

    fd = open(path, O_RDWR);
    if (fd >= 0) {
        char buffer[20];
        int bytes = snprintf(buffer, sizeof(buffer), "%d\n", value);
        ssize_t amt = write(fd, buffer, (size_t)bytes);
        close(fd);
        return amt == -1 ? -errno : 0;
    } else {
        if (already_warned == 0) {
            ALOGE("write_int failed to open %s\n", path);
            already_warned = 1;
        }
        return -errno;
    }
}

static inline int amplifier_enable() {
  return write_int(DEVICE_PATH, 1);
}

static inline int amplifier_disable() {
  return write_int(DEVICE_PATH, 0);
}


static int amp_set_input_devices(amplifier_device_t *device, uint32_t devices)
{
    return 0;
}

static int amp_set_output_devices(amplifier_device_t *device, uint32_t devices)
{
    ALOGE("amp_set_output_devices: %d", devices);
    if (!is_speaker(devices)) {
      speaker_ref_count = 0;
      amplifier_disable();
    }
    return 0;
}

static int amp_enable_output_devices(amplifier_device_t *device,
        uint32_t devices, bool enable)
{
    ALOGE("amp_enable_output_devices: %d, %d", devices, enable);
    if (is_speaker(devices) && !enable) {
      speaker_ref_count = 0;
    }
    if (is_speaker(devices) && enable) {
      amplifier_enable();
    }
    if (is_speaker(devices) && !enable) {
      amplifier_disable();
    }
    return 0;
}

static int amp_enable_input_devices(amplifier_device_t *device,
        uint32_t devices, bool enable)
{
    return 0;
}

static int amp_set_mode(amplifier_device_t *device, audio_mode_t mode)
{
    return 0;
}

static int amp_output_stream_start(amplifier_device_t *device,
        struct audio_stream_out *stream, bool offload)
{
    struct stream_out *out = (struct stream_out*) stream;
    uint32_t devices = out->devices;
    ALOGE("amp_output_stream_start: %d", devices);
    if (is_speaker(devices)) {
      if (!speaker_ref_count) {
        amplifier_enable();
      }
      speaker_ref_count++;
    }
    return 0;
}

static int amp_input_stream_start(amplifier_device_t *device,
        struct audio_stream_in *stream)
{
    return 0;
}

static int amp_output_stream_standby(amplifier_device_t *device,
        struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out*) stream;
    uint32_t devices = out->devices;
    ALOGE("amp_output_stream_standby: %d", devices);
    if (is_speaker(devices)) {
      speaker_ref_count--;
      if (speaker_ref_count == 0) {
        amplifier_disable();
      }
    }
    return 0;
}

static int amp_input_stream_standby(amplifier_device_t *device,
        struct audio_stream_in *stream)
{
    return 0;
}

static int amp_set_parameters(struct amplifier_device *device,
        struct str_parms *parms)
{
    return 0;
}

static int amp_dev_close(hw_device_t *device)
{
    if (device)
        free(device);
    return 0;
}

static int amp_module_open(const hw_module_t *module, const char *name,
        hw_device_t **device)
{
    if (strcmp(name, AMPLIFIER_HARDWARE_INTERFACE)) {
        ALOGE("%s:%d: %s does not match amplifier hardware interface name\n",
                __func__, __LINE__, name);
        return -ENODEV;
    }

    amplifier_device_t *amp_dev = calloc(1, sizeof(amplifier_device_t));
    if (!amp_dev) {
        ALOGE("%s:%d: Unable to allocate memory for amplifier device\n",
                __func__, __LINE__);
        return -ENOMEM;
    }

    amp_dev->common.tag = HARDWARE_DEVICE_TAG;
    amp_dev->common.module = (hw_module_t *) module;
    amp_dev->common.version = HARDWARE_DEVICE_API_VERSION(1, 0);
    amp_dev->common.close = amp_dev_close;

    amp_dev->set_input_devices = amp_set_input_devices;
    amp_dev->set_output_devices = amp_set_output_devices;
    amp_dev->enable_output_devices = amp_enable_output_devices;
    amp_dev->enable_input_devices = amp_enable_input_devices;
    amp_dev->set_mode = amp_set_mode;
    amp_dev->output_stream_start = amp_output_stream_start;
    amp_dev->input_stream_start = amp_input_stream_start;
    amp_dev->output_stream_standby = amp_output_stream_standby;
    amp_dev->input_stream_standby = amp_input_stream_standby;
    amp_dev->set_parameters = amp_set_parameters;

    *device = (hw_device_t *) amp_dev;

    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = amp_module_open,
};

amplifier_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AMPLIFIER_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AMPLIFIER_HARDWARE_MODULE_ID,
        .name = "Markw audio amplifier HAL",
        .author = "Nikolay Karev",
        .methods = &hal_module_methods,
    },
};
