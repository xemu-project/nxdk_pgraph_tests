#include "projection_vertex_shader.h"

#include <pbkit/pbkit.h>

#include <cstdint>
#include <memory>

#include "debug_output.h"
#include "math3d.h"

// clang format off
static constexpr uint32_t kVertexShaderLighting[] = {
#include "projection_vertex_shader.inl"
};

static constexpr uint32_t kVertexShaderNoLighting[] = {
#include "projection_vertex_shader_no_lighting.inl"
};

static constexpr uint32_t kVertexShaderNoLighting4ComponentTexcoord[] = {
#include "projection_vertex_shader_no_lighting_4c_texcoords.inl"
};
// clang format on

ProjectionVertexShader::ProjectionVertexShader(uint32_t framebuffer_width, uint32_t framebuffer_height, float z_min,
                                               float z_max, bool enable_lighting, bool use_4_component_texcoords)
    : VertexShaderProgram(),
      framebuffer_width_(static_cast<float>(framebuffer_width)),
      framebuffer_height_(static_cast<float>(framebuffer_height)),
      z_min_(z_min),
      z_max_(z_max),
      enable_lighting_{enable_lighting},
      use_4_component_texcoords_{use_4_component_texcoords} {
  matrix_unit(view_matrix_);

  VECTOR rot = {0, 0, 0, 1};
  create_world_view(view_matrix_, camera_position_, rot);
}

void ProjectionVertexShader::LookAt(const float *camera_position, const float *look_at_point, const float *up) {
  VECTOR direction;
  direction[0] = look_at_point[0] - camera_position[0];
  direction[1] = look_at_point[1] - camera_position[1];
  direction[2] = look_at_point[2] - camera_position[2];
  direction[3] = 1.0f;

  LookTo(camera_position, direction, up);
}

void ProjectionVertexShader::LookTo(const float *camera_position, const float *camera_direction, const float *up) {
  memcpy(camera_position_, camera_position, sizeof(camera_position_));

  VECTOR z_axis;
  z_axis[3] = 1.0f;
  vector_normalize_into(z_axis, const_cast<float *>(camera_direction));

  VECTOR x_axis_work;
  x_axis_work[3] = 1.0f;
  vector_outerproduct(x_axis_work, const_cast<float *>(up), z_axis);
  VECTOR x_axis{0.0f, 0.0f, 0.0f, 1.0f};
  vector_normalize_into(x_axis, x_axis_work);

  VECTOR y_axis;
  y_axis[3] = 1.0f;
  vector_outerproduct(y_axis, z_axis, x_axis_work);

  memset(view_matrix_, 0, sizeof(view_matrix_));
  view_matrix_[_11] = x_axis_work[0];
  view_matrix_[_12] = y_axis[0];
  view_matrix_[_13] = z_axis[0];
  view_matrix_[_14] = 0.0f;

  view_matrix_[_21] = x_axis_work[1];
  view_matrix_[_22] = y_axis[1];
  view_matrix_[_23] = z_axis[1];
  view_matrix_[_24] = 0.0f;

  view_matrix_[_31] = x_axis_work[2];
  view_matrix_[_32] = y_axis[2];
  view_matrix_[_33] = z_axis[2];
  view_matrix_[_34] = 0.0f;

  view_matrix_[_41] = -vector_innerproduct(x_axis_work, const_cast<float *>(camera_position));
  view_matrix_[_42] = -vector_innerproduct(y_axis, const_cast<float *>(camera_position));
  view_matrix_[_43] = -vector_innerproduct(z_axis, const_cast<float *>(camera_position));
  view_matrix_[_44] = 1.0f;
}

void ProjectionVertexShader::SetCamera(const VECTOR position, const VECTOR rotation) {
  memcpy(camera_position_, position, sizeof(camera_position_));

  matrix_unit(view_matrix_);
  create_world_view(view_matrix_, camera_position_, rotation);
}

void ProjectionVertexShader::SetDirectionalLightDirection(const VECTOR &direction) {
  memcpy(light_direction_, direction, sizeof(light_direction_));
}

void ProjectionVertexShader::UpdateMatrices() {
  CalculateProjectionMatrix();
  CalculateViewportMatrix();
  matrix_multiply(projection_viewport_matrix_, projection_matrix_, viewport_matrix_);

  /* Create local->world matrix given our updated object */
  matrix_unit(model_matrix_);
}

void ProjectionVertexShader::OnActivate() { UpdateMatrices(); }

void ProjectionVertexShader::OnLoadShader() {
  if (enable_lighting_) {
    LoadShaderProgram(kVertexShaderLighting, sizeof(kVertexShaderLighting));
  } else {
    if (use_4_component_texcoords_) {
      LoadShaderProgram(kVertexShaderNoLighting4ComponentTexcoord, sizeof(kVertexShaderNoLighting4ComponentTexcoord));
    } else {
      LoadShaderProgram(kVertexShaderNoLighting, sizeof(kVertexShaderLighting));
    }
  }
}

void ProjectionVertexShader::OnLoadConstants() {
  /* Send shader constants
   *
   * WARNING: Changing shader source code may impact constant locations!
   * Check the intermediate file (*.inl) for the expected locations after
   * changing the code.
   */

  int index = 0;
  SetBaseUniform4x4F(index, model_matrix_);
  index += 4;
  SetBaseUniform4x4F(index, view_matrix_);
  index += 4;
  SetBaseUniform4x4F(index, projection_viewport_matrix_);
  index += 4;
  SetBaseUniform4F(index, camera_position_);
  ++index;

  if (enable_lighting_) {
    SetBaseUniform4F(index, light_direction_);
    ++index;
  }

  // Send shader constants
  float constants_0[4] = {0, 0, 0, 0};
  SetBaseUniform4F(index, constants_0);
  ++index;
}

void ProjectionVertexShader::CalculateViewportMatrix() {
  if (use_d3d_style_viewport_) {
    // TODO: Support alternative screen space Z range and take in the max depthbuffer value separately.
    // This should mirror the `create_d3d_viewport` parameters. In practice none of the tests use a range other than
    // 0..1 so this is not currently implemented and z_far is understood to contain the maximum depthbuffer value.
    ASSERT(z_min_ == 0.0f && "Viewport z-range only implemented for 0..1");
    create_d3d_viewport(viewport_matrix_, framebuffer_width_, framebuffer_height_, z_max_, 0.0f, 1.0f);
  } else {
    matrix_unit(viewport_matrix_);
    viewport_matrix_[_11] = framebuffer_width_ * 0.5f;
    viewport_matrix_[_41] = viewport_matrix_[_11];
    viewport_matrix_[_42] = framebuffer_height_ * 0.5f;
    viewport_matrix_[_22] = -1.0f * viewport_matrix_[_42];

    viewport_matrix_[_33] = (z_max_ - z_min_) * 0.5f;
    viewport_matrix_[_43] = (z_min_ + z_max_) * 0.5f;
  }
}
