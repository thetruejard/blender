/*
 * Copyright 2011-2013 Blender Foundation
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

#ifndef __SESSION_H__
#define __SESSION_H__

#include "device/device.h"
#include "integrator/render_scheduler.h"
#include "render/buffers.h"
#include "render/shader.h"
#include "render/stats.h"
#include "render/tile.h"

#include "util/util_progress.h"
#include "util/util_stats.h"
#include "util/util_thread.h"
#include "util/util_unique_ptr.h"
#include "util/util_vector.h"

CCL_NAMESPACE_BEGIN

class BufferParams;
class Device;
class DeviceScene;
class PathTrace;
class Progress;
class GPUDisplay;
class RenderBuffers;
class Scene;
class SceneParams;

/* Session Parameters */

class SessionParams {
 public:
  DeviceInfo device;

  bool headless;
  bool background;

  bool experimental;
  int samples;
  int pixel_size;
  int threads;

  /* Limit in seconds for how long path tracing is allowed to happen.
   * Zero means no limit is applied. */
  double time_limit;

  bool use_profiling;

  bool use_auto_tile;
  int tile_size;

  ShadingSystem shadingsystem;

  function<bool(const uchar *pixels, int width, int height, int channels)> write_render_cb;

  SessionParams()
  {
    headless = false;
    background = false;

    experimental = false;
    samples = 1024;
    pixel_size = 1;
    threads = 0;
    time_limit = 0.0;

    use_profiling = false;

    use_auto_tile = true;
    tile_size = 2048;

    shadingsystem = SHADINGSYSTEM_SVM;
  }

  bool modified(const SessionParams &params) const
  {
    /* Modified means we have to recreate the session, any parameter changes
     * that can be handled by an existing Session are omitted. */
    return !(device == params.device && headless == params.headless &&
             background == params.background && experimental == params.experimental &&
             pixel_size == params.pixel_size && threads == params.threads &&
             use_profiling == params.use_profiling && shadingsystem == params.shadingsystem &&
             use_auto_tile == params.use_auto_tile && tile_size == params.tile_size);
  }
};

/* Session
 *
 * This is the class that contains the session thread, running the render
 * control loop and dispatching tasks. */

class Session {
 public:
  Device *device;
  Scene *scene;
  Progress progress;
  SessionParams params;
  Stats stats;
  Profiler profiler;

  function<void(void)> write_render_tile_cb;
  function<void(void)> update_render_tile_cb;
  function<void(void)> read_render_tile_cb;

  /* Callback is invoked by tile manager whenever on-dist tiles storage file is closed after
   * writing. Allows an engine integration to keep track of those files without worry about
   * transferring the information when it needs to re-create session during rendering. */
  function<void(string_view)> full_buffer_written_cb;

  explicit Session(const SessionParams &params, const SceneParams &scene_params);
  ~Session();

  void start();

  /* When quick cancel is requested path tracing is cancels as soon as possible, without waiting
   * for the buffer to be uniformly sampled. */
  void cancel(bool quick = false);

  void draw();
  void wait();

  bool ready_to_reset();
  void reset(const SessionParams &session_params, const BufferParams &buffer_params);

  void set_pause(bool pause);

  void set_samples(int samples);
  void set_time_limit(double time_limit);

  void set_gpu_display(unique_ptr<GPUDisplay> gpu_display);

  double get_estimated_remaining_time() const;

  void device_free();

  /* Returns the rendering progress or 0 if no progress can be determined
   * (for example, when rendering with unlimited samples). */
  float get_progress();

  void collect_statistics(RenderStats *stats);

  /* --------------------------------------------------------------------
   * Tile and tile pixels access.
   */

  bool has_multiple_render_tiles() const;

  /* Get size and offset (relative to the buffer's full x/y) of the currently rendering tile. */
  int2 get_render_tile_size() const;
  int2 get_render_tile_offset() const;

  string_view get_render_tile_layer() const;
  string_view get_render_tile_view() const;

  bool copy_render_tile_from_device();

  bool get_render_tile_pixels(const string &pass_name, int num_components, float *pixels);
  bool set_render_tile_pixels(const string &pass_name, int num_components, const float *pixels);

  /* --------------------------------------------------------------------
   * Full-frame on-disk storage.
   */

  /* Read given full-frame file from disk, perform needed processing and write it to the software
   * via the write callback. */
  void process_full_buffer_from_disk(string_view filename);

 protected:
  struct DelayedReset {
    thread_mutex mutex;
    bool do_reset;
    SessionParams session_params;
    BufferParams buffer_params;
  } delayed_reset_;

  void run();

  /* Update for the new iteration of the main loop in run implementation (run_cpu and run_gpu).
   *
   * Will take care of the following things:
   *  - Delayed reset
   *  - Scene update
   *  - Tile manager advance
   *  - Render scheduler work request
   *
   * The updates are done in a proper order with proper locking around them, which guarantees
   * that the device side of scene and render buffers are always in a consistent state.
   *
   * Returns render work which is to be rendered next. */
  RenderWork run_update_for_next_iteration();

  /* Wait for rendering to be unpaused, or for new tiles for render to arrive.
   * Returns true if new main render loop iteration is required after this function call.
   *
   * The `render_work` is the work which was scheduled by the render scheduler right before
   * checking the pause. */
  bool run_wait_for_work(const RenderWork &render_work);

  void run_main_render_loop();

  bool update_scene(int width, int height);

  void update_status_time(bool show_pause = false, bool show_done = false);

  void do_delayed_reset();

  int2 get_effective_tile_size() const;

  thread *session_thread_;

  bool pause_ = false;
  bool cancel_ = false;
  bool new_work_added_ = false;

  thread_condition_variable pause_cond_;
  thread_mutex pause_mutex_;
  thread_mutex tile_mutex_;
  thread_mutex buffers_mutex_;

  TileManager tile_manager_;
  BufferParams buffer_params_;

  /* Render scheduler is used to get work to be rendered with the current big tile. */
  RenderScheduler render_scheduler_;

  /* Path tracer object.
   *
   * Is a single full-frame path tracer for interactive viewport rendering.
   * A path tracer for the current big-tile for an offline rendering. */
  unique_ptr<PathTrace> path_trace_;
};

CCL_NAMESPACE_END

#endif /* __SESSION_H__ */
