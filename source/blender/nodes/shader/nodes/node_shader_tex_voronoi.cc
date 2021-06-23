/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2005 Blender Foundation.
 * All rights reserved.
 */

#include "../node_shader_util.h"

#include "BLI_hash.h"
#include "BLI_noise.h"

/* **************** VORONOI ******************** */

static bNodeSocketTemplate sh_node_tex_voronoi_in[] = {
    {SOCK_VECTOR, N_("Vector"), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, PROP_NONE, SOCK_HIDE_VALUE},
    {SOCK_FLOAT, N_("W"), 0.0f, 0.0f, 0.0f, 0.0f, -1000.0f, 1000.0f},
    {SOCK_FLOAT, N_("Scale"), 5.0f, 0.0f, 0.0f, 0.0f, -1000.0f, 1000.0f},
    {SOCK_FLOAT, N_("Smoothness"), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, PROP_FACTOR},
    {SOCK_FLOAT, N_("Exponent"), 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 32.0f},
    {SOCK_FLOAT, N_("Randomness"), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, PROP_FACTOR},
    {-1, ""},
};

static bNodeSocketTemplate sh_node_tex_voronoi_out[] = {
    {SOCK_FLOAT,
     N_("Distance"),
     0.0f,
     0.0f,
     0.0f,
     0.0f,
     0.0f,
     1.0f,
     PROP_NONE,
     SOCK_NO_INTERNAL_LINK},
    {SOCK_RGBA, N_("Color"), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, PROP_NONE, SOCK_NO_INTERNAL_LINK},
    {SOCK_VECTOR,
     N_("Position"),
     0.0f,
     0.0f,
     0.0f,
     0.0f,
     0.0f,
     1.0f,
     PROP_NONE,
     SOCK_NO_INTERNAL_LINK},
    {SOCK_FLOAT, N_("W"), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, PROP_NONE, SOCK_NO_INTERNAL_LINK},
    {SOCK_FLOAT,
     N_("Radius"),
     0.0f,
     0.0f,
     0.0f,
     0.0f,
     0.0f,
     1.0f,
     PROP_NONE,
     SOCK_NO_INTERNAL_LINK},
    {-1, ""},
};

static void node_shader_init_tex_voronoi(bNodeTree *UNUSED(ntree), bNode *node)
{
  NodeTexVoronoi *tex = (NodeTexVoronoi *)MEM_callocN(sizeof(NodeTexVoronoi), "NodeTexVoronoi");
  BKE_texture_mapping_default(&tex->base.tex_mapping, TEXMAP_TYPE_POINT);
  BKE_texture_colormapping_default(&tex->base.color_mapping);
  tex->dimensions = 3;
  tex->distance = SHD_VORONOI_EUCLIDEAN;
  tex->feature = SHD_VORONOI_F1;

  node->storage = tex;
}

static const char *node_shader_gpu_name_tex_voronoi(NodeTexVoronoi *tex)
{

  static const char *names[][5] = {
      {
          "",
          "node_tex_voronoi_f1_1d",
          "node_tex_voronoi_f1_2d",
          "node_tex_voronoi_f1_3d",
          "node_tex_voronoi_f1_4d",
      },
      {
          "",
          "node_tex_voronoi_f2_1d",
          "node_tex_voronoi_f2_2d",
          "node_tex_voronoi_f2_3d",
          "node_tex_voronoi_f2_4d",
      },
      {
          "",
          "node_tex_voronoi_smooth_f1_1d",
          "node_tex_voronoi_smooth_f1_2d",
          "node_tex_voronoi_smooth_f1_3d",
          "node_tex_voronoi_smooth_f1_4d",
      },
      {
          "",
          "node_tex_voronoi_distance_to_edge_1d",
          "node_tex_voronoi_distance_to_edge_2d",
          "node_tex_voronoi_distance_to_edge_3d",
          "node_tex_voronoi_distance_to_edge_4d",
      },
      {
          "",
          "node_tex_voronoi_n_sphere_radius_1d",
          "node_tex_voronoi_n_sphere_radius_2d",
          "node_tex_voronoi_n_sphere_radius_3d",
          "node_tex_voronoi_n_sphere_radius_4d",
      },
  };
  return names[tex->feature][tex->dimensions];
}

static int node_shader_gpu_tex_voronoi(GPUMaterial *mat,
                                       bNode *node,
                                       bNodeExecData *UNUSED(execdata),
                                       GPUNodeStack *in,
                                       GPUNodeStack *out)
{
  node_shader_gpu_default_tex_coord(mat, node, &in[0].link);
  node_shader_gpu_tex_mapping(mat, node, in, out);

  NodeTexVoronoi *tex = (NodeTexVoronoi *)node->storage;
  float metric = tex->distance;

  BLI_assert(tex->feature >= 0 && tex->feature < 5);
  BLI_assert(tex->dimensions > 0 && tex->dimensions < 5);

  return GPU_stack_link(
      mat, node, node_shader_gpu_name_tex_voronoi(tex), in, out, GPU_constant(&metric));
}

