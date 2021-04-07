/*
 * Copyright 2011-2021 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "integrator/denoiser_device.h"

#include "device/device.h"
#include "render/buffers.h"
#include "util/util_logging.h"

CCL_NAMESPACE_BEGIN

DeviceDenoiser::DeviceDenoiser(Device *device, const DenoiseParams &params)
    : Denoiser(device, params)
{
}

void DeviceDenoiser::denoise_buffer(const DenoiserBufferParams &buffer_params,
                                    RenderBuffers *render_buffers,
                                    const int num_samples)
{
  /* TODO(sergey): Need to pass those to the device somehow. */
  (void)buffer_params;
  (void)render_buffers;
  (void)num_samples;

  device_->denoise_buffer();
}

CCL_NAMESPACE_END
