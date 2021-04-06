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

#include "integrator/work_tile_scheduler.h"

#include "device/device_queue.h"
#include "integrator/tile.h"
#include "render/buffers.h"
#include "util/util_atomic.h"
#include "util/util_logging.h"

CCL_NAMESPACE_BEGIN

WorkTileScheduler::WorkTileScheduler()
{
}

void WorkTileScheduler::set_max_num_path_states(int max_num_path_states)
{
  max_num_path_states_ = max_num_path_states;
}

void WorkTileScheduler::reset(const BufferParams &buffer_params, int sample_start, int samples_num)
{
  /* Image buffer parameters. */
  image_full_offset_px_.x = buffer_params.full_x;
  image_full_offset_px_.y = buffer_params.full_y;

  image_size_px_ = make_int2(buffer_params.width, buffer_params.height);

  buffer_params.get_offset_stride(offset_, stride_);

  /* Samples parameters. */
  sample_start_ = sample_start;
  samples_num_ = samples_num;

  /* Initialize new scheduling. */
  reset_scheduler_state();
}

void WorkTileScheduler::reset_scheduler_state()
{
  tile_size_ = tile_calculate_best_size(image_size_px_, samples_num_, max_num_path_states_);

  VLOG(3) << "Number of unused path states: "
          << max_num_path_states_ - (tile_size_.x * tile_size_.y);

  num_tiles_x_ = divide_up(image_size_px_.x, tile_size_.x);
  num_tiles_y_ = divide_up(image_size_px_.y, tile_size_.y);

  total_tiles_num_ = num_tiles_x_ * num_tiles_y_;

  next_work_index_ = 0;
  total_work_size_ = total_tiles_num_ * samples_num_;
}

bool WorkTileScheduler::get_work(KernelWorkTile *work_tile_, const int max_work_size)
{
  DCHECK_NE(max_num_path_states_, 0);

  const int work_index = atomic_fetch_and_add_int32(&next_work_index_, 1);
  if (work_index >= total_work_size_) {
    return false;
  }

  const int sample = work_index / total_tiles_num_;
  const int tile_index = work_index - sample * total_tiles_num_;
  const int tile_y = tile_index / num_tiles_x_;
  const int tile_x = tile_index - tile_y * num_tiles_x_;

  KernelWorkTile work_tile;
  work_tile.x = tile_x * tile_size_.x;
  work_tile.y = tile_y * tile_size_.y;
  work_tile.w = tile_size_.x;
  work_tile.h = tile_size_.y;
  work_tile.start_sample = sample_start_ + sample;
  work_tile.num_samples = 1;
  work_tile.offset = offset_;
  work_tile.stride = stride_;

  work_tile.w = min(work_tile.w, image_size_px_.x - work_tile.x);
  work_tile.h = min(work_tile.h, image_size_px_.y - work_tile.y);

  work_tile.x += image_full_offset_px_.x;
  work_tile.y += image_full_offset_px_.y;

  DCHECK_LE(max_work_size, max_num_path_states_);
  if (max_work_size && work_tile.w * work_tile.h * work_tile.num_samples > max_work_size) {
    /* The work did not fit into the requested limit of the work size. Unchedule the tile,
     * allowing others (or ourselves later one) to pick it up.
     *
     * TODO: Such temporary decrement is not ideal, since it might lead to situation when another
     * device sees there is nothing to be done, finishing its work and leaving all work to be
     * done by us. */
    atomic_fetch_and_add_int32(&next_work_index_, -1);
    return false;
  }

  *work_tile_ = work_tile;

  return true;
}

CCL_NAMESPACE_END