static void node_shader_update_tex_voronoi(bNodeTree *UNUSED(ntree), bNode *node)
{
  bNodeSocket *inVectorSock = nodeFindSocket(node, SOCK_IN, "Vector");
  bNodeSocket *inWSock = nodeFindSocket(node, SOCK_IN, "W");
  bNodeSocket *inSmoothnessSock = nodeFindSocket(node, SOCK_IN, "Smoothness");
  bNodeSocket *inExponentSock = nodeFindSocket(node, SOCK_IN, "Exponent");

  bNodeSocket *outDistanceSock = nodeFindSocket(node, SOCK_OUT, "Distance");
  bNodeSocket *outColorSock = nodeFindSocket(node, SOCK_OUT, "Color");
  bNodeSocket *outPositionSock = nodeFindSocket(node, SOCK_OUT, "Position");
  bNodeSocket *outWSock = nodeFindSocket(node, SOCK_OUT, "W");
  bNodeSocket *outRadiusSock = nodeFindSocket(node, SOCK_OUT, "Radius");

  NodeTexVoronoi *tex = (NodeTexVoronoi *)node->storage;

  nodeSetSocketAvailability(inWSock, tex->dimensions == 1 || tex->dimensions == 4);
  nodeSetSocketAvailability(inVectorSock, tex->dimensions != 1);
  nodeSetSocketAvailability(
      inExponentSock,
      tex->distance == SHD_VORONOI_MINKOWSKI && tex->dimensions != 1 &&
          !ELEM(tex->feature, SHD_VORONOI_DISTANCE_TO_EDGE, SHD_VORONOI_N_SPHERE_RADIUS));
  nodeSetSocketAvailability(inSmoothnessSock, tex->feature == SHD_VORONOI_SMOOTH_F1);
  nodeSetSocketAvailability(outDistanceSock, tex->feature != SHD_VORONOI_N_SPHERE_RADIUS);
  nodeSetSocketAvailability(outColorSock,
                            tex->feature != SHD_VORONOI_DISTANCE_TO_EDGE &&
                                tex->feature != SHD_VORONOI_N_SPHERE_RADIUS);
  nodeSetSocketAvailability(outPositionSock,
                            tex->feature != SHD_VORONOI_DISTANCE_TO_EDGE &&
                                tex->feature != SHD_VORONOI_N_SPHERE_RADIUS &&
                                tex->dimensions != 1);
  nodeSetSocketAvailability(outWSock,
                            tex->feature != SHD_VORONOI_DISTANCE_TO_EDGE &&
                                tex->feature != SHD_VORONOI_N_SPHERE_RADIUS &&
                                (tex->dimensions == 1 || tex->dimensions == 4));
  nodeSetSocketAvailability(outRadiusSock, tex->feature == SHD_VORONOI_N_SPHERE_RADIUS);
}

class VoronoiTextureFunction : public blender::fn::MultiFunction {
 public:
  VoronoiTextureFunction()
  {
    static blender::fn::MFSignature signature = create_signature();
    this->set_signature(&signature);
  }

  static blender::fn::MFSignature create_signature()
  {
    blender::fn::MFSignatureBuilder signature{"Noise Texture"};
    signature.single_input<blender::float3>("Vector");
    signature.single_input<float>("Scale");
    signature.single_input<float>("Randomness");
    signature.single_output<float>("Distance");
    signature.single_output<blender::ColorGeometry4f>("Color");
    signature.single_output<blender::float3>("Position");
    return signature.build();
  }

  void call(blender::IndexMask mask,
            blender::fn::MFParams params,
            blender::fn::MFContext UNUSED(context)) const override
  {
    const blender::VArray<blender::float3> &vectors =
        params.readonly_single_input<blender::float3>(0, "Vector");
    const blender::VArray<float> &scales = params.readonly_single_input<float>(1, "Scale");

    blender::MutableSpan<float> r_distances = params.uninitialized_single_output<float>(
        3, "Distance");
    blender::MutableSpan<blender::ColorGeometry4f> r_colors =
        params.uninitialized_single_output<blender::ColorGeometry4f>(4, "Color");
    blender::MutableSpan<blender::float3> r_positions =
        params.uninitialized_single_output<blender::float3>(5, "Position");

    for (int i : mask) {
      const float scale = scales[i];
      const blender::float3 vector = vectors[i] * scale;
      float da[4];
      float pa[12];
      BLI_noise_voronoi(vector.x, vector.y, vector.z, da, pa, 1, 0);
      blender::ColorGeometry4f color;
      BLI_noise_cell_v3(pa[0], pa[1], pa[2], color);
      color.a = 1.0f;
      r_distances[i] = da[0];
      r_colors[i] = color;
      r_positions[i] = {pa[0], pa[1], pa[2]};
    }
  }
};

static void sh_node_tex_voronoi_expand_in_mf_network(blender::nodes::NodeMFNetworkBuilder &builder)
{
  NodeTexVoronoi *tex = builder.dnode()->storage<NodeTexVoronoi>();
  if (tex->dimensions != 3 || tex->feature != SHD_VORONOI_F1 ||
      tex->distance != SHD_VORONOI_EUCLIDEAN) {
    builder.set_not_implemented();
    return;
  }
  static VoronoiTextureFunction fn;
  builder.set_matching_fn(fn);
}

void register_node_type_sh_tex_voronoi(void)
{
  static bNodeType ntype;

  sh_fn_node_type_base(&ntype, SH_NODE_TEX_VORONOI, "Voronoi Texture", NODE_CLASS_TEXTURE, 0);
  node_type_socket_templates(&ntype, sh_node_tex_voronoi_in, sh_node_tex_voronoi_out);
  node_type_init(&ntype, node_shader_init_tex_voronoi);
  node_type_storage(
      &ntype, "NodeTexVoronoi", node_free_standard_storage, node_copy_standard_storage);
  node_type_gpu(&ntype, node_shader_gpu_tex_voronoi);
  node_type_update(&ntype, node_shader_update_tex_voronoi);
  ntype.expand_in_mf_network = sh_node_tex_voronoi_expand_in_mf_network;

  nodeRegisterType(&ntype);
}
